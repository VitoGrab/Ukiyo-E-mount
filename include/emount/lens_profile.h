#ifndef UKIYO_LENS_PROFILE_H
#define UKIYO_LENS_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime representation for supported prime lenses only. Aperture values
 * use the wire scale 4096 + 512 * log2(f-number). Not a serialized format. */
typedef struct {
    uint16_t focal_mm;
    uint16_t lens_id;
    uint16_t aperture_max_av;
    uint16_t aperture_min_av;
    uint16_t aperture_now_av;
    char model[64];
} ukiyo_lens_profile;

/* Stage a validated copy. Main-loop context only, with the camera bus idle.
 * No packet is changed until the next full em_init(). */
uint8_t ukiyo_profile_stage(const ukiyo_lens_profile *profile);

/* Apply the staged profile before the cold handshake, with UART inactive.
 * If no profile was staged, use the generated built-in default. */
void ukiyo_profile_apply(void);

#ifdef __cplusplus
}
#endif
#endif
