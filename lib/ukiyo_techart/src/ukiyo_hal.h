#ifndef UKIYO_PORT_HAL_H
#define UKIYO_PORT_HAL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void ukiyo_supply_init(void);
int ukiyo_supply_ok(void);
uint16_t ukiyo_supply_adc(void);
uint16_t ukiyo_supply_adc_min(void);
uint16_t ukiyo_supply_adc_max(void);
uint32_t ukiyo_supply_rises(void);
uint32_t ukiyo_supply_falls(void);
uint32_t ukiyo_supply_last_on_us(void);
uint32_t ukiyo_supply_last_off_us(void);
void ukiyo_bus_abort(void);
void ukiyo_gpio_lens_cs(int high);
void ukiyo_gpio_body_cs_input(void);
void ukiyo_gpio_body_vd_input(void);
void ukiyo_gpio_tx_low(void);
void ukiyo_gpio_tx_output(void);
void ukiyo_gpio_tx_unmux(void);
#ifdef __cplusplus
}
#endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
uint32_t ukiyo_bus_baud(void);
uint8_t ukiyo_trace_count(void);
uint32_t ukiyo_trace_time(uint8_t i);
uint8_t ukiyo_trace_level(uint8_t i);
void ukiyo_trace_reset(void);
#ifdef __cplusplus
}
#endif
