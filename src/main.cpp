#include <Arduino.h>
#include <string.h>

#include "ukiyo-e-mount/frame.h"
#include "ukiyo-e-mount/techart_sample.h"
#include "ukiyo-e-mount/session.h"
#include "ukiyo-e-mount/bench.h"

// v0.2.0 is a BENCH-ONLY firmware. No camera-facing pin is driven at boot.
// 'uart on' drives D6/TX and must NEVER be used while a camera is connected.
// Firmware uses USB CDC for diagnostics; it is not a functional lens yet.

namespace {

constexpr uint8_t kPinVd = 8;
constexpr uint8_t kPinBodyCs = 9;
constexpr uint8_t kPinLensCs = 10;
constexpr uint8_t kPinTx = 6;
constexpr uint8_t kPinRx = 7;
constexpr uint8_t kPinDetectFuture = 3;
constexpr uint32_t kUartBaud = 750000u;

constexpr uint8_t kLoopbackPayload[] = {0xA5, 0x5A, 0xC3, 0x3C};
char command[80] = {};
size_t command_used = 0;
bool uart_mode = false;
bool banner_printed = false;
uint8_t loop_match = 0;
uint32_t loop_frames = 0;
uint32_t loop_sent = 0;
uint32_t last_tx = 0;
uint32_t last_led = 0;
bool led_on = false;

void safe_pins() {
  pinMode(kPinVd, INPUT);
  pinMode(kPinBodyCs, INPUT);
  pinMode(kPinLensCs, INPUT);
  pinMode(kPinTx, INPUT);
  pinMode(kPinRx, INPUT);
  pinMode(kPinDetectFuture, INPUT);
}

void print_hex(const uint8_t* buf, size_t n) {
  const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < n; ++i) {
    if (i) Serial.print(' ');
    Serial.print(hex[(buf[i] >> 4) & 15]);
    Serial.print(hex[buf[i] & 15]);
  }
  Serial.println();
}

bool identity_frame(uint8_t* target, size_t& n) {
  return ukiyoemount::encode(0x02, 0x00, 0x07,
                         ukiyoemount::kTechartIdentityPayload,
                         ukiyoemount::kTechartIdentityPayloadSize,
                         target, ukiyoemount::kMaxFrame, n);
}

void protocol_test() {
  uint8_t request[ukiyoemount::kMaxFrame] = {};
  uint8_t reply[ukiyoemount::kMaxFrame] = {};
  size_t request_len = 0;
  size_t reply_len = 0;
  const uint8_t body_data[] = {0x00};
  const bool built_request = ukiyoemount::encode(0x02, 0, 0x07, body_data, 1,
                                           request, sizeof(request), request_len);
  const bool built_reply = identity_frame(reply, reply_len);
  ukiyoemount::FrameView a = {};
  ukiyoemount::FrameView b = {};
  const ukiyoemount::ParseError req_result = built_request
      ? ukiyoemount::decode(request, request_len, a)
      : ukiyoemount::ParseError::InvalidLength;
  const ukiyoemount::ParseError rep_result = built_reply
      ? ukiyoemount::decode(reply, reply_len, b)
      : ukiyoemount::ParseError::InvalidLength;

  Serial.print("REQUEST_STATUS="); Serial.println(ukiyoemount::error_string(req_result));
  Serial.print("REPLY_STATUS="); Serial.println(ukiyoemount::error_string(rep_result));
  if (req_result == ukiyoemount::ParseError::Ok) {
    Serial.print("REQUEST_ID=0x"); Serial.println(a.id, HEX);
    Serial.print("REQUEST_BYTES="); Serial.println(request_len);
  }
  if (rep_result == ukiyoemount::ParseError::Ok) {
    Serial.print("REPLY_ID=0x"); Serial.println(b.id, HEX);
    Serial.print("REPLY_BYTES="); Serial.println(reply_len);
    Serial.print("REPLY_LENS_ID=");
    Serial.println(static_cast<unsigned>(b.payload[9]) |
                   (static_cast<unsigned>(b.payload[10]) << 8));
    Serial.print("REPLY_FRAME="); print_hex(reply, reply_len);
  }
  uint8_t mutated[ukiyoemount::kMaxFrame] = {};
  memcpy(mutated, reply, reply_len);
  if (reply_len > 10) mutated[10] ^= 1;
  ukiyoemount::FrameView corrupted = {};
  const ukiyoemount::ParseError bad = ukiyoemount::decode(mutated, reply_len, corrupted);
  Serial.print("CORRUPTION_REJECTED=");
  Serial.println(bad == ukiyoemount::ParseError::InvalidChecksum ? "PASS" : "FAIL");
  Serial.println("PROTOCOL_TEST=USB_ONLY_NO_CAMERA_TRANSMISSION");
}

// Reference response sizes for the offline fixtures (payload bytes only).
size_t expected_reply_payload_length(uint8_t id) {
  switch (id) {
    case 0x01: return 32;
    case 0x07: return 34;
    case 0x0C: return 1;
    case 0x0B: return 2;
    case 0x08: return 201;
    case 0x09: return 11;
    case 0x0D: return 1;
    case 0x10: return 1;
    case 0x0A: return 16;
    case 0x3F: return 65;
    case 0x3D: return 63;
    default: return 0;
  }
}

// Exercise complete synthetic INIT transactions in memory, over USB only.
// The original Techart AF descriptor remains a fixture, never a camera profile.
bool simulated_session(bool extended) {
  const uint8_t a6000[] = {0x01, 0x07, 0x0C, 0x0B,
                            0x08, 0x09, 0x0D, 0x10, 0x0A};
  const uint8_t a9ii[] = {0x01, 0x07, 0x0C, 0x0B, 0x3F, 0x3D,
                           0x08, 0x09, 0x0D, 0x10, 0x0A};
  const uint8_t* ids = extended ? a9ii : a6000;
  const size_t count = extended ? sizeof(a9ii) : sizeof(a6000);
  ukiyoemount::LensSession session;
  uint8_t req[ukiyoemount::kMaxFrame] = {};
  uint8_t rep[ukiyoemount::kMaxFrame] = {};
  uint8_t payload[32] = {};
  size_t req_size = 0, rep_size = 0;
  bool ok = true;
  Serial.print("SESSION_PROFILE=");
  Serial.println(extended ? "A9II_EXTENDED_SYNTHETIC" : "A6000_SYNTHETIC");
  for (size_t i = 0; i < count; ++i) {
    memset(payload, 0, sizeof(payload));
    if (ids[i] == 0x0C) payload[0] = 0x02;
    const size_t n = ukiyoemount::request_payload_length(ids[i]);
    const bool built = ukiyoemount::encode(0x02, 0, ids[i], payload, n,
                                            req, sizeof(req), req_size);
    const ukiyoemount::SessionResult result = built
        ? session.accept(req, req_size, rep, sizeof(rep), rep_size)
        : ukiyoemount::SessionResult::InvalidFrame;
    ukiyoemount::FrameView decoded = {};
    const bool good = result == ukiyoemount::SessionResult::Ok &&
        ukiyoemount::decode(rep, rep_size, decoded) == ukiyoemount::ParseError::Ok &&
        decoded.id == ids[i] && decoded.message_class == 0x02 &&
        decoded.payload_size == expected_reply_payload_length(ids[i]);
    if (!good) ok = false;
    Serial.print("INIT_ID=0x"); Serial.print(ids[i], HEX);
    Serial.print(" RESULT="); Serial.print(ukiyoemount::session_result_string(result));
    Serial.print(" REPLY_BYTES="); Serial.println(rep_size);
    if (!good) break;
  }
  Serial.print("SIM_ACCEPTED="); Serial.println(session.accepted());
  Serial.print("SIM_BAUD="); Serial.println(session.baud());
  Serial.print("SIM_STAGE="); Serial.println(ukiyoemount::session_step_string(session.step()));
  Serial.print("SESSION_TEST="); Serial.println(ok && session.ready() &&
      session.accepted() == count ? "PASS" : "FAIL");
  Serial.println("BUS_MODE=USB_ONLY_GPIO_HIGH_Z");
  return ok && session.ready() && session.accepted() == count;
}

void negative_session_test() {
  ukiyoemount::LensSession session;
  uint8_t req[ukiyoemount::kMaxFrame] = {};
  uint8_t rep[ukiyoemount::kMaxFrame] = {};
  size_t req_size = 0, rep_size = 0;
  const uint8_t payload[] = {0x00};
  ukiyoemount::encode(0x02, 0, 0x07, payload, sizeof(payload),
                        req, sizeof(req), req_size);
  const bool wrong_order = session.accept(req, req_size, rep, sizeof(rep), rep_size)
      == ukiyoemount::SessionResult::UnexpectedMessage;
  req[6] ^= 0x80;
  const bool bad_checksum = session.accept(req, req_size, rep, sizeof(rep), rep_size)
      == ukiyoemount::SessionResult::InvalidFrame;
  Serial.print("BAD_ORDER_REJECTED="); Serial.println(wrong_order ? "PASS" : "FAIL");
  Serial.print("BAD_CHECKSUM_REJECTED="); Serial.println(bad_checksum ? "PASS" : "FAIL");
  Serial.print("STATE_UNCHANGED="); Serial.println(session.accepted() == 0 &&
      session.step() == ukiyoemount::SessionStep::Cold ? "PASS" : "FAIL");
  Serial.println("BUS_MODE=USB_ONLY_GPIO_HIGH_Z");
}

void print_status() {
  Serial.println("=== ukiyoemount v0.2.1 BENCH ===");
  Serial.print("UPTIME_MS="); Serial.println(millis());
  Serial.print("UART_BAUD="); Serial.println(kUartBaud);
  Serial.print("UART_MODE="); Serial.println(uart_mode ? "LOOPBACK" : "DISABLED");
  Serial.print("UART_SENT="); Serial.println(loop_sent);
  Serial.print("UART_LOOPBACK_FRAMES="); Serial.println(loop_frames);
  Serial.println("CAMERA_TRANSMISSION=DISABLED");
  Serial.println("OFFLINE_SESSION_ENGINE=ENABLED");
}

void stop_uart() {
  if (uart_mode) {
    Serial1.flush();
    Serial1.end();
    uart_mode = false;
  }
  safe_pins();
  Serial.println("UART=OFF_GPIO_INPUTS");
}

void start_uart() {
  // Explicit bench-only mode. The caller must disconnect the camera first.
  stop_uart();
  loop_match = 0;
  loop_frames = 0;
  loop_sent = 0;
  Serial1.begin(kUartBaud);
  uart_mode = true;
  last_tx = millis();
  Serial.println("UART=ON_BENCH_ONLY_CONNECT_D6_TO_D7");
}

void handle_command(const char* cmd) {
  if (!strcmp(cmd, "help")) {
    Serial.println("Commands: help, status, proto, session a6000, session a9ii, session faults, uart on, uart off, bench gpio, bench a6000, bench a9ii");
    Serial.println("Offline sessions are memory-only. BENCH commands drive pins; NEVER attach a camera.");
  } else if (!strcmp(cmd, "status")) {
    print_status();
  } else if (!strcmp(cmd, "proto")) {
    protocol_test();
  } else if (!strcmp(cmd, "session a6000")) {
    if (uart_mode) stop_uart();
    simulated_session(false);
  } else if (!strcmp(cmd, "session a9ii")) {
    if (uart_mode) stop_uart();
    simulated_session(true);
  } else if (!strcmp(cmd, "session faults")) {
    if (uart_mode) stop_uart();
    negative_session_test();
  } else if (!strcmp(cmd, "bench gpio")) {
    if (uart_mode) stop_uart();
    ukiyoemount::bench_gpio_test();
  } else if (!strcmp(cmd, "bench a6000")) {
    if (uart_mode) stop_uart();
    ukiyoemount::bench_uart_session(false);
  } else if (!strcmp(cmd, "bench a9ii")) {
    if (uart_mode) stop_uart();
    ukiyoemount::bench_uart_session(true);
  } else if (!strcmp(cmd, "uart on")) {
    start_uart();
  } else if (!strcmp(cmd, "uart off")) {
    stop_uart();
  } else if (cmd[0]) {
    Serial.println("UNKNOWN_COMMAND (type help)");
  }
}

void poll_command() {
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (command_used) {
        command[command_used] = '\0';
        handle_command(command);
        command_used = 0;
      }
    } else if (ch >= 32 && ch <= 126) {
      if (command_used + 1 < sizeof(command)) {
        command[command_used++] = static_cast<char>(ch);
      } else {
        command_used = 0;
        Serial.println("COMMAND_TOO_LONG");
      }
    }
  }
}

void poll_uart() {
  if (!uart_mode) return;
  const uint32_t now = millis();
  if (now - last_tx >= 1000) {
    last_tx = now;
    Serial1.write(kLoopbackPayload, sizeof(kLoopbackPayload));
    ++loop_sent;
  }
  while (Serial1.available()) {
    const int val = Serial1.read();
    if (val == kLoopbackPayload[loop_match]) {
      ++loop_match;
      if (loop_match == sizeof(kLoopbackPayload)) {
        ++loop_frames;
        loop_match = 0;
      }
    } else {
      loop_match = (val == kLoopbackPayload[0]) ? 1 : 0;
    }
  }
}

}  // namespace

void setup() {
  safe_pins();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
}

void loop() {
  const uint32_t now = millis();
  if (now - last_led >= 500) {
    last_led = now;
    led_on = !led_on;
    digitalWrite(LED_BUILTIN, led_on ? LOW : HIGH);
  }
  if (Serial && !banner_printed) {
    banner_printed = true;
    Serial.println("ukiyoemount v0.2.1 READY / SAFE BOOT");
    Serial.println("Type help, proto, session a6000, session a9ii, status.");
  }
  poll_command();
  poll_uart();
}
