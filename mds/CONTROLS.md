# VitaMediaDeck controls

## Shared navigation

| Input | Behavior |
| --- | --- |
| D-pad / left stick | Move focus or scroll the active list. |
| Cross | Open or confirm the selected item. |
| Circle | Close the active view or return. |
| L1 | Open or close the shared section sidebar. |
| R1 | Open the context or information sidebar where available. |
| Touch | Activate supported visible controls. |

An open sidebar owns navigation before the underlying screen. Circle closes the
active panel before leaving its page. While resumable playback is running, the
sidebar adds **Active player** above Home; selecting it restores the full player.

## Local Media

The combined Library opens as a folder grid instead of flattening every indexed
file. It groups `video`, `movies`, `music`, `picture`, `photo`, and `download`
separately on `ux0:` and `uma0:`; Cross opens the selected folder without losing
its hierarchy. The R1 panel switches grid/list layout, filters memory-card versus
external storage, sorts folders by name or aggregate size, and opens the device
root. Image covers are decoded on the bounded thumbnail worker and cached as
fixed-size previews, so scrolling never synchronously loads viewer-sized images.

| Input | Behavior |
| --- | --- |
| Left/Right | Change Library, Video, Audio, or Images filter. |
| Up/Down | Move through visible items. |
| Cross | Open the selected media folder. |
| Circle | Return. |

The R1 panel also opens **Browse folders**, a direct browser for `ux0:` and
`uma0:`. It defaults to a four-column grid inspired by Finder's icon view.

| Input | Local folder browser behavior |
| --- | --- |
| D-pad / left stick | Move between folders and files. |
| Cross | Open a folder, play compatible video/audio, or view an image. |
| R1 | Switch the shared filesystem view between grid and list. |
| Circle | Move to the parent folder; return to Library at the device root. |

### Image viewer

| Input | Behavior |
| --- | --- |
| One-finger drag | Pan the image. |
| Two-finger pinch | Zoom around the gesture midpoint. |
| Two-finger twist | Rotate continuously. |
| D-pad / left stick | Pan in the inverted viewport direction. |
| Right stick horizontally | Rotate continuously. |
| Triangle / Square | Zoom in / out. |
| L1 / R1 | Zoom out / in continuously. |
| Cross | Reset fit, pan, and rotation. |
| Circle | Close the image. |

## Network Sources

### Saved sources and Download Tools

| Input | Behavior |
| --- | --- |
| Square | Add a source. |
| Triangle | Edit the selected saved source. |
| Select | Remove the selected saved source definition. |
| Start | Open Direct URL in the separate Download Tools panel. |
| R | Open Scan QR in the separate Download Tools panel. |
| Cross | Browse the selected saved server; request a password if needed. |
| Circle | Return. |

Jellyfin sources accept an HTTP or HTTPS host, optional base path, username,
and password. A bare host on port 8096 uses HTTP; `http://` and `https://`
select the transport explicitly. The app signs in when the source opens, keeps
the returned access token only in memory, browses video libraries, and prefers
the server Primary image for each grid cell. Playback requests the original
compatible file with seekable byte ranges; server transcoding is not used.

The password can be entered in the add/edit form and is retained while the app
is running. The System setting can optionally remember it in plaintext at
`ux0:data/VitaMediaDeck/network/passwords.txt`; this is disabled by default and the
file is removed when the option is disabled. The first SFTP connection presents
the server SHA-256 fingerprint on a read-only confirmation page.

### Remote browser

Jellyfin video cells prefer server artwork. Other remote cells progressively
show an embedded cover or a representative frame. Missing or blank artwork
falls through to frame extraction; the selected
cell can preempt obsolete viewport work. Preview decoding and network reads stay
on the bounded thumbnail worker.

After remote playback closes, the browser restores the exact folder and focused
item. A stored folder stack is used instead of deriving parents from path text,
so this also works with opaque Jellyfin item identifiers.

| Input | Behavior |
| --- | --- |
| Up/Down | Move through folders and files. |
| Left/Right | Move between items while grid view is active. |
| Cross | Open a folder or play a compatible video. |
| Triangle | Open Jellyfin video metadata, or download a selected WebDAV, SFTP, or SMB file. |
| R1 | Switch the shared filesystem view between grid and list. |
| Circle | Move to the parent folder; leave at the root. |

### Download destination and transfer

| Input | Behavior |
| --- | --- |
| Cross | Open the selected destination folder. |
| Start | Use the current destination folder. |
| Triangle | Create a folder in the current destination. |
| Circle | Go up one folder; cancel at `ux0:`. The initial folder is `ux0:download`. |
| Start during transfer | Pause or resume. |
| Circle during transfer | Abort and remove the partial file. |

## Video player

| Input | Behavior |
| --- | --- |
| Cross | Pause or resume. |
| Circle | Stop playback and return. |
| D-pad Left/Right | Seek backward or forward. |
| Left stick horizontally | Seek backward or forward, with controlled repeat while held. |
| L1 | Open the shared section sidebar. |
| R1 | Open playback options and decoder information. |
| Right stick vertically | Change volume. |
| Short Start (release before 900 ms) | Minimize supported local video into the shared mini-player. |
| Hold Start (900 ms) | Toggle OLED ECO mode while playback and audio continue. |
| Select | Immediately lock or unlock player input. |
| Touch video | Show or hide the player HUD. |
| Touch/drag timeline | Seek to a position. |

The R1 playback panel also selects the active AAC audio track and text subtitle
track. Jellyfin external SRT/ASS/SSA/WebVTT sidecars are fetched through the
authenticated provider API and appear after the embedded tracks. Left/Right
changes the pending choice and Cross applies it; the subtitle selector includes
an explicit Off choice. Track changes retain the current playback position and
apply equally to local and authenticated remote videos. Subtitle activation
never opens a full-screen loading scene: the first
cursor open and all later seeks run as serial requests on one persistent worker,
while the R panel remains responsive and shows **Changing...** or a retryable
failure. The old cue disappears immediately, and read-ahead stops at a small cue
limit or short future-time horizon. A request that does not complete within five
seconds becomes a retryable failure instead of trapping the UI in
**Changing...**; selecting the same failed or pending track and pressing Cross
restarts that request. Circle and the L1 navigation remain available throughout
the operation. Video seeks
release playback after the first decoded preroll frame instead of waiting for a
multi-frame startup cushion.

Each local or remote video remembers its last useful playback position. If the
player opens from that position, the R1 panel adds **Start from beginning** as a
fifth row. Activating it seeks to `00:00`, removes the saved resume point
immediately, and closes the panel. While a recovered video is still opening,
Cross on **Play from beginning** cancels that attempt and performs a clean open
at `00:00`, so the action does not depend on the R1 panel becoming available.

Video mini-player hand-off retains the playback position and selected
audio/subtitle tracks when fullscreen is restored. It uses the same HW/SW
decoder stack as fullscreen playback and renders the live video surface. Tap
the video surface to expand/collapse it at one quarter of the screen width;
tap the title or press Start to restore fullscreen. Selecting the same active
video in the grid also restores it instead of restarting it. The same hand-off
is available to authenticated remote video by reopening its seekable source at
the saved position and track selection; navigating to another section minimizes
rather than closes that player.

The Settings > Subtitles tab controls the font, foreground and background
colors, small/medium/large text size, 60/75/88/96 percent maximum width,
one-to-four minimum and maximum lines, and four vertical positions. Its live
preview renders Western, Cyrillic, Japanese, Chinese, and Korean samples. Inter
is rasterized from independent exact-size instances; Japanese, Chinese, and
Korean glyphs come from the matching native PS Vita system PGFs.

The player displays an `HW DEC` or `SW DEC` badge, resolution, frame rate, and
stream-reported video bitrate in both the HUD and the right information panel.
The Playback settings tab selects Auto, HW H.264, or SW FFmpeg for newly opened
videos. Auto is the default and permits hardware-to-software fallback.
The Controls settings tab can explicitly swap L1/R1 with D-pad Left/Right. That
compatibility mapping makes the shoulders seek and the D-pad open the panels;
it is disabled by default.

## Music player and mini-player

| Input | Behavior |
| --- | --- |
| Cross / touch play | Pause or resume. |
| Left/Right / touch timeline | Seek. |
| Left stick horizontally | Seek backward or forward, with controlled repeat while held. |
| L1 | Open the shared section sidebar. |
| R1 | Open shuffle and repeat options. |
| Right stick vertically | Change volume. |
| Short Start | Minimize into the shared mini-player. |
| Hold Start (900 ms) | Toggle OLED ECO mode while audio continues. |
| Select | Immediately lock or unlock player input. |
| Circle | Stop full-screen playback. |
| Start in mini-player | Restore the active local media fullscreen. |
| Tap mini-player title | Restore the full music or video player. |
| Tap mini-player video | Expand or collapse live video to one quarter of the screen width; audio-only playback ignores this action. |
| Mini-player controls | Seek, pause/resume, or stop the active local track. |

Audio-only media uses artwork when available; local video uses the same decoder
compatibility path as fullscreen playback. The ECO status and input-lock badge
use separate positions so the lock never covers the OLED ECO label.
