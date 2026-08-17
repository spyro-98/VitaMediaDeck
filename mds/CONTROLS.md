# VitaTube controls

## Shared navigation

| Input | Behavior |
| --- | --- |
| D-pad / left stick | Move focus or scroll the active list. |
| Cross | Open or confirm the selected item. |
| Circle | Close the active view or return. |
| L1 | Open or close the shared section sidebar. |
| Touch | Activate supported visible controls. |

An open sidebar owns navigation before the underlying screen. Circle closes the
active panel before leaving its page.

## Local Media

| Input | Behavior |
| --- | --- |
| Left/Right | Change All, Video, or Music filter. |
| Up/Down | Move through visible items. |
| Cross | Play the selected file. |
| Square | Open file actions. |
| Circle | Return. |

File actions currently include rename and delete. Deleting local media also
removes recognized artwork/metadata/subtitle sidecars with the same basename.

## Network Sources

### Saved sources

| Input | Behavior |
| --- | --- |
| Square | Add a source. |
| Triangle | Edit the selected source. |
| Select | Remove the selected source definition. |
| Cross | Enter credentials and browse the selected server. |
| Circle | Return. |

The password prompt appears every time a server is opened; passwords are never
saved. The first SFTP connection also presents the server SHA-256 fingerprint
for explicit confirmation.

### Remote browser

| Input | Behavior |
| --- | --- |
| Up/Down | Move through folders and compatible videos. |
| Cross | Open a folder or play a video. |
| Circle | Move to the parent folder; leave at the root. |

## Video player

| Input | Behavior |
| --- | --- |
| Cross | Pause or resume. |
| Circle | Stop playback and return. |
| D-pad Left/Right | Seek backward or forward. |
| Left stick horizontally | Continuous seek. |
| Right stick vertically | Change volume. |
| Touch video | Show or hide the player HUD. |
| Touch/drag timeline | Seek to a position. |

The player displays an `HW DEC` or `SW DEC` badge for the active H.264 decoder.

## Music player and mini-player

| Input | Behavior |
| --- | --- |
| Cross / touch play | Pause or resume. |
| Left/Right / touch timeline | Seek. |
| Right stick vertically | Change volume. |
| Circle | Leave full-screen playback while preserving the supported background session. |
| Mini-player controls | Seek, pause/resume, restore, or stop the active local track. |

The exact mini-player behavior depends on the local audio format and whether a
decoded video surface is available.
