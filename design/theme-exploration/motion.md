# Motion system — proposed, not implemented in VitaMediaDeck

> Historical design/research evidence. Current release: [1.4.1](../../docs/releases/1.4.1.md); current checks: [validation](../../docs/VALIDATION.md). Proposals and old measurements below are not new implementation or console proof.

## Focus is one shared state machine

```text
Tabs ↔ Page content ↔ Left OR right panel
                         ↓
                       Dialog
```

Persist **activeTab**, **selectedItem per page**, and **focusOwner** separately. Only focusOwner draws the strong highlight. An active tab retains a quiet underline while page content owns input. A focused tab has no focused page item. Background playback and a download are session states, not additional focus owners.

The motion lab implements the basic ownership and retargeting experiment with a single focus surface. Desktop Q/E test panel exclusivity; these are not Vita remappings. Gallery page layouts are illustrative and are not a complete app navigation simulation.

## Transition vocabulary

| Interaction | Proposed transition | Invariant / reduced-motion behavior |
| --- | --- | --- |
| Grid/list selection | Position and size interpolation of one focus surface; 90–160 ms directions | Logical selection changes immediately. Retarget current presentation, no animation queue. Reduced: snap, retaining strong focus. |
| Tab → page | Focus transfers down; current tab keeps only its underline | Never two owners. Do not load a page merely for a cosmetic focus animation. |
| Menu row change | Same shared focus primitive | No independent per-row animator or particles per item. |
| L1 ↔ R1 | Close/switch the previous panel, transfer ownership; 160–180 ms proposal | No period in which both panels consume input. Reduced: immediate replacement. |
| Folder enter/back | Short content translation/fade, preserve breadcrumb continuity; 100–140 ms proposal | Restore path and selection. No artificial delay awaiting the transition. |
| Touch scroll / lazy page append | Follow direct manipulation; fade only newly available artwork once | Never animate every visible row again on each append. Focus target remains in viewport. |
| Dialog open/close | 100–140 ms opacity/short displacement | Opening key cannot accept it; destructive default remains Cancel. |
| Artwork arrival | One 100 ms opacity handoff from placeholder | Cached texture; no decode/upload triggered by focus movement. No repeated shimmer on cached content. |
| Music layout change | Interpolate artwork rectangle and metadata groups, 180–220 ms proposal | Same media session, timestamps, selected tracks, pause state and art texture. Missing metadata collapses. |
| Play/pause | Compact icon/state change, optional 80 ms accent response | A command is never held until an animation ends. Radio resumes live; no false seek affordance. |
| Seek gesture | Thumb follows input directly; label updates at a bounded rate | Do not ease the actual media clock or misrepresent the requested seek point. |
| Full ↔ mini player | Interpolate the chrome/art container where supported; 160–180 ms proposal | No decoder restart, pause toggle, subtitle loss or render ownership conflict. |
| HUD show/hide | 100–120 ms opacity | Subtitle safe area and z-order remain correct throughout, not only at rest. |
| Image pan/zoom/rotate | Direct transform from input, no loading transition while manipulating | Never trigger a texture decode for a decorative animation. Performance/detail loader work remains separate. |
| Download progress | Real determinate value; optionally interpolate between received samples | Do not fabricate total/ETA. Background ring is distinct from generic busy state. |
| Download success/error | One check/error state transition | Success is not an alert triangle. No looping completion effect. |
| QR acquisition | Animate a status affordance only on actual processing/state changes | Camera framing stays stable; no fake scanline or recognition flash. |
| Settings changes | Focus continuity plus short state/value handoff | Text stays sharp; previews remain representative of the full display. |
| ECO/lock | Fade to black; very dim static lock only when locked | ECO alone does not disable input. Disable decorative particles/ambient motion. |

Not every element moves continuously. Every interaction family has an intentional motion rule, including “direct manipulation” and “remain still.” Labels, movie frames and subtitles must not wobble, glitch, or lag behind input.

## Effects and particles

- Current browser experiment: eight preallocated 2-pixel particles, event-driven and finite (under 220 ms), disabled by default. Rapid input cancels the prior burst rather than accumulating emitters. This is a bounded experiment, not an approved final effect.
- Native proposal: one shared focus interpolator; small fixed-capacity effect state; no allocation, file I/O, image decoding, texture upload or font rasterization on the animation hot path.
- Original optical textures, if approved, are prepared once and reused. Avoid full-screen multipass blur, render-to-texture feedback, dynamic noise generation and expensive blending over full-screen video.
- Tie artwork color analysis to cached artwork arrival, not each frame. The native player keeps the existing image/cache lifecycle unless separately changed and tested.
- Ambient motion is optional, confined to non-reading areas and reduced or disabled during competing playback/download/image work if measured headroom is insufficient. UI feedback must not be sacrificed before optional decoration.
- Stop updating invisible effects. No particles in ECO. Honor both app Reduce motion and system-equivalent preferences where available.

## Native performance acceptance — future work

Proposed target: smooth 60 Hz UI presentation when the existing rendering mode permits it (16.67 ms total frame time). This is **not a measured capability** and does not mean the UI gets the entire budget. Decoder, audio, transfers, texture uploads and system work share resources.

Measure baseline and candidate on actual Vita with the same clocks and media. Record CPU UI update time, GPU/frame submission timing where available, p50/p95/p99 frame time, missed frame count, draw/texture count, allocations and memory high-water marks. Do not infer GPU time from CPU wall time alone. An initial incremental UI-animation target of approximately 1 ms CPU is a hypothesis to negotiate against measurements, not a shipping promise.

Test: fast repeated navigation; tab→grid→opposite drawer→dialog; long localized labels; cover arrival while navigating; HW and SW video; podcast/radio; background HTTP/SMB/WebDAV transfer; mini-player plus scrolling and download; large image transforms; Reduce motion; OLED ECO/lock; PS+Start screenshot chord. Playback audio continuity, subtitle selection/sync and input response are release gates.

If a proposal regresses baseline responsiveness or playback, first remove particles and optical decoration, then reduce transition work. Do not hide stalls with a longer easing duration. App implementation starts only after design approval.
