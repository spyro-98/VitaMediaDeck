# Online audio host checks

Part of the 1.4.1 validation set. See [suite index and evidence boundaries](../../docs/VALIDATION.md).

Run the existing audio, artwork/cache and read-ahead regressions under
ASan/UBSan and ThreadSanitizer with one command (requires host Jansson,
FFmpeg, pkg-config, mpg123, OpenSSL and libxml2):

```sh
VMD_HOST_JANSSON=/path/to/host/jansson bash tests/online_audio/run-host-tests.sh
```

The runner creates fresh temporary persistence fixtures on every run. No real
app data is read or changed. Host timing does not establish Vita performance.


Compile `test.c` together with `src/media/live_audio.c`, `src/network/online_audio.c`, `src/network/http_audio_stream.c` and `src/network/artwork_cache.c`. Put this directory's `include` first in the include search path, then the app `src`, `../vita-https/include`, and host Jansson, libxml2, mpg123 and OpenSSL headers. Link the host Jansson, libxml2, mpg123, OpenSSL crypto and pthread libraries. AddressSanitizer and UndefinedBehaviorSanitizer are recommended. No Vita library or actual network/audio device is used by this harness.

Generate a two-second stereo 44.1 kHz 128 kbps MP3 with FFmpeg's sine source. Run the executable with that MP3 path, **from a new temporary working directory**: the persistence fixture creates and removes its own `ux0:data/VitaMediaDeck/online-audio.json` beneath the current directory. Do not run in a directory containing real app data.

Optional subsequent arguments are local RSS fixtures. Each is exercised through
all 64-item pages and the production fetch path with a mocked HTTP response.
The September 7 run passed 6,066 assertions with the saved Nova Lectio feed
(2,484,364 bytes / 283 episodes) and La Zanzara (47,087 bytes / 30 episodes).
Synthetic checks cover feeds beyond 2 MiB, late channel metadata, cancellation,
DTD rejection and the unchanged 2 MiB catalogue response cap. RSS has an 8 MiB
hard limit; the reader retains one channel-child subtree at a time.

The September 2026 host run passed 5,990 assertions: ICY metadata isolation and transport chunks from 1 to 65,536 bytes, PCM grain accounting, disconnect-on-pause/reconnect, cancellation, three-attempt reconnect, invalid stream handling, URL validation, RSS bounds/duplicate handling/DTD rejection, public-directory JSON mapping, country filtering, escaped queries, 200-match podcast pagination, RSS authors/descriptions/artwork, normalized search, cover cache hits, HTTP cursor lifecycle, saved metadata and corrupt-file preservation. Device scheduling, real HTTP/TLS transfers and visual behavior remain separate checks.

`live_http_smoke.c` is a separate, opt-in network test. Compile it with `src/media/live_audio.c`, the same mock include path, and host curl/mpg123. Pass a public direct MP3 URL. It decodes five seconds with paced simulated audio output, without saving or emitting audio. SomaFM Groove Salad delivered station/song metadata and 44.1 kHz / 128 kbps MP3 (216 PCM grains, 5,015 ms). TLS certificate validation remains enabled in the host curl adapter; that adapter is not a substitute for testing Vita HTTPS on the console.

## Read-ahead and persistent artwork

`prefetch_test.c` includes the production HTTP audio stream implementation. From
the app root:

```sh
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined \
  -Isrc -I../vita-https/include tests/online_audio/prefetch_test.c \
  -lpthread -o /tmp/vmd-prefetch-test
/tmp/vmd-prefetch-test
```

Repeat with `-fsanitize=thread`. The mock injects 800 ms range-fetch latency;
the paced consumer verifies read-ahead, seeks during a fetch, EOF, transport
failure and cancellation. These checks do not prove Vita audio scheduling.

Compile `cache_test.c` with `artwork_cache.c`, `iptv_catalog.c`,
`iptv_manifest.c` and `iptv_saved.c` from `src/network`; use the app `src` and
host OpenSSL, Jansson and libxml2 headers, and link crypto, Jansson, libxml2 and
pthread. Run **from a new temporary directory**, optionally passing a local
extended-M3U fixture. It covers cache reuse, corruption, eviction and byte
limits, saved lists/channel favourites, invalid-store preservation and catalogue
layout. The supplied IPTV-Italia fixture imported 57 entries (19 DASH entries,
no logos); importing a URL is not proof that the remote stream is playable.

Catalogue snapshots are tested by `query_cache_test.c` under ASan/UBSan and
TSan, including selector isolation, corruption, cancellation, concurrent writes
and eviction. RSS channel-cover recovery is part of the production HTTP mock
tests. Native UI transition tests are in `tests/iptv/cache_transition_test.c`.


Artwork follow-up: `run-host-tests.sh` exercises the actual persistent artwork
cache under ASan/UBSan and TSan. It keeps more than 128 small covers, enforces
256 MiB/4096-entry bounds, reconstructs the memory index in a new process,
returns local paths without HTTP, revalidates aged entries, preserves the last
valid image on failure, publishes changed bytes atomically and resolves old
persisted paths to the new revision. Render-side peek uses a nonblocking lock.
The simulated HTTP/image bytes are test data, not device decoding acceptance.
