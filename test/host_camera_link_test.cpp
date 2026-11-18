#include "Arduino.h"
#include "ukiyo-e-mount/camera_link.h"
#include "ukiyo-e-mount/frame.h"
#include <cassert>
#include <cstdio>
#include <string>

namespace mock {
int pins[32] = {};
int modes[32] = {};
bool links[4] = {};
uint32_t tick = 0;
void (*irq_handlers[32])() = {};
void reset() {
  for (unsigned i = 0; i < 32; ++i) { pins[i] = 0; modes[i] = INPUT; }
  tick = 0;
  for (unsigned i = 0; i < 32; ++i) irq_handlers[i] = nullptr;
  Serial.log.clear();
  Serial1.end();
}
}
MockSerial Serial;
MockUart Serial1;

int main() {
  mock::reset();
  assert(ukiyoemount::camera_link::start(false));
  assert(!ukiyoemount::camera_link::start(true));
  assert(ukiyoemount::camera_link::active());
  assert(mock::modes[6] == INPUT);  // UART TX remains high impedance
  assert(mock::modes[10] == INPUT); // Lens CS stays high impedance in listen mode
  assert(Serial1.begun && Serial1.baud == 750000);
  ukiyoemount::camera_link::status();
  assert(Serial.log.find("MODE=LISTEN") != std::string::npos);
  assert(Serial.log.find("CAMERA_UART_TX=DISABLED") != std::string::npos);
  ukiyoemount::camera_link::stop();
  assert(!Serial1.begun && !ukiyoemount::camera_link::active());
  assert(mock::modes[6] == INPUT && mock::modes[10] == INPUT);
  std::puts("HOST_CAMERA_LISTEN=PASS");

  // Mock a complete body->lens CS window and verify checksum-aware parsing.
  mock::reset();
  assert(ukiyoemount::camera_link::start(false));
  uint8_t wire[ukiyoemount::kMaxFrame] = {};
  size_t wire_size = 0;
  const uint8_t payload[1] = {0x00};
  assert(ukiyoemount::encode(0x02, 0, 0x07, payload, 1,
                            wire, sizeof(wire), wire_size));
  for (size_t i = 0; i < wire_size; ++i) Serial1.queue.push_back(wire[i]);
  mock::pins[9] = LOW;
  assert(mock::irq_handlers[9] != nullptr);
  mock::irq_handlers[9]();
  ukiyoemount::camera_link::poll();
  assert(Serial.log.find("CAMERA_RX_CLASS=0x2 ID=0x7") != std::string::npos);
  ukiyoemount::camera_link::status();
  assert(Serial.log.find("VALID_FRAMES=1") != std::string::npos);
  ukiyoemount::camera_link::stop();
  std::puts("HOST_CAMERA_FRAME_CAPTURE=PASS");

  mock::reset();
  assert(ukiyoemount::camera_link::start(true));
  assert(mock::modes[10] == INPUT); // No premature high output.
  assert(mock::modes[6] == INPUT);
  assert(Serial.log.find("CAMERA_MODE=PROBE_ARMED_WAITING_FOR_BODY_CS") != std::string::npos);

  // Simulate the first body-CS edge. The lens-CS HIGH must be delayed.
  mock::pins[9] = HIGH;
  assert(mock::irq_handlers[9] != nullptr);
  mock::irq_handlers[9]();
  assert(mock::modes[10] == INPUT);
  ukiyoemount::camera_link::poll();
  assert(mock::modes[10] == OUTPUT && mock::pins[10] == HIGH);

  // Simulate a subsequent CS edge; lens-CS LOW must follow the delay.
  mock::pins[9] = LOW;
  mock::irq_handlers[9]();
  ukiyoemount::camera_link::poll();
  assert(mock::pins[10] == LOW);
  for (int i = 0; i < 6; ++i) ukiyoemount::camera_link::poll();
  assert(mock::modes[10] == INPUT);
  ukiyoemount::camera_link::status();
  assert(Serial.log.find("PROBE_STATE=COMPLETE_RX_ONLY") != std::string::npos);
  assert(Serial.log.find("FIRST_EDGE_US=") != std::string::npos);
  ukiyoemount::camera_link::trace();
  assert(Serial.log.find("CAMERA_TRACE_COUNT=2") != std::string::npos);
  ukiyoemount::camera_link::stop();
  assert(mock::modes[10] == INPUT);
  std::puts("HOST_CAMERA_PROBE_TWO_EDGES=PASS");

  mock::reset();
  assert(ukiyoemount::camera_link::start(true));
  // Host mock's monotonic tick is one ms per function call.
  mock::tick += 32000;
  ukiyoemount::camera_link::poll();
  assert(mock::modes[10] == INPUT);
  ukiyoemount::camera_link::status();
  assert(Serial.log.find("PROBE_STATE=TIMEOUT_FIRST_EDGE") != std::string::npos);
  ukiyoemount::camera_link::stop();
  std::puts("HOST_CAMERA_PROBE_NO_EDGE_TIMEOUT=PASS");

  mock::reset();
  assert(ukiyoemount::camera_link::start(true));
  mock::pins[9] = HIGH;
  mock::irq_handlers[9]();
  ukiyoemount::camera_link::poll();
  assert(mock::pins[10] == HIGH);
  mock::tick += 200;
  ukiyoemount::camera_link::poll();
  assert(mock::modes[10] == INPUT);
  ukiyoemount::camera_link::status();
  assert(Serial.log.find("PROBE_STATE=TIMEOUT_SECOND_EDGE") != std::string::npos);
  ukiyoemount::camera_link::stop();
  std::puts("HOST_CAMERA_PROBE_SECOND_EDGE_TIMEOUT=PASS");

  std::puts("HOST_CAMERA_LINK_TEST=PASS");
}
