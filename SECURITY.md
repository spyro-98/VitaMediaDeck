# Security policy

Do not report vulnerabilities, credentials, private server addresses or crash
dumps in public issues. Contact the maintainer privately before disclosure;
the private reporting address must be configured on the hosting platform
before the first public release.

Supported code is the current default branch and the most recent signed
release. Older VPKs, development builds and third-party Vita plugins are not
guaranteed to receive security updates.

Reports should include the affected commit/version, reproduction steps,
network-source type and whether the issue exposes credentials or bypasses TLS,
host-key, SMB signing or certificate validation. Remove personal media and
secrets from logs and dumps.

VitaWave intentionally has no option to disable TLS verification. A build that
links the legacy VitaSDK OpenSSL libcurl is not a supported or releasable build.
