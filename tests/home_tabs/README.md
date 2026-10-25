# Home rail regression checks

Part of the 1.4.1 validation set. See [suite index and evidence boundaries](../../docs/VALIDATION.md).

Compile from the repository root on macOS:

```sh
clang -O1 -g -fsanitize=address,undefined -ffunction-sections -fdata-sections \
  -Itests/home_tabs/include -Itests/online_audio/include -Isrc \
  tests/home_tabs/test.c src/ui/sections_sidebar.c -Wl,-dead_strip \
  -o /tmp/vmd-home-tabs-test
/tmp/vmd-home-tabs-test
```

This exercises the production rail and repeat logic, including held-button screen transitions, every one of the 720 configurable tab orders in both directions, timed repetition, and animated centering, reduced motion, swipe selection, gap/margin hit tests. It does not emulate Vita rendering or physical input polling.

`bash tests/home_tabs/run-host-tests.sh` also verifies per-theme persistence in
an isolated filesystem: eight independent orders, global/legacy migration,
semantic tab preservation, failed save, corrupted primary and valid backup.
