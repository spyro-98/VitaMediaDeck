#include "media/media_tracks.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>

#define TRACKS_AVIO_BUFFER_SIZE (32 * 1024)
#define SUBTITLE_QUEUE_CAPACITY 24u
#define SUBTITLE_TEXT_CAPACITY 512
#define SUBTITLE_NORMALIZED_CAPACITY 2048
#define SUBTITLE_THREAD_PRIORITY 0x10000140
#define SUBTITLE_THREAD_STACK 0x40000

typedef struct VtMediaInput {
	VtDecoderStreamHandle stream;
	AVIOContext *avio;
	AVFormatContext *format;
	volatile int *cancel;
} VtMediaInput;

typedef struct VtSubtitleCue {
	uint64_t start_ms;
	uint64_t end_ms;
	char text[SUBTITLE_TEXT_CAPACITY];
} VtSubtitleCue;

struct VtSubtitleReader {
	VtDecoderStreamFactory factory;
	VtMediaInput input;
	volatile int cancel;
	SceUID thid;
	int started;
	int stream_index;
	volatile int requested_stream_index;
	volatile uint64_t requested_position_ms;
	volatile unsigned int switch_serial;
	volatile unsigned int applied_serial;
	volatile unsigned int read_index;
	volatile unsigned int write_index;
	volatile int eof;
	volatile int error;
	VtSubtitleCue cues[SUBTITLE_QUEUE_CAPACITY];
};

static size_t normalize_subtitle_text(const unsigned char *data, size_t size,
	                                  unsigned char *out, size_t out_size);

static int media_read(void *opaque, uint8_t *buffer, int size) {
	VtMediaInput *input = opaque;
	if (!input || !input->stream.read || (input->cancel && *input->cancel))
		return AVERROR_EXIT;
	int ret = input->stream.read(input->stream.opaque, buffer, (size_t)size);
	if (ret == 0) return AVERROR_EOF;
	return ret < 0 ? AVERROR(EIO) : ret;
}

static int64_t media_seek(void *opaque, int64_t offset, int whence) {
	VtMediaInput *input = opaque;
	if (!input || (input->cancel && *input->cancel)) return AVERROR_EXIT;
	if (whence == AVSEEK_SIZE) return input->stream.size;
	if (!input->stream.seek) return AVERROR(ENOSYS);
	return input->stream.seek(input->stream.opaque, offset,
	                          whence & ~AVSEEK_FORCE);
}

static int media_interrupt(void *opaque) {
	volatile int *cancel = opaque;
	return cancel && *cancel;
}

static int indexed_container(const AVFormatContext *format) {
	const char *name = format && format->iformat ? format->iformat->name : NULL;
	return name && (strstr(name, "mov,mp4") || strstr(name, "matroska") ||
	                strstr(name, "webm") || strstr(name, "avi"));
}

static int track_streams_ready(const AVFormatContext *format) {
	if (!format || !format->nb_streams) return 0;
	for (unsigned int i = 0; i < format->nb_streams; i++) {
		const AVStream *stream = format->streams[i];
		const AVCodecParameters *params = stream->codecpar;
		if (params->codec_type != AVMEDIA_TYPE_AUDIO &&
		    params->codec_type != AVMEDIA_TYPE_VIDEO &&
		    params->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
		if (params->codec_id == AV_CODEC_ID_NONE || stream->time_base.den <= 0)
			return 0;
		if (params->codec_type == AVMEDIA_TYPE_VIDEO &&
		    !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC) &&
		    (params->width <= 0 || params->height <= 0)) return 0;
		if (params->codec_type == AVMEDIA_TYPE_AUDIO &&
		    (params->sample_rate <= 0 || params->ch_layout.nb_channels <= 0))
			return 0;
	}
	return 1;
}

static void media_input_close(VtMediaInput *input) {
	if (!input) return;
	if (input->format) avformat_close_input(&input->format);
	if (input->avio) {
		av_freep(&input->avio->buffer);
		avio_context_free(&input->avio);
	}
	if (input->stream.close && input->stream.opaque)
		input->stream.close(input->stream.opaque);
	memset(input, 0, sizeof(*input));
}

static int media_input_open(VtMediaInput *input,
	                        const VtDecoderStreamFactory *factory,
	                        volatile int *cancel) {
	if (!input || !factory || !factory->open) return AVERROR(EINVAL);
	memset(input, 0, sizeof(*input));
	input->cancel = cancel;
	int ret = factory->open(factory->opaque, &input->stream);
	if (ret < 0 || !input->stream.read || !input->stream.seek) {
		media_input_close(input);
		return ret < 0 ? ret : AVERROR(EINVAL);
	}
	unsigned char *buffer = av_malloc(TRACKS_AVIO_BUFFER_SIZE);
	if (!buffer) {
		media_input_close(input);
		return AVERROR(ENOMEM);
	}
	input->avio = avio_alloc_context(buffer, TRACKS_AVIO_BUFFER_SIZE, 0,
	                                input, media_read, NULL, media_seek);
	if (!input->avio) {
		av_free(buffer);
		media_input_close(input);
		return AVERROR(ENOMEM);
	}
	input->avio->seekable = AVIO_SEEKABLE_NORMAL;
	input->format = avformat_alloc_context();
	if (!input->format) {
		media_input_close(input);
		return AVERROR(ENOMEM);
	}
	input->format->pb = input->avio;
	input->format->flags |= AVFMT_FLAG_CUSTOM_IO;
	input->format->interrupt_callback.callback = media_interrupt;
	input->format->interrupt_callback.opaque = (void *)cancel;
	input->format->probesize = 1024 * 1024;
	input->format->max_analyze_duration = 2 * AV_TIME_BASE;
	ret = avformat_open_input(&input->format, NULL, NULL, NULL);
	if (ret >= 0 && !(indexed_container(input->format) &&
	                  track_streams_ready(input->format)))
		ret = avformat_find_stream_info(input->format, NULL);
	if (ret < 0) media_input_close(input);
	return ret;
}

static int text_subtitle_codec(enum AVCodecID codec) {
	return codec == AV_CODEC_ID_SUBRIP || codec == AV_CODEC_ID_ASS ||
	       codec == AV_CODEC_ID_SSA || codec == AV_CODEC_ID_WEBVTT ||
	       codec == AV_CODEC_ID_MOV_TEXT || codec == AV_CODEC_ID_TEXT ||
	       codec == AV_CODEC_ID_MICRODVD;
}

static void copy_metadata(char *out, size_t out_size,
	                      const AVDictionary *metadata, const char *key) {
	if (!out || !out_size) return;
	out[0] = '\0';
	const AVDictionaryEntry *entry = av_dict_get(metadata, key, NULL, 0);
	if (!entry || !entry->value || !strcmp(entry->value, "und")) return;
	normalize_subtitle_text((const unsigned char *)entry->value,
	                        strlen(entry->value), (unsigned char *)out,
	                        out_size);
}

static void fill_track(VtDecoderTrackInfo *out, const AVStream *stream,
	                   int stream_index) {
	memset(out, 0, sizeof(*out));
	out->stream_index = stream_index;
	out->is_default = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
	out->channels = stream->codecpar->ch_layout.nb_channels;
	copy_metadata(out->language, sizeof(out->language), stream->metadata,
	              "language");
	copy_metadata(out->title, sizeof(out->title), stream->metadata, "title");
	snprintf(out->codec, sizeof(out->codec), "%s",
	         avcodec_get_name(stream->codecpar->codec_id));
}

int vt_media_tracks_probe(const VtDecoderStreamFactory *factory,
	                      VtDecoderTrackInfo *audio_tracks, int *audio_count,
	                      VtDecoderTrackInfo *subtitle_tracks,
	                      int *subtitle_count, volatile int *cancel) {
	if (!audio_count || !subtitle_count) return AVERROR(EINVAL);
	*audio_count = 0;
	*subtitle_count = 0;
	VtMediaInput input;
	int ret = media_input_open(&input, factory, cancel);
	if (ret < 0) return ret;
	for (unsigned int i = 0; i < input.format->nb_streams; i++) {
		AVStream *stream = input.format->streams[i];
		AVCodecParameters *params = stream->codecpar;
		if (params->codec_type == AVMEDIA_TYPE_AUDIO &&
		    params->codec_id == AV_CODEC_ID_AAC && audio_tracks &&
		    *audio_count < VT_MEDIA_MAX_AUDIO_TRACKS) {
			fill_track(&audio_tracks[(*audio_count)++], stream, (int)i);
		} else if (params->codec_type == AVMEDIA_TYPE_SUBTITLE &&
		           text_subtitle_codec(params->codec_id) && subtitle_tracks &&
		           *subtitle_count < VT_MEDIA_MAX_SUBTITLE_TRACKS) {
			fill_track(&subtitle_tracks[(*subtitle_count)++], stream, (int)i);
		}
	}
	media_input_close(&input);
	return 0;
}

static size_t subtitle_payload(const AVPacket *packet, enum AVCodecID codec,
	                           const unsigned char **payload) {
	if (!packet || !payload || !packet->data || packet->size <= 0) return 0;
	const unsigned char *data = packet->data;
	size_t size = (size_t)packet->size;
	if (codec == AV_CODEC_ID_MOV_TEXT) {
		if (size < 2) return 0;
		size_t declared = ((size_t)data[0] << 8) | data[1];
		data += 2;
		size -= 2;
		if (declared < size) size = declared;
	} else if (codec == AV_CODEC_ID_ASS || codec == AV_CODEC_ID_SSA) {
		/* Matroska ASS packets prefix the dialogue with eight CSV fields. */
		int commas = 0;
		for (size_t i = 0; i < size; i++) {
			if (data[i] != ',') continue;
			if (++commas == 8) {
				data += i + 1;
				size -= i + 1;
				break;
			}
		}
	}
	*payload = data;
	return size;
}

static size_t encode_utf8(uint32_t codepoint, unsigned char out[4]) {
	if (codepoint <= 0x7FU) {
		out[0] = (unsigned char)codepoint;
		return 1;
	}
	if (codepoint <= 0x7FFU) {
		out[0] = 0xC0U | (unsigned char)(codepoint >> 6);
		out[1] = 0x80U | (unsigned char)(codepoint & 0x3FU);
		return 2;
	}
	if (codepoint >= 0xD800U && codepoint <= 0xDFFFU)
		codepoint = 0xFFFDU;
	if (codepoint <= 0xFFFFU) {
		out[0] = 0xE0U | (unsigned char)(codepoint >> 12);
		out[1] = 0x80U | (unsigned char)((codepoint >> 6) & 0x3FU);
		out[2] = 0x80U | (unsigned char)(codepoint & 0x3FU);
		return 3;
	}
	if (codepoint > 0x10FFFFU) codepoint = 0xFFFDU;
	out[0] = 0xF0U | (unsigned char)(codepoint >> 18);
	out[1] = 0x80U | (unsigned char)((codepoint >> 12) & 0x3FU);
	out[2] = 0x80U | (unsigned char)((codepoint >> 6) & 0x3FU);
	out[3] = 0x80U | (unsigned char)(codepoint & 0x3FU);
	return 4;
}

static size_t valid_utf8_sequence(const unsigned char *data, size_t size) {
	if (!size) return 0;
	if (data[0] < 0x80U) return 1;
	if (data[0] >= 0xC2U && data[0] <= 0xDFU && size >= 2 &&
	    (data[1] & 0xC0U) == 0x80U) return 2;
	if (data[0] >= 0xE0U && data[0] <= 0xEFU && size >= 3 &&
	    (data[1] & 0xC0U) == 0x80U && (data[2] & 0xC0U) == 0x80U &&
	    !(data[0] == 0xE0U && data[1] < 0xA0U) &&
	    !(data[0] == 0xEDU && data[1] >= 0xA0U)) return 3;
	if (data[0] >= 0xF0U && data[0] <= 0xF4U && size >= 4 &&
	    (data[1] & 0xC0U) == 0x80U && (data[2] & 0xC0U) == 0x80U &&
	    (data[3] & 0xC0U) == 0x80U &&
	    !(data[0] == 0xF0U && data[1] < 0x90U) &&
	    !(data[0] == 0xF4U && data[1] >= 0x90U)) return 4;
	return 0;
}

static size_t normalize_subtitle_text(const unsigned char *data, size_t size,
	                                  unsigned char *out, size_t out_size) {
	if (!out || !out_size) return 0;
	size_t used = 0;
	int utf16 = size >= 2 &&
	            ((data[0] == 0xFEU && data[1] == 0xFFU) ||
	             (data[0] == 0xFFU && data[1] == 0xFEU));
	if (utf16) {
		int little_endian = data[0] == 0xFFU;
		for (size_t i = 2; i + 1 < size;) {
			uint32_t codepoint = little_endian
			                   ? (uint32_t)data[i] | (uint32_t)data[i + 1] << 8
			                   : (uint32_t)data[i] << 8 | (uint32_t)data[i + 1];
			i += 2;
			if (codepoint >= 0xD800U && codepoint <= 0xDBFFU && i + 1 < size) {
				uint32_t low = little_endian
				             ? (uint32_t)data[i] | (uint32_t)data[i + 1] << 8
				             : (uint32_t)data[i] << 8 | (uint32_t)data[i + 1];
				if (low >= 0xDC00U && low <= 0xDFFFU) {
					codepoint = 0x10000U + ((codepoint - 0xD800U) << 10) +
					            (low - 0xDC00U);
					i += 2;
				}
			}
			unsigned char encoded[4];
			size_t bytes = encode_utf8(codepoint, encoded);
			if (used + bytes >= out_size) break;
			memcpy(out + used, encoded, bytes);
			used += bytes;
		}
	} else {
		size_t i = size >= 3 && data[0] == 0xEFU && data[1] == 0xBBU &&
		           data[2] == 0xBFU ? 3 : 0;
		while (i < size) {
			size_t bytes = valid_utf8_sequence(data + i, size - i);
			unsigned char replacement[4];
			const unsigned char *source = data + i;
			if (!bytes) {
				bytes = encode_utf8(0xFFFDU, replacement);
				source = replacement;
				i++;
			} else {
				i += bytes;
			}
			if (used + bytes >= out_size) break;
			memcpy(out + used, source, bytes);
			used += bytes;
		}
	}
	out[used] = '\0';
	return used;
}

static int entity(const unsigned char *data, size_t size, size_t *consumed,
	              unsigned char value[4], size_t *value_size) {
	struct { const char *name; unsigned char value; } entities[] = {
		{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
		{ "&quot;", '"' }, { "&apos;", '\'' }, { "&nbsp;", ' ' }
	};
	for (unsigned int i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
		size_t length = strlen(entities[i].name);
		if (size >= length && !memcmp(data, entities[i].name, length)) {
			*consumed = length;
			value[0] = entities[i].value;
			*value_size = 1;
			return 1;
		}
	}
	if (size >= 4 && data[0] == '&' && data[1] == '#') {
		size_t i = 2;
		int base = 10;
		if (i < size && (data[i] == 'x' || data[i] == 'X')) {
			base = 16;
			i++;
		}
		uint32_t codepoint = 0;
		int digits = 0;
		for (; i < size && i < 12 && data[i] != ';'; i++) {
			int digit = data[i] >= '0' && data[i] <= '9' ? data[i] - '0'
			          : base == 16 && data[i] >= 'a' && data[i] <= 'f'
			          ? data[i] - 'a' + 10
			          : base == 16 && data[i] >= 'A' && data[i] <= 'F'
			          ? data[i] - 'A' + 10 : -1;
			if (digit < 0) return 0;
			codepoint = codepoint * (uint32_t)base + (uint32_t)digit;
			digits++;
		}
		if (digits && i < size && data[i] == ';' && codepoint > 0 &&
		    codepoint <= 0x10FFFFU &&
		    !(codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
			*consumed = i + 1;
			*value_size = encode_utf8(codepoint, value);
			return 1;
		}
	}
	return 0;
}

static int subtitle_text(const AVPacket *packet, enum AVCodecID codec,
	                     char out[SUBTITLE_TEXT_CAPACITY]) {
	const unsigned char *data = NULL;
	size_t size = subtitle_payload(packet, codec, &data);
	unsigned char normalized[SUBTITLE_NORMALIZED_CAPACITY];
	size = normalize_subtitle_text(data, size, normalized, sizeof(normalized));
	data = normalized;
	size_t used = 0;
	int in_ass_tag = 0;
	int in_html_tag = 0;
	for (size_t i = 0; i < size && used + 1 < SUBTITLE_TEXT_CAPACITY; i++) {
		unsigned char value = data[i];
		if (value == '\0' || value == '\r') continue;
		if (value == '{') { in_ass_tag = 1; continue; }
		if (in_ass_tag) {
			if (value == '}') in_ass_tag = 0;
			continue;
		}
		if (value == '<') { in_html_tag = 1; continue; }
		if (in_html_tag) {
			if (value == '>') in_html_tag = 0;
			continue;
		}
		if (value == '\\' && i + 1 < size) {
			unsigned char command = data[i + 1];
			if (command == 'N' || command == 'n') {
				out[used++] = '\n';
				i++;
				continue;
			}
			if (command == 'h') {
				out[used++] = ' ';
				i++;
				continue;
			}
		}
		if (value == '&') {
			size_t consumed = 0;
			unsigned char decoded[4];
			size_t decoded_size = 0;
			if (entity(data + i, size - i, &consumed, decoded,
			           &decoded_size)) {
				if (used + decoded_size >= SUBTITLE_TEXT_CAPACITY) break;
				memcpy(out + used, decoded, decoded_size);
				used += decoded_size;
				i += consumed - 1;
				continue;
			}
		}
		if (value >= 0x80U) {
			size_t bytes = valid_utf8_sequence(data + i, size - i);
			if (!bytes || used + bytes >= SUBTITLE_TEXT_CAPACITY) break;
			memcpy(out + used, data + i, bytes);
			used += bytes;
			i += bytes - 1;
			continue;
		}
		out[used++] = (char)value;
	}
	while (used && (out[used - 1] == ' ' || out[used - 1] == '\n')) used--;
	out[used] = '\0';
	return used > 0;
}

static int subtitle_worker(SceSize args, void *argp) {
	(void)args;
	VtSubtitleReader *reader = *(VtSubtitleReader **)argp;
	AVPacket *packet = av_packet_alloc();
	if (!packet) {
		reader->error = AVERROR(ENOMEM);
		return sceKernelExitThread(0);
	}
	while (!reader->cancel) {
		unsigned int requested_serial = reader->switch_serial;
		__sync_synchronize();
		if (requested_serial != reader->applied_serial) {
			int requested_stream = reader->requested_stream_index;
			uint64_t requested_position = reader->requested_position_ms;
			reader->read_index = reader->write_index = 0;
			reader->eof = 0;
			reader->error = 0;
			memset(reader->cues, 0, sizeof(reader->cues));
			if (requested_stream >= 0 &&
			    (unsigned int)requested_stream < reader->input.format->nb_streams) {
				AVStream *requested = reader->input.format->streams[requested_stream];
				int64_t target = av_rescale_q((int64_t)requested_position,
				                              (AVRational){ 1, 1000 },
				                              requested->time_base);
				int ret = avformat_seek_file(reader->input.format, requested_stream,
				                             INT64_MIN, target, target,
				                             AVSEEK_FLAG_BACKWARD);
				if (ret < 0)
					ret = av_seek_frame(reader->input.format, requested_stream,
					                    target, AVSEEK_FLAG_BACKWARD);
				if (ret >= 0) {
					avformat_flush(reader->input.format);
					reader->stream_index = requested_stream;
				} else {
					reader->error = ret;
					reader->stream_index = -1;
				}
			} else reader->stream_index = -1;
			__sync_synchronize();
			reader->applied_serial = requested_serial;
			continue;
		}
		if (reader->stream_index < 0 || reader->eof) {
			sceKernelDelayThread(5000);
			continue;
		}
		if (reader->write_index - reader->read_index >=
		    SUBTITLE_QUEUE_CAPACITY) {
			sceKernelDelayThread(5000);
			continue;
		}
		int ret = av_read_frame(reader->input.format, packet);
		if (ret < 0) {
			if (ret != AVERROR_EOF && !reader->cancel) reader->error = ret;
			reader->eof = 1;
			av_packet_unref(packet);
			continue;
		}
		if (packet->stream_index == reader->stream_index) {
			AVStream *stream = reader->input.format->streams[reader->stream_index];
			int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
			if (pts != AV_NOPTS_VALUE) {
				VtSubtitleCue cue;
				memset(&cue, 0, sizeof(cue));
				int64_t start = av_rescale_q(pts, stream->time_base,
				                             (AVRational){ 1, 1000 });
				int64_t duration = packet->duration > 0
				                 ? av_rescale_q(packet->duration, stream->time_base,
				                                (AVRational){ 1, 1000 })
				                 : 5000;
				if (start >= 0 && subtitle_text(packet, stream->codecpar->codec_id,
				                                cue.text)) {
					cue.start_ms = (uint64_t)start;
					cue.end_ms = cue.start_ms + (duration > 0
					                                 ? (uint64_t)duration : 5000ULL);
					unsigned int slot = reader->write_index % SUBTITLE_QUEUE_CAPACITY;
					reader->cues[slot] = cue;
					__sync_synchronize();
					reader->write_index++;
				}
			}
		}
		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	return sceKernelExitThread(0);
}

VtSubtitleReader *vt_subtitle_reader_create(void) {
	VtSubtitleReader *reader = calloc(1, sizeof(*reader));
	if (reader) reader->thid = -1;
	return reader;
}

static void subtitle_reader_stop(VtSubtitleReader *reader, int close_input) {
	if (!reader) return;
	reader->cancel = 1;
	__sync_synchronize();
	if (reader->started && reader->thid >= 0)
		sceKernelWaitThreadEnd(reader->thid, NULL, NULL);
	if (reader->started && reader->thid >= 0)
		sceKernelDeleteThread(reader->thid);
	if (close_input) media_input_close(&reader->input);
	reader->thid = -1;
	reader->started = 0;
	reader->stream_index = -1;
	reader->requested_stream_index = -1;
	reader->requested_position_ms = 0;
	reader->switch_serial = 0;
	reader->applied_serial = 0;
	reader->read_index = 0;
	reader->write_index = 0;
	reader->eof = 0;
	reader->error = 0;
	memset(reader->cues, 0, sizeof(reader->cues));
}

int vt_subtitle_reader_open(VtSubtitleReader *reader,
	                        const VtDecoderStreamFactory *factory,
	                        int stream_index, uint64_t start_position_ms,
	                        volatile int *open_cancel) {
	if (!reader || !factory || stream_index < 0) return AVERROR(EINVAL);
	int reuse_input = reader->input.format && reader->started &&
	                  reader->factory.open == factory->open &&
	                  reader->factory.opaque == factory->opaque;
	if (reuse_input) {
		if ((unsigned int)stream_index >= reader->input.format->nb_streams ||
		    reader->input.format->streams[stream_index]->codecpar->codec_type !=
		        AVMEDIA_TYPE_SUBTITLE ||
		    !text_subtitle_codec(
		        reader->input.format->streams[stream_index]->codecpar->codec_id))
			return AVERROR_DECODER_NOT_FOUND;
		reader->requested_stream_index = stream_index;
		reader->requested_position_ms = start_position_ms;
		__sync_synchronize();
		__sync_add_and_fetch(&reader->switch_serial, 1);
		return 0;
	}
	subtitle_reader_stop(reader, 1);
	reader->cancel = 0;
	int ret = 0;
	reader->factory = *factory;
	ret = media_input_open(&reader->input, factory,
	                       open_cancel ? open_cancel : &reader->cancel);
	if (ret < 0) return ret;
	if (open_cancel && *open_cancel) {
		media_input_close(&reader->input);
		return AVERROR_EXIT;
	}
	/* The UI cancellation flag is owned by other decoder operations after the
	 * open task returns. Rebind the long-lived subtitle worker to its private
	 * stop flag so an audio switch or seek cannot terminate it accidentally. */
	reader->input.cancel = &reader->cancel;
	reader->input.format->interrupt_callback.opaque = (void *)&reader->cancel;
	if ((unsigned int)stream_index >= reader->input.format->nb_streams ||
	    reader->input.format->streams[stream_index]->codecpar->codec_type !=
	        AVMEDIA_TYPE_SUBTITLE ||
	    !text_subtitle_codec(
	        reader->input.format->streams[stream_index]->codecpar->codec_id)) {
		media_input_close(&reader->input);
		return AVERROR_DECODER_NOT_FOUND;
	}
	reader->stream_index = stream_index;
	if (start_position_ms) {
		AVStream *stream = reader->input.format->streams[stream_index];
		int64_t target = av_rescale_q((int64_t)start_position_ms,
		                              (AVRational){ 1, 1000 }, stream->time_base);
		ret = avformat_seek_file(reader->input.format, stream_index, INT64_MIN,
		                         target, target, AVSEEK_FLAG_BACKWARD);
		if (ret < 0)
			ret = av_seek_frame(reader->input.format, stream_index, target,
			                    AVSEEK_FLAG_BACKWARD);
		if (ret < 0) {
			media_input_close(&reader->input);
			return ret;
		}
		avformat_flush(reader->input.format);
	}
	reader->requested_stream_index = stream_index;
	reader->requested_position_ms = start_position_ms;
	reader->switch_serial = 1;
	reader->applied_serial = 1;
	reader->thid = sceKernelCreateThread(
		"VitaMediaDeckSubtitles", subtitle_worker, SUBTITLE_THREAD_PRIORITY,
		SUBTITLE_THREAD_STACK, 0, 0, NULL);
	if (reader->thid < 0) {
		ret = reader->thid;
		media_input_close(&reader->input);
		return ret;
	}
	VtSubtitleReader *self = reader;
	ret = sceKernelStartThread(reader->thid, sizeof(self), &self);
	if (ret < 0) {
		sceKernelDeleteThread(reader->thid);
		reader->thid = -1;
		media_input_close(&reader->input);
		return ret;
	}
	reader->started = 1;
	return 0;
}

void vt_subtitle_reader_disable(VtSubtitleReader *reader) {
	if (!reader || !reader->started) return;
	reader->requested_stream_index = -1;
	reader->requested_position_ms = 0;
	__sync_synchronize();
	__sync_add_and_fetch(&reader->switch_serial, 1);
}

void vt_subtitle_reader_close(VtSubtitleReader *reader) {
	subtitle_reader_stop(reader, 1);
}

void vt_subtitle_reader_destroy(VtSubtitleReader *reader) {
	if (!reader) return;
	vt_subtitle_reader_close(reader);
	free(reader);
}

int vt_subtitle_reader_text(VtSubtitleReader *reader, uint64_t position_ms,
	                        char *text, size_t text_size) {
	if (!text || !text_size) return AVERROR(EINVAL);
	text[0] = '\0';
	if (!reader || !reader->started) return 0;
	if (reader->applied_serial != reader->switch_serial) return 0;
	for (;;) {
		__sync_synchronize();
		if (reader->read_index == reader->write_index)
			return reader->error ? reader->error : 0;
		VtSubtitleCue *cue =
		    &reader->cues[reader->read_index % SUBTITLE_QUEUE_CAPACITY];
		if (position_ms >= cue->end_ms) {
			reader->read_index++;
			continue;
		}
		if (position_ms < cue->start_ms) return 0;
		snprintf(text, text_size, "%s", cue->text);
		return 1;
	}
}
