# QR links and IPTV failure handling — 27 October 2026

This app-side follow-up remains in the standard 1.4.1 build. The hardware/software
decoder libraries, their ABI, decoder clocks and the tested HLS timestamp fix are
unchanged. There is no separate experimental VPK.

## Console evidence and scope

The mounted console's installed executable matches the preceding standard VPK,
SHA-256 `c8c6b6363b4261399d2569d319da4a9cbfe3a26aaa9ebbce28447e90cf54d1a4`.
The user confirms that their QR is recognized and the failure occurs when opening
the public IPTV-org list. The session records list loads returning `-1` with zero
received bytes. Separate optical QR attempts also record recoverable ECC failures;
those are not evidence that the recognized link's HTTP request failed.

The saved public list at
[IPTV-org Italy](https://iptv-org.github.io/iptv/countries/it.m3u) currently returns
HTTP 200, `audio/x-mpegurl`, 76,139 bytes and 334 entries. The response fixture stays
outside the repository. This establishes that the public list is available; it
does not reproduce the Vita-only rejection or prove the new build fixes that
exact console attempt. The old log lacks the recognized URL/validation outcome.
No private URLs, credentials or raw console logs are published.

## Corrections

- QR and typed links share one bounded validator. It accepts outer ASCII
  whitespace, a UTF-8 BOM, case-insensitive HTTP(S) schemes and one optional
  terminal NUL. Client-only fragments are removed; signed paths and query bytes
  are preserved. Embedded NUL, internal whitespace, userinfo and unsupported
  protocols remain rejected. Fetched references keep strict HTTPS downgrade
  rejection. HTTP requests use the validated canonical URL.
- General loading operations and the QR decoder use `pthread_create`/
  `pthread_join`, matching the player loader. This gives network/XML callbacks the Vita pthread OSAL context;
  completion uses release/acquire atomics. Worker creation failure has a distinct
  result and never falls back to running network work on the UI thread. The
  earlier [thread-contract investigation](startup-search-threads-2026-10-26.md)
  explains why a host kernel-thread shim cannot prove Vita POSIX compatibility.
- Validated playlist User-Agent values are supplied as an explicit HTTP header.
  This preserves up to 255 bytes despite the shared HTTP client's 128-byte default
  agent field. Arbitrary playlist headers, cookies and authentication directives
  are still not imported.
- Failures distinguish malformed playlist/data, H.264 video availability,
  AAC availability, video dimensions, audio format, HTTP 429, HTTP 5xx and byte-range
  failures. Existing DNS, TLS, access, missing-stream and encryption errors remain.
  The last source failure takes precedence over a generic empty-buffer message.
- Notice text wraps across multiple lines and redraws while native PAF labels
  settle. IPTV failure notices allow more reading time and remain dismissible.
  Empty input cannot start playback. QR completion and input-validation results
  are logged numerically without recording URL paths, queries or payloads.

## Verification

- Actual scanner payload/validator checks under ASan/UBSan: 34 checks. A controlled
  extraction of revision `75486bb` exits 1 on valid normalized input, without a
  crash. Camera recognition is not simulated.
- Real source/session/catalogue/UI regressions and the public 334-entry M3U fixture.
- Real FFmpeg 6 HLS remux/decode fixtures, including AES rotation, ranges, separate
  tracks and low-delay timestamps; ASan/UBSan and ThreadSanitizer.
- Synthetic MPEG-2, AC-3, direct 1080p, AAC 5.1 and HTML resources return distinct
  errors through the real HLS producer, under ASan/UBSan and ThreadSanitizer.
- Real FFmpeg 6 DASH parser, remux/decode and lifecycle fixtures with the specific
  malformed-manifest result; ASan/UBSan and ThreadSanitizer.
- Shared loading/message UI checks, including the actual POSIX callback thread,
  forced creation failure and wrapped notices across eight themes in EN/IT.
- Native ARM Release/LTO compilation and package audits are recorded with the
  delivered artifact. Host renders are not Sony firmware or physical-playback proof.

## Physical checks

1. Scan the same IPTV-org QR, open the list and confirm its channels appear.
2. Cancel a scan, retry, and confirm no empty or previous link starts unexpectedly.
3. Try both previously working channels and the failing channels. The latter
   should provide a readable specific cause, rather than a generic decoder error.
4. Return the new `session_standard.txt` if a valid list is still rejected. It now
   distinguishes QR acceptance, URL validation, transport and parsing.

Actual Vita acceptance of this new build remains pending. Some IPTV channels may
still require unsupported codecs, DRM or resolutions; these changes do not add
HEVC/AV1/AC-3 decoding or remove the current rendition limits.
