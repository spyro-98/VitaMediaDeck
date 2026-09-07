# Navigation and online audio preview

App version 1.3.0. This work originated on `feature/cyberpunk-ui-study` and is included in the local master integration. GitHub publication and hardware acceptance are separate steps.

## Navigation

- The header displays **VITA // MEDIA DECK**. Press L1 or touch the logo (no L1 badge beneath it). Regular sections: Home, Network, Radio/Podcast, Settings, Controls, About; Player and Download appear while active.
- Shared Home tabs: Library, Local Video, Local Audio, Radio, Podcast Online, Podcast Offline, Images. Touch/swipe or focus tabs with Up, change with Left/Right and enter results with Down. Reorder under Settings > Interface.
- `ux0:podcast`, `ux0:podcasts` and equivalent `uma0:` roots are indexed separately from music. The index is rebuilt for this partition; no podcast folder is created.
- Controls is a standalone scrollable guide, without a fake Settings tab.

## Online audio

- Home Radio and Podcast Online contain favorites only, with no search/discovery toolbar. Use L1 > Radio/Podcast for discovery. Its landing browser offers popular MP3 stations, radio genres, countries, saved entries and Apple's top 100 podcast shows by country. Show URLs are resolved to public RSS feeds before opening or saving.
- Triangle searches; R1 opens the catalogue browser; Start adds a public stream/feed URL; Square saves a favorite; Select shows information.
- Radio searches combine spaced/compact names with tags and state metadata. CamelCase and spaces normalize to equivalent queries. Podcast discovery uses normalized Apple directory queries, including publisher/topic matches.
- Scroll via D-pad or touch for asynchronous pagination. A four-page window retains at most 256 results and reloads earlier pages in reverse navigation. Filters are retained. Apple returns at most 200 matches per variant; RSS responses are bounded to 2 MiB and episodes page automatically.
- Cross plays radio. Only play/pause transport buttons are shown. Pause disconnects; Play reconnects live without queued audio. Radio supports MP3, not AAC/HLS or time shifting.
- Cross opens a podcast and streams its selected episode. Start downloads to the chosen folder. Square shows episode authors/description. Circle restores the parent results and selection.
- Failed, cancelled or empty feed opens retain the previous list and selection; Home does not switch to an empty Download/Info episode view. Failed episode playback returns Home to saved podcasts. Feed/HTTP/playback diagnostics are saved on failure when logging is enabled, without logging URLs or response bodies. The reported device opening failure still requires fresh console logs to identify its cause.
- MP3 podcasts use mpg123 with a seekable HTTP cursor. AAC/M4A uses AvPlayer custom file callbacks over the same verified HTTP/TLS transport. Public URLs and HTTP byte-range support are required. Unsupported enclosures can still be downloaded for compatible local playback. Native playback needs Vita validation.

## Artwork and metadata

- Radio artwork uses catalogue favicons. Older saved stations with a UUID but no artwork are refreshed from the public catalogue; re-saving also refreshes metadata without duplicating the favorite. Podcast artwork uses Apple/RSS, with episode images falling back to the feed cover. Authors, descriptions and tags are retained where supplied, including favorites.
- Visible covers load on a worker into the existing asynchronous thumbnail pipeline. JPEG, PNG, WebP, GIF and BMP are accepted. Responses are limited to 2 MiB and disk caching to 16 replacement slots.
- Opening playback resolves the selected cover and loads a bounded 512-pixel texture. The existing Cover theme derives its palette from this image, including online audio; local audio retains the same behavior. Missing/unsupported artwork uses the no-cover fallback.
- ICY metadata is removed before MP3 decoding. The catalogue station stays in the title row; available song/programme information updates below it. Servers without metadata cannot provide this information. Parsing handles arbitrary network chunk boundaries and resets after redirects/reconnection.

## Validation boundary

- The ASan/UBSan host harness passed **6,022 assertions**: RSS metadata/pagination, normalized searches, saved metadata, cover caching, HTTP cursor lifecycle, PCM accounting, pause/reconnect, cancellation and ICY isolation with 1–65,536-byte chunks.
- The production-screen navigation harness passed 29 assertions covering failed/cancelled/empty feed transactions, selection restoration and player error/miniplayer return paths.
- A separate rail test passed 136,083 assertions over all 5,040 configurable orders, both directions, cross-screen held inputs, timed repeats and touch navigation. The repeat clock is shared across the local/online screen boundary rather than reinitialized on entry.
- In the earlier console investigation, Radio 24's saved entry had empty artwork; its catalogue UUID returned a PNG URL that responded HTTP 200 (19,353 bytes).
- An earlier real SomaFM stream supplied station/song metadata while the production MP3 module decoded 5,015 ms at 44.1 kHz / 128 kbps. Host curl used verified TLS and paced mock audio; no sound was emitted or saved. This does not test the Vita audio port/network stack.
- Shared components were host-rendered and inspected for header placement and favorite/catalog separation, result rows and all eight sidebar entries when both live actions exist. This is layout evidence, not a Vita screenshot.
- Native build, VPK audit and console-copy results accompany the artifact handoff. On-device cover rendering, podcast playback/seeking, repeated pause/resume, tab reordering and concurrent radio/download/navigation remain acceptance checks.

References: [Radio Browser API](https://docs.radio-browser.info/), [Apple public search](https://developer.apple.com/library/archive/documentation/AudioVideo/Conceptual/iTuneSearchAPI/Searching.html), [Icecast ICY metadata](https://lists.xiph.org/pipermail/icecast-dev/2014-July/002347.html).
