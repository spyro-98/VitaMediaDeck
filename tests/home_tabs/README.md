# Home rail regression checks

Compile from the repository root on macOS:

```sh
clang -O1 -g -fsanitize=address,undefined -ffunction-sections -fdata-sections \
  -Itests/home_tabs/include -Itests/online_audio/include -Isrc \
  tests/home_tabs/test.c src/ui/sections_sidebar.c -Wl,-dead_strip \
  -o /tmp/vmd-home-tabs-test
/tmp/vmd-home-tabs-test
```

This exercises the production rail and repeat logic, including held-button screen transitions, every one of the 5,040 configurable tab orders in both directions, timed repetition, and touch scrolling/hit tests. It does not emulate Vita rendering or physical input polling.
