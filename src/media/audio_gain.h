#ifndef VITA_AUDIO_GAIN_H
#define VITA_AUDIO_GAIN_H
#include <stddef.h>
#include <stdint.h>

/* Kept identical in the independent HW/SW decoder packages. Hardware reaches
 * unity at 100%; 100..150% adds linear PCM gain with saturating peak clipping. */
#define VITA_AUDIO_VOLUME_NORMAL_PERCENT 100
#define VITA_AUDIO_VOLUME_MAX_PERCENT 150
static inline int vita_audio_volume_clamp(int percent) {
    return percent < 0 ? 0 : percent > VITA_AUDIO_VOLUME_MAX_PERCENT
        ? VITA_AUDIO_VOLUME_MAX_PERCENT : percent;
}
static inline int vita_audio_hardware_percent(int percent) {
    percent = vita_audio_volume_clamp(percent);
    return percent > VITA_AUDIO_VOLUME_NORMAL_PERCENT
        ? VITA_AUDIO_VOLUME_NORMAL_PERCENT : percent;
}
/* Normal-range attenuation belongs to AudioOut, so PCM stays bit-exact there.
 * Source and destination may coincide; queued input can also stay immutable. */
static inline void vita_audio_boost_pcm16(const int16_t *source, int16_t *output,
                                         size_t samples, int percent) {
    percent = vita_audio_volume_clamp(percent);
    if (percent < VITA_AUDIO_VOLUME_NORMAL_PERCENT)
        percent = VITA_AUDIO_VOLUME_NORMAL_PERCENT;
    for (size_t i = 0; i < samples; i++) {
        int32_t value = (int32_t)source[i] * percent / 100;
        output[i] = value > INT16_MAX ? INT16_MAX : value < INT16_MIN
            ? INT16_MIN : (int16_t)value;
    }
}
#endif
