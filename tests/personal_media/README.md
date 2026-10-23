# Personal media host tests

Run from the repository with host Jansson, MbedTLS headers, OpenSSL, libcurl,
FreeType, libpng, Python 3, clang and the macOS SDK:

```sh
VMD_HOST_JANSSON=/path/to/jansson-prefix VMD_HOST_CRYPTO=/path/to/mbedtls/include bash tests/personal_media/run-host-tests.sh
```

The harness compiles the production API, HTTPS and cached range transport.
OpenSSL supplies MD5 in the host test only; native Vita uses the already linked
MbedTLS implementation and the kernel random generator. The Vita network and
thread primitives are adapted to host networking and pthreads. TLS pin probing
is not invoked by the host HTTP tests. Local test servers validate credentials,
request bodies and ranges; an independent redirect target must receive zero
requests. All response data is synthetic.

The UI harness rasterizes the actual new screen using FreeType and platform
stubs. Output PNGs are host layout evidence, not console screenshots. Existing
player internals, real authentication servers, audio output and console timing
still need the device checks in `docs/personal-media-2026-10-23.md`.
