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
bool automatic_enabled = false;
#if UKIYO_TECHART_VENDOR_PRESENT
uint32_t armed_at_ms = 0;
uint32_t session_at_ms = 0;
uint32_t init_started_ms = 0;
uint32_t last_frames_rx = 0;
uint32_t last_frames_tx = 0;
uint8_t last_reported_id = 0;
bool handshake_reported = false;
uint32_t completed_sessions = 0;
uint32_t failed_sessions = 0;
#endif
uint32_t boot_setup_entry_ms = 0;
uint32_t boot_pins_ready_ms = 0;
uint32_t boot_setup_done_ms = 0;

#if UKIYO_TECHART_VENDOR_PRESENT
void arm_internal() {
  mode = Armed;
  automatic_enabled = true;
  armed_at_ms = millis();
  session_at_ms = 0;
  init_started_ms = 0;
  last_frames_rx = last_frames_tx = 0;
  last_reported_id = 0;
  handshake_reported = false;
}

void log_mode(const char* what) {
  if (!Serial) return;  // Never block camera initialization on USB CDC.
  Serial.print("[t_ms="); Serial.print(millis());
  Serial.print("] TECHART_MODE="); Serial.println(what);
}
#endif
}  // namespace

bool active() { return mode != Off; }

// Called by setup() before USB CDC is initialized. There is no 30-second arm
// timeout: this is the normal, persistent operating state of a lens adapter.
void boot_auto_arm(uint32_t setup_entry_ms, uint32_t pins_ready_ms) {
  boot_setup_entry_ms = setup_entry_ms;
  boot_pins_ready_ms = pins_ready_ms;
#if UKIYO_TECHART_VENDOR_PRESENT
  ukiyo_supply_init();
  arm_internal();
#endif
}

void record_setup_done(uint32_t done_ms) { boot_setup_done_ms = done_ms; }

bool arm() {
#if !UKIYO_TECHART_VENDOR_PRESENT
  Serial.println("TECHART_PORT=UNAVAILABLE_RUN_tools/import_techart.py");
  return false;
#else
  if (mode == Running) {
    Serial.println("TECHART_PORT=RUNNING_USE_x_TO_STOP");
    return false;
  }
  if (mode == Armed) {
    Serial.println("TECHART_MODE=ALREADY_ARMED_AUTO");
    return true;
  }
  ukiyo_bus_abort();
  // Do not reset the supply history just because a monitor command was typed.
  // The next sample reasserts the cached state if LOGIC_VCC is truly present.
  arm_internal();
  Serial.println("TECHART_MODE=ARMED_WAITING_FOR_LOGIC_VCC_ON_A0");
  return true;
#endif
}

void stop() {
#if UKIYO_TECHART_VENDOR_PRESENT
  ukiyo_bus_abort();
#endif
  automatic_enabled = false;
  mode = Off;
  Serial.println("TECHART_MODE=OFF_ALL_CAMERA_OUTPUTS_HIGH_Z");
  Serial.println("AUTO_ARM=DISABLED_UNTIL_e_OR_RESET");
}

void poll() {
#if UKIYO_TECHART_VENDOR_PRESENT
  if (mode == Off) return;
  const bool powered = ukiyo_supply_ok();

  if (mode == Fault) {
    // Retry only after a *confirmed* power-off, never while a failing body
    // keeps its supply active. This avoids repeatedly driving LENS_CS.
    if (automatic_enabled && !powered) {
      arm_internal();
      log_mode("AUTO_REARMED_WAITING_FOR_POWER");
    }
    return;
  }
  if (mode == Armed) {
    if (!powered) return;
    init_started_ms = millis();
    // These fields describe one transaction session. Techart's protocol engine
    // still owns its request queues and shutdown/wake state (0x16 -> 0x01).
    em_handshake_done = 0;
    em_t_handshake = 0;
    em_t_first_frame = 0;
    em_t_first_status = 0;
    // Techart's original protocol start, with its tested CS timings and queue.
    // The vendoring tool guards the blocking wait against sustained power loss.
    em_init();
    if (!ukiyo_supply_ok() || !em_t_init_done) {
      ukiyo_bus_abort();
      ++failed_sessions;
      mode = Fault;
      log_mode("ABORT_DURING_INITIAL_WINDOW");
      return;
    }
    session_at_ms = millis();
    ++completed_sessions;
    mode = Running;
    last_frames_rx = em_frames_rx;
    last_frames_tx = em_frames_tx;
    last_reported_id = em_last_id;
    handshake_reported = false;
    log_mode("RUNNING_PROTOCOL_ENGINE");
  }
  if (mode != Running) return;
  if (!ukiyo_supply_ok()) {
    ukiyo_bus_abort();
    ++failed_sessions;
    mode = Fault;
    log_mode("ABORT_CONFIRMED_LOGIC_VCC_LOSS");
    return;
  }
  em_poll();

  // Keepalive 0x0A is deliberately silent; all counters remain in 's'.
  // Other IDs and the completed handshake are visible in the monitor.
  if (!handshake_reported && em_handshake_done) {
    handshake_reported = true;
    log_mode("HANDSHAKE_DONE");
  }
  if (em_frames_rx != last_frames_rx || em_frames_tx != last_frames_tx) {
    const uint8_t id = em_last_id;
    if (id != 0x0Au && Serial &&
        (id != last_reported_id || em_frames_rx != last_frames_rx)) {
      Serial.print("[t_ms="); Serial.print(millis());
      Serial.print("] PROTO_RX="); Serial.print(em_frames_rx);
      Serial.print(" TX="); Serial.print(em_frames_tx);
      Serial.print(" ID=0x"); Serial.print(id, HEX);
      Serial.print(" BAUD="); Serial.println(ukiyo_bus_baud());
    }
    last_reported_id = id;
    last_frames_rx = em_frames_rx;
    last_frames_tx = em_frames_tx;
  }
#endif
}

void status() {
  Serial.println("=== TECHART PORT v0.4.1 ===");
  Serial.print("UPTIME_MS="); Serial.println(millis());
  Serial.print("MODE=");
  switch (mode) {
    case Armed: Serial.println("ARMED"); break;
    case Running: Serial.println("RUNNING"); break;
    case Fault: Serial.println("FAULT"); break;
    default: Serial.println("OFF"); break;
  }
  Serial.print("AUTO_ARM="); Serial.println(automatic_enabled ? "ENABLED" : "DISABLED");
  Serial.print("BOOT_SETUP_ENTRY_MS="); Serial.println(boot_setup_entry_ms);
  Serial.print("BOOT_PINS_READY_MS="); Serial.println(boot_pins_ready_ms);
  Serial.print("BOOT_SETUP_DONE_MS="); Serial.println(boot_setup_done_ms);
#if UKIYO_TECHART_VENDOR_PRESENT
  ukiyo_supply_ok();
  const int adc = ukiyo_supply_adc();
  Serial.println("TECHART_VENDOR=IMPORTED_MIT");
  Serial.print("LOGIC_VCC_ADC10="); Serial.println(adc);
  Serial.print("LOGIC_VCC_EST_MV="); Serial.println(adc * 6600L / 1023L);
  Serial.print("LOGIC_VCC_DETECTED="); Serial.println(ukiyo_supply_ok() ? "YES" : "NO");
  Serial.print("LOGIC_VCC_MIN_ADC10="); Serial.println(ukiyo_supply_adc_min());
  Serial.print("LOGIC_VCC_MAX_ADC10="); Serial.println(ukiyo_supply_adc_max());
  Serial.print("LOGIC_VCC_RISES="); Serial.println(ukiyo_supply_rises());
  Serial.print("LOGIC_VCC_FALLS="); Serial.println(ukiyo_supply_falls());
  Serial.print("LOGIC_VCC_LAST_ON_US="); Serial.println(ukiyo_supply_last_on_us());
  Serial.print("LOGIC_VCC_LAST_OFF_US="); Serial.println(ukiyo_supply_last_off_us());
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
  Serial.print("INIT_STARTED_MS="); Serial.println(init_started_ms);
  Serial.print("INIT_DURATION_MS=");
  Serial.println(session_at_ms && init_started_ms ? (session_at_ms - init_started_ms) : 0);
  Serial.print("SESSION_COUNT="); Serial.println(completed_sessions);
  Serial.print("FAILED_SESSIONS="); Serial.println(failed_sessions);
  Serial.println("OPTICS=COMPILE_TIME_LENS_PROFILE");
  Serial.println("PROFILE_CONFIG=config/lens_profile.json");
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
