# Release and licensing plan

## Repository license

VitaMediaDeck is distributed under GPL-3.0-only. Source changes to the covered
program must remain available under that license when binaries are distributed.
Third-party components are not relicensed; their notices and source obligations
remain in force.

## Release contents

A public release should contain:

- source for the exact tagged revision;
- reproducible dependency build scripts and pinned revisions;
- `VitaMediaDeck.vpk` and SHA-256 checksum;
- GPLv3, FFmpeg LGPL, ReAvPlayer MIT, libssh2 BSD, Inter OFL, and consolidated
  third-party notices inside the package;
- a hardware/server validation summary;
- known limitations and configuration instructions.

Generated dependency trees, credentials, source databases, host fingerprints,
media indexes, logs, and private research worktrees must not be committed.

## Independent decoder packages

`vita-hw-decoder` and `vita-sw-decoder` are independent GPL-3.0-only repositories
with 2.0.0 ABI, CMake targets, install rules, examples, dependency locks and
source-packaging/audit tools. The app pins exact commits in its dependency lock.
Their runtime/performance guides distinguish source contracts, host fixtures,
Vita builds and physical acceptance. The app shares patched FFmpeg n6.0 between
them; the SW standalone 8.1.2 builder is a separate distribution configuration.
Preserve matching source/license obligations for the actual linked archives.
Documentation-only decoder commits do not change ABI or playback code.

## Security release gates

- No saved passwords or credentials in VPK resources, logs, fixtures, or Git
  history for the release commit.
- WebDAV rejects plain HTTP. Jellyfin permits explicit LAN HTTP while clearly
  warning that it is unencrypted; HTTPS still rejects invalid TLS peers.
- Jellyfin access tokens are session-only and never written to the source or
  optional password databases.
- SFTP rejects unknown or changed host keys until explicit confirmation.
- SMB does not fall back to guest access.
- Remote browsing is read-only; explicit downloads write only to the chosen
  local destination. Do not claim every provider API is read-only: Jellyfin
  playback/favourite interactions may update user state.
- Network failures cannot strand decoder threads or keep secrets alive after a
  session closes.

## Trademark statement

VitaMediaDeck is an independent homebrew project. PlayStation, PS Vita, Sony, and
related marks belong to their respective owners. No affiliation or endorsement
is claimed.
