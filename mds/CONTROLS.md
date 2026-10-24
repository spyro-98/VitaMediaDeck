# VitaMediaDeck controls

## Shared navigation

| Input | Behavior |
| --- | --- |
| D-pad / left stick | Move focus or scroll the active list. |
| Cross | Open or confirm the selected item. |
| Circle | Close the active view or return. |
| L1 | Open or close Sections. Up/down selects and scrolls its contents. |
| R1 | Open the context or information sidebar where available. |
| Touch | Activate supported visible controls. |

With the mini-player visible in browsers, Start restores it and Select belongs
to its held input-lock gesture; brief Select does not reach the page. Use available
R1 actions or stop the mini-player before using those page shortcuts.

An open sidebar owns navigation before the underlying screen. Circle closes the
active panel before leaving its page. While resumable playback is running, the
sidebar adds **Active player** above Home; selecting it restores the full player.

## Theme-specific Home controls

Ghost Mesh keeps **right stick → section wheel** and **left stick / D-pad → content**
independent across local, radio and podcast screens. Returning to the first item
never transfers the left stick to the wheel. A held sector does not select again
on a screen change; close modal menus and return the right stick to neutral
before selecting a new sector.

| Theme | Section selection | Content / return |
| --- | --- | --- |
| Legacy | Up from first result row, then left/right on top tabs | Down enters; Up from first row returns |
| Ghost Mesh | Right stick selects wheel | Left stick/D-pad owns content at all times; X opens |
| Deck 90 | Up/down on left menu | Right/X enters; Left returns |
| Signal | Left/right on top tabs | Down/X enters; Up from first row returns |
| Aperture | Left/right on bottom tabs | Up/X enters; Down from last entry returns |
| Relay | Up/down on side menu | Right/X enters; Left from grid's left edge returns |
| Mosaic | Left/right on top tabs | Down/X enters; left/right covers, Down/Up pages; Up from first page returns |

These are D-pad directions; the left stick follows navigation. Reordering Home
positions changes which section occupies a slot, not these gestures. The
Home footer describes the currently focused menu/results. Player mappings
below remain independent from the Home theme.

## Radio, podcasts and IPTV

Home Radio and Podcast contain favourites; use **L1 → Radio/Podcast** for
discovery. **R1 → Options** provides relevant search, catalogue, URL, save,
information, refresh and download actions. L1 and R1 close the opposite panel.
Country/genre/station grids navigate with the d-pad or touch and page lazily.
Radio: X plays/pauses and reconnects live after pause. Podcast: X opens a show
or streams an episode, Start downloads an episode, Select/Square shows details.
Outside episodes, Triangle searches, Start adds a URL and **Square toggles the
selected favourite**, including Home. R1 explicitly offers Add to favourites or
Remove from favourites; removing the last item leaves an empty favourites view.
The action removes only the saved reference, preserving downloads and playback.
In provider discovery, Up from the first row focuses the Radio/Podcast tabs;
left/right switches provider and Down/X enters content. These two provider tabs
remain separate from the themed Home menu. Circle restores each previous list,
selection and scroll position, including country → shows → episodes.

Podcast episodes resume automatically from saved progress, including downloads
with app metadata in a custom folder. Positions below ten seconds and completed
episodes start at the beginning. Saving runs separately from audio/UI; pause and
suspend request a checkpoint. Closing the bubble forcibly can lose roughly the
last 15 seconds. See [resume implementation and console checks](../docs/podcast-resume-2026-10-24.md).

**L1 → IPTV** opens saved lists/channels. **Start / Add stream** opens the URL
form. Its R1 menu offers Enter link, Scan QR and Save. In saved lists/channels,
R1 starts with Open/Play, the explicit favourite toggle, Rename and Remove saved
entry; its other actions include All/Favourites, Search, Refresh and Add. In the
form, Triangle changes the detected format and X activates the focused control.
In a list, X opens, Square stars a channel, Select stars the current playlist;
Triangle searches lists/channels; R1 offers All/Favourites for saved entries.
Circle returns one level and restores the saved-list selection; a search does
not consume Back. The saved browser toolbar switches All/Favourites; inside a
playlist the same position controls availability filtering. Remove saved entry
requires confirmation and does not remove other channels or remote content.
During IPTV playback, **Square** or the HUD Favourite button
toggles the current channel's favourite state without restarting playback.
**Filter errors** (also in R1 Options) checks entry URLs without decoding media
and hides only DNS/403/404 failures. Circle cancels; **Show all** restores the
list without deleting anything. Other errors and unchecked channels stay visible.
Live pause resumes at the live point; finite segmented VOD resumes in place.
See [IPTV limits](../docs/iptv-plan.md) before choosing a source.

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
`uma0:`. Grid/list geometry follows the chosen theme and each saved view mode.

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
| D-pad / left stick | Pan with bounded displacement; neutral stops movement. |
| Triangle / Cross | Zoom in / out. |
| Square | Rotate 90 degrees: 0, 90, 180, 270. |
| L1 / R1 | Sections / image options. |
| Start | Reset fit, pan and rotation. |
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
| Cross on Background / L1 navigation | Continue browsing while the download runs. |

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
| Hold Select (900 ms) | Lock or unlock player input; a short press does not lock. |
| Touch video | Show or hide the player HUD. |
| Touch/drag timeline | Seek to a position. |

In the guide, the Player chapter shows the currently saved panel/seek mapping.
In the R1 playback panel, up/down selects a row, left/right prepares the audio
or subtitle choice, Cross applies it and Circle closes the panel.

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
| Hold Select (900 ms) | Lock or unlock player input; a short press does not lock. |
| Circle | Stop full-screen playback. |
| Start in mini-player | Restore the active local media fullscreen. |
| Tap mini-player title | Restore the full music or video player. |
| Tap mini-player video | Expand or collapse live video to one quarter of the screen width; audio-only playback ignores this action. |
| Mini-player controls | Seek, pause/resume, or stop the active local track. |

Audio-only media uses artwork when available; local video uses the same decoder
compatibility path as fullscreen playback. The ECO status and input-lock badge
use separate positions so the lock never covers the OLED ECO label.

## Jellyfin, Navidrome and Immich

Open the dedicated section with L1. **Jellyfin** appears immediately before
Navidrome in all themes and lists your existing Jellyfin servers. Its video
libraries use the existing video controls and restore the folder/selection after
playback. Navidrome and Immich retain their Music/Images Home shortcuts.
Cross connects, Square adds a server,
Triangle edits. **R1 → Options** exposes Connect, Add server, Edit connection
and Remove connection. Removing asks for a fresh X confirmation; Circle cancels.
Only the saved connection is removed. Circle returns;
Hold Select for 900 ms to lock/unlock mini-player input. Navidrome uses the existing
music-player controls; Immich uses the existing image-viewer controls.
See [setup and console checks](../docs/personal-media-2026-10-23.md).

Nel tema **Legacy**, L1 separa In corso (player/download disponibili), Contenuti
e servizi (Home, rete, Jellyfin, Navidrome, Immich, Radio/Podcast, IPTV) e Applicazione
(impostazioni, guida, informazioni). Le intestazioni non ricevono il fuoco.

In **tutti i temi**, su/giù o levetta sinistra seleziona le voci e fa scorrere
il menu L1. Puoi trascinare il contenuto oppure toccare le frecce in basso per
scorrere; rilasciare un trascinamento non apre una sezione. X apre la selezione,
Cerchio chiude. Titolo e comandi restano fermi, mentre la barra laterale indica
la posizione. Riduci movimento rende immediati gli spostamenti. La pagina
Sezioni/Player conserva i propri comandi sinistra/destra e la scelta della cover.
Navidrome è disponibile anche in Audio locale; Immich in Immagini; entrambi
sono nella Libreria di tutti i temi.

**Posizioni Home per tema:** Impostazioni → Interfaccia → Posizione tab del
tema. Su/giù sceglie la voce; sinistra/destra o le frecce touch la spostano;
X salva, Cerchio annulla. La posizione viene salvata solo per il tema attivo.
Nei nuovi L1 con più colonne, sinistra/destra cambia colonna quando non è
abilitata la pagina di disposizione del player; in quel caso mantiene il
passaggio fra Sezioni e Player. La Libreria elenca i servizi dopo i locali;
Musica e Immagini tengono il rispettivo servizio in testa.

## QR scanner and system keyboard

Open QR from Network Download Tools or IPTV R1 Options. Aim a complete HTTP(S)
QR at the rear camera; Circle or the touch Cancel button returns without changing
the pending link. Unsupported payloads stay in the scanner with a translated
status. All seven themes frame the same unobscured 444x444 preview.

URL, search, username and password prompts use themed app chrome behind the
native Vita IME. Key layout and native keyboard appearance belong to Vita OS.
Reduced motion preserves static focus while freezing decorative animation.

## Guide navigation and translated hints

L1 → Controls guide or Settings → Controls opens the same bilingual reference.
Up/down selects one of seven chapters; Right/Cross focuses text, up/down scrolls,
Left returns to the index and Circle exits. Home and Player chapters prepend
the actual theme/mapping. Guide footer labels follow the current focus; R1 is
not a guide-navigation action. Key badges consistently use X and O; Square and
Triangle names are localized. See [follow-up evidence](../docs/controls-guide-2026-10-23.md).
