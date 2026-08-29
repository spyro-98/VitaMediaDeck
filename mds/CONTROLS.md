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

The combined Library opens as a rich thumbnail grid. The R1 View panel can
temporarily switch it to a list; Video and Music remember their own view.
Video cells prefer recognized local artwork, then an embedded cover, and finally
an asynchronously extracted representative frame. Blank embedded artwork is
ignored, and the selected cell is generated ahead of surrounding previews.

| Input | Behavior |
| --- | --- |
| Left/Right | Change All, Video, or Music filter. |
| Up/Down | Move through visible items. |
| Cross | Play the selected file. |
| Square | Open file actions. |
| Circle | Return. |

File actions currently include rename and delete. Deleting local media also
removes recognized artwork/metadata/subtitle sidecars with the same basename.

The R1 panel also opens **Browse folders**, a direct browser for `ux0:` and
`uma0:`. It defaults to a four-column grid inspired by Finder's icon view.

| Input | Local folder browser behavior |
| --- | --- |
| D-pad / left stick | Move between folders and files. |
| Cross | Open a folder or play a compatible media file. |
| R1 | Switch the shared filesystem view between grid and list. |
| Circle | Move to the parent folder; return to Library at the device root. |

## Network Sources

### Saved sources

| Input | Behavior |
| --- | --- |
| Square | Add a source. |
| Triangle | Edit the selected source. |
| Select | Remove the selected source definition. |
| Cross | Browse the selected server; request a password if none is available. |
| Circle | Return. |

The password can be entered in the add/edit form and is retained while the app
is running. The System setting can optionally remember it in plaintext at
`ux0:data/VitaMediaDeck/network/passwords.txt`; this is disabled by default and the
file is removed when the option is disabled. The first SFTP connection presents
the server SHA-256 fingerprint on a read-only confirmation page.

### Remote browser

Remote video cells progressively show an embedded cover or a representative
frame. Blank embedded artwork falls through to frame extraction; the selected
cell can preempt obsolete viewport work. Preview decoding and network reads stay
on the bounded thumbnail worker.

| Input | Behavior |
| --- | --- |
| Up/Down | Move through folders and files. |
| Left/Right | Move between items while grid view is active. |
| Cross | Open a folder or play a compatible video. |
| R1 | Switch the shared filesystem view between grid and list. |
| Circle | Move to the parent folder; leave at the root. |

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

The R1 playback panel also selects the active AAC audio track and embedded text
subtitle track. Left/Right changes the pending choice and Cross applies it; the
subtitle selector includes an explicit Off choice. Track changes retain the
current playback position and apply equally to local and authenticated remote
videos. Subtitle activation never opens a full-screen loading scene: the first
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
