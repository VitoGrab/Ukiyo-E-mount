#pragma once
#include <stdint.h>
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
uint32_t ukiyo_bus_baud(void);
void ukiyo_trace_reset(void);
uint8_t ukiyo_trace_count(void);
uint32_t ukiyo_trace_time(uint8_t i);
uint8_t ukiyo_trace_level(uint8_t i);
