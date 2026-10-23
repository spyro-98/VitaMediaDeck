# Navigation and online audio preview

App version 1.3.0. This work originated on `feature/cyberpunk-ui-study` and is included in the local master integration. GitHub publication and hardware acceptance are separate steps.

## Navigation

- The header displays **VITA // MEDIA DECK**. Press L1 or touch the logo (no L1 badge beneath it). Regular sections: Home, Network, Radio/Podcast, Settings, Controls, About; Player and Download appear while active.
- Shared Home tabs: Library, Local Video, Local Audio, Radio, Podcast, Images. Podcast has Online favourites and Offline folder navigation in two columns. Existing seven-tab order files migrate without resetting the remaining order. Touch/swipe or focus tabs with Up, change with Left/Right and enter results with Down. Reorder under Settings > Interface.
- `ux0:podcast`, `ux0:podcasts` and equivalent `uma0:` roots are indexed separately from music. The index is rebuilt for this partition; no podcast folder is created.
- Controls is a standalone scrollable guide, without a fake Settings tab.

## Online audio

- Home Radio and the Podcast Online column contain favorites only, with no search/discovery toolbar. Use L1 > Radio/Podcast for discovery. Its landing browser offers popular MP3 stations, radio genres, countries, saved entries and Apple's top 100 podcast shows by country. Show URLs are resolved to public RSS feeds before opening or saving. Countries/regions use three columns with bundled flags; genres use two columns. All result cards are taller, with consistent touch/d-pad navigation and lazy paging.
- R1 opens context-sensitive Options (search, catalogue, adding URLs, saving,
  details, refresh, or episode download as appropriate). L1 switches directly
  to Sections and closes Options; R1 does the reverse. Triangle still searches;
  Start adds a public stream/feed URL; Square saves a favorite; Select shows
  information. Country/genre/show/station cards have consistent grid navigation;
  episodes retain wide titles. Discovery remains outside Home.
- Radio searches combine spaced/compact names with tags and state metadata. CamelCase and spaces normalize to equivalent queries. Podcast discovery uses normalized Apple directory queries, including publisher/topic matches.
- Scroll via D-pad or touch for asynchronous pagination. A four-page window retains at most 256 results and reloads earlier pages in reverse navigation. Filters are retained. Apple returns at most 200 matches per variant. RSS responses grow on demand up to 8 MiB and are parsed one channel child at a time, without retaining a complete DOM; JSON/artwork responses remain capped at 2 MiB. Episodes page automatically.
- Cross plays radio. Only play/pause transport buttons are shown. Pause disconnects; Play reconnects live without queued audio. Radio supports MP3, not AAC/HLS or time shifting.
- Cross opens a podcast and streams its selected episode. Start downloads to the chosen folder. Square shows episode authors/description. Circle restores the parent results and selection.
- Failed, cancelled or empty feed opens retain the previous list and selection; Home does not switch to an empty Download/Info episode view. Failed episode playback returns Home to saved podcasts. Feed/HTTP/playback diagnostics are saved on failure when logging is enabled, without logging URLs or response bodies. The saved Nova Lectio feed reproduced the old 2 MiB limit failure (2,484,364 bytes, 283 episodes); the larger bounded reader passes this feed and La Zanzara (47,087 bytes, 30 episodes) on the host. Both tested episode URLs returned verified-range-compatible HTTP 206 responses. Console playback still needs a retest.
- MP3 podcasts use mpg123 with a seekable HTTP cursor. AAC/M4A uses AvPlayer custom file callbacks over the same verified HTTP/TLS transport. A dedicated worker prefetches four 512 KiB compressed blocks, plus one bounded incoming block, so range refills no longer run on the audio-output thread. Seeking changes the requested window; obsolete in-flight data cannot overwrite the new position. Public URLs and HTTP byte-range support are required. Unsupported enclosures can still be downloaded for compatible local playback. Native playback needs Vita validation.
- Online episodes use RSS-feed + GUID resume identities (enclosure URL fallback); offline podcast files use their paths. Progress checkpoints every 15 seconds and on stop, including mini-player playback. Resume uses the same minimum/end guards as movie history. The radio player's explanatory pause/reconnect sentence has been removed, without changing live-resume behaviour.

## Artwork and metadata

- Radio artwork uses catalogue favicons. Older saved stations with a UUID but no artwork are refreshed from the public catalogue; re-saving also refreshes metadata without duplicating the favorite. Podcast artwork uses Apple/RSS, with episode images falling back to the feed cover. Authors, descriptions and tags are retained where supplied, including favorites.
- Visible covers load on a worker into an independently keyed 192×192 artwork
  thumbnail, containing the complete original rather than a 16:9 video crop.
  JPEG, PNG, WebP, GIF and BMP are accepted. Radio, podcast shows/episodes and supplied IPTV logos share a persistent SHA-256-keyed cache in `ux0:data/VitaMediaDeck/artwork`: 128 entries / 64 MiB maximum, 2 MiB per response, oldest-access eviction, signature checks and atomic publication. URL query tokens are not written into cache filenames. Logos must be supplied by the catalogue/feed/M3U; missing artwork is not fabricated.
- Country flags load one PNG at a time from `app0:flags.pack`. Source PNGs remain
  in the repository; installation no longer creates hundreds of tiny flag files.
  Compact audio mini-player artwork is square, aspect-preserving and decoded
  through the bounded common image loader (not unrestricted PNG/JPEG loading).
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
## Downloaded podcast metadata and covers

Podcast episode rows show the publication date (`YYYY-MM-DD`) and duration
(`M:SS` or `H:MM:SS`) in a reserved, right-aligned column. Missing values use
an em dash. Dates retain the publisher's calendar day rather than shifting it
through the console timezone. The publication date is also retained in new
downloaded-episode metadata.

New episode downloads retain the RSS episode title, author, show title,
description and duration (when supplied) alongside the completed audio file.
Episode artwork falls back to the channel cover when the feed supplies no
episode image. The background job fetches/copies it into a permanent hidden
sibling after the audio transfer completes, independent of the evictable artwork
cache. Ordinary local browsing and the offline music player read these details,
including the cover-based player colour theme, without a network request.

Files are named `.<final-audio-filename>.podcast.json` and
`.<final-audio-filename>.podcast.<image-extension>`. The association uses the
actual saved filename after Content-Disposition and duplicate-name handling;
no signed download URLs or credentials are saved in this metadata. Keep these
hidden companions when manually copying episodes. In-app media rename/move/delete
handles these exact companions; it does not delete a shared folder cover.

If the feed has no cover, text metadata is still saved. If the artwork request or
metadata write fails, the completed audio is retained and completion reports a
warning. Covers retain the shared cache's 2 MiB JPEG/PNG/WebP/GIF/BMP limit.
Earlier audio-only downloads are not automatically backfilled: download the
episode again to acquire its metadata and cover.
