#include "Arduino.h"
#include "ukiyo-e-mount/emulator.h"
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
  for (unsigned i = 0; i < 32; ++i) { pins[i] = 0; modes[i] = INPUT; irq_handlers[i] = nullptr; }
  tick = 0; Serial.log.clear(); Serial1.end();
}
}
MockSerial Serial;
MockUart Serial1;
int main() {
  mock::reset();
  assert(!ukiyoemount::emulator::active());
  assert(!ukiyoemount::emulator::arm());
  assert(Serial.log.find("EMULATOR=PROTOCOL_ENGINE_UNAVAILABLE") != std::string::npos);
  ukiyoemount::emulator::status();
  assert(Serial.log.find("PROTOCOL_ENGINE=UNAVAILABLE") != std::string::npos);
  ukiyoemount::emulator::poll();
  ukiyoemount::emulator::trace();
  ukiyoemount::emulator::stop();
  assert(!ukiyoemount::emulator::active());
  std::puts("HOST_EMULATOR_SAFE_FALLBACK=PASS");
}
