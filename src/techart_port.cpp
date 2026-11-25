#include <Arduino.h>
#include <stdio.h>
#include "ukiyo-e-mount/techart_port.h"
#include "ukiyo-e-mount/vendor_status.h"
#if UKIYO_TECHART_VENDOR_PRESENT
extern "C" {
#include "emount.h"
#include "em_uart.h"
#include "em_eic.h"
#include "ukiyo_hal.h"
}
#endif

namespace ukiyoemount { namespace techart {
namespace {
enum Mode { Off, Armed, Running, Suspended, WakeWaitBodyTx, WakeWaitCsFall, Fault };
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
uint32_t wake_rise_started_us = 0;
uint32_t wake_started_ms = 0;
uint32_t wake_successes = 0;
uint32_t wake_retries = 0;
uint32_t wake_supply_resets = 0;
bool log_suspended_pending = false;
struct TraceDump {
  bool active;
  uint16_t count;
  uint16_t index;
  uint16_t len;
  uint16_t pos;
  char line[224];
};
TraceDump trace_dump = {};
const uint32_t kWakeHandshakeTimeoutUs = 2000000u;
bool supply_event_known = false;
bool last_supply_state = false;
uint32_t completed_sessions = 0;
uint32_t failed_sessions = 0;
#endif
uint32_t boot_setup_entry_ms = 0;
uint32_t boot_pins_ready_ms = 0;
uint32_t boot_setup_done_ms = 0;

#if UKIYO_TECHART_VENDOR_PRESENT
/* After the 0x16 ACK, immediately release UART TX and signal LOW.
 * Do not print or wait here: the camera is polling its lens RX line. */
void on_shutdown_from_body() {
  em_eic_pause();
  em_uart_disable();
  ukiyo_gpio_tx_low();
  ukiyo_gpio_tx_output();
  ukiyo_gpio_tx_unmux();
  ukiyo_gpio_lens_cs(0);
  em_suspend_begin();
  ukiyo_wake_monitor_start();
  mode = Suspended;
  log_suspended_pending = true;
  wake_rise_started_us = 0;
}

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
  em_set_powerdown_handler(on_shutdown_from_body);
  arm_internal();
#endif
}

void record_setup_done(uint32_t done_ms) { boot_setup_done_ms = done_ms; }

bool arm() {
#if !UKIYO_TECHART_VENDOR_PRESENT
  Serial.println("TECHART_PORT=UNAVAILABLE_RUN_tools/import_techart.py");
  return false;
#else
  if (mode == Running || mode == Suspended ||
      mode == WakeWaitBodyTx || mode == WakeWaitCsFall) {
    Serial.println("TECHART_PORT=SESSION_ACTIVE_OR_SUSPENDED_USE_x_TO_STOP");
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
  if (!supply_event_known || powered != last_supply_state) {
    em_trace_supply(powered ? 1u : 0u, ukiyo_supply_adc());
    last_supply_state = powered;
    supply_event_known = true;
  }
  em_trace_pin_probe();
  if (log_suspended_pending) {
    log_suspended_pending = false;
    log_mode("SUSPENDED_AFTER_0x16_TX_LOW");
  }

  /* Standby is an electrically signaled state, not a loss of VCC.
   * The body may retain LOGIC_VCC even after switching the camera off. */
  if (mode == Suspended || mode == WakeWaitBodyTx ||
      mode == WakeWaitCsFall) {
    if (!powered) {
      ++wake_supply_resets;
      ukiyo_bus_abort();
      arm_internal();
      log_mode("SUSPEND_POWER_LOSS_COLD_REARM");
      return;
    }
    if (mode == Suspended && ukiyo_wake_monitor_triggered()) {
      wake_rise_started_us = ukiyo_wake_monitor_rise_us();
      wake_started_ms = millis();
      mode = WakeWaitBodyTx;
    }
    if (mode == WakeWaitBodyTx) {
      if (!ukiyo_body_cs_high() ||
          (uint32_t)(micros() - wake_rise_started_us) >
              kWakeHandshakeTimeoutUs) {
        ++wake_retries;
        ukiyo_wake_monitor_stop();
        ukiyo_gpio_lens_cs(0);
        ukiyo_wake_monitor_start();
        mode = Suspended;
        return;
      }
      /* Sony's short wake: BODY_CS high -> LENS_CS high ->
       * body TX high -> lens TX high -> BODY_CS low -> LENS_CS low. */
      if (ukiyo_body_tx_high()) {
        em_uart_resume();                 // Retain the 1.5 Mbaud setting.
        mode = WakeWaitCsFall;
      }
    }
    if (mode == WakeWaitCsFall) {
      if (!ukiyo_body_cs_high()) {
        delayMicroseconds(200);
        ukiyo_gpio_lens_cs(0);
        ukiyo_wake_monitor_stop();
        em_suspend_woke();
        em_eic_resume();
        mode = Running;
        ++wake_successes;
        handshake_reported = false;
        log_mode("SHORT_WAKE_PIN_HANDSHAKE_DONE");
        return;
      }
      if ((uint32_t)(micros() - wake_rise_started_us) >
          kWakeHandshakeTimeoutUs) {
        ++wake_retries;
        em_uart_disable();
        ukiyo_gpio_tx_low();
        ukiyo_gpio_tx_output();
        ukiyo_gpio_lens_cs(0);
        ukiyo_wake_monitor_stop();
        ukiyo_wake_monitor_start();
        mode = Suspended;
      }
    }
    return;
  }

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
  if (mode != Running) return; // A 0x16 can transition to standby.

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
    case Suspended: Serial.println("SUSPENDED"); break;
    case WakeWaitBodyTx: Serial.println("WAKE_WAIT_BODY_TX"); break;
    case WakeWaitCsFall: Serial.println("WAKE_WAIT_CS_FALL"); break;
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
  Serial.print("SHUTDOWN_ACKED="); Serial.println(em_shutdown_acked);
  Serial.print("SHUTDOWN_PL0=0x"); Serial.println(em_m16_pl0, HEX);
  Serial.print("SHUTDOWN_AT_MS="); Serial.println(em_t_shutdown);
  Serial.print("IGNORED_AFTER_SHUTDOWN="); Serial.println(em_ignored_after_shutdown);
  Serial.print("SLEEP_COUNT="); Serial.println(em_sleep_n);
  Serial.print("WAKE_COUNT="); Serial.println(em_wake_n);
  Serial.print("WAKE_FIRST_ID=0x"); Serial.println(em_wake_first_id, HEX);
  Serial.print("WAKE_AT_MS="); Serial.println(em_t_wake);
  Serial.print("PROTOCOL_TRACE_COUNT="); Serial.println(em_trace_count());
  Serial.print("PROTOCOL_TRACE_TRIGGERED="); Serial.println(em_trace_triggered());
  Serial.print("PROTOCOL_TRACE_CS_OMITTED="); Serial.println(em_trace_cs_omitted());
  Serial.print("PROTOCOL_TRACE_FROZEN="); Serial.println(em_trace_frozen());
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
  Serial.print("WAKE_SUCCESS_COUNT="); Serial.println(wake_successes);
  Serial.print("WAKE_RETRY_COUNT="); Serial.println(wake_retries);
  Serial.print("WAKE_COLD_REINIT_COUNT="); Serial.println(wake_supply_resets);
  Serial.print("WAKE_RISE_AT_US="); Serial.println(wake_rise_started_us);
  Serial.print("WAKE_START_AT_MS="); Serial.println(wake_started_ms);
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

/* Request a snapshot of the available trace. Unlike the old implementation,
 * this is permitted while RUNNING; transmission is handled in short,
 * nonblocking USB CDC chunks by poll_trace(). */
void trace() {
#if UKIYO_TECHART_VENDOR_PRESENT
  if (!Serial) return;
  trace_dump.active = false;
  const uint16_t n = em_trace_count();
  Serial.print("PROTOCOL_TRACE_COUNT="); Serial.println(n);
  Serial.print("PROTOCOL_TRACE_FROZEN="); Serial.println(em_trace_frozen());
  Serial.print("PROTOCOL_TRACE_TRIGGERED="); Serial.println(em_trace_triggered());
  Serial.print("PROTOCOL_TRACE_CS_OMITTED="); Serial.println(em_trace_cs_omitted());
  Serial.println("PIN_MASK_BITS=BODY_CS:1 BODY_VD:2 BODY_TX_TO_XIAO_RX:4 XIAO_TX_TO_BODY_RX:8 LENS_CS:16");
  Serial.println("PROTOCOL_TRACE_STREAM=NONBLOCKING_SNAPSHOT");
  trace_dump.count = n;
  trace_dump.index = 0;
  trace_dump.len = 0;
  trace_dump.pos = 0;
  trace_dump.active = n > 0;
#else
  Serial.println("PROTOCOL_TRACE=UNAVAILABLE_NO_VENDOR");
#endif
}

void poll_trace() {
#if UKIYO_TECHART_VENDOR_PRESENT
  if (!trace_dump.active || !Serial) return;
  if (trace_dump.pos == trace_dump.len) {
    if (trace_dump.index >= trace_dump.count) {
      trace_dump.active = false;
      return;
    }
    struct em_trace_event e;
    if (!em_trace_get(trace_dump.index++, &e)) {
      trace_dump.active = false;
      return;
    }
    const size_t cap = sizeof(trace_dump.line);
    int written = snprintf(trace_dump.line, cap,
       "EVT=%u T_MS=%lu T_US=%lu KIND=%u",
       (unsigned)(trace_dump.index - 1u),
       (unsigned long)e.t_ms, (unsigned long)e.t_us, (unsigned)e.kind);
    if (written < 0 || (size_t)written >= cap) {
      trace_dump.active = false;
      return;
    }
    size_t used = (size_t)written;
    if (e.kind >= EM_TRACE_CS_RISE && e.kind <= EM_TRACE_IDLE) {
      written = snprintf(trace_dump.line + used, cap - used,
                         " PINS=0x%X", (unsigned)e.length);
    } else {
      written = snprintf(trace_dump.line + used, cap - used,
                         " LEN_OR_ADC=%u CLASS=0x%X ID=0x%X",
                         (unsigned)e.length, (unsigned)e.msg_class,
                         (unsigned)e.id);
    }
    if (written < 0 || (size_t)written >= cap - used) {
      trace_dump.active = false;
      return;
    }
    used += (size_t)written;
    if (used + 3u + (size_t)e.sample_len * 3u >= cap) {
      trace_dump.active = false;
      return;
    }
    trace_dump.line[used++] = ' ';
    trace_dump.line[used++] = 'H';
    trace_dump.line[used++] = '=';
    static const char hex[] = "0123456789ABCDEF";
    for (uint8_t k = 0; k < e.sample_len; ++k) {
      trace_dump.line[used++] = hex[e.sample[k] >> 4];
      trace_dump.line[used++] = hex[e.sample[k] & 15u];
      trace_dump.line[used++] = ' ';
    }
    trace_dump.line[used++] = '\n';
    trace_dump.len = (uint16_t)used;
    trace_dump.pos = 0;
  }

  /* Never block waiting for USB space. Main bus work always runs first. */
  const int space = Serial.availableForWrite();
  if (space <= 0) return;
  uint16_t remaining = trace_dump.len - trace_dump.pos;
  size_t chunk = (size_t)space;
  if (chunk > 32u) chunk = 32u;
  if (chunk > remaining) chunk = remaining;
  const size_t sent = Serial.write(
      (const uint8_t *)trace_dump.line + trace_dump.pos, chunk);
  trace_dump.pos += (uint16_t)sent;
#endif
}

void mark_trace() {
#if UKIYO_TECHART_VENDOR_PRESENT
  if (!em_trace_triggered()) {
    Serial.println("PROTOCOL_TRACE_NOT_TRIGGERED_WAIT_FOR_0x16");
    return;
  }
  if (em_trace_frozen()) {
    Serial.println("PROTOCOL_TRACE_FROZEN_USE_tr");
    return;
  }
  em_trace_user_mark();
  Serial.println("PROTOCOL_TRACE_USER_MARK_ADDED");
#else
  Serial.println("PROTOCOL_TRACE_UNAVAILABLE");
#endif
}
void clear_trace() {
#if UKIYO_TECHART_VENDOR_PRESENT
  em_trace_clear();
  Serial.println("PROTOCOL_TRACE_CLEARED");
#else
  Serial.println("PROTOCOL_TRACE_UNAVAILABLE");
#endif
}
} }
