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

## Hardware player module

The module currently belongs to the GPL-3.0-only VitaMediaDeck tree and can be
published under the same license. Before a standalone release:

1. move all required support sources under the module directory;
2. provide a standalone CMake target and install rules;
3. list exact FFmpeg/wiliwili/VitaSDK dependencies;
4. build the example independently;
5. document the independent-cursor, cancellation, and render-completion
   contracts;
6. validate local file plus at least one external custom stream factory;
7. include all applicable license texts and source-offer information.

## Security release gates

- No saved passwords or credentials in VPK resources, logs, fixtures, or Git
  history for the release commit.
- Jellyfin and WebDAV reject plain HTTP and invalid TLS peers.
- Jellyfin access tokens are session-only and never written to the source or
  optional password databases.
- SFTP rejects unknown or changed host keys until explicit confirmation.
- SMB does not fall back to guest access.
- Remote operations are read-only.
- Network failures cannot strand decoder threads or keep secrets alive after a
  session closes.

## Trademark statement

VitaMediaDeck is an independent homebrew project. PlayStation, PS Vita, Sony, and
related marks belong to their respective owners. No affiliation or endorsement
is claimed.
