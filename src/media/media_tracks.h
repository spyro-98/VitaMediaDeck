#ifndef VITAMEDIADECK_MEDIA_MEDIA_TRACKS_H
#define VITAMEDIADECK_MEDIA_MEDIA_TRACKS_H

#include <stddef.h>
#include <stdint.h>

#include "media/vita_decoder.h"

#define VT_MEDIA_MAX_AUDIO_TRACKS VT_DECODER_MAX_AUDIO_TRACKS
#define VT_MEDIA_MAX_SUBTITLE_TRACKS VT_DECODER_MAX_SUBTITLE_TRACKS

typedef struct VtSubtitleReader VtSubtitleReader;

int vt_media_tracks_probe(const VtDecoderStreamFactory *factory,
	                      VtDecoderTrackInfo *audio_tracks, int *audio_count,
	                      VtDecoderTrackInfo *subtitle_tracks,
	                      int *subtitle_count, volatile int *cancel);

VtSubtitleReader *vt_subtitle_reader_create(void);
int vt_subtitle_reader_open(VtSubtitleReader *reader,
	                        const VtDecoderStreamFactory *factory,
	                        int stream_index, uint64_t start_position_ms,
	                        volatile int *open_cancel);
void vt_subtitle_reader_disable(VtSubtitleReader *reader);
void vt_subtitle_reader_close(VtSubtitleReader *reader);
void vt_subtitle_reader_destroy(VtSubtitleReader *reader);
int vt_subtitle_reader_text(VtSubtitleReader *reader, uint64_t position_ms,
	                        char *text, size_t text_size);

#endif
