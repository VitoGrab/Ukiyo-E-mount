#include <Arduino.h>
#include "ukiyo-e-mount/techart_port.h"
#include "ukiyo-e-mount/vendor_status.h"
#if UKIYO_TECHART_VENDOR_PRESENT
extern "C" {
#include "emount.h"
#include "em_uart.h"
#include "ukiyo_hal.h"
}
#endif

namespace ukiyoemount { namespace techart {
namespace {
enum Mode { Off, Armed, Running, Fault };
Mode mode = Off;
uint32_t armed_at_ms = 0;
uint32_t session_at_ms = 0;
uint32_t last_print_ms = 0;
uint32_t last_frames_rx = 0;
uint32_t last_frames_tx = 0;
}
bool active() { return mode != Off; }

bool arm() {
#if !UKIYO_TECHART_VENDOR_PRESENT
  Serial.println("TECHART_PORT=UNAVAILABLE_RUN_tools/import_techart.py");
  return false;
#else
  if (active()) {
    Serial.println("TECHART_PORT=ALREADY_ACTIVE_USE_x");
    return false;
  }
  pinMode(A0, INPUT);
  analogReadResolution(10);
  ukiyo_trace_reset();
  mode = Armed;
  armed_at_ms = millis();
  session_at_ms = 0;
  last_print_ms = 0;
  last_frames_rx = last_frames_tx = 0;
  Serial.println("TECHART_MODE=ARMED_WAITING_FOR_LOGIC_VCC_ON_A0");
  Serial.println("TECHART_ACTIVE_UART_TX=YES_AFTER_POWER_DETECTED");
  return true;
#endif
}

void stop() {
#if UKIYO_TECHART_VENDOR_PRESENT
  ukiyo_bus_abort();
#endif
  mode = Off;
  Serial.println("TECHART_MODE=OFF_ALL_CAMERA_OUTPUTS_HIGH_Z");
}

void poll() {
#if UKIYO_TECHART_VENDOR_PRESENT
  if (mode == Off || mode == Fault) return;
  if (mode == Armed) {
    if (millis() - armed_at_ms >= 30000u) {
      mode = Fault;
      Serial.println("TECHART_MODE=ARM_TIMEOUT_30S");
      return;
    }
    if (!ukiyo_supply_ok()) return;
    // Techart's own initial startup, with its request queue and protocol engine.
    // This function blocks while the camera completes the first BODY_CS window.
    // The vendoring tool patches the waits to abort if LOGIC_VCC disappears.
    em_init();
    if (!ukiyo_supply_ok() || !em_t_init_done) {
      ukiyo_bus_abort();
      mode = Fault;
      Serial.println("TECHART_MODE=ABORT_DURING_INITIAL_WINDOW");
      return;
    }
    session_at_ms = millis();
    mode = Running;
    Serial.print("[t_ms="); Serial.print(session_at_ms);
    Serial.println("] TECHART_MODE=RUNNING_PROTOCOL_ENGINE");
  }
  if (mode != Running) return;
  if (!ukiyo_supply_ok()) {
    ukiyo_bus_abort();
    mode = Fault;
    Serial.println("TECHART_MODE=ABORT_LOGIC_VCC_LOST");
    return;
  }
  em_poll();
  const uint32_t now = millis();
  if ((em_frames_rx != last_frames_rx || em_frames_tx != last_frames_tx)
      && now - last_print_ms >= 80u) {
    last_print_ms = now;
    last_frames_rx = em_frames_rx;
    last_frames_tx = em_frames_tx;
    Serial.print("[t_ms="); Serial.print(now);
    Serial.print("] PROTO_RX="); Serial.print(em_frames_rx);
    Serial.print(" TX="); Serial.print(em_frames_tx);
    Serial.print(" ID=0x"); Serial.print(em_last_id, HEX);
    Serial.print(" BAUD="); Serial.println(ukiyo_bus_baud());
  }
#endif
}

void status() {
  Serial.println("=== TECHART PORT v0.4.0 ===");
  Serial.print("UPTIME_MS="); Serial.println(millis());
  Serial.print("MODE=");
  switch (mode) {
    case Armed: Serial.println("ARMED"); break;
    case Running: Serial.println("RUNNING"); break;
    case Fault: Serial.println("FAULT"); break;
    default: Serial.println("OFF"); break;
  }
#if UKIYO_TECHART_VENDOR_PRESENT
  const int adc = analogRead(A0);
  Serial.println("TECHART_VENDOR=IMPORTED_MIT");
  Serial.print("LOGIC_VCC_ADC10="); Serial.println(adc);
  Serial.print("LOGIC_VCC_EST_MV="); Serial.println(adc * 6600L / 1023L);
  Serial.print("LOGIC_VCC_DETECTED="); Serial.println(ukiyo_supply_ok() ? "YES" : "NO");
  Serial.print("UART_BAUD="); Serial.println(ukiyo_bus_baud());
  Serial.print("UART_BYTES="); Serial.println(em_rx_bytes);
  Serial.print("UART_ERRORS="); Serial.println(em_rx_errors);
  Serial.print("RX_FRAMES="); Serial.println(em_frames_rx);
  Serial.print("TX_FRAMES="); Serial.println(em_frames_tx);
  Serial.print("CS_EDGES="); Serial.println(em_cs_edges);
  Serial.print("VD_EDGES="); Serial.println(em_vd_edges);
  Serial.print("HANDSHAKE_DONE="); Serial.println(em_handshake_done);
  Serial.print("LAST_ID=0x"); Serial.println(em_last_id, HEX);
  Serial.print("BAD_FRAMES="); Serial.println(em_bad_frames);
  Serial.print("RX_SKIPPED_BYTES="); Serial.println(em_rx_skipped);
  Serial.print("REQUEST_DROPPED="); Serial.println(em_req_dropped);
  Serial.print("FIRST_BYTE_MS="); Serial.println(em_t_first_byte);
  Serial.print("FIRST_BODY_CS_MS="); Serial.println(em_t_cs_first_high);
  Serial.print("FIRST_VALID_FRAME_MS="); Serial.println(em_t_first_frame);
  Serial.print("INIT_DONE_MS="); Serial.println(em_t_init_done);
  Serial.print("HANDSHAKE_DONE_MS="); Serial.println(em_t_handshake);
  Serial.print("FIRST_STATUS_MS="); Serial.println(em_t_first_status);
  Serial.print("REQUEST_ID_TRACE=");
  const uint8_t id_count = em_id_log_n < EM_ID_LOG_LEN ? em_id_log_n : EM_ID_LOG_LEN;
  for (uint8_t i = 0; i < id_count; ++i) {
    Serial.print(" 0x");
    if (em_id_log[i] < 0x10) Serial.print('0');
    Serial.print(em_id_log[i], HEX);
  }
  Serial.println();
  Serial.print("SESSION_AT_MS="); Serial.println(session_at_ms);
  Serial.println("OPTICS=58MM_F2_FIXED_REPORTED");
  Serial.println("AF_DECLARATION=UPSTREAM_PROFILE_UNVERIFIED");
#else
  Serial.println("TECHART_VENDOR=NOT_IMPORTED");
  Serial.println("ACTION=RUN_python3_tools/import_techart.py");
#endif
}

void trace() {
#if UKIYO_TECHART_VENDOR_PRESENT
  const uint8_t n = ukiyo_trace_count();
  Serial.print("TECHART_CS_TRACE_COUNT="); Serial.println(n);
  for (uint8_t i = 0; i < n; ++i) {
    Serial.print("CS_EDGE_"); Serial.print(i + 1);
    Serial.print("_US="); Serial.print(ukiyo_trace_time(i));
    Serial.print(" LEVEL="); Serial.println(ukiyo_trace_level(i) ? "HIGH" : "LOW");
  }
#else
  Serial.println("TECHART_TRACE=UNAVAILABLE_NO_VENDOR");
#endif
}
} }
