#include "Arduino.h"
#include "ukiyo-e-mount/bench.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <string>

namespace mock {
int pins[32] = {};
int modes[32] = {};
bool links[4] = {true,true,true,true};
uint32_t tick = 0;
void reset() {
  for (unsigned i = 0; i < 32; ++i) { pins[i] = 0; modes[i] = INPUT; }
  links[0] = links[1] = links[2] = links[3] = true;
  tick = 0;
  Serial.log.clear();
  Serial1.end();
}
}
MockSerial Serial;
MockUart Serial1;

static void check(const char* log_token) {
  if (Serial.log.find(log_token) == std::string::npos) {
    std::fprintf(stderr, "Missing: %s\n%s\n", log_token, Serial.log.c_str());
    assert(false);
  }
}
static void safe_after_test() {
  assert(!Serial1.begun);
  for (uint8_t p : {1,2,3,6,7,8,9,10}) assert(mock::modes[p] == INPUT);
}
int main() {
  mock::reset();
  assert(ukiyoemount::bench_gpio_test());
  check("BENCH_GPIO=PASS");
  safe_after_test();
  std::puts("HOST_BENCH_GPIO=PASS");

  mock::reset();
  assert(ukiyoemount::bench_uart_session(false));
  check("BENCH_ACCEPTED=9");
  check("BENCH_SESSION=PASS");
  check("BENCH_BAUD_SWITCH=PASS");
  safe_after_test();
  std::puts("HOST_BENCH_A6000=PASS");

  mock::reset();
  assert(ukiyoemount::bench_uart_session(true));
  check("BENCH_ACCEPTED=11");
  check("BENCH_SESSION=PASS");
  safe_after_test();
  std::puts("HOST_BENCH_A9II=PASS");

  mock::reset(); mock::links[0] = false;
  assert(!ukiyoemount::bench_gpio_test());
  check("BENCH_GPIO=FAIL");
  safe_after_test();
  std::puts("HOST_BENCH_DISCONNECTED_GPIO=PASS");

  mock::reset(); mock::links[3] = false;
  assert(!ukiyoemount::bench_uart_session(false));
  check("BENCH_SESSION=FAIL");
  safe_after_test();
  std::puts("HOST_BENCH_DISCONNECTED_UART=PASS");
  std::puts("HOST_BENCH_TEST=PASS");
}
