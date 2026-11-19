#ifndef UKIYO_PORT_DELAY_H
#define UKIYO_PORT_DELAY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
uint32_t micros(void);
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);
#ifdef __cplusplus
}
#endif
#endif
