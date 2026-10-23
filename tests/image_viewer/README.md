# Image controls regression checks

Part of the 1.4.1 validation set. See [suite index and evidence boundaries](../../docs/VALIDATION.md).

Run from the repository root:

```sh
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined \
  -Itests/home_tabs/include -Isrc tests/image_viewer/test.c -o /tmp/vmd-image-test
/tmp/vmd-image-test
```

Uses the production input/geometry helpers: analog rest jitter, release, frame
rate independence, delayed frames, opposing buttons, bounds, rotated fit, 1000
quarter turns, pinch, reordered/replaced contacts, drag and release. Physical
stick drift, Vita image upload and native detail overlays require console QA.
