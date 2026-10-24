#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "media/background_playback.h"
typedef struct {
    char library_id[16];
    int podcast_position_valid, decoder_source;
    uint64_t podcast_position_ms;
    VtBackgroundPlaybackSnapshot snapshot;
} BackgroundPlaybackJob;
static BackgroundPlaybackJob g_background;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void snapshot_lock(void) {
    pthread_mutex_lock(&mutex);
}
static void snapshot_unlock(void) {
    pthread_mutex_unlock(&mutex);
}
#include "background-sample.inc"
int main(void) {
    char id[64];
    uint64_t p = 0, d = 0;
    strcpy(g_background.library_id, "u-fixture");
    g_background.decoder_source = 1;
    g_background.snapshot.state = VT_BACKGROUND_PLAYING;
    g_background.snapshot.duration_ms = 3600000;
    /* Optimistic seek destinations and an unconfirmed opening position cannot save. */
    g_background.snapshot.position_ms = 900000;
    assert(!vt_background_playback_library_sample(id, &p, &d));
    g_background.podcast_position_valid = 1;
    g_background.podcast_position_ms = 120000;
    assert(vt_background_playback_library_sample(id, &p, &d) && p == 120000 && d == 3600000 &&
           !strcmp(id, "u-fixture"));
    g_background.snapshot.position_ms = 0;
    assert(vt_background_playback_library_sample(id, &p, &d) && p == 120000);
    g_background.podcast_position_ms = 0;
    assert(vt_background_playback_library_sample(id, &p, &d) && p == 0);
    g_background.podcast_position_valid = 0;
    assert(!vt_background_playback_library_sample(id, &p, &d));
    g_background.library_id[0] = 0;
    g_background.podcast_position_valid = 1;
    assert(!vt_background_playback_library_sample(id, &p, &d));
    puts("Production background checkpoint sampler: 6 checks; unconfirmed open, optimistic seek, "
         "failed-seek reset, confirmed rewind and session isolation passed");
    return 0;
}
