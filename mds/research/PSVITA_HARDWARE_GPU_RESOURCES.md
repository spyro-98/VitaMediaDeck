# PlayStation Vita hardware and GPU resource dossier

Related outcome: [H.264 acceleration research](../H264_ACCELERATION_RESEARCH_PLAN.md).

## How to use this document

Separate public facts from measurements. Sony did not publish a complete SoC/bus/video-decoder datasheet, and clock plugins expose controls rather than a proof of internal topology. Treat undocumented bandwidth, cache, DMA, and codec scheduling claims as hypotheses until measured on hardware.

## Confirmed platform facts

- CPU family: quad-core ARM Cortex-A9 with NEON SIMD.
- GPU family: PowerVR SGX543MP4+ exposed to homebrew through `SceGxm`/vita2d.
- Physical memory architecture: 512 MB main RAM and 128 MB VRAM; an application receives smaller runtime budgets.
- Display: 960×544.
- Public VitaSDK interfaces exist for AVC video decode, AAC audio decode, graphics surfaces, display, power, and application memory-budget queries.
- The current public AVC path accepts at most 921,600 source pixels, allowing 1280×720 and 720×1280.
- `SceGxm` is a graphics API, not a standard general-purpose compute environment.

## Important unknowns to measure

- Sustainable CPU↔main RAM↔CDRAM transfer bandwidth under decoder and display load.
- Cache effects of surface layout and queue depth.
- How codec-engine, bus, GPU, and crossbar clock settings interact on each firmware/plugin combination.
- The exact source of occasional high-complexity decode-pressure events.
- Whether higher CPU clocks increase contention or timing jitter in specific clock combinations.
- Hardware-decoder surface/DPB memory limits across AVC profiles and reference structures.

## Public VitaSDK surfaces

### Video and audio

- [`SceVideoDec` documentation](https://docs.vitasdk.org/group__SceVideoDecUser.html)
- [`SceAudioDec` documentation](https://docs.vitasdk.org/group__SceAudiodecUser.html)
- [`SceAvPlayer` documentation](https://docs.vitasdk.org/group__SceAvPlayerUser.html)
- [VitaSDK headers](https://github.com/vitasdk/vita-headers)

The public stubs provide codec operations, not a complete streaming player. Applications still own input, timestamps, surfaces, synchronization, and lifecycle.

### Graphics and memory

- [`SceGxm` documentation](https://docs.vitasdk.org/group__SceGxmUser.html)
- [`SceKernel` memory documentation](https://docs.vitasdk.org/group__SceKernelMemBlock.html)
- [vita2d](https://github.com/xerpi/libvita2d)

GXM is appropriate for texture/surface setup, sampling, scaling, color conversion shaders, composition, and display. It does not make serial H.264 entropy decoding a natural GPU workload.

### Power and clocks

- [`ScePower` documentation](https://docs.vitasdk.org/group__ScePower.html)
- [PSVshell](https://github.com/Electry/PSVshell)

VitaTube offers either an exact application profile or zero clock writes so PSVshell remains fully authoritative. Debug data should report read-back values rather than assuming a requested clock was applied.

## CPU: Cortex-A9 and NEON

Useful sources:

- [Arm Cortex-A9 processor page](https://developer.arm.com/Processors/Cortex-A9)
- [Arm Architecture Reference Manual](https://developer.arm.com/documentation/ddi0406/latest/)
- [Arm NEON intrinsics reference](https://developer.arm.com/architectures/instruction-sets/intrinsics/)

Good CPU/NEON candidates include chroma interleaving, planar copies, audio sample conversion, checksums, and fixed-size pixel transforms. H.264 entropy parsing and reference-dependent control flow remain difficult to parallelize efficiently.

## GPU: PowerVR SGX543MP4+

Useful sources:

- [Imagination Technologies](https://www.imaginationtech.com/)
- [PowerVR architecture documentation portal](https://docs.imgtec.com/)
- [Vita3K](https://github.com/Vita3K/Vita3K), especially renderer and GXM translation code

The SGX architecture is tile-based deferred rendering. This can make normal game rendering efficient and reduce external bandwidth, but it does not imply a supported compute queue for arbitrary codec kernels. Use it for presentation work that maps cleanly to shaders.

## Multimedia accelerator

Public Vita software and hardware analysis identifies a dedicated multimedia/video capability in the Vita SoC/package, often discussed under Toshiba's “Venezia” lineage. Public detail is incomplete. For VitaTube, the actionable interface is not a speculative block diagram; it is the validated VitaSDK video-decoder contract used by the open-source `h264_vita` backend.

References and implementation evidence:

- [wiliwili](https://github.com/xfangfang/wiliwili)
- [FFmpeg](https://github.com/FFmpeg/FFmpeg)
- [Vita-Media-Player](https://github.com/SonicMastr/Vita-Media-Player)
- [NetStream](https://github.com/GrapheneCt/NetStream)
- [ReAvPlayer](https://github.com/SonicMastr/ReAvPlayer)

## System and board analysis

- [The PSVita's Architecture: A Practical Analysis](https://www.copetti.org/writings/consoles/playstation-vita/)
- [iFixit PlayStation Vita teardown](https://www.ifixit.com/Teardown/PlayStation+Vita+Teardown/7872)
- [Vita Development Wiki](https://www.psdevwiki.com/vita/)

Community reverse engineering is valuable, but confidence must be labeled. Use primary VitaSDK headers and on-device measurements for implementation decisions.

## H.264 references

- [ITU-T H.264 recommendation](https://www.itu.int/rec/T-REC-H.264)
- [FFmpeg H.264 decoder source](https://github.com/FFmpeg/FFmpeg/tree/master/libavcodec)
- [JM reference software archive](https://iphome.hhi.de/suehring/tml/)

A complete decoder requires bitstream parsing, entropy decode, inverse transforms, prediction, motion compensation, deblocking, decoded-picture-buffer management, reordering, timing, and error handling. That scope explains why the production result uses a complete maintained codec backend rather than a custom shader experiment.

## Recommended measurement suite

### CPU

- Per-core utilization and thread affinity.
- NEON copy/interleave throughput for realistic aligned and unaligned surfaces.
- Decoder packet-submit and frame-receive time distributions.

### GPU/GXM

- Frame presentation cost with direct NV12 versus copied textures.
- Surface reuse latency and GPU completion waits.
- UI-only frame rate with and without fixed sleeps/vblank waits.

### Memory and bus

- Main RAM↔CDRAM bandwidth at each relevant clock profile.
- Cache-hot and cache-cold surface operations.
- Surface-pool depths of 8, 16, 20, and 32 with total memory and latency recorded.

### Decoder

- Format profile/level, reference-frame count, resolution, FPS, and bitrate.
- Packets submitted, decoded FPS, ready surfaces, shown FPS, drops, and A/V lag.
- Repeated open/close sessions and vertical video.

## Distribution rules

- Use public VitaSDK interfaces and redistributable source.
- Do not commit Sony SDK material, firmware modules, or copied unlicensed implementation code.
- Pin external revisions and hashes.
- Keep the software fallback until a replacement passes repeated hardware tests.
- Distinguish measured behavior from inferred SoC behavior in every report.
