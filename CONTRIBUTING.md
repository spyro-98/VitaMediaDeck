# Contributing

Contributions must be submitted under GPL-3.0-only and must be work the
contributor has the right to license. By submitting a change, the contributor
certifies the Developer Certificate of Origin 1.1 using a `Signed-off-by` line.

Do not submit proprietary Sony SDK material, firmware, keys, copyrighted media,
YouTube challenge/circumvention code, credentials or assets whose redistribution
rights cannot be demonstrated. Third-party code must retain its copyright and
license notices and must be recorded in `THIRD_PARTY_NOTICES.md` and
`release/DEPENDENCIES.lock`.

Dependency changes must pin an immutable version or commit, verify downloaded
content with SHA-256, preserve corresponding source, and update the release
license bundle. Security-sensitive changes must not add certificate-verification
bypasses or plaintext credential persistence.

Before proposing a change, run `tools/release-audit.sh --allow-dirty`, build the
VPK, run `git diff --check`, and test archive integrity. Hardware-specific
behavior still requires a physical PS Vita test.
