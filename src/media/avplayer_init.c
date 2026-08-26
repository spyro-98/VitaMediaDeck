/*
 * VitaWave - hardware decoder initialization (sceAvPlayer) + vita2d.
 *
 * See avplayer_init.h for the list of verified sources. In summary, the
 * four "tricky" points of this module and their confirmation:
 *
 *  1. System module: it does NOT load with a plain
 *     sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER) call (what earlier
 *     versions of this file did, consistent with lpp-vita/source/
 *     luaVideo.cpp — but that path turned out to be unstable: a
 *     100%-deterministic crash in "avPlayer Controller", reproducible with
 *     any video, whose cause was isolated by comparing behavior with
 *     Vita-Media-Player on the same console — see mds/DEVELOPMENT_LOG.md,
 *     2026-08-04). The stock SceAvPlayer system module requires a taiHEN
 *     patch (reverse engineering by GrapheneCT, packaged as ReAvPlayer by
 *     SonicMastr, MIT license) loaded from app0:modules/reAvPlayer.suprx —
 *     its module_start() calls sceSysmoduleLoadModule() internally, so it
 *     replaces the direct call rather than sitting alongside it.
 *
 *  2. "Generic" allocator (memoryReplacement.allocate/deallocate):
 *     memalign()/free() on the normal heap. Identical in both reference
 *     implementations (lpp-vita memalloc/dealloc, Vita-Media-Player
 *     Allocate/Deallocate).
 *
 *  3. Video frame allocator (memoryReplacement.allocateTexture): it CANNOT
 *     be malloc. It must be memory visible to the GPU, i.e. a
 *     sceKernelAllocMemBlock() mapped with sceGxmMapMemory(). Both
 *     implementations use CDRAM. The minimum alignment for CDRAM memblocks
 *     is 256 KiB (0x40000): this is stated explicitly both by the comment
 *     in Vita-Media-Player/src/avplayer/avplayerUtils.c ("CDRAM memblocks
 *     must be 256KiB aligned") and by get_aligned_size() in the vita2d
 *     source.
 *
 *  4. Init/term ordering relative to sceGxm: see the comments in
 *     avplayer_init.h.
 */

#include <malloc.h>
#include <string.h>

#include <psp2/avplayer.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <vita2d.h>

#include "common/text_log.h"
#include "media/avplayer_init.h"
#include <vita_hw_decoder.h>

/* Minimum alignment required by SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW
 * memblocks. Same value (0x40000) used by lpp-vita (FRAMEBUFFER_ALIGNMENT),
 * by Vita-Media-Player, and by vita2d itself. */
#define CDRAM_ALIGNMENT 0x40000u

/* Safety alignment for the generic allocator: sceAvPlayer can call
 * allocate() with alignment 0, and memalign(0, n) is undefined behavior.
 * (The reference implementations pass the value through as-is; here we
 * prefer an explicit guard, which is harmless in the normal case.) */
#define GENERIC_MIN_ALIGNMENT 8u

#define ALIGN_UP(x, a) (((x) + ((a) - 1u)) & ~((a) - 1u))

/* Base priority for sceAvPlayer's internal threads. 0xA0 is the value used
 * by lpp-vita; Vita-Media-Player uses decimal 125. Both work: it's a lower
 * priority (higher number) than the main thread's default, so the player
 * doesn't starve the render loop. */
#define AVPLAYER_BASE_PRIORITY 0xA0

/* Number of decoded video frames that sceAvPlayer keeps ready for output.
 * lpp-vita uses 2 (and allocates 2 output textures), Vita-Media-Player uses
 * 6. We keep 2: it's the minimum that allows double buffering and limits
 * CDRAM usage, which we share with vita2d's two framebuffers. */
#define AVPLAYER_OUTPUT_FRAME_BUFFERS 2

/* SceAvPlayerHandle is declared `int` by the VitaSDK header, but the actual
 * ABI result of sceAvPlayerInit() is an opaque pointer-like handle in the
 * user area 0x81xxxxxx (0x816CEAA0 and 0x81681240 observed). Bit 31 is
 * therefore part of the handle, not the sign of an error. Never use
 * <0/>=0 on this type. Vita3K's own reverse engineering confirms the result
 * is defined as `void *`; the VitaSDK header's "< 0 on error" comment is
 * therefore unusable for distinguishing handles on hardware. -1 is only our
 * internal sentinel, compared for equality. */
#define AVPLAYER_INVALID_HANDLE ((SceAvPlayerHandle)-1)

static int avplayer_init_result_is_known_error(SceAvPlayerHandle handle) {
	uint32_t raw = (uint32_t)handle;
	/* All known AvPlayer errors belong to facility 0x806A; VitaSDK publicly
	 * exposes 0001/0003, other SDKs also 0002/0004. */
	return raw == 0 || (raw & 0xFFFF0000u) == 0x806A0000u;
}

/* --- Allocation callbacks required by sceAvPlayerInit ---------------------- */

/* memset to zero after memalign(), not just memalign() alone (2026-08-04):
 * comparing two independent crash dumps (different videos, different
 * absolute addresses) with vita-parse-core, the memory pointed to by
 * register R4 at the moment of the crash contained exactly the SAME byte
 * pattern in both cases (`ff*8 00*8` repeated twice) — not random garbage,
 * it's deterministic. Neither lpp-vita nor Vita-Media-Player zero this
 * memory, but neither of the two has been verified to reproduce this exact
 * scenario (same bring-up, same firmware version) — the low-risk testable
 * hypothesis is that sceAvPlayer allocates an internal structure here that
 * it expects to be pre-zeroed (e.g. an optional function pointer: if it's
 * not NULL it tries to call it). memalign() never zeroes, unlike calloc();
 * this makes it equivalent, but on the custom allocator required by
 * sceAvPlayerInit. */
static void *av_allocate(void *arg, uint32_t alignment, uint32_t size) {
	(void)arg;
	if (alignment < GENERIC_MIN_ALIGNMENT) {
		alignment = GENERIC_MIN_ALIGNMENT;
	}
	void *ptr = memalign(alignment, size);
	if (ptr) {
		memset(ptr, 0, size);
	}
	return ptr;
}

static void av_deallocate(void *arg, void *ptr) {
	(void)arg;
	free(ptr);
}

/* Video frame buffer allocator: CDRAM memblock mapped for the GPU.
 * Note: we do NOT keep track of the SceUID; to free it we look up the
 * memblock with sceKernelFindMemBlockByAddr(), exactly as both reference
 * implementations do (lpp-vita gpu_dealloc, Vita-Media-Player
 * avTextureFree with memUid == SCE_UID_INVALID_UID). */
static void *av_allocate_texture(void *arg, uint32_t alignment, uint32_t size) {
	(void)arg;

	SceKernelAllocMemBlockOpt opt;
	void *base = NULL;
	SceUID memblock;

	if (alignment < CDRAM_ALIGNMENT) {
		alignment = CDRAM_ALIGNMENT;
	}
	size = ALIGN_UP(size, alignment);

	memset(&opt, 0, sizeof(opt));
	opt.size = sizeof(opt);
	opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
	opt.alignment = alignment;

	memblock = sceKernelAllocMemBlock("VitaWaveAvFrame",
	                                   SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
	                                   size, &opt);
	if (memblock < 0) {
		return NULL;
	}

	if (sceKernelGetMemBlockBase(memblock, &base) < 0 || base == NULL) {
		sceKernelFreeMemBlock(memblock);
		return NULL;
	}

	if (sceGxmMapMemory(base, size,
	                     SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE) < 0) {
		sceKernelFreeMemBlock(memblock);
		return NULL;
	}

	/* Same precaution as av_allocate() above: CDRAM memory freshly
	 * allocated by sceKernelAllocMemBlock() is not guaranteed to be
	 * zeroed. */
	memset(base, 0, size);

	return base;
}

static void av_deallocate_texture(void *arg, void *ptr) {
	(void)arg;

	if (ptr == NULL) {
		return;
	}

	SceUID memblock = sceKernelFindMemBlockByAddr(ptr, 0);
	if (memblock < 0) {
		return;
	}

	/* The GPU might still have a queued draw that samples this buffer:
	 * unmapping/freeing before rendering has finished is a classic silent
	 * crash. Vita-Media-Player does exactly this wait in avTextureFree();
	 * lpp-vita omits it. */
	vita2d_wait_rendering_done();

	sceGxmUnmapMemory(ptr);
	sceKernelFreeMemBlock(memblock);
}

/* --- Public API ------------------------------------------------------------- */

int media_init_with_autostart(
    SceAvPlayerHandle *out_handle,
    const SceAvPlayerFileReplacement *file_replacement,
    SceBool auto_start) {
	if (out_handle == NULL) {
		return -1;
	}
	*out_handle = AVPLAYER_INVALID_HANDLE;

	int prepare_ret = vita_hw_decoder_prepare_runtime();
	if (prepare_ret < 0) return prepare_ret;

	/* 0x48 bytes (VITASDK_BUILD_ASSERT_EQ in psp2/avplayer.h): a trivial
	 * size, no problem keeping it on the stack. */
	SceAvPlayerInitData init_data;
	memset(&init_data, 0, sizeof(init_data));

	init_data.memoryReplacement.allocate         = av_allocate;
	init_data.memoryReplacement.deallocate       = av_deallocate;
	init_data.memoryReplacement.allocateTexture  = av_allocate_texture;
	init_data.memoryReplacement.deallocateTexture = av_deallocate_texture;

	if (file_replacement != NULL) {
		init_data.fileReplacement = *file_replacement;
	}

	/* fileReplacement: if file_replacement is NULL (historical behavior),
	 * it is left zeroed — sceAvPlayer uses its own internal I/O on the path
	 * passed to sceAvPlayerAddSource(). This is what lpp-vita does, opening
	 * local "ux0:..." files exactly like we do.
		 * If non-NULL, it is copied as-is. This is the same mechanism used by
		 * other Vita media players for custom seekable file callbacks.
	 *
	 * eventReplacement is left zeroed: the event constants
	 * (SCE_AVPLAYER_STATE_READY and similar) are NOT defined in VitaSDK's
	 * psp2/avplayer.h header, so using the callback would mean copying
	 * magic numbers from a different SDK. Normal playback uses autoStart,
		 * like lpp-vita. */

	init_data.basePriority = AVPLAYER_BASE_PRIORITY;
	init_data.numOutputVideoFrameBuffers = AVPLAYER_OUTPUT_FRAME_BUFFERS;
	init_data.autoStart = auto_start;
	init_data.debugLevel = 0;

	SceAvPlayerHandle handle = sceAvPlayerInit(&init_data);
	log_printf("media_init: sceAvPlayerInit(autoStart=%d) -> 0x%08X",
	           auto_start == SCE_TRUE, (unsigned)handle);
	/* Don't compare `handle < 0`: valid handles are opaque addresses
	 * 0x81xxxxxx and come out negative as int32_t. Vita-Media-Player,
	 * lpp-vita, OpenFMV, and vitaGL's video sample all pass the value
	 * straight to sceAvPlayerAddSource(). We only filter the AvPlayer error
	 * facility 0x806A (and NULL), so we don't again mistake a valid handle
	 * for an error code. */
	if (avplayer_init_result_is_known_error(handle)) {
		/* reAvPlayer.suprx is NOT unloaded here: it stays resident for the
		 * whole process (see the comment above where it's loaded) —
		 * unloading it on an sceAvPlayerInit() failure would put it back
		 * into the same "needs reloading next time" state that this fix
		 * was meant to avoid. */
		return handle ? (int)handle : -1;
	}

	*out_handle = handle;
	return 0;
}

int media_init(SceAvPlayerHandle *out_handle,
               const SceAvPlayerFileReplacement *file_replacement) {
	return media_init_with_autostart(out_handle, file_replacement, SCE_TRUE);
}

void media_term(SceAvPlayerHandle handle) {
	if (handle != AVPLAYER_INVALID_HANDLE) {
		/* sceAvPlayerClose() invokes deallocateTexture for any frames still
		 * allocated: sceGxm (and therefore vita2d) must still be alive
		 * here. */
		sceAvPlayerClose(handle);
	}
	/* reAvPlayer.suprx is no longer unloaded here (2026-08-04) — it stays
	 * loaded for the whole lifetime of the process, see the comment in
	 * media_init() for why. */
}
