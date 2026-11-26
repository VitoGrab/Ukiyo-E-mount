#pragma once

#ifndef UKIYO_DEBUG_LEDS
#define UKIYO_DEBUG_LEDS 1
#endif
#if (UKIYO_DEBUG_LEDS != 0) && (UKIYO_DEBUG_LEDS != 1)
#error "UKIYO_DEBUG_LEDS must be 0 or 1"
#endif
#include <stdint.h>
namespace ukiyoemount { namespace emulator {
void boot_auto_arm(uint32_t setup_entry_ms, uint32_t pins_ready_ms);
void record_setup_done(uint32_t done_ms);
// Call after Serial.begin(): USB CDC initializes the RX/TX LED GPIOs.
void status_leds_init();
bool arm();
void poll();
void stop();
void status();
void trace();
void poll_trace();
void clear_trace();
void mark_trace();
bool active();
} }
