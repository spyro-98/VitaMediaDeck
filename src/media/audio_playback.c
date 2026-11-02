/* One playback/output engine for both video backends. Codec implementations
 * remain in the independent HW/SW audio packages. */
#include "media/audio_playback.h"
#include "common/text_log.h"
#include <vita_hw_audio_decoder.h>
#include <vita_sw_audio_decoder.h>
#include <vita_audio_gain.h>
#include <vita_audio_formats.h>
#include <psp2/kernel/threadmgr.h>
#include <libavformat/avformat.h>
#include <stdlib.h>
#define ELEMENTARY_AUDIO_RING_GRAINS 8
#define ELEMENTARY_AUDIO_GRAIN_FRAMES 1024
#define ELEMENTARY_AUDIO_MAX_CHANNELS 2
#define ELEMENTARY_AUDIO_MAX_GRAIN_FRAMES 1536
typedef struct {
 VitaAudioPlayback *signals;
 int mode;
	AVFormatContext *demux; /* Caller-owned independent audio cursor. */
	int stream_index;
	VitaHwAudioDecoder *decoder;
 uint32_t channels;
 VitaSwAudioDecoder *software;
 uint32_t output_grain,partial_frames,trim_leading;
 int64_t partial_end_us;int partial_pts_known;
 int16_t partial_pcm[ELEMENTARY_AUDIO_MAX_GRAIN_FRAMES*ELEMENTARY_AUDIO_MAX_CHANNELS];



	uint32_t sample_rate;
	AVPacket *prefetched_packet; /* Probed ADTS AU, consumed by the worker. */
	int input_is_adts;
	uint64_t initial_position_ms;
	int64_t timeline_origin_us;
	int port;
	SceUID thid;
	SceUID output_thid;

	int16_t *pcm_ring; /* Aligned storage, retained through final AudioOut drain. */
	uint32_t pcm_end_ms[ELEMENTARY_AUDIO_RING_GRAINS];
	uint8_t pcm_pts_valid[ELEMENTARY_AUDIO_RING_GRAINS];
	volatile uint32_t pcm_write_seq;
	volatile uint32_t pcm_read_seq;
	volatile int producer_done;
	int output_started;
} AudioPlaybackEngine;

#include <malloc.h>
#include <string.h>

#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/intreadwrite.h>

/* Keep the blocking AudioOut consumer above video and the demux/decode producer
 * below it. The bounded PCM ring absorbs occasional transport and AAC decode
 * stalls without making the hardware port wait for the next grain. */
#define ELEMENTARY_AUDIO_OUTPUT_PRIORITY 64
#define ELEMENTARY_AUDIO_THREAD_PRIORITY 66
#define ELEMENTARY_AUDIO_THREAD_STACK    0x40000
#define ELEMENTARY_AUDIO_OUTPUT_STACK    0x10000
/* Default AAC grain; MP3 and AC-3 choose their codec-specific size at open. */
#define ELEMENTARY_AUDIO_GRAIN ELEMENTARY_AUDIO_GRAIN_FRAMES
/* Short wait while paused: it is not pacing, just to avoid spinning the CPU
 * while the thread stays alive without producing output. */
#define ELEMENTARY_AUDIO_PAUSE_DELAY_US (10 * 1000)

static int first_audio_stream(const AVFormatContext *ctx) {
	for (unsigned int i = 0; ctx && i < ctx->nb_streams; i++) {
		if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			return (int)i;
	}
	return -1;
}

static int64_t media_timeline_origin_us(const AVFormatContext *format) {
	if (!format) return 0;
	if (format->start_time != AV_NOPTS_VALUE) return format->start_time;
	int64_t earliest = AV_NOPTS_VALUE;
	for (unsigned int i = 0; i < format->nb_streams; i++) {
		const AVStream *stream = format->streams[i];
		if (stream->start_time == AV_NOPTS_VALUE ||
		    stream->time_base.num <= 0 || stream->time_base.den <= 0 ||
		    (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
		     stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
		     stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) ||
		    (stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) continue;
		int64_t value = av_rescale_q(stream->start_time, stream->time_base,
		                             AV_TIME_BASE_Q);
		if (earliest == AV_NOPTS_VALUE || value < earliest) earliest = value;
	}
	return earliest == AV_NOPTS_VALUE ? 0 : earliest;
}

static int parse_adts_header(const uint8_t *data, size_t size,
	                         uint32_t *channels, uint32_t *sample_rate) {
	static const uint32_t rates[13] = {
		96000, 88200, 64000, 48000, 44100, 32000, 24000,
		22050, 16000, 12000, 11025, 8000, 7350
	};
	if (!data || size < 7 || data[0] != 0xff || (data[1] & 0xf6) != 0xf0)
		return -1;
	unsigned rate_index = (data[2] >> 2) & 0x0f;
	unsigned channel_config = ((unsigned)(data[2] & 1) << 2) |
	                          ((unsigned)data[3] >> 6);
	if (rate_index >= 13 || channel_config < 1 || channel_config > 2)
		return -1;
	if (channels) *channels = channel_config;
	if (sample_rate) *sample_rate = rates[rate_index];
	return 0;
}

/* Reads the next packet of the selected audio track, silently discarding
 * packets belonging to other tracks that may be present in the same
 * AVFormatContext. Returns
 * 1 if *packet was filled in, 0 at EOF, <0 on error (including the abort due
 * to cancel: the interrupt_callback is already attached to the
 * AVFormatContext by the caller, see vita_audio_playback.h). */
static int read_audio_packet(AudioPlaybackEngine *st, AVPacket *packet) {
	if (st->prefetched_packet) {
		if (st->signals->stop) return AVERROR_EXIT;
		av_packet_move_ref(packet, st->prefetched_packet);
		av_packet_free(&st->prefetched_packet);
		return 1;
	}
	int ret = AVERROR_EXIT;
	while (!st->signals->stop && (ret = av_read_frame(st->demux, packet)) >= 0) {
		if (packet->stream_index == st->stream_index) return 1;
		av_packet_unref(packet);
	}
	return st->signals->stop ? AVERROR_EXIT : ret == AVERROR_EOF ? 0 : ret;
}

static int audio_output_thread(SceSize args, void *argp) {
	(void)args;
	AudioPlaybackEngine *st = *(AudioPlaybackEngine **)argp;
	uint64_t last_output_wall_us = 0;
	uint64_t maximum_output_gap_us = 0;
	unsigned int output_grains = 0;
	unsigned int delayed_output_grains = 0;
	int last_applied_volume = -1;
	int16_t boosted[ELEMENTARY_AUDIO_MAX_GRAIN_FRAMES * ELEMENTARY_AUDIO_MAX_CHANNELS]
	    __attribute__((aligned(64)));
	log_printf("elementary_audio: output priority=%d ring=%d",
	           sceKernelGetThreadCurrentPriority(), ELEMENTARY_AUDIO_RING_GRAINS);

	while (st->signals->start_gate && !*st->signals->start_gate && !*st->signals->cancel && !st->signals->stop)
		sceKernelDelayThread(1000);
	while (!*st->signals->cancel && !st->signals->stop) {
		if (st->signals->paused) {
			last_output_wall_us = 0;
			sceKernelDelayThread(ELEMENTARY_AUDIO_PAUSE_DELAY_US);
			continue;
		}
		uint32_t read_seq = st->pcm_read_seq;
		if (read_seq == st->pcm_write_seq) {
			if (st->producer_done) break;
			sceKernelDelayThread(1000);
			continue;
		}
		int requested_volume = vita_audio_volume_clamp(
		    __atomic_load_n(&st->signals->volume_percent, __ATOMIC_ACQUIRE));
		if (requested_volume != last_applied_volume && st->port >= 0) {
			last_applied_volume = requested_volume;
			int hardware_percent = vita_audio_hardware_percent(requested_volume);
			int hardware_volume = hardware_percent * SCE_AUDIO_VOLUME_0DB / 100;
			int volume[2] = { hardware_volume, hardware_volume };
			sceAudioOutSetVolume(st->port,
			                      SCE_AUDIO_VOLUME_FLAG_L_CH |
			                          SCE_AUDIO_VOLUME_FLAG_R_CH,
			                      volume);
		}
		uint32_t slot = read_seq % ELEMENTARY_AUDIO_RING_GRAINS;
		int16_t *grain = st->pcm_ring +
		                 slot * ELEMENTARY_AUDIO_MAX_GRAIN_FRAMES *
		                     ELEMENTARY_AUDIO_MAX_CHANNELS;
		/* Apply the latest boost when consuming each grain, not while filling
		 * the ring. Queued original PCM survives boost-down and mute toggles. */
		const int16_t *output = grain;
		if (requested_volume > 100) {
			vita_audio_boost_pcm16(grain, boosted,
			    st->output_grain * st->channels, requested_volume);
			output = boosted;
		}
		int output_ret = sceAudioOutOutput(st->port, output);
		if (output_ret < 0) {
			log_printf("elementary_audio: sceAudioOutOutput -> 0x%08X",
			           (unsigned)output_ret);
			st->signals->had_error = 1;
			st->signals->stop = 1;
			break;
		}
		uint64_t output_wall_us = sceKernelGetProcessTimeWide();
		if (last_output_wall_us) {
			uint64_t gap_us = output_wall_us - last_output_wall_us;
			uint64_t expected_us =
			    (uint64_t)st->output_grain * 1000000ULL / st->sample_rate;
			if (gap_us > maximum_output_gap_us) maximum_output_gap_us = gap_us;
			if (gap_us > expected_us + 4000ULL) delayed_output_grains++;
		}
		last_output_wall_us = output_wall_us;
		output_grains++;
		if (st->pcm_pts_valid[slot]) st->signals->played_until_ms = st->pcm_end_ms[slot];
		__sync_synchronize();
		st->pcm_read_seq = read_seq + 1;
	}
    /* AudioOut accepts complete grains. Wait for the final submitted grain
     * before publishing clean EOF or releasing its backing storage/port. */
    if(output_grains&&st->producer_done&&!st->signals->had_error&&!*st->signals->cancel&&!st->signals->stop&&
       sceAudioOutOutput(st->port,NULL)<0)st->signals->had_error=1;

	if (output_grains)
		log_printf("elementary_audio: cadence grains=%u delayed=%u gap_max=%llu us underrun_waits=buffered",
		           output_grains, delayed_output_grains,
		           (unsigned long long)maximum_output_gap_us);
	return 0;
}

/* Decode frame sizes belong to the codec. Pack every sample continuously;
 * never pad an individual packet or require AAC's 1024-frame alignment. */
static int publish_grain(AudioPlaybackEngine *st){
 while(st->pcm_write_seq-st->pcm_read_seq>=ELEMENTARY_AUDIO_RING_GRAINS&&!*st->signals->cancel&&!st->signals->stop)sceKernelDelayThread(1000);
 if(*st->signals->cancel||st->signals->stop)return AVERROR_EXIT;
 uint32_t seq=st->pcm_write_seq,slot=seq%ELEMENTARY_AUDIO_RING_GRAINS;
 int16_t *out=st->pcm_ring+slot*ELEMENTARY_AUDIO_MAX_GRAIN_FRAMES*ELEMENTARY_AUDIO_MAX_CHANNELS;
 memcpy(out,st->partial_pcm,st->output_grain*st->channels*sizeof(*out));
 st->pcm_pts_valid[slot]=st->partial_pts_known&&st->partial_end_us>=0;
 uint64_t end=st->partial_end_us>=0?(uint64_t)st->partial_end_us/1000:0;
 st->pcm_end_ms[slot]=end>UINT32_MAX?UINT32_MAX:(uint32_t)end;
 __sync_synchronize();st->pcm_write_seq=seq+1;st->partial_frames=0;return 0;
}
static int queue_pcm(AudioPlaybackEngine *st,const VitaDecodedAudio *decoded,int64_t pts_us){
 if(!decoded->pcm||!decoded->frames||decoded->channels!=st->channels||decoded->sample_rate!=st->sample_rate)return AVERROR_INVALIDDATA;
 if(!st->signals->first_packet_ready){
  st->signals->first_packet_pts_valid=pts_us!=AV_NOPTS_VALUE&&pts_us>=0;
  if(st->signals->first_packet_pts_valid)st->signals->first_packet_pts_ms=(uint64_t)pts_us/1000>UINT32_MAX?UINT32_MAX:(uint32_t)(pts_us/1000);
  __sync_synchronize();st->signals->first_packet_ready=1;
 }
 for(uint32_t offset=0;offset<decoded->frames;){
  uint32_t n=st->output_grain-st->partial_frames;if(n>decoded->frames-offset)n=decoded->frames-offset;
  memcpy(st->partial_pcm+st->partial_frames*st->channels,decoded->pcm+offset*st->channels,n*st->channels*sizeof(int16_t));
  st->partial_frames+=n;offset+=n;
  if(pts_us!=AV_NOPTS_VALUE){st->partial_end_us=pts_us+(int64_t)offset*1000000/st->sample_rate;st->partial_pts_known=1;}
  else if(st->partial_pts_known)st->partial_end_us+=(int64_t)n*1000000/st->sample_rate;
  if(st->partial_frames==st->output_grain){int r=publish_grain(st);if(r<0)return r;}
 }
 return 0;
}
static int drain_software(AudioPlaybackEngine *st){
 int produced=0,r;VitaDecodedAudio out;int64_t pts;
 while((r=vita_sw_audio_decoder_receive(st->software,&out,&pts))==1){
  if(pts!=AV_NOPTS_VALUE)pts-=st->timeline_origin_us;
  int q=queue_pcm(st,&out,pts);if(q<0)return q;produced++;
 }
 return r==0||r==AVERROR_EOF?produced:r;
}
static int software_packet(AudioPlaybackEngine *st,const AVPacket *packet){
 int r;
 while((r=vita_sw_audio_decoder_send(st->software,packet))==AVERROR(EAGAIN)){
  int progress=drain_software(st);if(progress<=0)return progress<0?progress:AVERROR_INVALIDDATA;
 }
 if(r<0&&r!=AVERROR_EOF)return r;
 return drain_software(st);
}
static void decoder_term(AudioPlaybackEngine *st) {
 vita_sw_audio_decoder_destroy(st->software);st->software=NULL;
 vita_hw_audio_decoder_destroy(st->decoder);st->decoder=NULL;
 st->signals->decoder_ready=0;
}
static int audio_decode_thread(SceSize args, void *argp) {
	(void)args;
	AudioPlaybackEngine *st = *(AudioPlaybackEngine **)argp;

	AVPacket *packet = av_packet_alloc();
	if (!packet) {
		st->signals->had_error = 1;
		__sync_synchronize();
		st->signals->thread_done = 1;
		return 0;
	}

	const AVRational stream_tb = st->demux->streams[st->stream_index]->time_base;
	const AVRational us_tb = { 1, 1000000 };
	int decode_attempts = 0;
	int have_packet = 0;
	int reached_eof = 0;
	log_printf("elementary_audio: producer priority=%d",
	           sceKernelGetThreadCurrentPriority());
	/* Prime one selected AU while output is still gated. Besides overlapping the
	 * demux read with video startup, this gives a track-switch transaction an exact
	 * readiness point: clean EOF here means the requested track has no audio at the
	 * current media position and must not be committed. */
	while (!*st->signals->cancel && !st->signals->stop && !have_packet) {
		int ret = read_audio_packet(st, packet);
		if (ret == 0) {
			__sync_synchronize();
			st->signals->eof = 1;
			goto done;
		}
		if (ret < 0) {
			if (!*st->signals->cancel && !st->signals->stop) {
				log_printf("elementary_audio: initial av_read_frame -> %d", ret);
				st->signals->had_error = 1;
			}
			goto done;
		}
		int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
		int64_t pts_us = pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE
		               : av_rescale_q(pts, stream_tb, us_tb) -
		                     st->timeline_origin_us;
		int64_t pts_ms = pts_us == AV_NOPTS_VALUE ? -1 : pts_us / 1000;
		uint64_t requested_ms = st->initial_position_ms;
		if (requested_ms > 0 &&
		    (pts_ms < 0 || (uint64_t)pts_ms < requested_ms)) {
			av_packet_unref(packet);
			continue;
		}
		if (pts_ms >= 0) {
			st->signals->first_packet_pts_ms = (uint64_t)pts_ms > UINT32_MAX
			                         ? UINT32_MAX : (uint32_t)pts_ms;
			st->signals->first_packet_pts_valid = 1;
		}
		log_printf("elementary_audio: landing AU=%lld ms requested=%llu ms",
		           (long long)pts_ms, (unsigned long long)requested_ms);
		st->initial_position_ms = 0;
		have_packet = 1;
	}
	if (*st->signals->cancel || st->signals->stop) goto done;
	st->output_thid = sceKernelCreateThread(
	    "VmdAudioOut", audio_output_thread,
	    ELEMENTARY_AUDIO_OUTPUT_PRIORITY, ELEMENTARY_AUDIO_OUTPUT_STACK, 0, 0, NULL);
	if (st->output_thid < 0) {
		st->signals->had_error = 1;
		goto done;
	}
	void *output_self = st;
	int output_start_ret = sceKernelStartThread(
	    st->output_thid, sizeof(output_self), &output_self);
	if (output_start_ret < 0) {
		log_printf("elementary_audio: output start -> 0x%08X",
		           (unsigned)output_start_ret);
		sceKernelDeleteThread(st->output_thid);
		st->output_thid = -1;
		st->signals->had_error = 1;
		goto done;
	}
	st->output_started = 1;
	__sync_synchronize();
	/* Readiness is published after actual decoded PCM, preserving rollback. */

	while (!*st->signals->cancel && !st->signals->stop) {
		if (st->signals->paused) {
			/* Let the consumer own pause semantics; stop producing once the ring
			 * is full so decoded PCM remains bounded and ordered. */
			sceKernelDelayThread(ELEMENTARY_AUDIO_PAUSE_DELAY_US);
		}

		if (have_packet) {
			have_packet = 0;
		} else {
			int ret = read_audio_packet(st, packet);
			if (ret == 0) {
				/* EOF belongs to the public playback clock only after AudioOut has
				 * consumed every queued grain. Otherwise video and subtitles would
				 * hand off to wall time while buffered audio is still playing. */
				reached_eof = 1;
                if(st->software){int r=software_packet(st,NULL);if(r<0&&!*st->signals->cancel&&!st->signals->stop)st->signals->had_error=1;}
                if(st->partial_frames&&!st->signals->had_error&&!*st->signals->cancel&&!st->signals->stop){
                 memset(st->partial_pcm+st->partial_frames*st->channels,0,(st->output_grain-st->partial_frames)*st->channels*sizeof(int16_t));
                 if(publish_grain(st)<0&&!*st->signals->cancel&&!st->signals->stop)st->signals->had_error=1;
                }
                break;
			}
			if (ret < 0) {
				/* If cancel is already raised, the error is almost certainly the
				 * interrupt_callback abort: it is not a decode failure, so we
				 * exit cleanly without had_error. */
				if (!*st->signals->cancel && !st->signals->stop) {
					log_printf("elementary_audio: av_read_frame -> %d", ret);
					st->signals->had_error = 1;
				}
				break;
			}
		}

        if(st->software){
         int r=software_packet(st,packet);av_packet_unref(packet);
         if(r<0){if(!*st->signals->cancel&&!st->signals->stop){log_printf("elementary_audio: software decode failed %d",r);st->signals->had_error=1;}break;}
         continue;
        }

        size_t trim_size=0;const uint8_t *trim=av_packet_get_side_data(packet,AV_PKT_DATA_SKIP_SAMPLES,&trim_size);
        uint32_t trim_end=0;
        if(trim&&trim_size>=10){st->trim_leading=AV_RL32(trim);trim_end=AV_RL32(trim+4);}
		VitaDecodedAudio decoded;
		memset(&decoded, 0, sizeof(decoded));
		int decode_ret = vita_hw_audio_decoder_decode(st->decoder, packet->data,
		                                         (size_t)packet->size, &decoded);
		if (decode_ret < 0) {
			log_printf("elementary_audio: vita_hw_audio_decoder_decode -> %d (attempt #%d)",
			          decode_ret, decode_attempts);
			av_packet_unref(packet);
			/* Error not on the first packet: fatal, SS5 point 7. On the first
			 * packet it is tolerated (hardware decoder warm-up) and we carry
			 * on. */
			if (st->signals->first_packet_ready || decode_attempts >=
                (st->demux->streams[st->stream_index]->codecpar->codec_id==AV_CODEC_ID_MP3?8:1)) {
				st->signals->had_error = 1;
				break;
			}
			st->trim_leading=0;decode_attempts++;
			continue;
		}
		decode_attempts++;

        uint32_t skipped=st->trim_leading<decoded.frames?st->trim_leading:decoded.frames;
        st->trim_leading-=skipped;if(skipped)decoded.pcm+=skipped*decoded.channels;decoded.frames-=skipped;
        if(trim_end>decoded.frames)trim_end=decoded.frames;
        decoded.frames-=trim_end;
		/* The pts must be read before av_packet_unref(): unref clears it. */
		int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
		av_packet_unref(packet);

		if (decode_ret == 0 || !decoded.pcm || decoded.frames == 0) {
			/* No samples produced for this packet: it is not an error
			 * (symmetry with the video decoder, SS5 point 2), we keep
			 * feeding packets. */
			continue;
		}
		/* A pause that lands after decode must not discard this access unit.
		 * The per-grain loop below holds the decoded PCM until resume. */
		if (*st->signals->cancel || st->signals->stop) continue;

        int64_t pts_us=pts!=AV_NOPTS_VALUE?av_rescale_q(pts,stream_tb,us_tb)-st->timeline_origin_us:AV_NOPTS_VALUE;
        if(pts_us!=AV_NOPTS_VALUE)pts_us+=(int64_t)skipped*1000000/st->sample_rate;
        if(queue_pcm(st,&decoded,pts_us)<0&&!*st->signals->cancel&&!st->signals->stop)st->signals->had_error=1;
		if (st->signals->had_error || *st->signals->cancel || st->signals->stop) break;
	}

done:
	__sync_synchronize();
	st->producer_done = 1;
	if (st->output_started && st->output_thid >= 0) {
		sceKernelWaitThreadEnd(st->output_thid, NULL, NULL);
		sceKernelDeleteThread(st->output_thid);
		st->output_thid = -1;
		st->output_started = 0;
	}
	if (reached_eof && !st->signals->had_error && !*st->signals->cancel && !st->signals->stop) {
		__sync_synchronize();
		st->signals->eof = 1;
	}
	av_packet_free(&packet);
	__sync_synchronize();
	st->signals->thread_done = 1;
	return 0;
}

static int engine_start(AudioPlaybackEngine *st, VitaAudioPlayback *signals, int mode, AVFormatContext *demux,
                           int stream_index, uint64_t initial_position_ms,
                           volatile int *cancel,
                           volatile int *start_gate) {
	if (!st || !demux || !cancel) return -1;
	memset(st, 0, sizeof(*st));
 st->signals=signals;st->mode=mode;
	st->port = -1;
	st->thid = -1;
	st->output_thid = -1;
	st->demux = demux;
	st->signals->cancel = cancel;
	st->signals->start_gate = start_gate;
	st->initial_position_ms = initial_position_ms;
	st->timeline_origin_us = media_timeline_origin_us(demux);
	st->signals->volume_percent = 100;

	if (stream_index < 0) stream_index = first_audio_stream(demux);
	if (stream_index < 0 || (unsigned int)stream_index >= demux->nb_streams ||
	    demux->streams[stream_index]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
		log_printf("elementary_audio: no valid audio track (stream_index=%d)",
		          stream_index);
		return -1;
	}
	st->stream_index = stream_index;
 if(!vmd_audio_parameters_playable(demux->streams[stream_index]->codecpar))return AVERROR_DECODER_NOT_FOUND;

	const AVCodecParameters *params = demux->streams[stream_index]->codecpar;
	uint32_t channels = params->ch_layout.nb_channels > 0
	                   ? (uint32_t)params->ch_layout.nb_channels : 0;
	uint32_t sample_rate = params->sample_rate > 0 ? (uint32_t)params->sample_rate : 0;
	/* MPEG-TS carries AAC as ADTS. FFmpeg can identify the stream while still
	 * leaving codecpar channels/rate empty after a short probe.
	 * Inspect one AU directly, retain it for the worker, and configure
	 * sceAudiodec for ADTS instead of rejecting an otherwise valid live. */
	if (params->codec_id == AV_CODEC_ID_AAC &&
	    (!channels || !sample_rate || params->extradata_size <= 0)) {
		AVPacket *probe = av_packet_alloc();
		int probe_ret = probe ? read_audio_packet(st, probe) : AVERROR(ENOMEM);
		if (probe_ret > 0) {
			uint32_t adts_channels = 0, adts_rate = 0;
			if (parse_adts_header(probe->data, (size_t)probe->size,
			                      &adts_channels, &adts_rate) == 0) {
				st->input_is_adts = 1;
				if (!channels) channels = adts_channels;
				if (!sample_rate) sample_rate = adts_rate;
				log_printf("elementary_audio: ADTS detected ch=%u rate=%u packet=%d",
				          adts_channels, adts_rate, probe->size);
			}
			st->prefetched_packet = probe;
		} else {
			av_packet_free(&probe);
			if ((!channels || !sample_rate) && probe_ret < 0)
				log_printf("elementary_audio: probe ADTS -> %d", probe_ret);
		}
	}
	if (!channels || !sample_rate) {
		log_printf("elementary_audio: missing codec parameters (ch=%u rate=%u)",
		          channels, sample_rate);
		av_packet_free(&st->prefetched_packet);
		return -1;
	}
	st->sample_rate = sample_rate;
	size_t ring_bytes = ELEMENTARY_AUDIO_RING_GRAINS *
	                    ELEMENTARY_AUDIO_MAX_GRAIN_FRAMES *
	                    ELEMENTARY_AUDIO_MAX_CHANNELS * sizeof(int16_t);
	st->pcm_ring = memalign(64, ring_bytes);
	if (!st->pcm_ring) {
		av_packet_free(&st->prefetched_packet);
		return AVERROR(ENOMEM);
	}
	memset(st->pcm_ring, 0, ring_bytes);

 int ret=AVERROR_DECODER_NOT_FOUND;
 st->output_grain=params->codec_id==AV_CODEC_ID_AC3?1536:
                  params->codec_id==AV_CODEC_ID_MP3?(sample_rate<=24000?576:1152):1024;
 if(mode!=VT_AUDIO_DECODE_SW&&params->codec_id!=AV_CODEC_ID_AC3){
  st->decoder=vita_hw_audio_decoder_create();
  VitaHwAudioDecoderConfig config={.codec=params->codec_id==AV_CODEC_ID_MP3?VITA_AUDIO_CODEC_MP3:VITA_AUDIO_CODEC_AAC,
   .channels=channels,.sample_rate=sample_rate,.input_is_adts=st->input_is_adts};
  ret=st->decoder?vita_hw_audio_decoder_open(st->decoder,&config):AVERROR(ENOMEM);
 }
 if(mode!=VT_AUDIO_DECODE_HW&&(mode==VT_AUDIO_DECODE_SW||ret<0)){
  vita_hw_audio_decoder_destroy(st->decoder);st->decoder=NULL;
  st->software=vita_sw_audio_decoder_create();
  ret=st->software?vita_sw_audio_decoder_open(st->software,params,demux->streams[stream_index]->time_base):AVERROR(ENOMEM);
  if(ret>=0){channels=VITA_SOFTWARE_AUDIO_CHANNELS;sample_rate=VITA_SOFTWARE_AUDIO_RATE;st->sample_rate=sample_rate;}
  log_printf("audio_playback: software codec=%s ret=%d; video unchanged",avcodec_get_name(params->codec_id),ret);
 }
 if(ret<0){decoder_term(st);av_packet_free(&st->prefetched_packet);free(st->pcm_ring);st->pcm_ring=NULL;return ret;}
 st->channels=channels;st->signals->decoder_ready=1;

	int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, (int)st->output_grain,
	                               (int)sample_rate,
	                               channels == 1 ? SCE_AUDIO_OUT_MODE_MONO
	                                             : SCE_AUDIO_OUT_MODE_STEREO);
	log_printf("elementary_audio: sceAudioOutOpenPort -> 0x%08X (ch=%u rate=%u)",
	          (unsigned)port, channels, sample_rate);
	if (port < 0) {
		decoder_term(st);
		av_packet_free(&st->prefetched_packet);
		free(st->pcm_ring);
		st->pcm_ring = NULL;
		return port;
	}
	st->port = port;
	st->signals->port_open = 1;
	int volume[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
	sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH,
	                     volume);

	st->thid = sceKernelCreateThread("VmdAudioDecode", audio_decode_thread,
	                                 ELEMENTARY_AUDIO_THREAD_PRIORITY,
	                                 ELEMENTARY_AUDIO_THREAD_STACK, 0, 0, NULL);
	if (st->thid < 0) {
		ret = st->thid;
		sceAudioOutReleasePort(st->port);
		st->port = -1;
		st->signals->port_open = 0;
		decoder_term(st);
		av_packet_free(&st->prefetched_packet);
		free(st->pcm_ring);
		st->pcm_ring = NULL;
		return ret;
	}

	void *self = st;
	ret = sceKernelStartThread(st->thid, sizeof(self), &self);
	if (ret < 0) {
		log_printf("elementary_audio: sceKernelStartThread -> 0x%08X", (unsigned)ret);
		sceKernelDeleteThread(st->thid);
		st->thid = -1;
		sceAudioOutReleasePort(st->port);
		st->port = -1;
		st->signals->port_open = 0;
		decoder_term(st);
		av_packet_free(&st->prefetched_packet);
		free(st->pcm_ring);
		st->pcm_ring = NULL;
		return ret;
	}

	st->signals->started = 1;
	return 0;
}

static void engine_join(AudioPlaybackEngine *st) {
	if (!st) return;
	/* Each step is gated on its own state flag, not on the numeric value of
	 * the corresponding field: if engine_start() was never called
	 * (or failed) on a state zeroed by the caller, port/thid set to 0 would
	 * be indistinguishable from valid handles. */
	if (st->signals->started && st->thid >= 0) sceKernelWaitThreadEnd(st->thid, NULL, NULL);
	if (st->signals->port_open) {
		sceAudioOutReleasePort(st->port);
		st->signals->port_open = 0;
	}
	decoder_term(st);
	if (st->signals->started && st->thid >= 0) sceKernelDeleteThread(st->thid);
	av_packet_free(&st->prefetched_packet);
	free(st->pcm_ring);
	st->pcm_ring = NULL;
	st->thid = -1;
	st->port = -1;
	st->signals->started = 0;
}

static int playback_start_mode(int mode,VitaAudioPlayback *state, AVFormatContext *demux,
 int stream,uint64_t position,volatile int *cancel,volatile int *gate) {
 if(!state||!demux||!cancel||state->implementation)return AVERROR(EINVAL);
 AudioPlaybackEngine *engine=calloc(1,sizeof(*engine));if(!engine)return AVERROR(ENOMEM);
 const VitaAudioPlaybackDriver *driver=state->driver;
 memset(state,0,sizeof(*state));state->driver=driver;state->implementation=engine;
 int ret=engine_start(engine,state,mode,demux,stream,position,cancel,gate);
 if(ret<0){engine_join(engine);free(engine);state->implementation=NULL;}
 return ret;
}
static int playback_start(VitaAudioPlayback *s,AVFormatContext *f,int stream,uint64_t position,volatile int *cancel,volatile int *gate){return playback_start_mode(VT_AUDIO_DECODE_AUTO,s,f,stream,position,cancel,gate);}
static int playback_start_hw(VitaAudioPlayback *s,AVFormatContext *f,int stream,uint64_t position,volatile int *cancel,volatile int *gate){return playback_start_mode(VT_AUDIO_DECODE_HW,s,f,stream,position,cancel,gate);}
static int playback_start_sw(VitaAudioPlayback *s,AVFormatContext *f,int stream,uint64_t position,volatile int *cancel,volatile int *gate){return playback_start_mode(VT_AUDIO_DECODE_SW,s,f,stream,position,cancel,gate);}
static void playback_join(VitaAudioPlayback *state){
 if(!state||!state->implementation)return;
 AudioPlaybackEngine *engine=state->implementation;engine_join(engine);free(engine);state->implementation=NULL;
}
static void playback_set_volume(VitaAudioPlayback *state,int percent){
 if(state)__atomic_store_n(&state->volume_percent,vita_audio_volume_clamp(percent),__ATOMIC_RELEASE);
}
static const VitaAudioPlaybackDriver playback_driver={playback_start,playback_join,playback_set_volume};
const VitaAudioPlaybackDriver *vt_audio_playback_driver(void){return &playback_driver;}
const VitaAudioPlaybackDriver *vt_audio_playback_driver_for_mode(int mode){
 static const VitaAudioPlaybackDriver hardware={playback_start_hw,playback_join,playback_set_volume};
 static const VitaAudioPlaybackDriver software={playback_start_sw,playback_join,playback_set_volume};
 return mode==VT_AUDIO_DECODE_HW?&hardware:mode==VT_AUDIO_DECODE_SW?&software:&playback_driver;
}
