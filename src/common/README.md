# VitaSDK sample-derived files

`debugScreen.c` and `debugScreen.h` originate from the public-domain `common/` directory in the [VitaSDK samples repository](https://github.com/vitasdk/samples), specifically the `hello_world` sample.

The files are kept close to upstream so early-startup and crash diagnostics can render without depending on the full VitaWave UI stack. VitaWave-specific logging lives in the adjacent `text_log` implementation.
