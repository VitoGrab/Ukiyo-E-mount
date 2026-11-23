#include "ukiyo-e-mount/supply_gate.h"
#include <cassert>
#include <cstdio>
using ukiyoemount::SupplyGate;
int main() {
  SupplyGate g;
  g.reset();
  assert(!g.good());
  assert(!g.update(500, 100));                  // rising edge qualified, not instant
  assert(!g.update(500, 3099));
  assert(g.update(500, 3100));
  assert(g.rises() == 1 && g.last_on_us() == 3100);
  // A single bad ADC sample cannot terminate a live transaction.
  assert(g.update(30, 5000));
  assert(g.update(500, 7000));
  assert(g.falls() == 0);
  assert(g.update(305, 10000));
  assert(g.update(305, 21999));
  assert(!g.update(305, 22000));
  assert(g.falls() == 1 && g.last_off_us() == 22000);
  // Dead-band samples neither activate power nor complete debounce.
  assert(!g.update(340, 25000));
  assert(!g.update(500, 26000));
  assert(!g.update(340, 30000));
  assert(!g.update(500, 31000));
  assert(g.update(500, 34000));
  assert(g.rises() == 2);
  // micros() wraps every ~71 minutes; subtraction must remain correct.
  g.reset();
  assert(!g.update(500, 0xfffff000u));
  assert(g.update(500, 0x00000000u));
  std::puts("HOST_SUPPLY_GATE=PASS");
}
