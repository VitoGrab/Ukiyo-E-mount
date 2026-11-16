#include <Arduino.h>
#include <string.h>

#include "ukiyo-e-mount/frame.h"
#include "ukiyo-e-mount/techart_sample.h"

// v0.1.0 is a BENCH-ONLY firmware. No camera-facing pin is driven at boot.
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

void print_status() {
  Serial.println("=== ukiyoemount v0.1.0 BENCH ===");
  Serial.print("UPTIME_MS="); Serial.println(millis());
  Serial.print("UART_BAUD="); Serial.println(kUartBaud);
  Serial.print("UART_MODE="); Serial.println(uart_mode ? "LOOPBACK" : "DISABLED");
  Serial.print("UART_SENT="); Serial.println(loop_sent);
  Serial.print("UART_LOOPBACK_FRAMES="); Serial.println(loop_frames);
  Serial.println("CAMERA_TRANSMISSION=DISABLED");
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
    Serial.println("Commands: help, status, proto, uart on, uart off");
    Serial.println("No E-mount handshakes are transmitted in v0.1.0.");
  } else if (!strcmp(cmd, "status")) {
    print_status();
  } else if (!strcmp(cmd, "proto")) {
    protocol_test();
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
    Serial.println("ukiyoemount v0.1.0 READY / SAFE BENCH MODE");
    Serial.println("Type help, proto, status, or uart on (bench only).");
  }
  poll_command();
  poll_uart();
}
