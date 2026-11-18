#include <Arduino.h>
#include <string.h>

#include "ukiyo-e-mount/bench.h"
#include "ukiyo-e-mount/frame.h"
#include "ukiyo-e-mount/session.h"

namespace ukiyoemount {
namespace {

// Physical XIAO SAMD21 pins; these match the planned adapter wiring.
constexpr uint8_t kLensTx = 6;     // UART TX, Sony E pin 7
constexpr uint8_t kLensRx = 7;     // UART RX, Sony E pin 8
constexpr uint8_t kVdInput = 8;    // BODY_VD_LENS, Sony E pin 4
constexpr uint8_t kBodyCsInput = 9;// BODY_CS_LENS, Sony E pin 9
constexpr uint8_t kLensCsOutput = 10; // LENS_CS_BODY, Sony E pin 6

// Synthetic body stimuli / observer, ONLY in the standalone bench harness.
constexpr uint8_t kMockVd = 1;     // jumper D1 -> D8
constexpr uint8_t kMockBodyCs = 2; // jumper D2 -> D9
constexpr uint8_t kObserveLensCs = 3; // jumper D10 -> D3

constexpr uint32_t kInitialBaud = 750000u;
constexpr uint32_t kNegotiatedBaud = 1500000u;
constexpr uint32_t kByteTimeoutMs = 30u;

void release_pins() {
  Serial1.end();
  pinMode(kLensTx, INPUT);
  pinMode(kLensRx, INPUT);
  pinMode(kVdInput, INPUT);
  pinMode(kBodyCsInput, INPUT);
  pinMode(kLensCsOutput, INPUT);
  pinMode(kMockVd, INPUT);
  pinMode(kMockBodyCs, INPUT);
  pinMode(kObserveLensCs, INPUT);
}

void set_mock_signals() {
  // Set output latches BEFORE changing direction, avoiding a startup glitch.
  digitalWrite(kMockVd, HIGH);     // VD idle high; low pulse is frame sync
  digitalWrite(kMockBodyCs, LOW); // CS idle low
  digitalWrite(kLensCsOutput, LOW);
  pinMode(kVdInput, INPUT_PULLUP);
  pinMode(kBodyCsInput, INPUT_PULLUP);
  pinMode(kObserveLensCs, INPUT_PULLUP);
  pinMode(kMockVd, OUTPUT);
  pinMode(kMockBodyCs, OUTPUT);
  pinMode(kLensCsOutput, OUTPUT);
}

bool expect_level(uint8_t input, int expected) {
  delayMicroseconds(250);
  return digitalRead(input) == expected;
}

bool check_gpio_links() {
  // This checks *electrical connectivity*, not actual E-mount timing.
  bool ok = true;
  digitalWrite(kMockVd, LOW);
  ok = expect_level(kVdInput, LOW) && ok;
  digitalWrite(kMockVd, HIGH);
  ok = expect_level(kVdInput, HIGH) && ok;

  digitalWrite(kMockBodyCs, LOW);
  ok = expect_level(kBodyCsInput, LOW) && ok;
  digitalWrite(kMockBodyCs, HIGH);
  ok = expect_level(kBodyCsInput, HIGH) && ok;
  digitalWrite(kMockBodyCs, LOW);

  digitalWrite(kLensCsOutput, LOW);
  ok = expect_level(kObserveLensCs, LOW) && ok;
  digitalWrite(kLensCsOutput, HIGH);
  ok = expect_level(kObserveLensCs, HIGH) && ok;
  digitalWrite(kLensCsOutput, LOW);
  return ok;
}

// A byte-at-a-time echo intentionally avoids RX ring-buffer overflow for
// the 210-byte Techart descriptor, but creates inter-byte gaps. It proves
// electrical TX/RX and byte integrity, NOT continuous-transfer timing.
bool echoed_uart_bytes(const uint8_t* data, size_t size, uint8_t* output) {
  while (Serial1.available()) Serial1.read();
  for (size_t i = 0; i < size; ++i) {
    if (Serial1.write(data[i]) != 1u) return false;
    const uint32_t started = millis();
    while (!Serial1.available() && millis() - started < kByteTimeoutMs) {
      // Wait for the physical D6->D7 loopback, not for a software echo.
    }
    if (!Serial1.available()) return false;
    const int byte = Serial1.read();
    if (byte < 0 || static_cast<uint8_t>(byte) != data[i]) return false;
    output[i] = static_cast<uint8_t>(byte);
  }
  Serial1.flush();
  return true;
}

bool frame_sync_pulse() {
  digitalWrite(kMockVd, LOW);
  const bool low = expect_level(kVdInput, LOW);
  digitalWrite(kMockVd, HIGH);
  const bool high = expect_level(kVdInput, HIGH);
  return low && high;
}

// Mimic a body-to-lens transfer window, and read our own TX via UART RX.
// Because there is only one UART, the mock body is NOT an independent device.
bool transfer_request(const uint8_t* data, size_t count, uint8_t* rx) {
  digitalWrite(kMockBodyCs, HIGH);
  if (!expect_level(kBodyCsInput, HIGH)) {
    digitalWrite(kMockBodyCs, LOW);
    return false;
  }
  const bool ok = echoed_uart_bytes(data, count, rx);
  digitalWrite(kMockBodyCs, LOW);
  return ok && expect_level(kBodyCsInput, LOW);
}

// Mimic a lens-to-body transfer window and verify the lens-CS GPIO via D3.
bool transfer_reply(const uint8_t* data, size_t count, uint8_t* rx) {
  digitalWrite(kLensCsOutput, HIGH);
  if (!expect_level(kObserveLensCs, HIGH)) {
    digitalWrite(kLensCsOutput, LOW);
    return false;
  }
  const bool ok = echoed_uart_bytes(data, count, rx);
  digitalWrite(kLensCsOutput, LOW);
  return ok && expect_level(kObserveLensCs, LOW);
}

void emit_result(const char* key, bool ok) {
  Serial.print(key);
  Serial.println(ok ? "PASS" : "FAIL");
}

}  // namespace

bool bench_gpio_test() {
  release_pins();
  set_mock_signals();
  const bool ok = check_gpio_links();
  release_pins();
  emit_result("BENCH_GPIO=", ok);
  Serial.println("BENCH_MODE=OFFLINE_GPIO_JUMPERS");
  Serial.println("CAMERA_CONNECTION=FORBIDDEN");
  return ok;
}

bool bench_uart_session(bool extended) {
  release_pins();
  set_mock_signals();
  const bool pins_ok = check_gpio_links();
  if (!pins_ok) {
    release_pins();
    emit_result("BENCH_GPIO=", false);
    Serial.println("BENCH_SESSION=SKIPPED_GPIO_NOT_CONNECTED");
    return false;
  }
  emit_result("BENCH_GPIO=", true);

  const uint8_t a6000[] = {0x01, 0x07, 0x0C, 0x0B, 0x08, 0x09, 0x0D, 0x10, 0x0A};
  const uint8_t a9ii[] = {0x01, 0x07, 0x0C, 0x0B, 0x3F, 0x3D,
                          0x08, 0x09, 0x0D, 0x10, 0x0A};
  const uint8_t* ids = extended ? a9ii : a6000;
  const size_t count = extended ? sizeof(a9ii) : sizeof(a6000);

  LensSession session;
  uint8_t request[kMaxFrame] = {};
  uint8_t request_rx[kMaxFrame] = {};
  uint8_t reply[kMaxFrame] = {};
  uint8_t reply_rx[kMaxFrame] = {};
  uint8_t request_payload[32] = {};
  size_t request_size = 0;
  size_t reply_size = 0;
  uint32_t physical_baud = kInitialBaud;
  bool negotiated = false;
  bool ok = true;
  uint32_t byte_count = 0;

  Serial1.begin(physical_baud);
  for (size_t i = 0; i < count; ++i) {
    memset(request_payload, 0, sizeof(request_payload));
    if (ids[i] == 0x0C) request_payload[0] = 0x02;
    const size_t payload_size = request_payload_length(ids[i]);
    if (!frame_sync_pulse() ||
        !encode(0x02, 0x00, ids[i], request_payload, payload_size,
                request, sizeof(request), request_size) ||
        !transfer_request(request, request_size, request_rx)) {
      ok = false;
      break;
    }
    byte_count += request_size;
    const SessionResult result = session.accept(request_rx, request_size,
                                                reply, sizeof(reply), reply_size);
    if (result != SessionResult::Ok ||
        !transfer_reply(reply, reply_size, reply_rx)) {
      ok = false;
      break;
    }
    byte_count += reply_size;
    FrameView parsed = {};
    if (decode(reply_rx, reply_size, parsed) != ParseError::Ok ||
        parsed.id != ids[i] || parsed.message_class != 0x02) {
      ok = false;
      break;
    }
    if (ids[i] == 0x0C) {
      // Synthetic transition. Both CS lines high with NO data for 5 ms.
      // The real camera's voltage and exact timing must still be measured.
      digitalWrite(kMockBodyCs, HIGH);
      digitalWrite(kLensCsOutput, HIGH);
      delay(5);
      digitalWrite(kMockBodyCs, LOW);
      digitalWrite(kLensCsOutput, LOW);
      Serial1.flush();
      Serial1.end();
      physical_baud = kNegotiatedBaud;
      Serial1.begin(physical_baud);
      negotiated = true;
    }
    Serial.print("BENCH_INIT_ID=0x"); Serial.print(ids[i], HEX);
    Serial.println(" PASS");
  }
  const bool session_ok = ok && session.ready() && session.accepted() == count &&
                          session.baud() == physical_baud && negotiated;
  release_pins();
  Serial.print("BENCH_ACCEPTED="); Serial.println(session.accepted());
  Serial.print("BENCH_UART_BYTES="); Serial.println(byte_count);
  Serial.print("BENCH_FINAL_BAUD="); Serial.println(physical_baud);
  emit_result("BENCH_BAUD_SWITCH=", negotiated && physical_baud == kNegotiatedBaud);
  emit_result("BENCH_SESSION=", session_ok);
  Serial.println("BENCH_MODE=SINGLE_UART_PHYSICAL_SELF_ECHO");
  Serial.println("TIMING_VALIDATION=NOT_PERFORMED");
  Serial.println("CAMERA_CONNECTION=FORBIDDEN");
  return session_ok;
}

}  // namespace ukiyoemount
