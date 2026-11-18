#include <Arduino.h>
#include "ukiyo-e-mount/camera_link.h"
#include "ukiyo-e-mount/frame.h"

namespace ukiyoemount {
namespace camera_link {
namespace {
constexpr uint8_t kPinTx = 6;       // XIAO D6 -> Sony E pin 7; never driven by camera modes
constexpr uint8_t kPinRx = 7;       // XIAO D7 <- Sony E pin 8
constexpr uint8_t kPinVd = 8;       // XIAO D8 <- Sony E pin 4
constexpr uint8_t kPinBodyCs = 9;   // XIAO D9 <- Sony E pin 9
constexpr uint8_t kPinLensCs = 10;  // XIAO D10 -> Sony E pin 6; probe only
constexpr uint8_t kPinDetect = 3;   // Reserved GPIO; physical XDETECT uses external 680 ohm
constexpr uint32_t kInitialBaud = 750000u;

// LexOptical observed these two transitions on a NEX-7. They are a starting
// point for A6000 timing measurements, not a verified universal specification.
constexpr uint32_t kFirstDelayUs = 990u;
constexpr uint32_t kSecondDelayUs = 305u;
constexpr uint32_t kArmTimeoutMs = 30000u;     // Enough time to start the camera manually.
constexpr uint32_t kSecondEdgeTimeoutUs = 50000u; // Limit a driven-HIGH pulse.
constexpr uint32_t kLowHoldUs = 2000u;         // Observe falling CS, then float the output.
constexpr uint32_t kRxSettleUs = 30u;
constexpr uint32_t kQuietMs = 2000u;
constexpr size_t kCapture = kMaxFrame + 32;
constexpr uint8_t kTraceCount = 16;

enum class ProbeStep : uint8_t {
  Off, Listening, AwaitFirst, WaitFirstDelay, AwaitSecond,
  WaitSecondDelay, HoldLow, Complete, FirstTimeout, SecondTimeout
};

volatile uint32_t g_cs_edges = 0;
volatile uint32_t g_vd_edges = 0;
volatile uint32_t g_cs_fall_us = 0;
volatile bool g_window_closed = false;
volatile ProbeStep g_step = ProbeStep::Off;
volatile uint32_t g_first_edge_us = 0;
volatile uint32_t g_second_edge_us = 0;
volatile uint32_t g_trace_times[kTraceCount] = {};
volatile uint8_t g_trace_levels[kTraceCount] = {};
volatile uint8_t g_trace_used = 0;

bool g_running = false;
bool g_probe = false;
bool g_startup_high = false;
bool g_log_pending = false;
uint32_t g_started_ms = 0;
uint32_t g_started_us = 0;
uint32_t g_last_event_ms = 0;
uint32_t g_high_at_us = 0;
uint32_t g_low_at_us = 0;
uint32_t g_release_at_us = 0;
uint8_t g_bytes[kCapture] = {};
size_t g_count = 0;
uint32_t g_frames = 0;
uint32_t g_bad = 0;
uint32_t g_overflows = 0;
uint32_t g_empty_windows = 0;
uint32_t g_bytes_seen = 0;
uint8_t g_last_id = 0;
uint8_t g_last_class = 0;
uint8_t g_last_sequence = 0;

const char* step_name(ProbeStep s) {
  switch (s) {
    case ProbeStep::Off: return "OFF";
    case ProbeStep::Listening: return "LISTENING";
    case ProbeStep::AwaitFirst: return "AWAIT_FIRST_EDGE";
    case ProbeStep::WaitFirstDelay: return "WAIT_990US";
    case ProbeStep::AwaitSecond: return "AWAIT_SECOND_EDGE";
    case ProbeStep::WaitSecondDelay: return "WAIT_305US";
    case ProbeStep::HoldLow: return "HOLD_LOW";
    case ProbeStep::Complete: return "COMPLETE_RX_ONLY";
    case ProbeStep::FirstTimeout: return "TIMEOUT_FIRST_EDGE";
    case ProbeStep::SecondTimeout: return "TIMEOUT_SECOND_EDGE";
  }
  return "UNKNOWN";
}

bool probe_busy(ProbeStep s) {
  return s == ProbeStep::WaitFirstDelay || s == ProbeStep::AwaitSecond ||
         s == ProbeStep::WaitSecondDelay || s == ProbeStep::HoldLow;
}

void on_cs_change() {
  const uint32_t t = micros();
  const uint8_t level = digitalRead(kPinBodyCs);
  ++g_cs_edges;
  if (g_trace_used < kTraceCount) {
    const uint8_t index = g_trace_used;
    g_trace_times[index] = t;
    g_trace_levels[index] = level;
    g_trace_used = static_cast<uint8_t>(index + 1);
  }
  if (level == LOW) {
    g_cs_fall_us = t;
    g_window_closed = true;
  }
  // Do not print or delay inside the interrupt. Only timestamp transitions.
  if (g_probe && g_step == ProbeStep::AwaitFirst) {
    g_first_edge_us = t;
    g_step = ProbeStep::WaitFirstDelay;
  } else if (g_probe && g_step == ProbeStep::AwaitSecond) {
    g_second_edge_us = t;
    g_step = ProbeStep::WaitSecondDelay;
  }
}
void on_vd_rise() { ++g_vd_edges; }

void float_lens_cs() {
  // LOW first, never leave an actively driven HIGH output on a powered-off body.
  digitalWrite(kPinLensCs, LOW);
  pinMode(kPinLensCs, INPUT);
  g_startup_high = false;
}

void release() {
  Serial1.end();
  pinMode(kPinTx, INPUT);
  pinMode(kPinRx, INPUT);
  float_lens_cs();
  pinMode(kPinBodyCs, INPUT);
  pinMode(kPinVd, INPUT);
  pinMode(kPinDetect, INPUT);
}

void print_probe_event() {
  if (!g_log_pending) return;
  const ProbeStep step = g_step;
  if (probe_busy(step)) return;
  g_log_pending = false;
  Serial.print("[t_ms="); Serial.print(millis()); Serial.print("] CAMERA_PROBE=");
  Serial.println(step_name(step));
  if (g_first_edge_us) {
    Serial.print("FIRST_EDGE_US="); Serial.print(g_first_edge_us);
    Serial.print(" HIGH_US="); Serial.print(g_high_at_us);
    Serial.print(" FIRST_LATENCY_US=");
    Serial.println(g_high_at_us ? static_cast<uint32_t>(g_high_at_us - g_first_edge_us) : 0u);
  }
  if (g_second_edge_us) {
    Serial.print("SECOND_EDGE_US="); Serial.print(g_second_edge_us);
    Serial.print(" LOW_US="); Serial.print(g_low_at_us);
    Serial.print(" SECOND_LATENCY_US=");
    Serial.println(g_low_at_us ? static_cast<uint32_t>(g_low_at_us - g_second_edge_us) : 0u);
  }
}

void service_probe() {
  if (!g_probe || !g_running) return;
  const uint32_t now_us = micros();
  switch (g_step) {
    case ProbeStep::AwaitFirst:
      if (static_cast<uint32_t>(millis() - g_started_ms) >= kArmTimeoutMs) {
        float_lens_cs();
        g_step = ProbeStep::FirstTimeout;
        g_log_pending = true;
      }
      break;
    case ProbeStep::WaitFirstDelay:
      if (static_cast<uint32_t>(now_us - g_first_edge_us) >= kFirstDelayUs) {
        // Stay high impedance until a real BODY_CS transition has occurred.
        // Use a series resistor on this active output for the first camera test.
        digitalWrite(kPinLensCs, HIGH);
        pinMode(kPinLensCs, OUTPUT);
        g_high_at_us = micros();
        g_startup_high = true;
        g_step = ProbeStep::AwaitSecond;
      }
      break;
    case ProbeStep::AwaitSecond:
      if (static_cast<uint32_t>(now_us - g_high_at_us) >= kSecondEdgeTimeoutUs) {
        float_lens_cs();
        g_step = ProbeStep::SecondTimeout;
        g_log_pending = true;
      }
      break;
    case ProbeStep::WaitSecondDelay:
      if (static_cast<uint32_t>(now_us - g_second_edge_us) >= kSecondDelayUs) {
        digitalWrite(kPinLensCs, LOW);
        g_low_at_us = micros();
        g_startup_high = false;
        g_step = ProbeStep::HoldLow;
      }
      break;
    case ProbeStep::HoldLow:
      if (static_cast<uint32_t>(now_us - g_low_at_us) >= kLowHoldUs) {
        float_lens_cs();
        g_release_at_us = micros();
        g_step = ProbeStep::Complete;
        g_log_pending = true;
      }
      break;
    default: break;
  }
}

void capture_byte(uint8_t b) {
  ++g_bytes_seen;
  g_last_event_ms = millis();
  if (g_count < sizeof(g_bytes)) g_bytes[g_count++] = b;
  else ++g_overflows;
}

void drain_uart() {
  while (Serial1.available() > 0) {
    const int b = Serial1.read();
    if (b >= 0) capture_byte(static_cast<uint8_t>(b));
  }
}

void parse_window() {
  if (!g_count) { ++g_empty_windows; return; }
  size_t pos = 0;
  bool parsed_any = false;
  while (pos + 9 <= g_count) {
    if (g_bytes[pos] != kFrameStart) { ++pos; continue; }
    const size_t length = static_cast<size_t>(g_bytes[pos + 1]) |
                          (static_cast<size_t>(g_bytes[pos + 2]) << 8);
    if (length < 9 || length > kMaxFrame || length > g_count - pos) {
      ++g_bad;
      ++pos;
      continue;
    }
    FrameView v = {};
    const ParseError error = decode(g_bytes + pos, length, v);
    if (error == ParseError::Ok) {
      ++g_frames;
      g_last_id = v.id;
      g_last_class = v.message_class;
      g_last_sequence = v.sequence;
      if (v.message_class == 0x02 || g_frames <= 4) {
        Serial.print("[t_ms="); Serial.print(millis()); Serial.print("] CAMERA_RX_CLASS=0x");
        Serial.print(v.message_class, HEX);
        Serial.print(" ID=0x"); Serial.print(v.id, HEX);
        Serial.print(" SEQ="); Serial.print(v.sequence);
        Serial.print(" LEN="); Serial.println(v.declared_size);
      }
      parsed_any = true;
      pos += length;
    } else {
      ++g_bad;
      Serial.print("CAMERA_BAD_FRAME="); Serial.println(error_string(error));
      ++pos;
    }
  }
  if (!parsed_any && g_count > 0) {
    Serial.print("CAMERA_UNPARSED_WINDOW_BYTES="); Serial.println(g_count);
  }
}

} // namespace

bool start(bool probe_handshake) {
  if (g_running) return false;
  g_probe = probe_handshake;
  g_running = true;
  g_startup_high = false;
  g_log_pending = false;
  g_window_closed = false;
  g_cs_edges = g_vd_edges = g_cs_fall_us = 0;
  g_first_edge_us = g_second_edge_us = 0;
  g_trace_used = 0;
  g_high_at_us = g_low_at_us = g_release_at_us = 0;
  g_frames = g_bad = g_overflows = g_empty_windows = g_bytes_seen = 0;
  g_count = 0;
  g_last_id = g_last_class = g_last_sequence = 0;
  g_started_ms = millis();
  g_started_us = micros();
  g_last_event_ms = g_started_ms;

  pinMode(kPinVd, INPUT);
  pinMode(kPinBodyCs, INPUT);
  float_lens_cs();
  pinMode(kPinDetect, INPUT);
  Serial1.begin(kInitialBaud);
  pinMode(kPinTx, INPUT);  // No UART transmit into the camera.
  attachInterrupt(digitalPinToInterrupt(kPinBodyCs), on_cs_change, CHANGE);
  attachInterrupt(digitalPinToInterrupt(kPinVd), on_vd_rise, RISING);
  g_step = g_probe ? ProbeStep::AwaitFirst : ProbeStep::Listening;
  if (g_probe) {
    Serial.println("CAMERA_MODE=PROBE_ARMED_WAITING_FOR_BODY_CS");
    Serial.println("CAMERA_UART_TX=DISABLED");
  } else {
    Serial.println("CAMERA_MODE=LISTEN_HIGH_Z_NO_UART_TX");
  }
  return true;
}

void stop() {
  if (!g_running) { Serial.println("CAMERA_NOT_ACTIVE"); return; }
  detachInterrupt(digitalPinToInterrupt(kPinBodyCs));
  detachInterrupt(digitalPinToInterrupt(kPinVd));
  float_lens_cs();
  release();
  g_running = false;
  g_step = ProbeStep::Off;
  Serial.println("CAMERA=STOPPED_ALL_GPIO_INPUT");
}

bool active() { return g_running; }
bool timing_critical() { return g_probe && probe_busy(g_step); }

void poll() {
  if (!g_running) return;
  // Process fast physical transitions before any USB diagnostics or parser work.
  service_probe();
  drain_uart();
  service_probe();
  const uint32_t now = millis();
  bool finished;
  uint32_t fallen;
  noInterrupts();
  finished = g_window_closed;
  fallen = g_cs_fall_us;
  interrupts();
  if (finished && digitalRead(kPinBodyCs) == LOW &&
      static_cast<uint32_t>(micros() - fallen) >= kRxSettleUs && !timing_critical()) {
    drain_uart();
    noInterrupts(); g_window_closed = false; interrupts();
    parse_window();
    g_count = 0;
  }
  if (!timing_critical() && static_cast<uint32_t>(now - g_last_event_ms) > kQuietMs && g_count) {
    g_count = 0;
    ++g_bad;
  }
  if (!timing_critical()) print_probe_event();
}

void status() {
  if (timing_critical()) {
    Serial.println("CAMERA_STATUS_DEFERRED_DURING_HANDSHAKE");
    return;
  }
  Serial.println("=== CAMERA LINK v0.3.1 ===");
  Serial.print("UPTIME_MS="); Serial.println(millis());
  Serial.print("UPTIME_US_MOD_2^32="); Serial.println(micros());
  Serial.print("MODE="); Serial.println(!g_running ? "OFF" : g_probe ? "PROBE" : "LISTEN");
  Serial.print("PROBE_STATE="); Serial.println(step_name(g_step));
  Serial.print("ARMED_AT_MS="); Serial.println(g_started_ms);
  Serial.print("CAMERA_UART_TX=DISABLED\n");
  Serial.print("STARTUP_CS="); Serial.println(g_startup_high ? "HIGH" : "LOW_OR_HIGH_Z");
  Serial.print("CS_EDGES="); Serial.println(g_cs_edges);
  Serial.print("VD_RISES="); Serial.println(g_vd_edges);
  Serial.print("UART_BYTES="); Serial.println(g_bytes_seen);
  Serial.print("VALID_FRAMES="); Serial.println(g_frames);
  Serial.print("BAD_FRAMES="); Serial.println(g_bad);
  Serial.print("BUFFER_OVERFLOWS="); Serial.println(g_overflows);
  Serial.print("EMPTY_WINDOWS="); Serial.println(g_empty_windows);
  Serial.print("LAST_ID=0x"); Serial.println(g_last_id, HEX);
  Serial.print("FIRST_EDGE_US="); Serial.println(g_first_edge_us);
  Serial.print("CS_HIGH_AT_US="); Serial.println(g_high_at_us);
  Serial.print("CS_HIGH_LATENCY_US="); Serial.println(g_high_at_us ? static_cast<uint32_t>(g_high_at_us-g_first_edge_us) : 0u);
  Serial.print("SECOND_EDGE_US="); Serial.println(g_second_edge_us);
  Serial.print("CS_LOW_AT_US="); Serial.println(g_low_at_us);
  Serial.print("CS_LOW_LATENCY_US="); Serial.println(g_low_at_us ? static_cast<uint32_t>(g_low_at_us-g_second_edge_us) : 0u);
  Serial.print("CS_RELEASE_AT_US="); Serial.println(g_release_at_us);
  Serial.println("EMULATOR_RESPONSES=NOT_IMPLEMENTED_IN_V0.3.1");
}

void trace() {
  if (timing_critical()) {
    Serial.println("CAMERA_TRACE_DEFERRED_DURING_HANDSHAKE");
    return;
  }
  uint8_t count;
  noInterrupts(); count = g_trace_used; interrupts();
  Serial.print("CAMERA_TRACE_COUNT="); Serial.println(count);
  for (uint8_t i = 0; i < count; ++i) {
    uint32_t timestamp;
    uint8_t level;
    noInterrupts();
    timestamp = g_trace_times[i];
    level = g_trace_levels[i];
    interrupts();
    Serial.print("CS_EDGE_"); Serial.print(i + 1);
    Serial.print("_US="); Serial.print(timestamp);
    Serial.print(" LEVEL="); Serial.println(level ? "HIGH" : "LOW");
  }
}

} // namespace camera_link
} // namespace ukiyoemount
