# IPTV branch: compatibility contract

Status: planning only. No IPTV playback implementation or decoder changes are
introduced by this branch setup.

## Integrated baseline

- VitaMediaDeck master: `911b2cf` (1.3.0 UI, online audio and accumulated fixes).
- vita-hw-decoder master: `2569874`.
- vita-sw-decoder master: `03dabef`.
- vita-https master: `c028d30`.

These are local commits; publication is a separate operation. The podcast Home
failure-navigation fix has host regression coverage. The reported device
podcast-opening failure still needs console logs; do not label it resolved by
the navigation fix alone.

## Required boundaries

1. Keep playlist import, authentication, HTTP/HLS connections, retries and
   bounded buffering in a shared app-side source/session layer. Do not duplicate
   connection management inside HW and SW decoding code.
2. Preserve current file/Jellyfin/WebDAV/SFTP/SMB paths and their behavior by
   default. Live playback is explicitly opt-in, not a reinterpretation of all
   existing stream factories.
3. Do not change existing public function signatures or public struct layouts
   for IPTV. Appending fields alone is not binary compatibility. If new live
   capabilities are unavoidable, prefer separate, versioned opt-in APIs/options
   with explicit size/version and retain existing entry points and defaults.
   Check both source and binary interface implications before implementation.
4. Keep codec internals unchanged unless a reproducible live-playback requirement
   demonstrates otherwise. Make corresponding input-contract changes consistently
   in both backends, without rewriting their stable file-seek/audio-track logic.
5. Use one logical live source/timeline for audio and video. Independent cursors
   may share bounded cached data; they must not start separate, unrelated live
   broadcasts. Define buffer eviction, lagging-reader and discontinuity behavior
   before accepting a cached-cursor implementation. A single-demux packet path
   is an alternative to evaluate, not an automatic whole-player refactor.
6. Empty live buffers are not EOF. Reads must remain cancellable; cancellation,
   transport failure and natural completion must remain distinct. Do not fake
   full seekability or a known duration for an endless stream.
7. Preserve the source session across backend changes where feasible, land at a
   valid keyframe and synchronize audio/video/subtitle resets. Do not promise
   seamless switching without hardware evidence.
8. Retain TLS verification, safe redirect handling and token-redacted diagnostics.
   Bound playlist, EPG, artwork, segment and decoded-frame memory independently.

## First implementation gate

Prove a narrowly defined H.264/AAC live source on hardware before building a
complete catalogue. Then add M3U import, channel groups/logos/favorites and XMLTV
guide data. Explicitly report unsupported stream formats. Keep DRM, recording,
timeshift and broad provider-specific account integration outside this first gate.

Regression acceptance includes existing finite-file playback; distant forward
and backward seeks; audio/subtitle track changes; repeated HW/SW changes;
pause/fullscreen/miniplayer transitions; cancellation and reconnect; and long
sessions alongside a background download. Host builds/tests and archive checks
are not substitutes for this Vita acceptance matrix.
