#include "system/background_audio.h"

#include <stdint.h>
#include <string.h>

#include <psp2/appmgr.h>

#include "common/text_log.h"

int vt_background_audio_acquire(VtBackgroundAudioLease *lease) {
	if (!lease) return -1;
	memset(lease, 0, sizeof(*lease));
	lease->acquire_result = sceAppMgrAcquireBgmPort();
	lease->acquired = lease->acquire_result >= 0;

	if ((uint32_t)lease->acquire_result ==
	    (uint32_t)SCE_APPMGR_ERROR_BGM_PORT_BUSY) {
		log_printf("background audio: porta BGM occupata da un'altra app -> 0x%08X\n",
		           (unsigned)lease->acquire_result);
	} else {
		log_printf("background audio: sceAppMgrAcquireBgmPort -> 0x%08X (acquired=%d)\n",
		           (unsigned)lease->acquire_result, lease->acquired);
	}
	return lease->acquire_result;
}

int vt_background_audio_release(VtBackgroundAudioLease *lease) {
	if (!lease) return -1;
	if (!lease->acquired) {
		log_printf("background audio: release non necessaria (acquire=0x%08X)\n",
		           (unsigned)lease->acquire_result);
		return lease->acquire_result;
	}

	int ret = sceAppMgrReleaseBgmPort();
	log_printf("background audio: sceAppMgrReleaseBgmPort -> 0x%08X\n",
	           (unsigned)ret);
	lease->acquired = 0;
	return ret;
}
