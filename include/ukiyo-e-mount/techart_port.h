#pragma once
#include <stdint.h>
namespace ukiyoemount { namespace techart {
void boot_auto_arm(uint32_t setup_entry_ms, uint32_t pins_ready_ms);
void record_setup_done(uint32_t done_ms);
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
