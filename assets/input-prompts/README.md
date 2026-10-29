# PlayStation input prompts

Six unchanged 64×64 PNGs and their SVG sources from [Kenney Input Prompts
1.5A](https://kenney.nl/assets/input-prompts), licensed CC0-1.0. The archive
and each downloaded file are recorded in `provenance.json`; the upstream
notice is `License.txt` and is included in the VPK.

Face buttons use the coloured outline variants. L1/R1 use the white alternative
shoulder caps. The renderer crops only transparent upstream gutters and retains
proportions and colours. A dark backing maintains contrast on all eight palettes.
Resources load once outside drawing and are freed after GPU completion.

App-owned strings use explicit U+E000…U+E005 tokens (`UI_PAD_*` in C and
universal UTF-8 escapes in the Italian/English catalogue). Ordinary X/O/L1/R1
in filenames, user input and media metadata are not automatically converted.
