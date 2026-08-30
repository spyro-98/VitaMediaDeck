#include "media/background_playback.h"

#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <FLAC/stream_decoder.h>
#include <mpg123.h>
#include <psp2/audioout.h>
#include <psp2/avplayer.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include "common/text_log.h"
#include "media/audio_volume.h"
#include "media/avplayer_init.h"
#include "media/vita_decoder.h"
#include "settings/preferences.h"
#include "system/background_audio.h"

#define BACKGROUND_THREAD_PRIORITY 0x10000100
#define BACKGROUND_THREAD_STACK    0x40000
#define BACKGROUND_AUDIO_GRAIN     1024
#define BACKGROUND_AUDIO_RATE      48000
#define AVPLAYER_INVALID_HANDLE    ((SceAvPlayerHandle)-1)
#define BACKGROUND_VIDEO_FORMAT    SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1

typedef struct {
	void *self;
	SceUID thid;
	volatile int done;
	volatile int cancel;
	volatile int activate;
	volatile unsigned int toggle_serial;
	volatile unsigned int seek_serial;
	uint64_t seek_position_ms;
	uint64_t start_position_ms;
	char input_path[512];
	int mp3_source;
	int flac_source;
	int decoder_source;
	int remote_video_source;
	VtNetworkStreamFactory remote_factory;
	int audio_track;
	volatile int player_lock;
	SceAvPlayerHandle active_player;
	VtDecoderPlayer *active_decoder;
	volatile int local_video_source;
	volatile int video_in_gpu;
	int video_have_frame;
	SceAvPlayerFrameInfo video_frame;
	vita2d_texture video_texture;
	VtBackgroundFullscreenResume fullscreen_resume;
	void *fullscreen_resume_ctx;
	VtBackgroundPlaybackSnapshot snapshot;
} BackgroundPlaybackJob;

static BackgroundPlaybackJob g_background;
static unsigned int g_activation_serial;
static volatile int g_snapshot_lock;

static void snapshot_lock(void) {
	while (__sync_lock_test_and_set(&g_snapshot_lock, 1))
		sceKernelDelayThread(100);
}

static void snapshot_unlock(void) {
	__sync_lock_release(&g_snapshot_lock);
}

static void player_lock(BackgroundPlaybackJob *job) {
	while (__sync_lock_test_and_set(&job->player_lock, 1))
		sceKernelDelayThread(100);
}

static void player_unlock(BackgroundPlaybackJob *job) {
	__sync_lock_release(&job->player_lock);
}

static uint64_t player_start_position(BackgroundPlaybackJob *job) {
	player_lock(job);
	uint64_t position_ms = job->start_position_ms;
	player_unlock(job);
	return position_ms;
}

/* A decoder frame can remain referenced by GXM after draw_video() releases the
 * player lock.  A background seek must therefore acquire the decoder only in a
 * frame boundary where the UI has already called render_complete(). */
static int player_lock_when_video_idle(BackgroundPlaybackJob *job,
	                                   uint64_t timeout_us) {
	uint64_t deadline = timeout_us
	                  ? sceKernelGetProcessTimeWide() + timeout_us : 0;
	for (;;) {
		player_lock(job);
		if (!job->video_in_gpu) return 1;
		player_unlock(job);
		if (deadline && sceKernelGetProcessTimeWide() >= deadline) return 0;
		sceKernelDelayThread(1000);
	}
}

static void copy_text(char *out, size_t out_size, const char *value) {
	if (!out || out_size == 0) return;
	snprintf(out, out_size, "%s", value ? value : "");
}

static void set_state(VtBackgroundPlaybackState state, int error) {
	snapshot_lock();
	g_background.snapshot.state = state;
	g_background.snapshot.error = error;
	snapshot_unlock();
}

static int path_is_mp3(const char *path) {
	if (!path) return 0;
	size_t length = strlen(path);
	return length >= 4 && path[length - 4] == '.' &&
	       (path[length - 3] == 'm' || path[length - 3] == 'M') &&
	       (path[length - 2] == 'p' || path[length - 2] == 'P') &&
	       path[length - 1] == '3';
}

static int path_is_flac(const char *path) {
	if (!path) return 0;
	size_t length = strlen(path);
	return length >= 5 && path[length - 5] == '.' &&
	       (path[length - 4] == 'f' || path[length - 4] == 'F') &&
	       (path[length - 3] == 'l' || path[length - 3] == 'L') &&
	       (path[length - 2] == 'a' || path[length - 2] == 'A') &&
	       (path[length - 1] == 'c' || path[length - 1] == 'C');
}

static void set_port_volume(int port, int percent, int *applied) {
	if (port < 0 || !applied || *applied == percent) return;
	*applied = percent;
	int hardware_percent = percent > 100 ? 100 : percent;
	if (hardware_percent < 0) hardware_percent = 0;
	int value = hardware_percent * SCE_AUDIO_VOLUME_0DB / 100;
	int volume[2] = { value, value };
	sceAudioOutSetVolume(port,
	                     SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH,
	                     volume);
}

static void amplify_pcm16(const int16_t *source, int16_t *destination,
	                      uint32_t bytes, int percent) {
	uint32_t samples = bytes / sizeof(int16_t);
	for (uint32_t i = 0; i < samples; i++) {
		int32_t value = (int32_t)source[i] * percent / 100;
		if (value > INT16_MAX) value = INT16_MAX;
		else if (value < INT16_MIN) value = INT16_MIN;
		destination[i] = (int16_t)value;
	}
}

static mpg123_ssize_t vita_mpg123_read(int fd, void *buffer, size_t size) {
	int ret = sceIoRead(fd, buffer, size);
	return ret < 0 ? -1 : (mpg123_ssize_t)ret;
}

static off_t vita_mpg123_seek(int fd, off_t offset, int whence) {
	SceOff ret = sceIoLseek(fd, (SceOff)offset, whence);
	return ret < 0 ? (off_t)-1 : (off_t)ret;
}

static int play_mp3(BackgroundPlaybackJob *job) {
	if (mpg123_init() != MPG123_OK) return -1;
	int error = MPG123_OK;
	mpg123_handle *decoder = mpg123_new(NULL, &error);
	SceUID fd = -1;
	int port = -1;
	VtBackgroundAudioLease lease;
	int lease_ready = 0;
	int ret = -1;
	if (!decoder) goto done;
	mpg123_param(decoder, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO, 0.0);
	mpg123_format_none(decoder);
	const long *rates = NULL;
	size_t rate_count = 0;
	mpg123_rates(&rates, &rate_count);
	for (size_t i = 0; i < rate_count; i++)
		mpg123_format(decoder, rates[i], MPG123_STEREO, MPG123_ENC_SIGNED_16);
	if (mpg123_replace_reader(decoder, vita_mpg123_read, vita_mpg123_seek) != MPG123_OK)
		goto done;
	fd = sceIoOpen(job->input_path, SCE_O_RDONLY, 0);
	if (fd < 0 || mpg123_open_fd(decoder, fd) != MPG123_OK) goto done;
	long rate = 0;
	int channels = 0, encoding = 0;
	if (mpg123_getformat(decoder, &rate, &channels, &encoding) != MPG123_OK ||
	    rate <= 0 || channels != 2 || !(encoding & MPG123_ENC_SIGNED_16)) goto done;
	off_t length = mpg123_length(decoder);
	if (length < 0 && mpg123_scan(decoder) == MPG123_OK) length = mpg123_length(decoder);
	if (length > 0 && !job->snapshot.duration_ms) {
		snapshot_lock();
		job->snapshot.duration_ms = (uint64_t)length * 1000ULL / (uint64_t)rate;
		snapshot_unlock();
	}
	uint64_t start_position_ms = player_start_position(job);
	mpg123_seek(decoder,
	            (off_t)(start_position_ms * (uint64_t)rate / 1000ULL), SEEK_SET);
	vt_background_audio_acquire(&lease);
	lease_ready = 1;
	port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
	                           BACKGROUND_AUDIO_GRAIN, (int)rate,
	                           SCE_AUDIO_OUT_MODE_STEREO);
	if (port < 0) goto done;
	set_state(VT_BACKGROUND_PLAYING, 0);
	unsigned int observed_toggle = job->toggle_serial;
	player_lock(job);
	unsigned int observed_seek = job->seek_serial;
	player_unlock(job);
	int applied_volume = -1;
	int paused = 0;
	uint64_t last_power_tick = 0;
	unsigned char pcm[BACKGROUND_AUDIO_GRAIN * 2 * sizeof(int16_t)];
	while (!job->cancel) {
		if (observed_toggle != job->toggle_serial) {
			observed_toggle = job->toggle_serial;
			paused = !paused;
			set_state(paused ? VT_BACKGROUND_PAUSED : VT_BACKGROUND_PLAYING, 0);
		}
		unsigned int pending_seek_serial;
		uint64_t pending_seek_position_ms;
		player_lock(job);
		pending_seek_serial = job->seek_serial;
		pending_seek_position_ms = job->seek_position_ms;
		player_unlock(job);
		if (observed_seek != pending_seek_serial) {
			observed_seek = pending_seek_serial;
			mpg123_seek(decoder,
			            (off_t)(pending_seek_position_ms * (uint64_t)rate / 1000ULL),
			            SEEK_SET);
		}
		if (paused) {
			sceKernelDelayThread(10 * 1000);
			continue;
		}
		size_t decoded = 0;
		int read_ret = mpg123_read(decoder, pcm, sizeof(pcm), &decoded);
		if (read_ret == MPG123_NEW_FORMAT) continue;
		if (read_ret != MPG123_OK && read_ret != MPG123_DONE) {
			ret = read_ret;
			break;
		}
		if (decoded > 0) {
			if (decoded < sizeof(pcm)) memset(pcm + decoded, 0, sizeof(pcm) - decoded);
			int requested_volume = vt_audio_volume_percent();
			set_port_volume(port, requested_volume, &applied_volume);
			if (requested_volume > 100)
				amplify_pcm16((const int16_t *)pcm, (int16_t *)pcm,
				              sizeof(pcm), requested_volume);
			sceAudioOutOutput(port, pcm);
			off_t position = mpg123_tell(decoder);
			if (position >= 0) {
				snapshot_lock();
				job->snapshot.position_ms = (uint64_t)position * 1000ULL /
				                            (uint64_t)rate;
				snapshot_unlock();
			}
		}
		uint64_t now = sceKernelGetProcessTimeWide();
		if (!last_power_tick || now - last_power_tick >= 1000 * 1000ULL) {
			sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
			last_power_tick = now;
		}
		if (read_ret == MPG123_DONE) {
			ret = 0;
			break;
		}
	}
	if (job->cancel) ret = -1;
done:
	if (port >= 0) sceAudioOutReleasePort(port);
	if (lease_ready) vt_background_audio_release(&lease);
	if (decoder) {
		mpg123_close(decoder);
		mpg123_delete(decoder);
	}
	if (fd >= 0) sceIoClose(fd);
	mpg123_exit();
	log_printf("local MP3 playback ended: result=0x%08X\n", (unsigned)ret);
	return ret;
}

typedef struct {
	BackgroundPlaybackJob *job;
	SceUID fd;
	FLAC__uint64 file_size;
	unsigned int sample_rate;
	unsigned int channels;
	unsigned int bits_per_sample;
	FLAC__uint64 total_samples;
	FLAC__uint64 sample_position;
	int port;
	int applied_volume;
	int output_error;
	unsigned int pending_frames;
	int16_t pcm[BACKGROUND_AUDIO_GRAIN * 2];
	uint64_t last_power_tick;
} FlacPlaybackContext;

static int flac_rate_supported(unsigned int rate) {
	static const unsigned int supported[] = {
		8000, 11025, 12000, 16000, 22050,
		24000, 32000, 44100, 48000
	};
	for (unsigned int i = 0; i < sizeof(supported) / sizeof(supported[0]); i++)
		if (supported[i] == rate) return 1;
	return 0;
}

static int16_t flac_pcm16(FLAC__int32 input, unsigned int bits_per_sample) {
	int64_t value = input;
	if (bits_per_sample > 16) {
		unsigned int shift = bits_per_sample - 16;
		int64_t rounding = 1LL << (shift - 1);
		value = value >= 0 ? (value + rounding) >> shift
		                   : -(((-value) + rounding) >> shift);
	} else if (bits_per_sample < 16) {
		value <<= 16 - bits_per_sample;
	}
	if (value > INT16_MAX) value = INT16_MAX;
	else if (value < INT16_MIN) value = INT16_MIN;
	return (int16_t)value;
}

static int flac_output_grain(FlacPlaybackContext *context,
	                         unsigned int valid_frames) {
	if (!context || context->port < 0 || valid_frames == 0) return 0;
	if (valid_frames < BACKGROUND_AUDIO_GRAIN)
		memset(context->pcm + valid_frames * 2, 0,
		       (BACKGROUND_AUDIO_GRAIN - valid_frames) * 2 * sizeof(int16_t));
	int requested_volume = vt_audio_volume_percent();
	set_port_volume(context->port, requested_volume, &context->applied_volume);
	if (requested_volume > 100)
		amplify_pcm16(context->pcm, context->pcm, sizeof(context->pcm),
		              requested_volume);
	int ret = sceAudioOutOutput(context->port, context->pcm);
	if (ret < 0) {
		context->output_error = ret;
		return ret;
	}
	context->sample_position += valid_frames;
	snapshot_lock();
	context->job->snapshot.position_ms = context->sample_rate
		? context->sample_position * 1000ULL / context->sample_rate : 0;
	snapshot_unlock();
	uint64_t now = sceKernelGetProcessTimeWide();
	if (!context->last_power_tick ||
	    now - context->last_power_tick >= 1000 * 1000ULL) {
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
		context->last_power_tick = now;
	}
	return 0;
}

static FLAC__StreamDecoderReadStatus flac_read_callback(
	const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes,
	void *client_data) {
	(void)decoder;
	FlacPlaybackContext *context = client_data;
	if (!context || !bytes || *bytes == 0 || context->job->cancel) {
		if (bytes) *bytes = 0;
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}
	int read = sceIoRead(context->fd, buffer, *bytes);
	if (read < 0) {
		*bytes = 0;
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}
	*bytes = (size_t)read;
	return read == 0 ? FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM
	                 : FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderSeekStatus flac_seek_callback(
	const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset,
	void *client_data) {
	(void)decoder;
	FlacPlaybackContext *context = client_data;
	if (!context || absolute_byte_offset > (FLAC__uint64)INT64_MAX)
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	return sceIoLseek(context->fd, (SceOff)absolute_byte_offset, SCE_SEEK_SET) < 0
	     ? FLAC__STREAM_DECODER_SEEK_STATUS_ERROR
	     : FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__StreamDecoderTellStatus flac_tell_callback(
	const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset,
	void *client_data) {
	(void)decoder;
	FlacPlaybackContext *context = client_data;
	SceOff position = context ? sceIoLseek(context->fd, 0, SCE_SEEK_CUR) : -1;
	if (position < 0) return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
	*absolute_byte_offset = (FLAC__uint64)position;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__StreamDecoderLengthStatus flac_length_callback(
	const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length,
	void *client_data) {
	(void)decoder;
	FlacPlaybackContext *context = client_data;
	if (!context || !context->file_size)
		return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
	*stream_length = context->file_size;
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool flac_eof_callback(const FLAC__StreamDecoder *decoder,
	                                void *client_data) {
	(void)decoder;
	FlacPlaybackContext *context = client_data;
	SceOff position = context ? sceIoLseek(context->fd, 0, SCE_SEEK_CUR) : -1;
	return position >= 0 && (FLAC__uint64)position >= context->file_size;
}

static FLAC__StreamDecoderWriteStatus flac_write_callback(
	const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame,
	const FLAC__int32 *const buffer[], void *client_data) {
	(void)decoder;
	FlacPlaybackContext *context = client_data;
	if (!context || !frame || !buffer || context->job->cancel)
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	if (frame->header.channels != context->channels ||
	    frame->header.bits_per_sample != context->bits_per_sample ||
	    frame->header.sample_rate != context->sample_rate)
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	for (unsigned int i = 0; i < frame->header.blocksize; i++) {
		int16_t left = flac_pcm16(buffer[0][i], context->bits_per_sample);
		int16_t right = context->channels == 2
		              ? flac_pcm16(buffer[1][i], context->bits_per_sample) : left;
		unsigned int output = context->pending_frames * 2;
		context->pcm[output] = left;
		context->pcm[output + 1] = right;
		context->pending_frames++;
		if (context->pending_frames == BACKGROUND_AUDIO_GRAIN) {
			if (flac_output_grain(context, BACKGROUND_AUDIO_GRAIN) < 0)
				return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
			context->pending_frames = 0;
		}
		if (context->job->cancel)
			return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flac_metadata_callback(const FLAC__StreamDecoder *decoder,
	                               const FLAC__StreamMetadata *metadata,
	                               void *client_data) {
	(void)decoder;
	FlacPlaybackContext *context = client_data;
	if (!context || !metadata || metadata->type != FLAC__METADATA_TYPE_STREAMINFO)
		return;
	context->sample_rate = metadata->data.stream_info.sample_rate;
	context->channels = metadata->data.stream_info.channels;
	context->bits_per_sample = metadata->data.stream_info.bits_per_sample;
	context->total_samples = metadata->data.stream_info.total_samples;
}

static void flac_error_callback(const FLAC__StreamDecoder *decoder,
	                            FLAC__StreamDecoderErrorStatus status,
	                            void *client_data) {
	(void)decoder;
	FlacPlaybackContext *context = client_data;
	log_printf("FLAC decode error: status=%d path=%s\n", (int)status,
	           context && context->job ? context->job->input_path : "");
}

static int play_flac(BackgroundPlaybackJob *job) {
	FlacPlaybackContext context;
	memset(&context, 0, sizeof(context));
	context.job = job;
	context.fd = -1;
	context.port = -1;
	context.applied_volume = -1;
	FLAC__StreamDecoder *decoder = NULL;
	int decoder_initialized = 0;
	VtBackgroundAudioLease lease;
	int lease_ready = 0;
	int ret = -1;

	context.fd = sceIoOpen(job->input_path, SCE_O_RDONLY, 0);
	if (context.fd < 0) return context.fd;
	SceOff end = sceIoLseek(context.fd, 0, SCE_SEEK_END);
	if (end <= 0 || sceIoLseek(context.fd, 0, SCE_SEEK_SET) < 0) goto done;
	context.file_size = (FLAC__uint64)end;
	decoder = FLAC__stream_decoder_new();
	if (!decoder) goto done;
	FLAC__stream_decoder_set_md5_checking(decoder, 0);
	FLAC__StreamDecoderInitStatus init = FLAC__stream_decoder_init_stream(
		decoder, flac_read_callback, flac_seek_callback, flac_tell_callback,
		flac_length_callback, flac_eof_callback, flac_write_callback,
		flac_metadata_callback, flac_error_callback, &context);
	if (init != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		ret = -(int)init - 1;
		goto done;
	}
	decoder_initialized = 1;
	if (!FLAC__stream_decoder_process_until_end_of_metadata(decoder) ||
	    !flac_rate_supported(context.sample_rate) ||
	    (context.channels != 1 && context.channels != 2) ||
	    context.bits_per_sample < 4 || context.bits_per_sample > 32) {
		ret = -2;
		goto done;
	}
	if (context.total_samples && !job->snapshot.duration_ms) {
		snapshot_lock();
		job->snapshot.duration_ms = context.total_samples * 1000ULL /
		                            context.sample_rate;
		snapshot_unlock();
	}
	uint64_t start_position_ms = player_start_position(job);
	FLAC__uint64 start_sample = start_position_ms * context.sample_rate / 1000ULL;
	if (context.total_samples && start_sample >= context.total_samples)
		start_sample = context.total_samples - 1;
	if (start_sample && !FLAC__stream_decoder_seek_absolute(decoder, start_sample)) {
		ret = -3;
		goto done;
	}
	context.sample_position = start_sample;
	vt_background_audio_acquire(&lease);
	lease_ready = 1;
	context.port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
	                                   BACKGROUND_AUDIO_GRAIN,
	                                   (int)context.sample_rate,
	                                   SCE_AUDIO_OUT_MODE_STEREO);
	if (context.port < 0) {
		ret = context.port;
		goto done;
	}
	set_state(VT_BACKGROUND_PLAYING, 0);
	unsigned int observed_toggle = job->toggle_serial;
	player_lock(job);
	unsigned int observed_seek = job->seek_serial;
	player_unlock(job);
	int paused = 0;
	ret = 0;
	while (!job->cancel) {
		if (observed_toggle != job->toggle_serial) {
			observed_toggle = job->toggle_serial;
			paused = !paused;
			set_state(paused ? VT_BACKGROUND_PAUSED : VT_BACKGROUND_PLAYING, 0);
		}
		unsigned int pending_seek_serial;
		uint64_t pending_seek_position_ms;
		player_lock(job);
		pending_seek_serial = job->seek_serial;
		pending_seek_position_ms = job->seek_position_ms;
		player_unlock(job);
		if (observed_seek != pending_seek_serial) {
			FLAC__uint64 target = pending_seek_position_ms * context.sample_rate /
			                      1000ULL;
			if (context.total_samples && target >= context.total_samples)
				target = context.total_samples - 1;
			context.pending_frames = 0;
			if (!FLAC__stream_decoder_seek_absolute(decoder, target)) {
				ret = -4;
				break;
			}
			context.sample_position = target;
			observed_seek = pending_seek_serial;
			snapshot_lock();
			job->snapshot.position_ms = context.sample_rate
				? target * 1000ULL / context.sample_rate : 0;
			snapshot_unlock();
		}
		if (paused) {
			sceKernelDelayThread(10 * 1000);
			continue;
		}
		if (!FLAC__stream_decoder_process_single(decoder)) {
			ret = context.output_error ? context.output_error : -5;
			break;
		}
		if (FLAC__stream_decoder_get_state(decoder) ==
		    FLAC__STREAM_DECODER_END_OF_STREAM)
			break;
	}
	if (!job->cancel && ret == 0 && context.pending_frames > 0)
		ret = flac_output_grain(&context, context.pending_frames);
	if (job->cancel) ret = -1;

done:
	if (context.port >= 0) sceAudioOutReleasePort(context.port);
	if (lease_ready) vt_background_audio_release(&lease);
	if (decoder) {
		if (decoder_initialized) FLAC__stream_decoder_finish(decoder);
		FLAC__stream_decoder_delete(decoder);
	}
	if (context.fd >= 0) sceIoClose(context.fd);
	log_printf("local FLAC playback ended: result=0x%08X rate=%u channels=%u bits=%u\n",
	           (unsigned)ret, context.sample_rate, context.channels,
	           context.bits_per_sample);
	return ret;
}

static int seek_player_ready(SceAvPlayerHandle player, uint64_t target_ms,
	                         volatile int *cancel) {
	if (target_ms == 0) return 0;
	for (int poll = 0; poll < 50 && (!cancel || !*cancel); poll++) {
		SceAvPlayerFrameInfo probe;
		memset(&probe, 0, sizeof(probe));
		sceAvPlayerGetAudioData(player, &probe);
		memset(&probe, 0, sizeof(probe));
		sceAvPlayerGetVideoData(player, &probe);
		if (sceAvPlayerCurrentTime(player) > 0) break;
		sceKernelDelayThread(10 * 1000);
	}
	for (int attempt = 0; attempt < 20 && (!cancel || !*cancel); attempt++) {
		int ret = sceAvPlayerJumpToTime(player, target_ms);
		if (ret >= 0) return 0;
		sceKernelDelayThread(25 * 1000);
	}
	return -1;
}

static int play_av_source(BackgroundPlaybackJob *job) {
	SceAvPlayerHandle player = AVPLAYER_INVALID_HANDLE;
	int ret = media_init(&player, NULL);
	if (ret < 0) return ret;
	ret = sceAvPlayerAddSource(player, job->input_path);
	if (ret < 0) {
		media_term(player);
		return ret;
	}
	int waited_ms = 0;
	while (!job->cancel && !sceAvPlayerIsActive(player) && waited_ms < 10000) {
		sceKernelDelayThread(10 * 1000);
		waited_ms += 10;
	}
	if (job->cancel || !sceAvPlayerIsActive(player)) {
		sceAvPlayerStop(player);
		media_term(player);
		return job->cancel ? -1 : -2;
	}
	uint64_t start_position_ms = player_start_position(job);
	if (start_position_ms > 0)
		seek_player_ready(player, start_position_ms, &job->cancel);

	VtBackgroundAudioLease lease;
	vt_background_audio_acquire(&lease);
	int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
	                               BACKGROUND_AUDIO_GRAIN,
	                               BACKGROUND_AUDIO_RATE,
	                               SCE_AUDIO_OUT_MODE_STEREO);
	if (port < 0) {
		vt_background_audio_release(&lease);
		sceAvPlayerStop(player);
		media_term(player);
		return port;
	}
	player_lock(job);
	job->active_player = player;
	/* This legacy AvPlayer path is audio-only. Embedded artwork/video streams are
	 * drained, never published as a live mini-player surface. */
	job->local_video_source = 0;
	job->video_have_frame = 0;
	memset(&job->video_frame, 0, sizeof(job->video_frame));
	memset(&job->video_texture, 0, sizeof(job->video_texture));
	player_unlock(job);

	set_state(VT_BACKGROUND_PLAYING, 0);
	SceAvPlayerFrameInfo frame;
	memset(&frame, 0, sizeof(frame));
	unsigned int observed_toggle = job->toggle_serial;
	unsigned int observed_seek = job->seek_serial;
	int applied_volume = -1;
	int paused = 0;
	int16_t *boost_buffer = NULL;
	uint32_t boost_capacity = 0;
	uint64_t last_snapshot = 0;
	uint64_t last_power_tick = 0;
	while (!job->cancel) {
		if (observed_seek != job->seek_serial) {
			player_lock(job);
			unsigned int served_seek = job->seek_serial;
			uint64_t served_position_ms = job->seek_position_ms;
			int seek_ret = sceAvPlayerJumpToTime(player, served_position_ms);
			job->video_have_frame = 0;
			if (seek_ret >= 0) observed_seek = served_seek;
			player_unlock(job);
			if (seek_ret >= 0) {
				snapshot_lock();
				job->snapshot.position_ms = served_position_ms;
				snapshot_unlock();
			}
		}
		if (observed_toggle != job->toggle_serial) {
			observed_toggle = job->toggle_serial;
			paused = !paused;
			player_lock(job);
			if (paused) sceAvPlayerPause(player);
			else sceAvPlayerResume(player);
			player_unlock(job);
			set_state(paused ? VT_BACKGROUND_PAUSED : VT_BACKGROUND_PLAYING, 0);
		}
		player_lock(job);
		int active = sceAvPlayerIsActive(player);
		int have_audio = !paused && active && sceAvPlayerGetAudioData(player, &frame);
		player_unlock(job);
		if (!paused && !active) break;
		if (have_audio) {
			int requested_volume = vt_audio_volume_percent();
			set_port_volume(port, requested_volume, &applied_volume);
			sceAudioOutSetConfig(port, (SceSize)-1,
			                      (int)frame.details.audio.sampleRate,
			                      frame.details.audio.channelCount == 1
			                          ? SCE_AUDIO_OUT_MODE_MONO
			                          : SCE_AUDIO_OUT_MODE_STEREO);
			const void *output = frame.pData;
			if (requested_volume > 100 && frame.pData &&
			    frame.details.audio.size >= sizeof(int16_t)) {
				uint32_t bytes = frame.details.audio.size;
				if (bytes > boost_capacity) {
					int16_t *larger = memalign(64, bytes);
					if (larger) {
						free(boost_buffer);
						boost_buffer = larger;
						boost_capacity = bytes;
					}
				}
				if (boost_buffer && boost_capacity >= bytes) {
					amplify_pcm16((const int16_t *)frame.pData, boost_buffer,
					              bytes, requested_volume);
					output = boost_buffer;
				}
			}
			sceAudioOutOutput(port, output);
		} else {
			sceKernelDelayThread(1000);
		}
		if (!paused) {
			SceAvPlayerFrameInfo discard;
			memset(&discard, 0, sizeof(discard));
			player_lock(job);
			sceAvPlayerGetVideoData(player, &discard);
			job->video_have_frame = 0;
			player_unlock(job);
		}
		uint64_t now = sceKernelGetProcessTimeWide();
		if (!last_power_tick || now - last_power_tick >= 1000 * 1000ULL) {
			sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
			last_power_tick = now;
		}
		if (!last_snapshot || now - last_snapshot >= 100 * 1000ULL) {
			player_lock(job);
			uint64_t position_ms = sceAvPlayerCurrentTime(player);
			player_unlock(job);
			snapshot_lock();
			job->snapshot.position_ms = position_ms;
			snapshot_unlock();
			last_snapshot = now;
		}
	}

	player_lock(job);
	uint64_t final_position_ms = sceAvPlayerCurrentTime(player);
	job->local_video_source = 0;
	player_unlock(job);
	snapshot_lock();
	job->snapshot.position_ms = final_position_ms;
	snapshot_unlock();
	sceAudioOutReleasePort(port);
	free(boost_buffer);
	vt_background_audio_release(&lease);
	for (int waited_us = 0; job->video_in_gpu && waited_us < 100 * 1000;
	     waited_us += 1000)
		sceKernelDelayThread(1000);
	player_lock(job);
	job->active_player = AVPLAYER_INVALID_HANDLE;
	job->video_have_frame = 0;
	sceAvPlayerStop(player);
	media_term(player);
	player_unlock(job);
	return 0;
}

static int play_decoder_source(BackgroundPlaybackJob *job) {
	VtBackgroundAudioLease lease;
	vt_background_audio_acquire(&lease);
	VtDecoderPlayer *decoder = vt_decoder_create();
	if (!decoder) {
		vt_background_audio_release(&lease);
		return -1;
	}
	VtDecoderStreamFactory factory;
	if (job->remote_video_source) factory = job->remote_factory.factory;
	else vt_decoder_file_stream_factory(job->input_path, &factory);
	uint64_t start_position_ms = player_start_position(job);
	VtDecoderPlayerConfig config = {
		.stream = factory,
		.preferred_backend = VT_DECODER_BACKEND_NONE,
		.audio_track = job->audio_track,
		.subtitle_track = 0,
		.start_position_ms = start_position_ms,
		.volume_percent = vt_audio_volume_percent(),
		.cancel_flag = &job->cancel
	};
	set_state(VT_BACKGROUND_BUFFERING, 0);
	/* Publish the allocated decoder before its cancellable open. Draw remains
	 * disabled until local_video_source is set, but stop can already propagate to
	 * the decoder-owned transport flags instead of waiting for network timeouts. */
	player_lock(job);
	job->active_decoder = decoder;
	player_unlock(job);
	int ret = vt_decoder_open(decoder, &config);
	if (ret < 0) {
		player_lock(job);
		if (job->active_decoder == decoder) job->active_decoder = NULL;
		player_unlock(job);
		vt_decoder_destroy(decoder);
		vt_background_audio_release(&lease);
		return ret;
	}
	player_lock(job);
	job->local_video_source = 1;
	player_unlock(job);
	set_state(VT_BACKGROUND_PLAYING, 0);
	unsigned int observed_toggle = job->toggle_serial;
	player_lock(job);
	unsigned int observed_seek = job->seek_serial;
	player_unlock(job);
	int paused = 0;
	uint64_t last_power_tick = 0;
	while (!job->cancel) {
		if (observed_seek != job->seek_serial) {
			if (!player_lock_when_video_idle(job, 250 * 1000ULL)) {
				/* The UI may be between scenes and late retiring the preceding draw.
				 * Keep the latest serial pending instead of converting a missed frame
				 * acknowledgement into a fatal playback error. */
				continue;
			}
			unsigned int served_seek = job->seek_serial;
			uint64_t served_position_ms = job->seek_position_ms;
			/* Exclude draws while the decoder restarts, but release the lifecycle
			 * lock so stop can interrupt a blocked network seek immediately. */
			job->local_video_source = 0;
			vt_decoder_prepare_background_restart(decoder);
			player_unlock(job);
			ret = vt_decoder_seek(decoder, served_position_ms);
			player_lock(job);
			if (ret >= 0) {
				observed_seek = served_seek;
				if (job->seek_serial == served_seek && !job->cancel)
					job->local_video_source = 1;
			}
			player_unlock(job);
			if (ret < 0) break;
		}
		if (observed_toggle != job->toggle_serial) {
			observed_toggle = job->toggle_serial;
			paused = !paused;
			player_lock(job);
			vt_decoder_set_paused(decoder, paused);
			player_unlock(job);
			set_state(paused ? VT_BACKGROUND_PAUSED : VT_BACKGROUND_PLAYING, 0);
		}
		/* With animated mini-player video disabled, no scene calls present_rect().
		 * Keep consuming frames against the presentation clock so the bounded video
		 * queue cannot fill and stall the decoder/audio pipeline. */
		if (!paused && !vt_preferences_mini_player_animated()) {
			player_lock(job);
			if (!job->video_in_gpu && job->local_video_source &&
			    job->active_decoder == decoder)
				vt_decoder_discard_video_to_clock(decoder);
			player_unlock(job);
		}
		VtDecoderPlayerStatus status;
		player_lock(job);
		vt_decoder_get_status(decoder, &status);
		player_unlock(job);
		snapshot_lock();
		job->snapshot.position_ms = status.position_ms;
		if (status.duration_ms) job->snapshot.duration_ms = status.duration_ms;
		job->snapshot.video_width = status.width;
		job->snapshot.video_height = status.height;
		snapshot_unlock();
		if (status.error) { ret = -1; break; }
		if (status.eof) { ret = 0; break; }
		uint64_t now = sceKernelGetProcessTimeWide();
		if (!last_power_tick || now - last_power_tick >= 1000 * 1000ULL) {
			sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
			last_power_tick = now;
		}
		sceKernelDelayThread(4 * 1000);
	}
	VtDecoderPlayerStatus final_status;
	/* Never free a decoder texture while GXM can still sample it. Normal UI
	 * pumping retires natural completion; the synchronous stop path performs the
	 * same fence before joining this worker. */
	player_lock_when_video_idle(job, 0);
	vt_decoder_get_status(decoder, &final_status);
	job->local_video_source = 0;
	job->active_decoder = NULL;
	vt_decoder_prepare_background_restart(decoder);
	player_unlock(job);
	snapshot_lock();
	job->snapshot.position_ms = final_status.position_ms;
	snapshot_unlock();
	vt_decoder_destroy(decoder);
	vt_background_audio_release(&lease);
	return job->cancel ? -1 : ret;
}

static int background_thread(SceSize args, void *argp) {
	(void)args;
	BackgroundPlaybackJob *job = *(void **)argp;
	set_state(VT_BACKGROUND_READY, 0);
	while (!job->cancel && !job->activate) sceKernelDelayThread(10 * 1000);
	int ret = job->cancel ? -1
	        : job->mp3_source ? play_mp3(job)
	        : job->flac_source ? play_flac(job)
	        : job->decoder_source ? play_decoder_source(job)
	                              : play_av_source(job);
	if (ret < 0 && !job->cancel) {
		set_state(VT_BACKGROUND_ERROR, ret);
		log_printf("local background playback failed: 0x%08X\n", (unsigned)ret);
	} else if (!job->cancel) {
		snapshot_lock();
		job->snapshot.visible = 0;
		job->snapshot.state = VT_BACKGROUND_IDLE;
		snapshot_unlock();
	}
	__sync_synchronize();
	/* The decoder has closed every stream handle; the session secret is no
	 * longer needed by this worker and must not survive natural completion. */
	memset(&job->remote_factory.credential, 0,
	       sizeof(job->remote_factory.credential));
	job->done = 1;
	return sceKernelExitThread(0);
}

void vt_background_playback_init(void) {
	memset(&g_background, 0, sizeof(g_background));
	g_background.thid = -1;
	g_background.active_player = AVPLAYER_INVALID_HANDLE;
	g_snapshot_lock = 0;
	g_activation_serial = 0;
}

static int prepare_local(const char *media_path, const char *media_id,
	                     const char *title, const char *artist,
	                     const char *artwork_path, uint64_t duration_ms,
	                     int decoder_source, int audio_track) {
	if (!media_path || !media_path[0] || !media_id || !media_id[0]) return -1;
	vt_background_playback_stop();
	BackgroundPlaybackJob *job = &g_background;
	memset(job, 0, sizeof(*job));
	job->self = job;
	job->thid = -1;
	job->active_player = AVPLAYER_INVALID_HANDLE;
	job->mp3_source = path_is_mp3(media_path);
	job->flac_source = path_is_flac(media_path);
	job->decoder_source = decoder_source;
	job->audio_track = audio_track;
	copy_text(job->input_path, sizeof(job->input_path), media_path);
	copy_text(job->snapshot.video_id, sizeof(job->snapshot.video_id), media_id);
	copy_text(job->snapshot.title, sizeof(job->snapshot.title), title);
	copy_text(job->snapshot.channel, sizeof(job->snapshot.channel), artist);
	copy_text(job->snapshot.thumbnail_url, sizeof(job->snapshot.thumbnail_url),
	          artwork_path);
	job->snapshot.duration_ms = duration_ms;
	job->snapshot.is_video = decoder_source != 0;
	job->snapshot.state = VT_BACKGROUND_PREPARING;
	job->thid = sceKernelCreateThread("VitaMediaDeckLocalAudio", background_thread,
	                                  BACKGROUND_THREAD_PRIORITY,
	                                  BACKGROUND_THREAD_STACK, 0, 0, NULL);
	if (job->thid < 0) return job->thid;
	int ret = sceKernelStartThread(job->thid, sizeof(job->self), &job->self);
	if (ret < 0) {
		sceKernelDeleteThread(job->thid);
		job->thid = -1;
		return ret;
	}
	return 0;
}

int vt_background_playback_prepare_local(const char *media_path,
	                                     const char *media_id,
	                                     const char *title,
	                                     const char *artist,
	                                     const char *artwork_path,
	                                     uint64_t duration_ms) {
	return prepare_local(media_path, media_id, title, artist, artwork_path,
	                     duration_ms, 0, 0);
}

int vt_background_playback_prepare_local_video(const char *media_path,
	                                           const char *media_id,
	                                           const char *title,
	                                           const char *artist,
	                                           const char *artwork_path,
	                                           uint64_t duration_ms,
	                                           int audio_track) {
	return prepare_local(media_path, media_id, title, artist, artwork_path,
	                     duration_ms, 1, audio_track);
}

int vt_background_playback_prepare_remote_video(
	const VtNetworkSource *source,
	const VtNetworkCredential *credential,
	const char *media_path,
	const char *media_id,
	const char *title,
	const char *location,
	uint64_t duration_ms,
	int audio_track) {
	if (!source || !media_path || !media_path[0] ||
	    !media_id || !media_id[0]) return -1;
	vt_background_playback_stop();
	BackgroundPlaybackJob *job = &g_background;
	memset(job, 0, sizeof(*job));
	job->self = job;
	job->thid = -1;
	job->active_player = AVPLAYER_INVALID_HANDLE;
	job->decoder_source = 1;
	job->remote_video_source = 1;
	job->audio_track = audio_track;
	int ret = vt_network_stream_factory_init(&job->remote_factory, source,
	                                         credential, media_path);
	if (ret < 0) return ret;
	copy_text(job->snapshot.video_id, sizeof(job->snapshot.video_id), media_id);
	copy_text(job->snapshot.title, sizeof(job->snapshot.title), title);
	copy_text(job->snapshot.channel, sizeof(job->snapshot.channel), location);
	job->snapshot.duration_ms = duration_ms;
	job->snapshot.is_video = 1;
	job->snapshot.state = VT_BACKGROUND_PREPARING;
	job->thid = sceKernelCreateThread("VitaMediaDeckRemoteVideo",
	                                  background_thread,
	                                  BACKGROUND_THREAD_PRIORITY,
	                                  BACKGROUND_THREAD_STACK, 0, 0, NULL);
	if (job->thid < 0) {
		memset(&job->remote_factory.credential, 0,
		       sizeof(job->remote_factory.credential));
		return job->thid;
	}
	ret = sceKernelStartThread(job->thid, sizeof(job->self), &job->self);
	if (ret < 0) {
		sceKernelDeleteThread(job->thid);
		job->thid = -1;
		memset(&job->remote_factory.credential, 0,
		       sizeof(job->remote_factory.credential));
		return ret;
	}
	return 0;
}

int vt_background_playback_prepared(void) {
	return g_background.thid >= 0 && !g_background.done;
}

int vt_background_playback_activate(uint64_t start_position_ms) {
	if (g_background.thid < 0 || g_background.done) return -1;
	player_lock(&g_background);
	g_background.start_position_ms = start_position_ms;
	__sync_synchronize();
	g_background.activate = 1;
	player_unlock(&g_background);
	snapshot_lock();
	g_background.snapshot.activation_serial =
	    __sync_add_and_fetch(&g_activation_serial, 1);
	g_background.snapshot.position_ms = start_position_ms;
	g_background.snapshot.visible = 1;
	snapshot_unlock();
	return 0;
}

void vt_background_playback_toggle_pause(void) {
	if (g_background.thid >= 0 && !g_background.done && g_background.activate)
		__sync_add_and_fetch(&g_background.toggle_serial, 1);
}

void vt_background_playback_seek_to(uint64_t position_ms) {
	BackgroundPlaybackJob *job = &g_background;
	if (job->thid < 0 || job->done || !job->activate) return;
	snapshot_lock();
	uint64_t duration = job->snapshot.duration_ms;
	if (duration > 0 && position_ms > duration) position_ms = duration;
	job->snapshot.position_ms = position_ms;
	snapshot_unlock();
	/* Position is 64-bit on a 32-bit CPU. Publish it with its serial while the
	 * same lock used by both playback workers is held, preventing torn/mismatched
	 * rapid-seek requests. */
	player_lock(job);
	job->seek_position_ms = position_ms;
	job->seek_serial++;
	__sync_synchronize();
	player_unlock(job);
}

void vt_background_playback_seek_relative(int64_t delta_ms) {
	VtBackgroundPlaybackSnapshot snapshot;
	if (!vt_background_playback_snapshot(&snapshot)) return;
	int64_t target = snapshot.position_ms > (uint64_t)INT64_MAX
	               ? INT64_MAX : (int64_t)snapshot.position_ms;
	if (delta_ms < 0 && target < -delta_ms) target = 0;
	else {
		target += delta_ms;
		if (target < 0) target = 0;
	}
	vt_background_playback_seek_to((uint64_t)target);
}

void vt_background_playback_set_fullscreen_resume(
	VtBackgroundFullscreenResume resume, void *ctx) {
	g_background.fullscreen_resume = resume;
	g_background.fullscreen_resume_ctx = ctx;
}

int vt_background_playback_can_resume_fullscreen(void) {
	VtBackgroundPlaybackSnapshot snapshot;
	return g_background.fullscreen_resume != NULL &&
	       vt_background_playback_snapshot(&snapshot) != 0;
}

int vt_background_playback_resume_fullscreen(void) {
	VtBackgroundFullscreenResume resume = g_background.fullscreen_resume;
	void *ctx = g_background.fullscreen_resume_ctx;
	VtBackgroundPlaybackSnapshot snapshot;
	if (!resume || !vt_background_playback_snapshot(&snapshot)) return -1;
	return resume(snapshot.position_ms, ctx);
}

void vt_background_playback_request_stop(void) {
	if (g_background.thid < 0) return;
	snapshot_lock();
	g_background.snapshot.visible = 0;
	snapshot_unlock();
	g_background.cancel = 1;
	g_background.activate = 1;
	player_lock(&g_background);
	if (g_background.active_decoder)
		vt_decoder_request_stop(g_background.active_decoder);
	player_unlock(&g_background);
}

void vt_background_playback_stop(void) {
	if (g_background.thid >= 0) {
		/* stop() is the synchronous UI-thread teardown path. Complete the most
		 * recent mini-player draw before waiting for the worker, otherwise the
		 * worker would correctly wait forever rather than freeing GPU-owned CDRAM. */
		vita2d_wait_rendering_done();
		vt_background_playback_video_render_complete();
		vt_background_playback_request_stop();
		sceKernelWaitThreadEnd(g_background.thid, NULL, NULL);
		sceKernelDeleteThread(g_background.thid);
		g_background.thid = -1;
	}
	memset(&g_background.remote_factory.credential, 0,
	       sizeof(g_background.remote_factory.credential));
	memset(&g_background.snapshot, 0, sizeof(g_background.snapshot));
}

void vt_background_playback_shutdown(void) {
	vt_background_playback_stop();
}

int vt_background_playback_snapshot(VtBackgroundPlaybackSnapshot *out) {
	if (!out) return 0;
	snapshot_lock();
	*out = g_background.snapshot;
	snapshot_unlock();
	return out->visible;
}

void vt_background_playback_video_render_complete(void) {
	BackgroundPlaybackJob *job = &g_background;
	player_lock(job);
	/* This callback is pumped by every scene, including frames where the mini
	 * player did not draw.  Only touch the decoder when retiring an actual GXM
	 * reference; otherwise an idle callback could race the unlocked seek path. */
	if (job->video_in_gpu) {
		if (job->active_decoder)
			vt_decoder_render_complete(job->active_decoder);
		__sync_synchronize();
		job->video_in_gpu = 0;
		__sync_synchronize();
	}
	player_unlock(job);
}

int vt_background_playback_draw_video(float x, float y,
	                                  float width, float height) {
	BackgroundPlaybackJob *job = &g_background;
	if (width <= 0.0f || height <= 0.0f ||
	    !vt_preferences_mini_player_animated() || job->video_in_gpu ||
	    !job->local_video_source)
		return 0;
	VtBackgroundPlaybackSnapshot snapshot;
	if (!vt_background_playback_snapshot(&snapshot)) return 0;
	if (!snapshot.is_video) return 0;
	player_lock(job);
	/* Revalidate after acquiring the lifecycle lock.  The worker can hide the
	 * source and enter an unlocked cancellable seek between the optimistic test
	 * above and this lock acquisition. */
	if (job->video_in_gpu || !job->local_video_source) {
		player_unlock(job);
		return 0;
	}
	if (job->active_decoder) {
		int drew = vt_decoder_present_rect(job->active_decoder, x, y,
		                                  width, height, 0);
		if (drew > 0) {
			__sync_synchronize();
			job->video_in_gpu = 1;
			__sync_synchronize();
		}
		player_unlock(job);
		return drew > 0;
	}
	if (!job->local_video_source || job->active_player == AVPLAYER_INVALID_HANDLE) {
		player_unlock(job);
		return 0;
	}
	if (snapshot.state != VT_BACKGROUND_PAUSED &&
	    sceAvPlayerGetVideoData(job->active_player, &job->video_frame)) {
		unsigned int frame_w = job->video_frame.details.video.width;
		unsigned int frame_h = job->video_frame.details.video.height;
		int texture_ret = frame_w && frame_h && job->video_frame.pData
		                ? sceGxmTextureInitLinear(&job->video_texture.gxm_tex,
		                                          job->video_frame.pData,
		                                          BACKGROUND_VIDEO_FORMAT,
		                                          frame_w, frame_h, 0)
		                : -1;
		if (texture_ret >= 0) {
			vita2d_texture_set_filters(&job->video_texture,
			                            SCE_GXM_TEXTURE_FILTER_LINEAR,
			                            SCE_GXM_TEXTURE_FILTER_LINEAR);
			job->video_have_frame = 1;
			snapshot_lock();
			job->snapshot.video_width = frame_w;
			job->snapshot.video_height = frame_h;
			snapshot_unlock();
		}
	}
	if (!job->video_have_frame) {
		player_unlock(job);
		return 0;
	}
	float frame_w = (float)job->video_frame.details.video.width;
	float frame_h = (float)job->video_frame.details.video.height;
	float scale_x = width / frame_w;
	float scale_y = height / frame_h;
	float scale = scale_x < scale_y ? scale_x : scale_y;
	float draw_w = frame_w * scale;
	float draw_h = frame_h * scale;
	vita2d_draw_texture_scale(&job->video_texture,
	                          x + (width - draw_w) * 0.5f,
	                          y + (height - draw_h) * 0.5f,
	                          scale, scale);
	__sync_synchronize();
	job->video_in_gpu = 1;
	player_unlock(job);
	return 1;
}
