#include "Arduino.h"
#include "ukiyo-e-mount/techart_port.h"
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
  assert(!ukiyoemount::techart::active());
  assert(!ukiyoemount::techart::arm());
  assert(Serial.log.find("TECHART_PORT=UNAVAILABLE_RUN_tools/import_techart.py") != std::string::npos);
  ukiyoemount::techart::status();
  assert(Serial.log.find("TECHART_VENDOR=NOT_IMPORTED") != std::string::npos);
  ukiyoemount::techart::poll();
  ukiyoemount::techart::trace();
  ukiyoemount::techart::stop();
  assert(!ukiyoemount::techart::active());
  std::puts("HOST_TECHART_PORT_SAFE_FALLBACK=PASS");
}
