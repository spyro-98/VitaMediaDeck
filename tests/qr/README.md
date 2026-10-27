# QR URL handoff checks

On macOS, with the pinned quirc source prepared by `tools/build-quirc-vita.sh`:

```sh
python3 tests/qr/test-payload.py
python3 tests/qr/test-payload.py --negative-ref 75486bb
```

The runner compiles the actual `publish_qr_data` implementation and the real
shared URL validator with ASan/UBSan. It checks canonical schemes, BOM/outer
whitespace, fragments, signed-query preservation, a terminal NUL, length bounds,
embedded-NUL rejection and protocol/header injection. The negative revision must
fail an assertion without crashing. It does not execute camera APIs or firmware.
The IPTV suite separately exercises QR output through the production list loader
and real M3U parser. Physical camera recognition and the reported QR remain Vita
acceptance checks.
