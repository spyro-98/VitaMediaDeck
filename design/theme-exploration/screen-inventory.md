# Draft coverage and source map

> Historical design/research evidence. Current release: [1.4.1](../../docs/releases/1.4.1.md); current checks: [validation](../../docs/VALIDATION.md). Proposals and old measurements below are not new implementation or console proof.

The atlas contains 58 initial page/state compositions, repeated across three directions. Families share primitives intentionally; this is not a claim to have captured every runtime branch, provider response, localization or system IME state.

| Drafts | Family | Current app source / behavior reference |
| --- | --- | --- |
| 01–08 | Startup, Home, local grids/lists, starred radio and combined podcast | `src/main.c`, `src/ui/local_media_screen.c`, `local_files_screen.c`, `home_tabs.c` |
| 09–12 | Network sources, collection, connection form, host verification | `src/ui/network_sources_screen.c` and provider security flows |
| 13–19 | Radio/podcast catalogs, countries, genres, shows, episodes | `src/ui/online_audio_screen.c`, `src/network/online_audio.c` |
| 20–22 | IPTV saved playlists, live/VOD channels, URL/QR entry | `src/ui/iptv_screen.c`, `src/network/iptv_saved.c` |
| 23–28 | Direct download, destination, active/paused/complete, QR | `src/ui/loading_screen.c`, `folder_picker.c`, `qr_scanner.c`, download manager |
| 29–34 | Video HUD/panel, left-art audio players, image HUD | `src/media/hw_player_screen.c`, `src/ui/music_player.c`, `image_viewer.c` |
| 35–37 | Mini-player + transfer + browsing; ECO and lock | `src/ui/mini_player.c`, player/runtime state |
| 38–41 | L1/R1, shared file actions, destructive confirmation | `src/ui/sections_sidebar.c`, `local_media_actions.c` |
| 42–47 | Settings categories, subtitle preview, tab order | `src/ui/settings_screen.c`, preferences and home tabs |
| 48–50 | Guide, concise About, log reader | `src/ui/text_reader.c`, `about_screen.c`, `log_viewer.c` |
| 51 | Loading, empty, error, offline | Shared feedback states; retain caller context |
| 52–57 | Center/right artwork for music, podcast, radio | Proposed alternatives for `src/ui/music_player.c`; left variants are 31–33 |
| 58 | Left panel audio layout picker | Proposed audio-only Player subpage, keeping Sections order intact |

## Shared overlays and state review still required before implementation

- System IME is platform-owned: preserve its contract; do not draw a pretend replacement keyboard. Network password/port/share/domain/path variants use the connection-form template and need provider-specific review.
- Rename and New folder use text entry; move uses destination selection. These share reviewed primitives but still require complete workflow tests in the app.
- Every media family needs missing artwork, missing album/author, empty folder, failed image decode, unavailable size/duration and long-title variants. The atlas shows representative states, not exhaustive localization coverage.
- Search results need both cached/offline and progressively arriving rows. Country/region and genre templates must keep focus when rows append.
- Download unknown-total, failure, resume unsupported and abort-confirmation variants use the shared state system; percentage/ETA must disappear rather than be invented when unknown.
- Player-specific right panels require all actual track, decoder and audio-theme choices. The draft panel demonstrates hierarchy; it does not remove unshown options. Artwork theme remains conditional on cover availability.
- Right-panel entry and global L1 availability must respect existing verified input bindings. Draft 34 shows only Square/Start/Cross actions verified in `image_viewer.c`; it does not resurrect earlier conflicting L1/R1 zoom mappings.
- Breadcrumb leaves must stay identifiable; long ancestors collapse first. Authentication tokens, passwords and sensitive URL query strings must never enter breadcrumbs.

## Motion review

The separate motion lab is the source of truth for proposed ownership: one strong focus surface, a distinct current-tab marker, interruptible movement, and exclusive side panels. Static gallery frames show a chosen focus state; they do not prove full navigation behavior.
