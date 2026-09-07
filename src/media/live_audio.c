#include "media/live_audio.h"
#include <mpg123.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <vita_https.h>
#include "media/audio_volume.h"
#include "system/background_audio.h"

#define LIVE_GRAIN 1024
typedef struct {
	const VtLiveAudioConfig *config;
	mpg123_handle *decoder;
	int port;
	long rate;
	unsigned bitrate;
	int16_t pcm[LIVE_GRAIN * 2];
	size_t pending;
	uint64_t position_us;
	size_t undecoded;
	uint64_t power_tick;
	size_t interval, audio_left, meta_left, meta_size;
	char metadata[4081], station[160], title[320];
} LiveContext;

static int live_progress(int64_t current, int64_t total, void *opaque) {
	(void)current; (void)total;
	LiveContext *c = opaque;
	uint64_t now = sceKernelGetProcessTimeWide();
	if (now >= c->power_tick) {
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
		c->power_tick = now + 1000000ULL;
	}
	return __atomic_load_n(c->config->cancel, __ATOMIC_ACQUIRE) ||
	       __atomic_load_n(c->config->paused, __ATOMIC_ACQUIRE);
}

static size_t decode_write(const void *data, size_t size, void *opaque) {
	LiveContext *c = opaque;
	if (live_progress(0, 0, c) || size > 65536 ||
	    mpg123_feed(c->decoder, data, size) != MPG123_OK) return 0;
	c->undecoded += size;
	for (;;) {
		if (live_progress(0, 0, c)) return 0;
		size_t decoded = 0;
		int ret = mpg123_read(c->decoder, (unsigned char *)c->pcm + c->pending,
		    sizeof(c->pcm) - c->pending, &decoded);
		if (ret == MPG123_NEW_FORMAT) {
			long rate; int channels, encoding;
			if (mpg123_getformat(c->decoder, &rate, &channels, &encoding) != MPG123_OK ||
			    channels != 2 || !(encoding & MPG123_ENC_SIGNED_16) || rate <= 0)
				return 0;
			if (c->port >= 0) sceAudioOutReleasePort(c->port);
			c->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, LIVE_GRAIN,
			    rate, SCE_AUDIO_OUT_MODE_STEREO);
			if (c->port < 0) return 0;
			c->rate = rate;
			c->pending = 0;
			struct mpg123_frameinfo2 info;
			if (mpg123_info(c->decoder, &info) == MPG123_OK)
				c->bitrate = info.bitrate > 0 ? (unsigned)info.bitrate : 0;
			continue;
		}
		c->pending += decoded;
		if (decoded) c->undecoded = 0;
		if (c->pending == sizeof(c->pcm)) {
			if (c->port < 0 || c->rate <= 0) return 0;
			int paused = __atomic_load_n(c->config->paused, __ATOMIC_ACQUIRE);
			int volume = paused ? 0 : vt_audio_volume_percent();
			int hardware = volume > 100 ? 100 : volume;
			int levels[2] = {hardware * SCE_AUDIO_VOLUME_0DB / 100,
			                 hardware * SCE_AUDIO_VOLUME_0DB / 100};
			sceAudioOutSetVolume(c->port, SCE_AUDIO_VOLUME_FLAG_L_CH |
			    SCE_AUDIO_VOLUME_FLAG_R_CH, levels);
			if (volume > 100)
				for (unsigned i = 0; i < LIVE_GRAIN * 2; i++) {
					int value = (int)c->pcm[i] * volume / 100;
					c->pcm[i] = value > 32767 ? 32767 : value < -32768 ? -32768 : value;
				}
			if (sceAudioOutOutput(c->port, c->pcm) < 0) return 0;
			if (!paused) c->position_us += LIVE_GRAIN * 1000000ULL / (uint64_t)c->rate;
			c->config->update(c->config->ctx, (unsigned)c->rate, c->bitrate,
			                  c->position_us / 1000ULL);
			c->pending = 0;
		}
		if (ret == MPG123_NEED_MORE) break;
		if (ret != MPG123_OK || (!decoded && c->pending != sizeof(c->pcm))) return 0;
	}
	/* An HTML error page, playlist or unsupported codec must not grow forever. */
	return c->undecoded > 256 * 1024 ? 0 : size;
}

static void publish_icy(LiveContext *c) {
	if(c->config->metadata)c->config->metadata(c->config->ctx,c->station,c->title);
}
static size_t live_header(const void *bytes, size_t size, void *opaque) {
	LiveContext *c=opaque;
	char line[1024];size_t n=size<sizeof(line)-1?size:sizeof(line)-1;
	memcpy(line,bytes,n);line[n]=0;
	if(!strncasecmp(line,"HTTP/",5)) {
		c->interval=c->audio_left=c->meta_left=c->meta_size=0;
		c->station[0]=c->title[0]=0;
	} else if(!strncasecmp(line,"icy-metaint:",12)) {
		char *end;unsigned long interval=strtoul(line+12,&end,10);
		if(end==line+12 || !interval || interval>16*1024*1024)return 0;
		c->interval=c->audio_left=interval;
	} else if(!strncasecmp(line,"icy-name:",9)) {
		char *value=line+9;while(*value==' '||*value=='\t')value++;
		value[strcspn(value,"\r\n")]=0;
		snprintf(c->station,sizeof(c->station),"%s",value);publish_icy(c);
	}
	return size;
}
static size_t live_write(const void *data, size_t size, void *opaque) {
	LiveContext *c=opaque;const unsigned char *p=data;size_t left=size;
	if(!c->interval)return decode_write(data,size,c);
	while(left) {
		if(live_progress(0,0,c))return 0;
		if(c->meta_left) {
			size_t n=left<c->meta_left?left:c->meta_left;
			memcpy(c->metadata+c->meta_size,p,n);c->meta_size+=n;c->meta_left-=n;p+=n;left-=n;
			if(!c->meta_left) {
				c->metadata[c->meta_size]=0;
				char *title=strstr(c->metadata,"StreamTitle='");
				if(title && strstr(title+13,"';")){title+=13;char *end=strstr(title,"';");*end=0;
					for(char *q=title;*q;q++)if((unsigned char)*q<32)*q=' ';
					snprintf(c->title,sizeof(c->title),"%s",title);publish_icy(c);}
				c->audio_left=c->interval;
			}
		} else if(!c->audio_left) {
			c->meta_left=(size_t)*p++*16;left--;c->meta_size=0;
			if(!c->meta_left)c->audio_left=c->interval;
		} else {
			size_t n=left<c->audio_left?left:c->audio_left;
			if(decode_write(p,n,c)!=n)return 0;
			c->audio_left-=n;p+=n;left-=n;
		}
	}
	return size;
}

int vt_live_audio_run(const VtLiveAudioConfig *config) {
	if (!config || !config->url || !config->cancel || !config->paused || !config->update)
		return -1;
	VtBackgroundAudioLease lease;
	if (mpg123_init() != MPG123_OK) return -1;
	vt_background_audio_acquire(&lease);
	int result = -1;
	uint64_t position_us = 0;
	/* Reconnect only a bounded number of times; never hide a permanent failure. */
	for (int attempt = 0; attempt < 3 &&
	     !__atomic_load_n(config->cancel, __ATOMIC_ACQUIRE); attempt++) {
		while (__atomic_load_n(config->paused, __ATOMIC_ACQUIRE) &&
		       !__atomic_load_n(config->cancel, __ATOMIC_ACQUIRE)) {
			config->update(config->ctx, 0, 0, position_us / 1000);
			sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
			sceKernelDelayThread(20000);
		}
		if (__atomic_load_n(config->cancel, __ATOMIC_ACQUIRE)) break;
		if (config->buffering) config->buffering(config->ctx);
		LiveContext c = { .config = config, .port = -1, .position_us = position_us };
		c.decoder = mpg123_new(NULL, NULL);
		VitaHttpsClientConfig options = { .user_agent = "VitaMediaDeck/1.3.0",
		    .allow_http = 1, .connect_timeout_ms = 10000,
		    .request_timeout_ms = LONG_MAX, .low_speed_bytes_per_second = 128,
		    .low_speed_seconds = 15 };
		VitaHttpsClient *client = vita_https_client_create(&options);
		if (!c.decoder || !client) {
			if (c.decoder) mpg123_delete(c.decoder);
			if (client) vita_https_client_destroy(client);
			break;
		}
		mpg123_param(c.decoder, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO, 0);
		mpg123_format_none(c.decoder);
		const long *rates; size_t count;
		mpg123_rates(&rates, &count);
		for (size_t i = 0; i < count; i++)
			mpg123_format(c.decoder, rates[i], MPG123_STEREO, MPG123_ENC_SIGNED_16);
		if (mpg123_open_feed(c.decoder) == MPG123_OK) {
			const char *headers[] = { "Icy-MetaData: 1", NULL };
			VitaHttpsRequest request = { .method = "GET", .url = config->url,
			    .headers = headers,
			    .write = live_write, .write_opaque = &c,
			    .progress = live_progress, .progress_opaque = &c,
			    .cancel_flag = config->cancel, .header = live_header, .header_opaque = &c };
			VitaHttpsResponse response;
			result = vita_https_perform(client, &request, &response);
		}
		if (c.port >= 0) sceAudioOutReleasePort(c.port);
		position_us = c.position_us;
		vita_https_client_destroy(client);
		mpg123_delete(c.decoder);
		if (__atomic_load_n(config->paused, __ATOMIC_ACQUIRE)) {
			/* Drop the transport and decoder queues. Play opens a fresh live GET. */
			attempt = -1;
			continue;
		}
		/* A live stream ending is a disconnect, even with HTTP 200. */
		result = -1;
		for (int wait = 0; attempt < 2 && wait < 20 &&
		     !__atomic_load_n(config->cancel, __ATOMIC_ACQUIRE); wait++)
			sceKernelDelayThread(100000);
	}
	vt_background_audio_release(&lease);
	mpg123_exit();
	return result;
}
