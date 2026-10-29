# Controller icons and spacing

Run from the app repository:

```sh
bash tests/input_prompts/run-host-tests.sh
bash tests/input_prompts/run-spacing-tests.sh
bash tests/ui_chrome/run-host-tests.sh
```

The first suite compiles production `font.c` and `input_prompts.c`, loads the
six downloaded PNGs and uses the real Inter and IBM Plex Mono metrics. It checks
mixed text/icon advances, fitting, alpha, clipping above the viewport, resource
lifecycle, native dispatch and every Italian/English catalogue entry. Literal
filenames containing X/O/L1/R1 remain ordinary text.

The spacing fixture compiles shared components in eight themes and two
languages, with seven button widths and all six keys. It checks separate icon
and label slots, 12px action-label insets, centered icon-only controls and a
20px minimum gap between footer columns. It emits host reference PNGs.

The chrome suite uses each theme's actual face and checks all 2,880 possible
24h/12h clock values per theme. It reserves 22px before the battery and at
least 12px between the search field and both active status indicators.

These are host metrics, geometry and raster tests with substituted platform
boundaries. Native firmware pixels, OLED readability, touch and runtime
performance still require a physical Vita.
