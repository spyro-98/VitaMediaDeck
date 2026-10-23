# IPTV favourites, availability and crash investigation

> Dated implementation/research record. Integrated status: [VitaMediaDeck 1.4.1](releases/1.4.1.md); [validation guide](VALIDATION.md). Build IDs, figures and pending checks below describe the recorded snapshot. External research links were not revalidated for this patch release.

## Controls and scope

During IPTV playback, tap the Favourite button in the HUD or press Square to
toggle the channel's saved favourite state without restarting playback. A save
failure leaves the previous state intact. Locked input cannot change favourites.

In a channel collection, tap **Filter errors**, or choose **R1 → Check / filter
errors**. The check makes one HTTP GET at a time, stops at the first body chunk
without retaining it, and never starts a decoder or requests child segments.
Connection timeout is three seconds; each whole request is bounded to five
seconds. Circle cancels. Large lists can still take minutes on a slow network.

Only DNS resolution failures and HTTP 403/404 responses are hidden. Timeouts,
TLS failures, 401/410/429/5xx responses and unchecked entries remain visible.
The filter checks the entry URL, not whether its media, keys or rendition URLs
will play. A temporary DNS failure or a regional 403 does not prove a channel is
permanently dead. **Show all** restores every entry without modifying storage;
refresh/reload also clears the temporary filter. Run the check again after
restoring the list if connectivity changes. Saved playlist URLs are skipped.

## September 7 crash evidence

The installed eboot was byte-identical to the pre-change local SELF:
`6fd15cd8ad55639f1ebe2c699735a6f8f753ff526f90dad1725f6974a2810caf`.
The corresponding symbol ELF SHA-256 was
`8f5c2ede6577e187d119e194406f54bac95fb8839839da24206716a55eada3aa`.

The 22:51 core dump, examined locally, identifies a data abort in the pthread
thread at PC `0x8147de08`, `pte_osThreadCreate`: `str r5, [r6]` with `r6 = 0`.
The helper had received a null result from `vitasdk_get_pthread_data` for the
new thread. Stack return addresses resolve to `pthread_create` (`0x8147d093`),
FFmpeg `init_thread`, pthread_frame.c:790 (`0x810fd51d`), `open_h264_decoder`
(`0x81354591`), and `vita_sw_software_video_thread` (`0x813554f3`).
This proves the null-pointer failure while creating software H.264 frame
workers; it does not establish why the SDK returned null or prove out-of-memory.

The scoped mitigation disables the nested FFmpeg frame-worker pool for
forward-only software video inputs in both decoder packages. Decoding still
runs on the existing asynchronous video worker. Seekable-file threading and
hardware decoding are unchanged, and no public decoder interface changes.
Startup diagnostics now report the actual thread count. Single-worker software
IPTV decoding may trade throughput for stability; repeat playback/open/close
and software fallback on real hardware before calling the crash resolved.
