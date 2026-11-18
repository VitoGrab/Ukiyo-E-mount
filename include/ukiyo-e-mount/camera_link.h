#pragma once
#include <stdint.h>

namespace ukiyoemount {
namespace camera_link {

// RX-only real-camera diagnostics. Neither mode transmits UART responses.
// Listen: lens CS high-impedance.
// Probe: timed two-edge lens CS sequence based on LexOptical's observations;
//        requires an explicitly connected, electrically checked D10 output.
bool start(bool probe_handshake);
void stop();
void poll();
void status();
void trace();
bool active();
bool timing_critical();

} // namespace camera_link
} // namespace ukiyoemount
