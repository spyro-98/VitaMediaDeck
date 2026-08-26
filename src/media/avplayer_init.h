#ifndef VITAWAVE_MEDIA_AVPLAYER_INIT_H
#define VITAWAVE_MEDIA_AVPLAYER_INIT_H

#include <psp2/avplayer.h>

/* Initialization of the hardware decoding chain (task P0
 * "fetch -> decode -> render"): vita2d (which in turn initializes sceGxm),
 * the SCE_SYSMODULE_AVPLAYER system module, and an sceAvPlayer instance with
 * the memory allocation callbacks required by sceAvPlayerInit.
 *
 * Unlike the rest of the project, there is NO official sample in
 * vitasdk/samples for this: every call, struct, and enum in this module has
 * been verified against two real, working open-source implementations that
 * agree with each other:
 *
 *  - lpp-vita (Rinnegatamante), Video module, source/luaVideo.cpp:
 *    https://github.com/Rinnegatamante/lpp-vita/blob/master/source/luaVideo.cpp
 *  - Vita-Media-Player (SonicMastr), src/avplayer/avplayer.c and
 *    src/avplayer/avplayerUtils.c:
 *    https://github.com/SonicMastr/Vita-Media-Player/blob/master/src/avplayer/avplayer.c
 *    https://github.com/SonicMastr/Vita-Media-Player/blob/master/src/avplayer/avplayerUtils.c
 *
 * The CDRAM memblock alignment constraint (256 KiB) is also confirmed by the
 * vita2d source itself (get_aligned_size() in libvita2d/source/utils.c):
 * https://github.com/xerpi/libvita2d/blob/master/libvita2d/source/utils.c
 */

/* Initializes AVPlayer using the persistent GXM from ui/runtime.h and writes
 * the player handle into *out_handle.
 * Returns 0 on success, an error code otherwise (in which case everything
 * that had been initialized is released before returning, and *out_handle
 * is not valid).
 * IMPORTANT: SceAvPlayerHandle is an opaque pointer-like handle 0x81xxxxxx
 * on real hardware. Even though VitaSDK typedefs it as int, it must never be
 * validated with signed comparisons `< 0`/`>= 0`. Vita3K's reverse
 * engineering documents the ABI result of sceAvPlayerInit() as `void *`:
 * https://github.com/Vita3K/Vita3K/blob/master/vita3k/modules/SceAvPlayer/SceAvPlayer.cpp
 *
 * Note on ordering: ui_runtime_init() must precede sceAvPlayerInit() because
 * the allocateTexture callback calls sceGxmMapMemory(), which requires GXM
 * to already be initialized. media_init()/media_term() never terminate
 * vita2d.
 *
 * file_replacement is optional. With NULL, SceAvPlayer uses its normal local
 * filesystem I/O. With a value, it uses the supplied application callbacks. */
int media_init(SceAvPlayerHandle *out_handle,
                const SceAvPlayerFileReplacement *file_replacement);

/* Same initialization, with explicit control over the initial clock state. */
int media_init_with_autostart(
    SceAvPlayerHandle *out_handle,
    const SceAvPlayerFileReplacement *file_replacement,
    SceBool auto_start);

/* Closes the player, which in turn invokes the deallocateTexture callbacks
 * while GXM is still alive. The shared renderer stays active for returning
 * to the UI. Safe even with an invalid internal sentinel. */
void media_term(SceAvPlayerHandle handle);

#endif /* VITAWAVE_MEDIA_AVPLAYER_INIT_H */
