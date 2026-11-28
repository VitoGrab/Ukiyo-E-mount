#ifndef UKIYO_LENS_PROFILE_H
#define UKIYO_LENS_PROFILE_H
#include <stdint.h>
#define UKIYO_PROFILE_HEADER_SIZE 16u
#define UKIYO_PROFILE_SIZE 559u
#define UKIYO_PROFILE_FLASH_SIZE 8192u
#ifdef __cplusplus
extern "C" {
#endif
/* Fixed-layout prime/constant-aperture zoom profile v1. Validation and staging only while bus idle.
 * Invalid staging leaves the previously selected profile unchanged. */
uint8_t ukiyo_profile_validate(const uint8_t *image, unsigned size);
uint8_t ukiyo_profile_stage(const uint8_t *image, unsigned size);
/* Try a read-only persistent image once per MCU boot (if enabled).
 * Return 1 if a valid image was staged from Flash. Never erases or writes. */
uint8_t ukiyo_profile_load_flash(void);
/* Copy active templates before cold handshake; fallback if none staged. */
void ukiyo_profile_apply(void);
#ifdef __cplusplus
}
#endif
#endif
