# Feature roadmap and service feasibility

Service research reviewed 6 September 2026. The wider roadmap below remains proposals, not a release commitment. No Apple/Spotify account integration has been implemented. Current implementation and validation status are maintained in [Online audio](online-audio-preview.md).

## Integrated implementation

- Tap the app logo or press L1 to open the sections drawer. Controls is a standalone guide; the first-run guide is retained. Home Radio and Podcast Online contain favorites only; discovery is in L1 > Radio/Podcast.
- **Radio & podcasts** is a separate section, not a media-server entry. Left/right select radio or podcasts; Triangle searches by name, Start adds a URL, Square saves the selected station/feed, and Circle returns to saved entries. Up/down select an entry; Cross opens it.
- Radio uses Radio Browser discovery with fallback, bounded results and saved direct HTTP/HTTPS MP3 stations. Playback shares the music HUD and mini-player, so navigation and a background download can coexist with radio. Only one media player runs at a time. Pause disconnects; resume reconnects live. Connection retries are bounded; available ICY song/programme metadata is displayed.
- Public podcast discovery uses Apple's unauthenticated directory and regional charts; RSS feeds can also be entered manually. Cross opens a feed, then Cross on an episode streams it; Start downloads it. This is not Apple-account synchronization or subscription-content access. Failed feed opens retain the previous list instead of exposing an empty episode toolbar.
- Country/territory browsing and country-filtered station search are implemented, with 64-row catalog pages. Public podcast discovery shows publisher/category metadata and paginates up to 200 directory matches. The selected country also selects the podcast storefront; episode Back restores the parent result list.
- Limits: 64 saved entries, 64-item lazy pages with a 256-entry window, 8 MiB RSS and 2 MiB JSON response limits, RSS audio enclosures only, no DTD/entity-bearing feeds. Radio is direct MP3 only; AAC, HLS, M3U/PLS expansion and sleep timers remain follow-up work. Podcast played-state, feed refresh scheduling and automatic downloads are not implemented.
- Plain HTTP is supported for public feeds/stations and is unencrypted. HTTPS certificate verification and downgrade protection remain enabled. URLs with embedded credentials are rejected. Favorites are written via a temporary file and recoverable rename; corrupted existing data is not silently replaced.

Validation and device-test checklist: [Online audio preview](online-audio-preview.md).

## Recommended order

1. **Navigation discovery:** an always-visible, tappable Sections entry corresponding to L1, plus a first-run controls guide that can be reopened. Direct link and QR should be discoverable without knowing a controller shortcut.
2. **Internet radio:** saved stations, country/genre search, manually entered station URLs, now-playing metadata, reconnect, and a sleep timer. A useful first online-audio feature without requiring accounts.
3. **Podcasts:** RSS subscriptions, episode lists, played/unplayed state, resume, manual downloads, and a bounded auto-download policy. Add directory search after feed playback works.
4. **Playback queue and favorites:** queue next/reorder, favorite folders/stations, and a Continue watching/listening view using existing history. Keep queues separate from the current directory navigation.
5. **Transfer queue:** sequential downloads, retry, persisted jobs, free-space checks and optional bandwidth limits so downloads coexist better with playback. Persisted HTTP resumes need resource validation (ETag/Last-Modified/range support), not just a byte offset.
6. **Safer file management:** optional trash/restore, explicit sidecar selection, multi-select, and cross-storage moves. Current moves are same-storage renames; cross-storage moves need a cancellable copy, flush/verification, then source deletion. Never silently overwrite.
7. **Incremental catalog refresh:** refresh the affected directory after a mutation instead of rescanning the whole library; preserve focus and viewport across every dialog and provider. The new mutation rescan fixes stale paths but can still be expensive.
8. **Thumbnail scheduling and cache tools:** maintain foreground playback priority, add cache size/clear controls and stale-entry cleanup. Instrument latency before changing concurrency; more workers can increase contention on Vita storage.
9. **Podcast/audiobook controls:** sleep timer, bookmarks, and chapter navigation first. Variable-speed playback with pitch preservation is a separate CPU/audio-clock task requiring hardware profiling.
10. **Shared UI consistency:** unified scrollable drawers, reusable modal focus rules and shared layouts for long translated labels. Bring network grids and file grids under common density rules where appropriate; keep artwork out of text/control bounds.

These are project-specific priorities, not promised release scope. Do not implement all of them at once; validate each transport/player change with mini-player, pause, background transfer and lock/eco tests.

## Internet radio: technically feasible

Radio Browser supplies searchable station metadata and resolved station URLs. Use its recommended server discovery/failover, an identifiable User-Agent, bounded result pages, and cached favorites. Stations and availability are not guaranteed; codec/bitrate metadata is a hint, not proof a station will play. [Radio Browser API](https://api.radio-browser.info/), [API reference](https://docs.radio-browser.info/).

Code implication: the local MP3 path in `src/media/background_playback.c` currently reads and seeks a file descriptor. A live station is not a finite, seekable file. Add a live-audio transport with a bounded ring buffer, cancellation, timeouts/reconnect, and ICY metadata handling. Begin with tested MP3/AAC station formats; support playlist resolution (M3U/PLS), then separately assess HLS rather than promising every station immediately. Live mode must not display a fabricated duration or seek bar. Keep only one audio-output owner, including when the mini player is active.

## Podcasts and Apple Podcasts: public catalog/RSS is the practical route

Apple's documented iTunes Search API includes the podcast media/entity type. Its documentation is archived, so validate current responses and cache/rate-limit requests before relying on it for a public release. Use directory results to resolve the publisher's public RSS feed, then consume episode enclosures from the publisher. Apple documents enclosure URLs, MIME types, lengths and stable episode GUIDs. [Search API](https://developer.apple.com/library/archive/documentation/AudioVideo/Conceptual/iTuneSearchAPI/Searching.html), [RSS requirements](https://podcasters.apple.com/support/823-podcast-requirements).

Public-feed listening does not need an Apple account. This is a podcast client that can discover shows listed in Apple's catalog, not a recreation of the Apple Podcasts app. I did not find a documented, generally available listener API for this Vita app to sign in and synchronize an Apple Podcasts library or unlock Apple-billed subscription episodes. Do not present that as supported. Partner integrations may have different access. Private RSS subscriptions explicitly supplied by their publisher are a distinct future option, not a way to unlock an Apple subscription. [Apple's private RSS guidance](https://podcasters.apple.com/support/3993-subscribe-podcasts).

Parse feeds with response/entry limits, no external entity resolution, safe redirect handling, and secret-redacted logs. Persist episode identity by feed/GUID, not a mutable enclosure URL. Reuse the existing downloader for offline episodes, but do not assume streamed podcasts already work through the local audio implementation.

## Apple Music: authentication/catalog access is not Vita playback

Apple Music API supports catalog and authorized user-library operations. Developer tokens require a MusicKit identifier/signing setup; personal-library requests also use a Music User Token. Apple supplies playback through MusicKit for Apple platforms, Android and the web. No Vita playback SDK is documented. [MusicKit overview](https://developer.apple.com/musickit/), [Music User Token authentication](https://developer.apple.com/documentation/applemusicapi/user-authentication-for-musickit).

Engineering conclusion: a companion web app could potentially support authorization and permitted metadata/library operations, subject to Apple's requirements. It would not turn protected full tracks into ordinary HTTP audio for VitaMediaDeck. Native full-track playback on Vita is therefore not a practical feature using the documented public stack. Do not promise playback merely because login succeeds, and do not bundle Apple's signing private key in a VPK. A supported companion player is a different product architecture and should not be assumed to grant permission to relay protected audio.

## Spotify: login and remote control are plausible; native playback is not a Web API feature

For a native/public client, use authorization-code flow with PKCE; never package a client secret. The Web API can control an existing Spotify playback device. Spotify's Start/Resume endpoint requires the listening user to have Premium. This would make VitaMediaDeck a **remote**, with sound coming from the phone/PC/speaker, not the Vita. [PKCE flow](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow), [Playback control](https://developer.spotify.com/documentation/web-api/reference/start-a-users-playback).

The Web Playback SDK runs in supported modern browsers and requires encrypted-media capability; it is not a native C decoder SDK. Hardware streaming uses a separate approved eSDK/certification program, currently accepting organizations rather than individuals. Engineering conclusion: there is no ready-to-integrate official Vita playback path just by adding OAuth and Web API calls. [Web Playback SDK](https://developer.spotify.com/documentation/web-playback-sdk), [Commercial hardware program](https://developer.spotify.com/documentation/commercial-hardware).

Distribution is also a gate. Current quota-mode documentation requires Premium for the development-app owner and limits new development apps to five allowlisted users. Extended/public access is a separate approval question. Do not plan a broadly usable homebrew release around development credentials. The July 2026 update increased client IDs per developer to 25 and made development quota counting per developer account; the older one-client-ID claim is stale. Handle quota exhaustion separately from transient rate limits. [Quota modes](https://developer.spotify.com/documentation/web-api/concepts/quota-modes), [July update](https://developer.spotify.com/blog/2026-07-23-web-api-quota-updates).

## Login architecture, if a service is approved

Prefer a phone/PC browser opened from a QR shown by the Vita. A companion HTTPS callback and a short-lived pairing exchange could return the result to the console. This is an application-designed pairing flow, not a claim that either provider offers OAuth device-code authorization. Spotify still needs registered redirects, PKCE, state validation and explicit scopes; Apple requires its own supported authorization mechanism. Do not collect Apple/Spotify passwords in VitaMediaDeck, put tokens in QR URLs/logs, or ship signing secrets. Pairing must bind one authorization to one requesting console and expire after use. On a homebrew device, token storage is not equivalent to hardware-backed secure storage; provide revocation/logout and minimize retained credentials.

Recommendation: build radio and public RSS podcasts first. Evaluate Spotify remote control only after testing a registered developer app and its permitted release mode. Keep native Apple Music/Spotify streaming outside the committed roadmap unless official playback support and distribution approval are established.
