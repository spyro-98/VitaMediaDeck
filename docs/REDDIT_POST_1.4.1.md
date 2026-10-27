# Title

[Update] VitaMediaDeck 1.4.1 — new native Vita UI, faster playback and seeking, eight themes, Navidrome, Immich, and IPTV improvements

# Body — Reddit Markdown

Hi everyone!

Sorry for the long gap since the last update. I’ve been bringing together a substantial collection of improvements to make VitaMediaDeck feel more complete, instead of publishing lots of small updates. The interface redesign also took longer than expected: I kept revisiting it because I wasn’t satisfied with the result.

There’s quite a bit to cover since the [1.2 release post](https://www.reddit.com/r/vitahacks/comments/1w534wb/release_vitamediadeck_12_faster_libraries_and/), especially around playback performance, navigation, and the new interface.

**Faster playback, faster seeking, and a more responsive app**

The player has received some of the most important improvements. Video loading, playback controls, seeking, and recovery after seeking feel noticeably quicker in my Vita tests. Audio playback controls and navigation have also received attention.

Changes include:

- Faster indexed seeking and better recovery of picture and sound at the new position.
- Improved audio/video synchronization after startup, resume, and seeking.
- Fixes for MP4 files with missing duration information or unavailable seeking.
- Fixed subtitles disappearing after a seek, preserving the selected subtitle track.
- Better handling of audio-track changes and recovery when a replacement track fails.
- Improved loading and buffering feedback, including retained frame previews during seeking.
- Fixes for flickering timing/volume text, delayed HUD labels, touch controls, and OLED ECO mode.
- Better coordination between full-screen playback, background playback, and the mini-player.

Browsing and menu navigation have also become smoother, with more consistent transitions and fewer repeated loading interruptions.

**Hardware decoding remains the recommended choice**

The default **Auto** mode tries the hardware decoder first, with software decoding available as a fallback.

For compatible video, **HW is by far my recommended first choice**. It uses the Vita’s dedicated video hardware and gives the best results in my testing, while leaving more CPU headroom for the rest of the app.

**The codec and player optimizations now let us keep the CPU at 333 MHz for tested HW playback, including 720p30, while maintaining smooth playback and responsive controls.** A 444 MHz CPU target is not necessary for those tested hardware-decoded files. This refers to the CPU clock; the dedicated video engine does the H.264 decoding.

The work includes more efficient frame handling, fewer unnecessary allocations and memory copies, better queue scheduling, and quicker startup and seek recovery. These are improvements throughout the playback pipeline, alongside the faster interface.

The app now supports automatic CPU targets based on the active decoder:

- **HW: 333 MHz**
- **SW: 444 MHz**

You can configure these independently. Manual PSVshell settings retain priority, and explicit PSVshell mode leaves frequency control external.

**VMD is the new default theme**

The new **VMD** theme uses actual Sony PAF interface components, including native text, buttons, toggles, and loading indicators.

It brings a more Vita-like presentation across the app: Home, browsers, settings, menus, player controls, dialogs, and less frequently used screens such as the QR scanner. It also introduces a darker animated background with thin cyan light trails and a subtle moving controller-symbol pattern.

There are now **eight themes**:

- **VMD** — the new default, using native PAF components.
- **Legacy** — the original interface.
- **Ghost Mesh**
- **Deck 90**
- **Signal**
- **Aperture**
- **Relay**
- **Mosaic**

The alternative themes have their own layouts, navigation styles, lighting, depth effects, and animations. Folder/page transitions and focus movement have been refined, including fixes for repeated entrance animations that could look like flickering during rapid navigation.

Tab order can be saved separately for each theme.

**An integrated Home and global search**

Home now brings together unfinished media, saved connections, favourites, and recently played items in one dashboard.

The dedicated video, music, image, and other category tabs remain available. The old aggregate Home has been retired.

Global search brings results together across indexed local media and configured providers, grouped by source. It searches available titles, filenames, artists, albums, podcasts, and channels.

Search runs in the background, uses cached metadata, and displays results progressively so a slow server doesn’t hold up everything else. It doesn’t reopen every media file to extract tags whenever you type a query.

**Navidrome and Immich**

Two new personal-media integrations are included:

- **Navidrome:** connect to your music server, browse artists, albums, playlists, and tracks, and listen through the existing music player.
- **Immich:** connect to your photo library, browse photos, albums, and favourites, and open images with zoom, pan, and rotation.

Both have dedicated sections and connection-management controls across the themes. Jellyfin also has its own clearly identified provider entry in the L1 menu.

Existing local, Jellyfin, WebDAV, SFTP, and SMB access remains available.

**Radio, podcasts, and IPTV**

Radio and podcast browsing now makes better use of cached content and background refreshes, with cached artwork and previews wherever available.

Other improvements include:

- Adding and removing favourites, with inline stars instead of repeated confirmation screens.
- Favourite countries for quicker access to regional radio and podcast catalogues.
- Clearer provider navigation, improved Back behaviour, and touch scrolling.
- More reliable podcast resume after closing or suspending the app, using lightweight background checkpoints.
- Improved IPTV loading feedback and HW/SW selection through the player’s R1 menu.

The latest IPTV fixes address HLS timestamps and hardware-decoder initialization/resource cleanup. **IPTV hardware playback is now working very well in my physical Vita tests.**

**Updates to vita-hw-decoder and vita-sw-decoder**

Both decoding paths have received performance and reliability work:

- **HW:** direct NV12 frame presentation, tighter allocation/lifetime handling, and corrections to initialization and resource cleanup.
- **SW:** more efficient YUV copying and padding, fewer redundant memory writes, and improved frame-queue/surface handling.
- **Both:** event-based waits, improved worker coordination, and cleaner cancellation and shutdown.
- **Playback recovery:** more reliable duration handling, faster indexed seek/preroll, and improved startup and audio-track recovery.
- **Diagnostics:** clearer timing information for opening, decoding, seeking, and first-frame delivery.

**Software decoding has also benefited from these optimizations**, with less overhead in copying and presenting decoded frames and more responsive playback/seek handling. Earlier console tests demonstrated approximately real-time SW playback with 960×544/24 fps material. Demanding 720p30 content can still exceed the SW budget, particularly at 333 MHz, so HW remains the preferred backend.

The latest `h264_vita` lifecycle correction is applied through **VitaMediaDeck’s FFmpeg build**. The standalone decoder repositories document that distinction.

**Further compilation and linking optimizations**

There has also been work on how the release is built:

- **Completed Link Time Optimization (LTO)** across the main app, both decoder libraries, HTTPS, and IPTV modules, alongside the existing FFmpeg C LTO.
- **Cortex-A9/NEON-targeted Release compilation**, retaining the existing `-O3` and hard-float configuration.
- **Compiler-matched GCC archive tools**, so LTO information is preserved through the static-library build and final link.
- **Unused-section removal** during linking, keeping code that is not needed out of the final executable.

Together, these settings let the compiler optimize across module boundaries and the linker discard unused code. They complement the decoder, buffering, and UI changes; I’m not claiming a separate measured FPS or battery gain from build flags alone.

**Other fixes**

There are also fixes for missing thumbnails after returning from playback, clipped selection borders, text truncation, search-result selection, tab focus skipping items, menu spacing, and image-viewer controls. Image panning is bounded again, with rotation in predictable 90-degree steps.

**VitaMediaDeck Transcoder**

For preparing compatible videos, there’s also [VitaMediaDeck Transcoder](https://github.com/spyro-98/VitaMediaDeck-Transcoder), the companion tool for macOS, Windows, and Linux.

Vita-targeted files prepared with it can play smoothly with the software decoder too, depending on the content and settings. **HW remains the recommended first choice.**

**Repositories**

- [VitaMediaDeck](https://github.com/spyro-98/VitaMediaDeck)
- [vita-hw-decoder](https://github.com/spyro-98/vita-hw-decoder)
- [vita-sw-decoder](https://github.com/spyro-98/vita-sw-decoder)
- [VitaMediaDeck Transcoder](https://github.com/spyro-98/VitaMediaDeck-Transcoder)

Thanks for your patience and for all the testing and feedback. VitaMediaDeck is still a public beta, and reports from different Vita models, media libraries, and servers are very welcome!
