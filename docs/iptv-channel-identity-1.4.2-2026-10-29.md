# IPTV channel identity — 1.4.2, 29 October 2026

Single channels imported from a link or a console file now receive source names
and icons when available. Their cards use **IPTV channel**, including a local
file containing one channel, rather than **M3U channel list**. Real collections
retain the list label. A single-entry file keeps its source reference and is
classified with saved channels; it does not become a duplicate collection.

Identity sources, in preference to generic host/file labels:

- M3U `tvg-name`, `tvg-id`, `tvg-logo`, a single entry's EXTINF metadata, and XSPF
  track images. Local image references resolve against the imported file.
- Explicit HLS channel header/session values and DASH ProgramInformation Title.
- Existing library metadata for the exact same channel URL.
- A uniquely identified MPEG-TS service name, read from the first already-fetched
  segment prefix; generic Service01-style names are ignored.
- The matched XMLTV channel's canonical display-name and icon, obtained by the
  existing background guide request. Programme titles never become channel names.

HLS session-data keys are application-defined: only explicit channel/service
name and channel-logo values are used. Arbitrary JSON session URIs, movie-title
metadata and website favicons are not treated as station identity. Sources that
supply no identity retain a readable channel fallback; names/logos are not
invented or matched by ambiguous URL fragments.

Optional saved-record fields track automatic/metadata/custom names and
single-channel descriptors without changing the saved JSON schema version.
Manual Rename marks the name as custom; metadata can fill its missing icon but
cannot overwrite that name. Existing non-generic names remain authoritative.
Legacy generic labels are recognized in all supported interface languages and
repaired when the source is opened. Reclassification retains the saved URL,
favourite state and confirmed quality. Failed metadata writes show a notice;
quality validation and rollback remain independent.

HTTP(S) and local ux0:/uma0: icons use the existing persistent raster cache.
Local reads are limited to 3 MiB and execute on the cover worker. Cached visible
images are restored before network work, including while another fetch is busy.
The cache retains its existing refresh, restart, size/count and last-good rules.
Remote metadata cannot reference console files. Guide metadata is published by
the ordinary player poll on the UI thread, never written by a stale guide job.

Regression coverage includes actual import/page/store code, cache-first image
publication, legacy repair, manual names, local images, all eight themes and
IT/EN. HLS/DASH discovery, XMLTV matching, asynchronous guide delivery, real
FFmpeg HLS selection/remux/decode and cache cold-restart tests remain required.
Host previews substitute firmware pixels. Build/copy checks do not certify
native appearance or playback of this binary on the physical Vita.

Format references: [HLS session data (RFC 8216)](https://www.rfc-editor.org/rfc/rfc8216.html#section-4.3.4.4),
[XMLTV channel names and icons](https://github.com/XMLTV/xmltv/blob/master/xmltv.dtd).
