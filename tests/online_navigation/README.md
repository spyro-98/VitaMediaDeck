# Online audio navigation regression checks

This harness includes the production screen implementation and substitutes only
the platform, network task and player entry points. It verifies transactional
feed loading, retained favorites/selection on failures and cancellation, empty
feed recovery, global-player error returns and unchanged successful miniplayer
and discovery navigation. It does not render the UI or use the Vita network stack.

From the repository root on macOS:

```sh
clang -O1 -g -fsanitize=address,undefined -ffunction-sections -fdata-sections \
  -Itests/online_navigation/include -Itests/home_tabs/include \
  -Itests/online_audio/include -Isrc tests/online_navigation/test.c \
  -Wl,-dead_strip -o /tmp/vmd-online-navigation-test
/tmp/vmd-online-navigation-test
```

On the console, test an unavailable RSS feed, malformed feed, cancelled fetch
and unplayable enclosure from Home > Podcast Online. The failed operation must
return to favorites without an empty Download/Info toolbar. Verify a working
feed still opens episodes and that minimizing a playing episode preserves it.
With logging enabled, failures save HTTP status/result, response-size limit,
RSS parse/count and playback/probe stages to `ux0:data/VitaMediaDeck/session_log.txt`.
URLs, authentication tokens and response bodies are not included in these new
diagnostic messages.
