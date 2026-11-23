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
}
MockSerial Serial;
MockUart Serial1;
extern "C" {
#include "emount.h"
#include "em_uart.h"
#include "ukiyo_hal.h"
volatile uint32_t em_frames_rx = 0, em_frames_tx = 0;
volatile uint32_t em_cs_edges = 0, em_vd_edges = 0;
volatile uint32_t em_rx_skipped = 0, em_req_dropped = 0;
volatile uint32_t em_t_cs_first_high = 0, em_t_first_frame = 0;
volatile uint32_t em_t_init_done = 0, em_t_handshake = 0, em_t_first_status = 0;
volatile uint8_t em_handshake_done = 0, em_last_id = 0, em_bad_frames = 0;
volatile uint8_t em_id_log[EM_ID_LOG_LEN] = {};
volatile uint8_t em_id_log_n = 0;
volatile uint32_t em_rx_bytes = 0, em_rx_errors = 0, em_t_first_byte = 0;
}
static bool powered = false;
static int init_calls = 0;
static int abort_calls = 0;
extern "C" {
void em_init(void) { ++init_calls; em_t_init_done = millis(); }
void em_poll(void) {}
void ukiyo_supply_init(void) { powered = false; }
int ukiyo_supply_ok(void) { return powered; }
uint16_t ukiyo_supply_adc(void) { return powered ? 512 : 20; }
uint16_t ukiyo_supply_adc_min(void) { return 20; }
uint16_t ukiyo_supply_adc_max(void) { return 512; }
uint32_t ukiyo_supply_rises(void) { return 1; }
uint32_t ukiyo_supply_falls(void) { return 0; }
uint32_t ukiyo_supply_last_on_us(void) { return 5; }
uint32_t ukiyo_supply_last_off_us(void) { return 0; }
void ukiyo_bus_abort(void) { ++abort_calls; }
uint32_t ukiyo_bus_baud(void) { return 1500000; }
void ukiyo_trace_reset(void) {}
uint8_t ukiyo_trace_count(void) { return 0; }
uint32_t ukiyo_trace_time(uint8_t) { return 0; }
uint8_t ukiyo_trace_level(uint8_t) { return 0; }
}
int main() {
  using namespace ukiyoemount::techart;
  assert(!active());
  boot_auto_arm(0, 1);
  record_setup_done(2);
  assert(active());
  poll();                          // no camera power; stay armed indefinitely
  assert(init_calls == 0);
  powered = true;
  poll();
  assert(init_calls == 1);
  assert(Serial.log.find("RUNNING_PROTOCOL_ENGINE") != std::string::npos);
  Serial.log.clear();
  em_last_id = 0x0a;
  em_frames_rx = 42;
  poll();
  assert(Serial.log.find("PROTO_RX") == std::string::npos);  // keepalive quiet
  em_last_id = 0x35;
  em_frames_rx = 43;
  poll();
  assert(Serial.log.find("ID=0x35") != std::string::npos);
  powered = false;
  poll();
  assert(abort_calls == 1);
  poll();                          // fault -> automatic wait for next supply
  assert(active());
  powered = true;
  poll();
  assert(init_calls == 2);
  stop();
  assert(!active());
  poll();
  assert(init_calls == 2);        // x must prevent any automatic restart
  assert(arm());
  poll();
  assert(init_calls == 3);
  std::puts("HOST_TECHART_AUTO_REARM_KEEPALIVE=PASS");
}
