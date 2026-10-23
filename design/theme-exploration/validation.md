# Design validation — 7 September 2026

> Historical design/research evidence. Current release: [1.4.1](../../docs/releases/1.4.1.md); current checks: [validation](../../docs/VALIDATION.md). Proposals and old measurements below are not new implementation or console proof.

Scope: browser drafts only. This is not a native app, decoder, camera or console test.

- `node --check` passes for both atlas and motion scripts.
- Browser rendered 58 drafts in each of three directions (174 compositions).
- DOM text rectangles were normalized to the native 960×544 coordinate space: no foreground text outside the frame, no detected foreground text-to-text collisions over a 3-pixel intersection threshold, and no draft with more than one strong focus shape after corrections. Opaque/modal underlays are excluded from foreground collision checks; this is not an exhaustive visual/accessibility certification.
- Corrected a real collision in compact folder rows by putting title and metadata in separate horizontal columns.
- Corrected competing focus styles in tabs, settings, menus and the audio-layout overlay. Underlying pages no longer retain the strong focus shape when a modal owns input.
- Motion sequence tested through browser UI: tabs / Library → content / The Long Way Home → left / Home → right / View → close → content / The Long Way Home → tabs / Local video. A single focus surface persisted.
- Reduce motion produced a 0 ms transition; particle pool remained fixed at eight elements. Native frame time and GPU cost have not been measured.
- All six external reference images loaded successfully and were visually inspected on the reference board. They remain externally hosted and credited.
- The design atlas was visually inspected at native-coordinate scale for the library, transfer/audio layouts and focus lab. Missing-art and full localization variants remain explicit review gates in the inventory.
- An experimental SVG download action did not emit a download event through the in-app browser. It was removed from the deliverable and replaced with the continuous-sheet toggle; no claim of portable export validation is made.

No production app source, version metadata, VPK, README release gallery, Git commit or remote publication is changed by this study.
