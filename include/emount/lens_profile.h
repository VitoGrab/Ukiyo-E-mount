#ifndef UKIYO_LENS_PROFILE_H
#define UKIYO_LENS_PROFILE_H
#include <stdint.h>
#define UKIYO_PROFILE_HEADER_SIZE 16u
#define UKIYO_PROFILE_SIZE 559u
#ifdef __cplusplus
extern "C" {
#endif
/* Fixed-layout prime profile v1. Validation and staging only while bus idle.
 * Invalid staging leaves the previously selected profile unchanged. */
uint8_t ukiyo_profile_validate(const uint8_t *image, unsigned size);
uint8_t ukiyo_profile_stage(const uint8_t *image, unsigned size);
/* Copy active templates before cold handshake; fallback if none staged. */
void ukiyo_profile_apply(void);
#ifdef __cplusplus
}
#endif
#endif
