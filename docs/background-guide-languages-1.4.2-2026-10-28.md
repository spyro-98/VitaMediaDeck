# Background guide and restored languages — 1.4.2

Live IPTV requests its programme guide immediately after the decoder opens,
before R1 is used. The ordinary playback loop polls a background worker and
publishes completed results; opening Programme or pressing Square never enters
a modal loading loop or resynchronizes playback. Pending feedback remains
inside the R1 panel. A refresh retains the last valid details and refreshes at
programme boundaries, with bounded retry intervals for missing data/errors.

The service owns a value copy of channel metadata, parent playlist and language.
It has one running request and one latest pending request. Channel changes
cancel/drop the old UI handle without joining the worker; cancelled results
cannot replace the new channel's programme. Only application teardown joins the
worker, before network/library services are destroyed. Benchmark trials and
recorded VOD do not start guide requests.

The reported channel is `27 TwentySeven`. Its playlist ID/title do not exactly
match `Twenty Seven.it` in the actual public guide. Display-name matching now
also considers a separated numeric channel prefix and trailing quality tags.
Exact IDs retain priority; ambiguous names, country suffixes and `+1` variants
remain distinct. The actual previously fetched playlist and public XMLTV fixture
pass current programme/description/end/upcoming checks for this channel and the
first eight national channels. This is actual-source host evidence, not new
console playback evidence. The broadcaster's channel identity is documented at
[Mediaset Infinity](https://mediasetinfinity.mediaset.it/27twentyseven).

Programme no longer displays cache/provider diagnostics or requests that users
inspect `tvg-id`/`tvg-name`. An unavailable programme receives a short message;
technical source/cache status is not programme information.

Settings now offers System, English, Italian, Spanish, French, German,
Portuguese, Russian, Japanese, Korean, simplified Chinese and traditional Chinese.
Existing preference IDs 0–7 are unchanged; IDs 8–11 extend the same persistent
record. System language resolves all supported firmware IDs, including Brazilian
Portuguese. Native PAF startup receives the selected firmware language ID.

Nine additional offline catalogs cover 750 fixed UI captions each, including
catalog IDs, inline captions, controller glyphs and format strings. They were
generated from app-owned English UI literals using Google Translate following
the user's explicit permission for those UI strings only. No source files,
playlist URLs, credentials or personal data were sent. Common media terms have
reviewed overrides in `src/i18n/translation_overrides.json`; other translations
are machine generated and have not received a full native-speaker review.
The application never calls the translation service at runtime.

Regeneration is resumable through `tools/generate-translations.py`; it validates
printf argument order/types, private-use controller glyphs and line breaks.
`tests/i18n/run-host-tests.sh` does not use the network and checks all 6,750
generated lookups, catalog keys/tokens/UTF-8, effective/system language mappings,
existing preference compatibility and failed-write rollback. UI/runtime labels
and external provider metadata not owned by the app retain their source text.

`tests/player_feedback/programme/run-host-tests.sh` runs the actual asynchronous
service under ASan/UBSan and TSan, keeps playback ticking during a blocked fetch,
checks owned context lifetime, cancellation/latest-only replacement, failed
refresh retention and shutdown. The real player helpers are exercised with the
drawer closed. Programme panel fixtures cover current/schedule/pending/missing
states, eight themes and IT/EN. Host previews substitute firmware pixels;
they do not establish CJK font appearance, menu fit, playback responsiveness or
touch behaviour on a physical PS Vita.

Delivery remains VitaMediaDeck-1.4.2.vpk, APP_VER 01.06, TITLE_ID VMDK00001.
Package integrity, native build/imports/LTO and host regressions are verified
separately from copying/installing or running this binary on the console.
