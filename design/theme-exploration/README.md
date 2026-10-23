# VitaMediaDeck — film-inspired interface study

> Historical design/research evidence. Current release: [1.4.1](../../docs/releases/1.4.1.md); current checks: [validation](../../docs/VALIDATION.md). Proposals and old measurements below are not new implementation or console proof.

Design review, 7 September 2026. **No production UI, player, decoder, packaging, preference or version changes belong to this study.** Existing app work on `feature/iptv` is preserved. These drafts are not README release screenshots.

## Review artifacts

- [Screen atlas](index.html): 58 page/state drafts, each available in three visual directions, at 960×544 native design coordinates. Use `?all=1` for a continuous sheet of the current direction.
- [Focus and motion lab](motion.html): interactive single-owner focus movement, tab/content ownership, mutually exclusive menus, reduced motion and an optional bounded particle experiment.
- [Film reference board](references.html): externally hosted studio images with credits and source links. Includes film frames, case-study screen graphics and concept images; these are not interchangeable categories.
- [Motion specification](motion.md): interaction coverage, performance hypotheses and native acceptance gates.
- [Screen inventory](screen-inventory.md): source-to-draft coverage and remaining review boundaries.

Serve the repository locally, then open `design/theme-exploration/index.html`. Fonts, app icon and flags are existing repository assets. All illustrative media covers are original SVG compositions. No media provider, camera or playback session is contacted. The film board alone loads external reference images, without a referrer. Use “Toggle all screens” to review a continuous sheet; the source SVG compositions remain editable in the design renderer.

## Art direction

The primary blend is **Ghost in the Shell + Blade Runner 2049**. The user's “Cyberpunk 2047 movie” reference was clarified conversationally; this study uses Blade Runner 2049 as the film title, with the earlier Cyberpunk 2077 game comparison retained only as a secondary reference. Ghost in the Shell editions stay labeled: Territory’s device work is from the 2017 live-action film, not the 1995 anime or SAC.

| Direction | Composition and shape | Signal palette | Motion hypothesis |
| --- | --- | --- | --- |
| Ghostline | Connected, softly joined surfaces; organic framing around media | Phosphor mint and pale green | Soft continuous settling, 160 ms focus experiment |
| Crossfade | Compact information structure; original optical character around artwork, not text | Mint context, warm amber focus | Direct, continuous 120 ms focus experiment |
| Afterburn | More segmented, physical/instrument-like surfaces | Amber context, pale cool focus | Short mechanical-feeling 90 ms focus experiment |

These are deliberately early alternatives, not three completed production themes. Film texture, optical effects and detailed motion choreography need further art review after composition and interaction approval. The prototype durations are experiments, **not Vita timing measurements**.

Crossfade's six core colors: OLED black `#000000`, graphite `#0C1518`, pale text `#EAF5F0`, ghost mint `#85E6C4`, warm amber `#F4C375`, coral `#FF6874`. Muted text and boundary shades are derived supporting tokens. Amber replaced the earlier game-influenced yellow after the second film reference was clarified.

Inter handles readable UI text, usually 20 px body and 16 px secondary text. Inter Semibold provides hierarchy; Poppins is tested for Ghostline headings at 28 px. Existing font licenses and the eventual native fallback for non-Latin scripts must be retained. The browser does not prove native fallback rendering.

## Research and interpretation

[Territory’s Ghost in the Shell study](https://territorystudio.com/project/ghost-in-the-shell/) explores holographic devices and organic/technical interaction. Our interpretation is artwork-anchored spatial layering, not literal floating film graphics. The [official franchise pages](https://theghostintheshell.jp/en/) distinguish the editions.

[Territory’s Blade Runner 2049 study](https://territorystudio.com/project/blade-runner-2049/) documents physical/optical experimentation and contrasting device systems. [Cantina’s post-production work](https://www.cantinacreative.com/film/blade-runner-2049) provides additional screen-design and animation references. The board links the precise scenes instead of treating every cyberpunk screen as the same aesthetic.

The earlier [Michael Rigley game concepts](https://www.michaelrigley.com/work/cyberpunk-2077) are explicitly **concept work**, not final shipped UI. [CDPR designer Zach Bohn](https://zachgamedev.com/cyberpunk-2077/) provides a separate workflow reference for consistency and prototyping. The [GDC UI talk overview](https://gdcvault.com/play/1034160/User-Interface-in-Cyberpunk-2077) concerns optimization; only its public overview was consulted, not the full presentation.

Readable contrast and optional nonessential motion use [W3C contrast guidance](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html) and [animation guidance](https://www.w3.org/WAI/WCAG22/Understanding/animation-from-interactions.html) as design benchmarks. This is not a claim of WCAG certification for a native Vita app.

## Interaction and layout decisions

1. **One input owner.** Current tab and input focus are different states. Context uses a thin accent underline; the moving focus surface belongs to one tab, item, menu row or dialog action. Opening a modal suppresses the underlying focus.
2. **Animate continuity, not waiting.** Retarget from the current rendered position when input repeats. Never queue selection behind animation completion. Returning from a drawer restores logical focus immediately.
3. **Use the screen.** Three-column media grids, compact gutters, purposeful two-column details, square radio/podcast/channel artwork. Dense does not mean smaller illegible labels.
4. **Audio layouts.** Centered cover, large left cover, large right cover. The requested left-panel setting lives in an audio-only Player subpage beside Sections; it does not reorder the global section list. Layout changes must not reopen streams or modify pause/resume state. Missing album/author lines collapse. Radio has play/pause only; podcasts/music retain seekable transport. Small transport symbols keep 44-pixel hit regions.
5. **Real state only.** No fake live transmission data, fake QR recognition, invented connection health, or decorative log output in the app. The atlas explicitly uses fictional samples.
6. **Preserve existing contracts.** Downloads keep Content-Disposition naming, destination choice, background operation, pause/abort and separate activity indicators. UI work must not change network security, seek clocks, subtitle selection, decoder interfaces or lock semantics.

## Approval gates

- Choose the balance between the two film influences; review the motion lab, not static colors alone.
- Review all page families and the three audio layouts, including absent art, long text and busy states.
- Refine original artwork framing and finite particle/optical tests. No copied film assets in the app.
- Only then implement a small native vertical slice, measure it with playback/download active, and test on Vita.
- Promote shared primitives only after frame-time, focus, text-layer and input checks pass. Complete page migration afterward.

## Evidence boundaries

JavaScript syntax and browser rendering can validate a design artifact. They cannot validate Vita smoothness, input timing, camera behavior, decoder concurrency, battery use, OLED preservation or frame budgets. No VPK is produced by this design-only work. No public screenshots or repository release pages are replaced.
