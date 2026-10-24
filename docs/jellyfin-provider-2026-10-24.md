# Dedicated Jellyfin provider — 24 October 2026

App 1.4.1 / Vita APP_VER 01.05 follow-up. Decoder ABI remains 2.0.0.

L1 lists Home, Network, **Jellyfin**, Navidrome, Immich, Radio/Podcast and IPTV,
followed by Settings, Controls guide and About. Player and Download precede
content when active. Section identifiers remain stable; presentation order is
independent of those identifiers and of saved Home tab positions.

The shared provider picker filters the existing saved-source database to
Jellyfin. It supports Connect, Add server, Edit connection and Remove connection
through R1, with explicit confirmation and save-failure rollback. Adding a
server fixes its type to Jellyfin and uses the existing native server editor.
Removing a connection preserves the server contents and other providers.

The route reuses the existing Jellyfin authentication, TLS trust, memory-only
session tokens, library browser and remote video player. Video selection keeps
its source, credentials and metadata; returning restores the directory and
selection. Generic Network access remains available for existing configurations.

All seven L1 layouts accommodate ten sections plus the optional player/download
rows. Legacy keeps its category gaps; Ghost Mesh has seven orbital content nodes;
Signal separates content from application tools; other layouts fit within their
existing panels. Home layouts are unchanged.

## Verification

Host checks cover IT/EN, all themes, controller/touch section mapping, live
player/download combinations, non-overlapping menu bounds, saved-server filtering,
video-selection handoff, browser-resume routing, connection editing and confirmed
removal with persistence rollback. The personal-media HTTP/range regressions and
native Vita Release build are also run. See the test bundle logs for results.
Host images do not establish console GPU performance or authenticated playback.

Test build: `jellyfin-1024`, bubble `VMD jellyfin-1024`, log
`ux0:data/VitaMediaDeck/session_jellyfin-1024.txt`.

On Vita, check every theme with Player/Download active; connect to a real
Jellyfin server; browse/play and return to the same item; check audio/subtitle
selection, mini-player navigation and R1 Add/Edit/Remove cancellation. Confirm
that other saved services remain available. Real-server authentication and
console input/rendering acceptance remain pending for this build.
