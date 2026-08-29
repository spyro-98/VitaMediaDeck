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

#include "common/text_log.h"

#define TRACKS_AVIO_BUFFER_SIZE (32 * 1024)
#define SUBTITLE_QUEUE_CAPACITY 24u
#define SUBTITLE_READ_AHEAD_MS 5000ULL
#define SUBTITLE_SEEK_PREROLL_MS 15000ULL
#define SUBTITLE_PENDING_TIMEOUT_MS 5000U
#define SUBTITLE_ABORT_GRACE_MS 40U
#define SUBTITLE_TEXT_CAPACITY 512
#define SUBTITLE_NORMALIZED_CAPACITY 2048
#define SUBTITLE_THREAD_CREATE_PRIORITY  0x10000100
#define SUBTITLE_THREAD_RUNTIME_PRIORITY 0xA0
#define SUBTITLE_THREAD_STACK 0x40000

typedef struct VtMediaInput {
	VtDecoderStreamHandle stream;
	AVIOContext *avio;
	AVFormatContext *format;
	volatile int *cancel;
	const volatile unsigned int *request_serial;
	unsigned int operation_serial;
	int64_t timeline_origin_ms;
} VtMediaInput;

typedef struct VtSubtitleCue {
	uint64_t start_ms;
	uint64_t end_ms;
	char text[SUBTITLE_TEXT_CAPACITY];
} VtSubtitleCue;

struct VtSubtitleReader {
	VtDecoderStreamFactory factory;
	VtMediaInput input;
	void *input_factory_opaque;
	void (*input_abort)(void *opaque);
	void *input_abort_opaque;
	volatile int input_io_active;
	volatile int input_poisoned;
	int (*input_factory_open)(void *opaque, VtDecoderStreamHandle *out);
	int (*input_factory_open_cancelable)(void *opaque,
	                                     VtDecoderStreamHandle *out,
	                                     volatile int *cancel_flag);
	volatile int cancel;
	/* This flag is private to the subtitle cursor. A newer serial interrupts the
	 * current open/read, then the sole worker clears it before reusing that same
	 * cursor; the live decoder's stop flag is never borrowed or reset. */
	volatile int operation_cancel;
	volatile int request_lock;
	SceUID thid;
	int started;
	volatile int worker_done;
	int stream_index;
	volatile int requested_stream_index;
	volatile uint64_t requested_position_ms;
	/* 32-bit milliseconds are atomic on ARM and cover more than 49 days. Avoid
	 * torn volatile uint64_t loads between the render and subtitle threads. */
	volatile uint32_t playback_position_ms;
	volatile uint64_t demux_position_ms;
	volatile uint32_t pending_deadline_ms;
	volatile uint32_t abort_deadline_ms;
	volatile unsigned int switch_serial;
	volatile unsigned int applied_serial;
	volatile unsigned int state_serial;
	volatile unsigned int timeout_serial;
	volatile VtSubtitleReaderState state;
	volatile int cue_lock;
	unsigned int cue_count;
	int clock_stream_index;
	volatile int eof;
	volatile int error;
	VtSubtitleCue cues[SUBTITLE_QUEUE_CAPACITY];
};

static size_t normalize_subtitle_text(const unsigned char *data, size_t size,
	                                  unsigned char *out, size_t out_size);
static int text_subtitle_codec(enum AVCodecID codec);
static int64_t media_timeline_origin_ms(const AVFormatContext *format);
static void subtitle_publish_input_abort(VtSubtitleReader *reader);
static void subtitle_withdraw_input_abort(VtSubtitleReader *reader,
	                                      const VtMediaInput *input);
static void subtitle_input_io_begin(VtSubtitleReader *reader);
static void subtitle_input_io_end(VtSubtitleReader *reader);
static int subtitle_reader_start(VtSubtitleReader *reader);

static void subtitle_request_lock(VtSubtitleReader *reader) {
	while (__sync_lock_test_and_set(&reader->request_lock, 1))
		sceKernelDelayThread(100);
}

static void subtitle_request_unlock(VtSubtitleReader *reader) {
	__sync_lock_release(&reader->request_lock);
}

static void subtitle_cue_lock(VtSubtitleReader *reader) {
	while (__sync_lock_test_and_set(&reader->cue_lock, 1))
		sceKernelDelayThread(100);
}

static void subtitle_cue_unlock(VtSubtitleReader *reader) {
	__sync_lock_release(&reader->cue_lock);
}

static uint32_t subtitle_now_ms(void) {
	return (uint32_t)(sceKernelGetProcessTimeWide() / 1000ULL);
}

static int subtitle_deadline_reached(uint32_t now, uint32_t deadline) {
	return deadline && (int32_t)(now - deadline) >= 0;
}

static int media_input_interrupted(const VtMediaInput *input) {
	return !input || (input->cancel && *input->cancel) ||
	       (input->request_serial &&
	        *input->request_serial != input->operation_serial);
}

static int media_read(void *opaque, uint8_t *buffer, int size) {
	VtMediaInput *input = opaque;
	if (!input || !input->stream.read || media_input_interrupted(input))
		return AVERROR_EXIT;
	int ret = input->stream.read(input->stream.opaque, buffer, (size_t)size);
	if (ret == 0) return AVERROR_EOF;
	return ret < 0 ? AVERROR(EIO) : ret;
}

static int64_t media_seek(void *opaque, int64_t offset, int whence) {
	VtMediaInput *input = opaque;
	if (media_input_interrupted(input)) return AVERROR_EXIT;
	if (whence == AVSEEK_SIZE) return input->stream.size;
	if (!input->stream.seek) return AVERROR(ENOSYS);
	return input->stream.seek(input->stream.opaque, offset,
	                          whence & ~AVSEEK_FORCE);
}

static int media_interrupt(void *opaque) {
	return media_input_interrupted((const VtMediaInput *)opaque);
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

static int selected_subtitle_ready(const AVFormatContext *format,
	                               int stream_index) {
	if (!format || stream_index < 0 ||
	    (unsigned int)stream_index >= format->nb_streams) return 0;
	const AVStream *stream = format->streams[stream_index];
	return stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE &&
	       text_subtitle_codec(stream->codecpar->codec_id) &&
	       stream->time_base.num > 0 && stream->time_base.den > 0;
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
	                        volatile int *cancel,
	                        const volatile unsigned int *request_serial,
	                        unsigned int operation_serial,
	                        int required_subtitle_stream,
	                        VtSubtitleReader *subtitle_reader) {
	if (!input || !factory || (!factory->open && !factory->open_cancelable))
		return AVERROR(EINVAL);
	memset(input, 0, sizeof(*input));
	input->cancel = cancel;
	input->request_serial = request_serial;
	input->operation_serial = operation_serial;
	/* Keep local ux0:/uma0:/ media on the same seekable factory/AVIO path as
	 * playback. Passing Vita device paths directly to libavformat treats the
	 * device prefix as a URL protocol and makes subtitle activation fail. */
	int ret = factory->open_cancelable
	        ? factory->open_cancelable(factory->opaque, &input->stream, cancel)
	        : factory->open(factory->opaque, &input->stream);
	if (ret < 0 || !input->stream.read || !input->stream.seek) {
		media_input_close(input);
		return ret < 0 ? ret : AVERROR(EINVAL);
	}
	/* Publish the cursor as soon as the factory returns, before FFmpeg performs
	 * header reads/probing. Circle, a newer request, or the watchdog can now
	 * wake a blocking network read instead of waiting for its socket timeout. */
	if (subtitle_reader) subtitle_publish_input_abort(subtitle_reader);
	unsigned char *buffer = av_malloc(TRACKS_AVIO_BUFFER_SIZE);
	if (!buffer) {
		if (subtitle_reader)
			subtitle_withdraw_input_abort(subtitle_reader, input);
		media_input_close(input);
		return AVERROR(ENOMEM);
	}
	input->avio = avio_alloc_context(buffer, TRACKS_AVIO_BUFFER_SIZE, 0,
	                                input, media_read, NULL, media_seek);
	if (!input->avio) {
		av_free(buffer);
		if (subtitle_reader)
			subtitle_withdraw_input_abort(subtitle_reader, input);
		media_input_close(input);
		return AVERROR(ENOMEM);
	}
	input->avio->seekable = AVIO_SEEKABLE_NORMAL;
	input->format = avformat_alloc_context();
	if (!input->format) {
		if (subtitle_reader)
			subtitle_withdraw_input_abort(subtitle_reader, input);
		media_input_close(input);
		return AVERROR(ENOMEM);
	}
	input->format->pb = input->avio;
	input->format->flags |= AVFMT_FLAG_CUSTOM_IO;
	input->format->interrupt_callback.callback = media_interrupt;
	input->format->interrupt_callback.opaque = input;
	input->format->probesize = 1024 * 1024;
	input->format->max_analyze_duration = 2 * AV_TIME_BASE;
	if (subtitle_reader) subtitle_input_io_begin(subtitle_reader);
	ret = avformat_open_input(&input->format, NULL, NULL, NULL);
	if (ret >= 0) {
		int ready = required_subtitle_stream >= 0
		          ? selected_subtitle_ready(input->format,
		                                    required_subtitle_stream)
		          : track_streams_ready(input->format);
		/* A subtitle switch already carries the stable stream index discovered by
		 * the playback demux. Do not probe unrelated audio/video streams before a
		 * SubRip cursor can become active. Non-indexed/incomplete inputs retain the
		 * bounded compatibility probe. */
		if (!(indexed_container(input->format) && ready))
			ret = avformat_find_stream_info(input->format, NULL);
	}
	if (subtitle_reader) subtitle_input_io_end(subtitle_reader);
	if (ret < 0) {
		if (subtitle_reader)
			subtitle_withdraw_input_abort(subtitle_reader, input);
		media_input_close(input);
	}
	else input->timeline_origin_ms = media_timeline_origin_ms(input->format);
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
	int ret = media_input_open(&input, factory, cancel, NULL, 0, -1, NULL);
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

static int subtitle_stream_valid(const AVFormatContext *format, int stream_index) {
	return format && stream_index >= 0 &&
	       (unsigned int)stream_index < format->nb_streams &&
	       format->streams[stream_index]->codecpar->codec_type ==
	           AVMEDIA_TYPE_SUBTITLE &&
	       text_subtitle_codec(format->streams[stream_index]->codecpar->codec_id);
}

static int timed_stream(const AVStream *stream) {
	return stream && stream->time_base.num > 0 && stream->time_base.den > 0;
}

static int64_t media_timeline_origin_ms(const AVFormatContext *format) {
	if (!format) return 0;
	if (format->start_time != AV_NOPTS_VALUE)
		return av_rescale_q(format->start_time, AV_TIME_BASE_Q,
		                    (AVRational){ 1, 1000 });
	int64_t earliest_us = AV_NOPTS_VALUE;
	for (unsigned int i = 0; i < format->nb_streams; i++) {
		const AVStream *stream = format->streams[i];
		if (!timed_stream(stream) || stream->start_time == AV_NOPTS_VALUE ||
		    (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
		     stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
		     stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) ||
		    (stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) continue;
		int64_t value = av_rescale_q(stream->start_time, stream->time_base,
		                             AV_TIME_BASE_Q);
		if (earliest_us == AV_NOPTS_VALUE || value < earliest_us)
			earliest_us = value;
	}
	return earliest_us == AV_NOPTS_VALUE ? 0
	     : av_rescale_q(earliest_us, AV_TIME_BASE_Q,
	                    (AVRational){ 1, 1000 });
}

static int subtitle_clock_stream(const AVFormatContext *format,
	                             int subtitle_stream) {
	if (!format) return -1;
	int video_default = -1;
	int video_fallback = -1;
	int audio_default = -1;
	int audio_fallback = -1;
	for (unsigned int i = 0; i < format->nb_streams; i++) {
		const AVStream *stream = format->streams[i];
		if (!timed_stream(stream)) continue;
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
		    !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
			if ((stream->disposition & AV_DISPOSITION_DEFAULT) &&
			    video_default < 0) video_default = (int)i;
			if (video_fallback < 0) video_fallback = (int)i;
		} else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			if ((stream->disposition & AV_DISPOSITION_DEFAULT) &&
			    audio_default < 0) audio_default = (int)i;
			if (audio_fallback < 0) audio_fallback = (int)i;
		}
	}
	if (video_default >= 0) return video_default;
	if (video_fallback >= 0) return video_fallback;
	if (audio_default >= 0) return audio_default;
	if (audio_fallback >= 0) return audio_fallback;
	return subtitle_stream_valid(format, subtitle_stream) ? subtitle_stream : -1;
}

static void subtitle_apply_stream_policy(AVFormatContext *format,
	                                     int subtitle_stream,
	                                     int clock_stream) {
	if (!format) return;
	for (unsigned int i = 0; i < format->nb_streams; i++)
		format->streams[i]->discard =
		    (int)i == subtitle_stream || (int)i == clock_stream
		        ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
}

static int subtitle_seek_stream(AVFormatContext *format, int stream_index,
	                            uint64_t position_ms) {
	if (!format || stream_index < 0 ||
	    (unsigned int)stream_index >= format->nb_streams ||
	    !timed_stream(format->streams[stream_index])) return AVERROR(EINVAL);
	AVStream *stream = format->streams[stream_index];
	/* Custom AVIO retains a failed seek/EOF in these fields. Every fallback below
	 * is an independent reposition attempt and must start from a clean transport
	 * state, otherwise one sparse SubRip miss poisons the dense-clock fallback. */
	if (format->pb) {
		format->pb->error = 0;
		format->pb->eof_reached = 0;
	}
	int64_t logical_target_ms = (int64_t)position_ms +
	                            media_timeline_origin_ms(format);
	int64_t target = av_rescale_q(logical_target_ms,
	                              (AVRational){ 1, 1000 }, stream->time_base);
	int ret = avformat_seek_file(format, stream_index, INT64_MIN, target, target,
	                             0);
	if (ret < 0) {
		if (format->pb) {
			format->pb->error = 0;
			format->pb->eof_reached = 0;
		}
		ret = av_seek_frame(format, stream_index, target, AVSEEK_FLAG_BACKWARD);
	}
	return ret;
}

static int subtitle_seek(VtMediaInput *input, int subtitle_stream,
	                     int clock_stream, uint64_t position_ms) {
	if (!input || !input->format) return AVERROR(EINVAL);
	/* Indexed Matroska/MP4-style containers can jump directly to the preceding
	 * selected-subtitle event. This preserves even a long SubRip cue that began
	 * well before the requested point without scanning intervening video data. */
	if (indexed_container(input->format) && subtitle_stream != clock_stream) {
		int subtitle_ret = subtitle_seek_stream(input->format, subtitle_stream,
		                                          position_ms);
		if (subtitle_ret >= 0) {
			avformat_flush(input->format);
			return 0;
		}
	}
	/* Seek through one stable, dense video/audio timeline. A global seek treats
	 * every non-discarded track as active and can walk far back for sparse text.
	 * A bounded preroll recovers a cue that began before the dense keyframe but
	 * still spans the requested point without turning every switch into a scan
	 * from the start of a long movie. */
	uint64_t seek_position = position_ms > SUBTITLE_SEEK_PREROLL_MS
	                       ? position_ms - SUBTITLE_SEEK_PREROLL_MS : 0;
	int ret = subtitle_seek_stream(input->format, clock_stream, seek_position);
	if (ret < 0 && subtitle_stream != clock_stream)
		ret = subtitle_seek_stream(input->format, subtitle_stream, position_ms);
	if (ret >= 0) avformat_flush(input->format);
	return ret;
}

static int packet_position_ms(const AVFormatContext *format,
	                           const AVPacket *packet, int64_t timeline_origin_ms,
	                           uint64_t *position_ms) {
	if (!format || !packet || !position_ms || packet->stream_index < 0 ||
	    (unsigned int)packet->stream_index >= format->nb_streams) return 0;
	int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
	if (pts == AV_NOPTS_VALUE) return 0;
	int64_t value = av_rescale_q(pts, format->streams[packet->stream_index]->time_base,
	                            (AVRational){ 1, 1000 }) -
	                timeline_origin_ms;
	if (value < 0) return 0;
	*position_ms = (uint64_t)value;
	return 1;
}

static void subtitle_queue_reset(VtSubtitleReader *reader,
	                              uint64_t position_ms) {
	subtitle_cue_lock(reader);
	reader->cue_count = 0;
	memset(reader->cues, 0, sizeof(reader->cues));
	subtitle_cue_unlock(reader);
	reader->playback_position_ms = position_ms > UINT32_MAX
	                             ? UINT32_MAX : (uint32_t)position_ms;
	reader->demux_position_ms = position_ms;
	reader->eof = 0;
	reader->error = 0;
}

static void subtitle_queue_remove(VtSubtitleReader *reader,
	                              unsigned int index) {
	if (!reader || index >= reader->cue_count) return;
	for (unsigned int i = index; i + 1 < reader->cue_count; i++)
		reader->cues[i] = reader->cues[i + 1];
	reader->cue_count--;
	memset(&reader->cues[reader->cue_count], 0,
	       sizeof(reader->cues[reader->cue_count]));
}

static void subtitle_queue_expire(VtSubtitleReader *reader,
	                              uint64_t position_ms) {
	for (unsigned int i = 0; i < reader->cue_count;) {
		if (position_ms >= reader->cues[i].end_ms)
			subtitle_queue_remove(reader, i);
		else i++;
	}
}

static void subtitle_queue_push(VtSubtitleReader *reader,
	                            const VtSubtitleCue *cue,
	                            uint64_t playback_position_ms) {
	if (!reader || !cue) return;
	subtitle_cue_lock(reader);
	subtitle_queue_expire(reader, playback_position_ms);
	if (reader->cue_count >= SUBTITLE_QUEUE_CAPACITY) {
		/* Keep the nearest cues. A long-lived overlay occupies one slot but cannot
		 * pin expired dialogue behind it or grow the queue without bound. */
		unsigned int farthest = 0;
		for (unsigned int i = 1; i < reader->cue_count; i++)
			if (reader->cues[i].start_ms > reader->cues[farthest].start_ms)
				farthest = i;
		if (cue->start_ms >= reader->cues[farthest].start_ms) {
			subtitle_cue_unlock(reader);
			return;
		}
		subtitle_queue_remove(reader, farthest);
	}
	unsigned int insert = reader->cue_count;
	while (insert > 0 &&
	       reader->cues[insert - 1].start_ms > cue->start_ms) {
		reader->cues[insert] = reader->cues[insert - 1];
		insert--;
	}
	reader->cues[insert] = *cue;
	reader->cue_count++;
	subtitle_cue_unlock(reader);
}

static size_t subtitle_append_text(char *out, size_t out_size, size_t used,
	                               const char *text) {
	if (!out || !out_size || !text || !text[0] || used >= out_size - 1)
		return used;
	if (used) {
		if (used + 1 >= out_size) return used;
		out[used++] = '\n';
	}
	const unsigned char *source = (const unsigned char *)text;
	size_t size = strlen(text);
	for (size_t i = 0; i < size;) {
		size_t bytes = valid_utf8_sequence(source + i, size - i);
		if (!bytes || used + bytes >= out_size) break;
		memcpy(out + used, source + i, bytes);
		used += bytes;
		i += bytes;
	}
	out[used] = '\0';
	return used;
}

static void subtitle_input_discard(VtSubtitleReader *reader) {
	if (!reader) return;
	/* Stop publishing the non-owning abort target before the worker releases the
	 * cursor. A UI cancellation holds the same lock while invoking abort. */
	subtitle_request_lock(reader);
	reader->input_abort = NULL;
	reader->input_abort_opaque = NULL;
	reader->input_io_active = 0;
	reader->input_poisoned = 0;
	subtitle_request_unlock(reader);
	media_input_close(&reader->input);
	reader->input_factory_open = NULL;
	reader->input_factory_open_cancelable = NULL;
	reader->input_factory_opaque = NULL;
}

static void subtitle_publish_input_abort(VtSubtitleReader *reader) {
	if (!reader) return;
	subtitle_request_lock(reader);
	reader->input_abort = reader->input.stream.abort;
	reader->input_abort_opaque = reader->input.stream.opaque;
	reader->input_io_active = 0;
	reader->input_poisoned = 0;
	subtitle_request_unlock(reader);
}

static void subtitle_withdraw_input_abort(VtSubtitleReader *reader,
	                                      const VtMediaInput *input) {
	if (!reader) return;
	subtitle_request_lock(reader);
	/* Do not erase a newer cursor that may already have replaced this one. */
	if (!input || reader->input_abort_opaque == input->stream.opaque) {
		reader->input_abort = NULL;
		reader->input_abort_opaque = NULL;
		reader->input_io_active = 0;
		reader->input_poisoned = 0;
	}
	subtitle_request_unlock(reader);
}

static void subtitle_input_io_begin(VtSubtitleReader *reader) {
	subtitle_request_lock(reader);
	reader->input_io_active = 1;
	__sync_synchronize();
	subtitle_request_unlock(reader);
}

static void subtitle_input_io_end(VtSubtitleReader *reader) {
	subtitle_request_lock(reader);
	reader->input_io_active = 0;
	__sync_synchronize();
	subtitle_request_unlock(reader);
}

/* request_lock must be held. The callback is non-owning and may only wake the
 * worker; ownership remains with subtitle_input_discard(). */
static void subtitle_abort_input_locked(VtSubtitleReader *reader) {
	if (reader && reader->input_io_active && reader->input_abort &&
	    reader->input_abort_opaque) {
		reader->input_poisoned = 1;
		__sync_synchronize();
		reader->input_abort(reader->input_abort_opaque);
	}
}

static int subtitle_request_timed_out(const VtSubtitleReader *reader,
	                                  unsigned int serial) {
	return reader && reader->timeout_serial == serial;
}

void vt_subtitle_reader_tick(VtSubtitleReader *reader) {
	if (!reader) return;
	uint32_t now = subtitle_now_ms();
	uint32_t abort_deadline = reader->abort_deadline_ms;
	if (subtitle_deadline_reached(now, abort_deadline)) {
		subtitle_request_lock(reader);
		if (reader->operation_cancel &&
		    subtitle_deadline_reached(subtitle_now_ms(),
		                              reader->abort_deadline_ms))
			subtitle_abort_input_locked(reader);
		reader->abort_deadline_ms = 0;
		subtitle_request_unlock(reader);
	}
	if (reader->state != VT_SUBTITLE_READER_PENDING ||
	    reader->requested_stream_index < 0) return;
	uint32_t deadline = reader->pending_deadline_ms;
	if (!subtitle_deadline_reached(now, deadline)) return;

	int timed_out = 0;
	subtitle_request_lock(reader);
	unsigned int serial = reader->switch_serial;
	if (reader->state == VT_SUBTITLE_READER_PENDING &&
	    reader->state_serial == serial && reader->requested_stream_index >= 0 &&
	    subtitle_deadline_reached(subtitle_now_ms(),
	                              reader->pending_deadline_ms)) {
		reader->operation_cancel = 1;
		reader->timeout_serial = serial;
		reader->pending_deadline_ms = 0;
		reader->abort_deadline_ms = 0;
		reader->error = AVERROR(ETIMEDOUT);
		reader->state_serial = serial;
		__sync_synchronize();
		reader->state = VT_SUBTITLE_READER_FAILED;
		subtitle_abort_input_locked(reader);
		timed_out = 1;
	}
	subtitle_request_unlock(reader);
	if (timed_out) log_printf("subtitle: timeout serial=%u\n", serial);
}

static int subtitle_worker(SceSize args, void *argp) {
	(void)args;
	VtSubtitleReader *reader = *(VtSubtitleReader **)argp;
	int priority_before = sceKernelGetThreadCurrentPriority();
	int priority_ret = sceKernelChangeThreadPriority(
		sceKernelGetThreadId(), SUBTITLE_THREAD_RUNTIME_PRIORITY);
	log_printf("subtitle: thread priority %d -> %d ret=0x%08X\n",
	           priority_before, sceKernelGetThreadCurrentPriority(),
	           (unsigned)priority_ret);
	AVPacket *packet = av_packet_alloc();
	if (!packet) {
		subtitle_request_lock(reader);
		unsigned int failed_serial = reader->switch_serial;
		reader->error = AVERROR(ENOMEM);
		reader->applied_serial = failed_serial;
		reader->state_serial = failed_serial;
		reader->pending_deadline_ms = 0;
		__sync_synchronize();
		reader->state = VT_SUBTITLE_READER_FAILED;
		subtitle_request_unlock(reader);
		__sync_synchronize();
		reader->worker_done = 1;
		return sceKernelExitThread(0);
	}
	while (!reader->cancel) {
		unsigned int requested_serial = reader->switch_serial;
		__sync_synchronize();
		if (requested_serial != reader->applied_serial) {
			subtitle_request_lock(reader);
			requested_serial = reader->switch_serial;
			if (reader->cancel) {
				subtitle_request_unlock(reader);
				break;
			}
			if (requested_serial == reader->applied_serial) {
				subtitle_request_unlock(reader);
				continue;
			}
			VtDecoderStreamFactory requested_factory = reader->factory;
			int requested_stream = reader->requested_stream_index;
			uint64_t requested_position = reader->requested_position_ms;
			int request_timed_out =
			    subtitle_request_timed_out(reader, requested_serial);
			reader->operation_cancel = request_timed_out ? 1 : 0;
			reader->abort_deadline_ms = 0;
			__sync_synchronize();
			subtitle_request_unlock(reader);
			if (requested_serial != reader->switch_serial) continue;
			subtitle_queue_reset(reader, requested_position);
			if (request_timed_out) {
				subtitle_input_discard(reader);
				subtitle_request_lock(reader);
				if (requested_serial == reader->switch_serial) {
					reader->error = AVERROR(ETIMEDOUT);
					reader->stream_index = -1;
					reader->clock_stream_index = -1;
					reader->pending_deadline_ms = 0;
					reader->applied_serial = requested_serial;
					reader->state_serial = requested_serial;
					__sync_synchronize();
					reader->state = VT_SUBTITLE_READER_FAILED;
				}
				subtitle_request_unlock(reader);
				continue;
			}
			if (requested_stream < 0) {
				subtitle_input_discard(reader);
				int applied = 0;
				subtitle_request_lock(reader);
				if (requested_serial == reader->switch_serial) {
					reader->stream_index = -1;
					reader->clock_stream_index = -1;
					reader->pending_deadline_ms = 0;
					reader->applied_serial = requested_serial;
					reader->state_serial = requested_serial;
					__sync_synchronize();
					reader->state = VT_SUBTITLE_READER_DISABLED;
					applied = 1;
				}
				subtitle_request_unlock(reader);
				if (applied)
					log_printf("subtitle: disabled serial=%u\n", requested_serial);
				continue;
			}

			subtitle_request_lock(reader);
			int input_poisoned = reader->input_poisoned;
			subtitle_request_unlock(reader);
			int reuse_input = !input_poisoned && reader->input.format &&
			    reader->input_factory_open == requested_factory.open &&
			    reader->input_factory_open_cancelable ==
			        requested_factory.open_cancelable &&
			    reader->input_factory_opaque == requested_factory.opaque;
			int ret = 0;
			uint64_t stage_started = sceKernelGetProcessTimeWide();
			if (!reuse_input) {
				subtitle_input_discard(reader);
				log_printf("subtitle: open serial=%u stream=%d\n",
				           requested_serial, requested_stream);
				ret = media_input_open(&reader->input, &requested_factory,
				                       &reader->operation_cancel,
				                       &reader->switch_serial,
				                       requested_serial, requested_stream, reader);
				if (ret >= 0) {
					reader->input_factory_open = requested_factory.open;
					reader->input_factory_open_cancelable =
					    requested_factory.open_cancelable;
					reader->input_factory_opaque = requested_factory.opaque;
				}
			} else {
				reader->input.cancel = &reader->operation_cancel;
				reader->input.request_serial = &reader->switch_serial;
				reader->input.operation_serial = requested_serial;
				reader->input.format->interrupt_callback.opaque = &reader->input;
				/* A cooperative serial interrupt does not poison the cursor. Clear
				 * transient AVIO error/EOF state before the mandatory reposition so
				 * local and responsive remote inputs avoid reconnecting. */
				if (reader->input.avio) {
					reader->input.avio->error = 0;
					reader->input.avio->eof_reached = 0;
				}
				avformat_flush(reader->input.format);
			}
			uint64_t open_elapsed = sceKernelGetProcessTimeWide() - stage_started;
			if (reader->cancel) break;
			if (requested_serial != reader->switch_serial) {
				subtitle_input_discard(reader);
				continue;
			}
			int clock_stream = -1;
			if (ret >= 0 && !subtitle_stream_valid(reader->input.format,
			                                           requested_stream)) {
				ret = AVERROR_DECODER_NOT_FOUND;
			} else if (ret >= 0) {
				clock_stream = subtitle_clock_stream(reader->input.format,
				                                        requested_stream);
				if (clock_stream < 0) ret = AVERROR_STREAM_NOT_FOUND;
				else subtitle_apply_stream_policy(reader->input.format,
				                                  requested_stream, clock_stream);
			}
			stage_started = sceKernelGetProcessTimeWide();
			if (ret >= 0 && (reuse_input || requested_position > 0)) {
				subtitle_input_io_begin(reader);
				ret = subtitle_seek(&reader->input, requested_stream, clock_stream,
				                    requested_position);
				subtitle_input_io_end(reader);
			}
			uint64_t seek_elapsed = sceKernelGetProcessTimeWide() - stage_started;
			if (reader->cancel) break;
			subtitle_request_lock(reader);
			int stale_request = requested_serial != reader->switch_serial;
			if (!stale_request &&
			    subtitle_request_timed_out(reader, requested_serial))
				ret = AVERROR(ETIMEDOUT);
			if (!stale_request) {
				reader->error = ret < 0 ? ret : 0;
				reader->stream_index = ret >= 0 ? requested_stream : -1;
				reader->clock_stream_index = ret >= 0 ? clock_stream : -1;
				reader->pending_deadline_ms = 0;
				reader->applied_serial = requested_serial;
				reader->state_serial = requested_serial;
				__sync_synchronize();
				reader->state = ret >= 0 ? VT_SUBTITLE_READER_ACTIVE
				                         : VT_SUBTITLE_READER_FAILED;
			}
			subtitle_request_unlock(reader);
			if (stale_request || ret < 0) subtitle_input_discard(reader);
			if (stale_request) continue;
			log_printf("subtitle: applied serial=%u stream=%d ret=%d open=%llu us seek=%llu us\n",
			           requested_serial, requested_stream, ret,
			           (unsigned long long)open_elapsed,
			           (unsigned long long)seek_elapsed);
			continue;
		}
		if (reader->state != VT_SUBTITLE_READER_ACTIVE ||
		    reader->stream_index < 0 || reader->eof) {
			sceKernelDelayThread(5000);
			continue;
		}
		uint64_t playback_position = reader->playback_position_ms;
		uint64_t horizon = playback_position > UINT64_MAX - SUBTITLE_READ_AHEAD_MS
		                 ? UINT64_MAX
		                 : playback_position + SUBTITLE_READ_AHEAD_MS;
		if (reader->demux_position_ms >= horizon) {
			sceKernelDelayThread(5000);
			continue;
		}
		subtitle_input_io_begin(reader);
		int ret = av_read_frame(reader->input.format, packet);
		subtitle_input_io_end(reader);
		/* A cancel-aware source can still return one buffered packet while a newer
		 * request is being published. Reuse is safe after a cooperative interruption
		 * and reposition; a destructive socket abort explicitly poisons the cursor. */
		if (reader->switch_serial != reader->applied_serial) {
			av_packet_unref(packet);
			subtitle_request_lock(reader);
			int poisoned = reader->input_poisoned;
			subtitle_request_unlock(reader);
			if (poisoned) subtitle_input_discard(reader);
			continue;
		}
		if (ret < 0) {
			av_packet_unref(packet);
			if (reader->cancel) break;
			int read_failed = 0;
			unsigned int failed_serial = 0;
			subtitle_request_lock(reader);
			int stale_read = reader->switch_serial != reader->applied_serial;
			if (!stale_read && ret == AVERROR_EOF) reader->eof = 1;
			else if (!stale_read) {
				reader->error = ret;
				reader->stream_index = -1;
				reader->clock_stream_index = -1;
				reader->pending_deadline_ms = 0;
				reader->state_serial = reader->applied_serial;
				__sync_synchronize();
				reader->state = VT_SUBTITLE_READER_FAILED;
				failed_serial = reader->applied_serial;
				read_failed = 1;
			}
			subtitle_request_unlock(reader);
			if (stale_read || read_failed) subtitle_input_discard(reader);
			if (read_failed)
				log_printf("subtitle: read failed serial=%u ret=%d\n",
				           failed_serial, ret);
			continue;
		}
		uint64_t packet_ms = 0;
		if (packet->stream_index == reader->clock_stream_index &&
		    packet_position_ms(reader->input.format, packet,
		                       reader->input.timeline_origin_ms, &packet_ms) &&
		    packet_ms > reader->demux_position_ms)
			reader->demux_position_ms = packet_ms;
		if (packet->stream_index == reader->stream_index) {
			AVStream *stream = reader->input.format->streams[reader->stream_index];
			int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
			if (pts != AV_NOPTS_VALUE) {
				VtSubtitleCue cue;
				memset(&cue, 0, sizeof(cue));
				int64_t start = av_rescale_q(pts, stream->time_base,
				                             (AVRational){ 1, 1000 }) -
				                reader->input.timeline_origin_ms;
				int64_t duration = packet->duration > 0
				                 ? av_rescale_q(packet->duration, stream->time_base,
				                                (AVRational){ 1, 1000 })
				                 : 5000;
				int64_t end = start + (duration > 0 ? duration : 5000);
				if (end > 0 && subtitle_text(packet, stream->codecpar->codec_id,
				                                cue.text)) {
					cue.start_ms = start > 0 ? (uint64_t)start : 0;
					cue.end_ms = (uint64_t)end;
					if (cue.end_ms > reader->playback_position_ms)
						subtitle_queue_push(reader, &cue,
						                    reader->playback_position_ms);
				}
			}
		}
		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	reader->worker_done = 1;
	__sync_synchronize();
	return sceKernelExitThread(0);
}

VtSubtitleReader *vt_subtitle_reader_create(void) {
	VtSubtitleReader *reader = calloc(1, sizeof(*reader));
	if (reader) {
		reader->thid = -1;
		reader->stream_index = -1;
		reader->requested_stream_index = -1;
		reader->clock_stream_index = -1;
		reader->state = VT_SUBTITLE_READER_DISABLED;
		/* Reserve the worker before the video decoder claims its runtime memory.
		 * Track selection can then remain a cheap serial request during playback. */
		int start_ret = subtitle_reader_start(reader);
		if (start_ret < 0)
			log_printf("subtitle: early worker start failed ret=%d; will retry on selection\n",
			           start_ret);
	}
	return reader;
}

static int subtitle_reader_start(VtSubtitleReader *reader) {
	if (!reader) return AVERROR(EINVAL);
	if (reader->started && !reader->worker_done) return 0;
	if (reader->started && reader->thid >= 0) {
		sceKernelWaitThreadEnd(reader->thid, NULL, NULL);
		sceKernelDeleteThread(reader->thid);
	}
	reader->thid = -1;
	reader->started = 0;
	reader->worker_done = 0;
	reader->cancel = 0;
	reader->operation_cancel = 0;
	reader->thid = sceKernelCreateThread(
		"VitaMediaDeckSubtitles", subtitle_worker,
		SUBTITLE_THREAD_CREATE_PRIORITY,
		SUBTITLE_THREAD_STACK, 0, 0, NULL);
	if (reader->thid < 0) return reader->thid;
	VtSubtitleReader *self = reader;
	int ret = sceKernelStartThread(reader->thid, sizeof(self), &self);
	if (ret < 0) {
		sceKernelDeleteThread(reader->thid);
		reader->thid = -1;
		return ret;
	}
	reader->started = 1;
	return 0;
}

static void subtitle_reader_stop(VtSubtitleReader *reader, int close_input) {
	if (!reader) return;
	subtitle_request_lock(reader);
	reader->cancel = 1;
	reader->operation_cancel = 1;
	__sync_add_and_fetch(&reader->switch_serial, 1);
	subtitle_abort_input_locked(reader);
	__sync_synchronize();
	subtitle_request_unlock(reader);
	if (reader->started && reader->thid >= 0)
		sceKernelWaitThreadEnd(reader->thid, NULL, NULL);
	if (reader->started && reader->thid >= 0)
		sceKernelDeleteThread(reader->thid);
	if (close_input) media_input_close(&reader->input);
	reader->thid = -1;
	reader->started = 0;
	reader->worker_done = 0;
	reader->operation_cancel = 0;
	reader->request_lock = 0;
	reader->stream_index = -1;
	reader->requested_stream_index = -1;
	reader->requested_position_ms = 0;
	reader->playback_position_ms = 0;
	reader->demux_position_ms = 0;
	reader->pending_deadline_ms = 0;
	reader->abort_deadline_ms = 0;
	reader->switch_serial = 0;
	reader->applied_serial = 0;
	reader->state_serial = 0;
	reader->timeout_serial = 0;
	reader->state = VT_SUBTITLE_READER_DISABLED;
	reader->cue_lock = 0;
	reader->cue_count = 0;
	reader->clock_stream_index = -1;
	reader->eof = 0;
	reader->error = 0;
	reader->input_factory_open = NULL;
	reader->input_factory_open_cancelable = NULL;
	reader->input_factory_opaque = NULL;
	reader->input_abort = NULL;
	reader->input_abort_opaque = NULL;
	reader->input_io_active = 0;
	reader->input_poisoned = 0;
	memset(reader->cues, 0, sizeof(reader->cues));
}

int vt_subtitle_reader_open(VtSubtitleReader *reader,
	                        const VtDecoderStreamFactory *factory,
	                        int stream_index, uint64_t start_position_ms,
	                        volatile int *open_cancel) {
	if (!reader || !factory || (!factory->open && !factory->open_cancelable) ||
	    stream_index < 0)
		return AVERROR(EINVAL);
	/* open_cancel belongs to the player session. It may reject a request that
	 * has not started, but the long-lived subtitle worker never retains or resets
	 * it; track selection therefore cannot stop the video/audio decoder. */
	if (open_cancel && *open_cancel) return AVERROR_EXIT;
	subtitle_request_lock(reader);
	reader->operation_cancel = 1;
	uint32_t abort_deadline = subtitle_now_ms() + SUBTITLE_ABORT_GRACE_MS;
	reader->abort_deadline_ms = abort_deadline ? abort_deadline : 1U;
	reader->factory = *factory;
	reader->requested_stream_index = stream_index;
	reader->requested_position_ms = start_position_ms;
	reader->playback_position_ms = start_position_ms > UINT32_MAX
	                              ? UINT32_MAX
	                              : (uint32_t)start_position_ms;
	reader->error = 0;
	unsigned int serial = __sync_add_and_fetch(&reader->switch_serial, 1);
	reader->timeout_serial = 0;
	reader->state_serial = serial;
	uint32_t deadline = subtitle_now_ms() + SUBTITLE_PENDING_TIMEOUT_MS;
	reader->pending_deadline_ms = deadline ? deadline : 1U;
	__sync_synchronize();
	reader->state = VT_SUBTITLE_READER_PENDING;
	subtitle_request_unlock(reader);
	int ret = subtitle_reader_start(reader);
	if (ret < 0) {
		reader->error = ret;
		reader->pending_deadline_ms = 0;
		reader->state_serial = serial;
		reader->state = VT_SUBTITLE_READER_FAILED;
		log_printf("subtitle: worker start failed serial=%u ret=%d\n", serial,
		           ret);
		return ret;
	}
	log_printf("subtitle: queued serial=%u stream=%d position=%llu ms\n",
	           serial, stream_index, (unsigned long long)start_position_ms);
	return 0;
}

void vt_subtitle_reader_disable(VtSubtitleReader *reader) {
	if (!reader) return;
	subtitle_request_lock(reader);
	reader->operation_cancel = 1;
	uint32_t abort_deadline = subtitle_now_ms() + SUBTITLE_ABORT_GRACE_MS;
	reader->abort_deadline_ms = abort_deadline ? abort_deadline : 1U;
	reader->requested_stream_index = -1;
	reader->requested_position_ms = 0;
	reader->playback_position_ms = 0;
	reader->pending_deadline_ms = 0;
	reader->timeout_serial = 0;
	unsigned int serial = reader->switch_serial;
	if (reader->started) serial = __sync_add_and_fetch(&reader->switch_serial, 1);
	reader->state_serial = serial;
	__sync_synchronize();
	reader->state = VT_SUBTITLE_READER_DISABLED;
	subtitle_request_unlock(reader);
}

void vt_subtitle_reader_close(VtSubtitleReader *reader) {
	subtitle_reader_stop(reader, 1);
}

void vt_subtitle_reader_destroy(VtSubtitleReader *reader) {
	if (!reader) return;
	vt_subtitle_reader_close(reader);
	free(reader);
}

VtSubtitleReaderState vt_subtitle_reader_state(const VtSubtitleReader *reader) {
	if (!reader) return VT_SUBTITLE_READER_DISABLED;
	unsigned int serial = reader->switch_serial;
	__sync_synchronize();
	if (reader->requested_stream_index < 0)
		return VT_SUBTITLE_READER_DISABLED;
	if (reader->state == VT_SUBTITLE_READER_FAILED &&
	    reader->state_serial == serial) return VT_SUBTITLE_READER_FAILED;
	if (!reader->started || reader->applied_serial != serial ||
	    reader->state_serial != serial)
		return VT_SUBTITLE_READER_PENDING;
	return reader->state;
}

int vt_subtitle_reader_error(const VtSubtitleReader *reader) {
	return vt_subtitle_reader_state(reader) == VT_SUBTITLE_READER_FAILED
	     ? reader->error : 0;
}

int vt_subtitle_reader_text(VtSubtitleReader *reader, uint64_t position_ms,
	                        char *text, size_t text_size) {
	if (!text || !text_size) return AVERROR(EINVAL);
	text[0] = '\0';
	if (!reader || !reader->started) return 0;
	reader->playback_position_ms = position_ms > UINT32_MAX
	                              ? UINT32_MAX : (uint32_t)position_ms;
	vt_subtitle_reader_tick(reader);
	VtSubtitleReaderState state = vt_subtitle_reader_state(reader);
	if (state == VT_SUBTITLE_READER_FAILED)
		return reader->error ? reader->error : AVERROR_INVALIDDATA;
	if (state != VT_SUBTITLE_READER_ACTIVE) return 0;
	subtitle_cue_lock(reader);
	subtitle_queue_expire(reader, position_ms);
	size_t used = 0;
	for (unsigned int i = 0; i < reader->cue_count; i++) {
		const VtSubtitleCue *cue = &reader->cues[i];
		if (position_ms < cue->start_ms || position_ms >= cue->end_ms) continue;
		used = subtitle_append_text(text, text_size, used, cue->text);
		if (used + 1 >= text_size) break;
	}
	subtitle_cue_unlock(reader);
	return used > 0;
}
