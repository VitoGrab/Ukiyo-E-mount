// Ukiyo XIAO SAMD21 transport bridge for unmodified Techart wire protocol.
// No USB logging in interrupts, UART writes, or CS callbacks.
#include <Arduino.h>
#include "board.h"
// Techart headers are C headers without their own C++ linkage guards.
// Match the C linkage used by their implementations in emount.c.
extern "C" {
#include "em_uart.h"
#include "em_eic.h"
}
#include "ukiyo_hal.h"

namespace {
uint8_t rx_buf[EM_RX_BUF_LEN];
volatile uint16_t rx_head = 0;
volatile uint16_t rx_tail = 0;
volatile bool uart_enabled = false;
volatile bool callbacks_enabled = false;
volatile uint32_t configured_baud = 750000u;
volatile bool supply_good_cached = false;
void (*cs_handler)(int) = nullptr;
void (*vd_handler)(void) = nullptr;

volatile uint32_t trace_time[32] = {};
volatile uint8_t trace_level[32] = {};
volatile uint8_t trace_used = 0;

inline uint16_t ring_count() {
  return static_cast<uint16_t>((rx_head - rx_tail) & (EM_RX_BUF_LEN - 1u));
}

// Drain only after a BODY_CS LOW, matching Techart's receive-ring contract.
void drain_uart() {
  if (!uart_enabled) return;
  while (Serial1.available()) {
    const int b = Serial1.read();
    if (b < 0) break;
    const uint16_t next = static_cast<uint16_t>((rx_head + 1u) & (EM_RX_BUF_LEN - 1u));
    if (next == rx_tail) {
      ++em_rx_errors;
      ++rx_tail;
      rx_tail &= (EM_RX_BUF_LEN - 1u);
    }
    rx_buf[rx_head] = static_cast<uint8_t>(b);
    rx_head = next;
    ++em_rx_bytes;
    if (!em_t_first_byte) em_t_first_byte = millis();
  }
}

void on_cs() {
  if (!callbacks_enabled) return;
  const int level = digitalRead(EM_PIN_BODY_CS);
  const uint8_t i = trace_used;
  if (i < 32) {
    trace_time[i] = micros();
    trace_level[i] = static_cast<uint8_t>(level);
    trace_used = static_cast<uint8_t>(i + 1);
  }
  if (cs_handler) cs_handler(level);
}
void on_vd() {
  if (callbacks_enabled && vd_handler) vd_handler();
}
}  // namespace

extern "C" {
volatile uint32_t em_rx_bytes = 0;
volatile uint32_t em_rx_errors = 0;
volatile uint16_t em_rx_status = 0;
volatile uint32_t em_t_first_byte = 0;
volatile uint32_t em_rx_dma_stuck = 0;

uint16_t em_uart_rx_count(void) {
  drain_uart();
  return ring_count();
}
uint8_t em_uart_rx_at(uint16_t i) {
  return rx_buf[(rx_tail + i) & (EM_RX_BUF_LEN - 1u)];
}
void em_uart_rx_consume(uint16_t n) {
  const uint16_t present = ring_count();
  if (n > present) n = present;
  rx_tail = static_cast<uint16_t>((rx_tail + n) & (EM_RX_BUF_LEN - 1u));
}
void em_uart_rx_flush(void) {
  drain_uart();
  rx_tail = rx_head;
}
void em_uart_init(void) {
  if (uart_enabled) Serial1.end();
  configured_baud = 750000u;
  rx_head = rx_tail = 0;
  em_rx_bytes = em_rx_errors = 0;
  em_rx_status = 0;
  em_t_first_byte = em_rx_dma_stuck = 0;
  Serial1.begin(configured_baud);
  uart_enabled = true;
}
void em_uart_send(const uint8_t* frame, uint16_t n) {
  if (!uart_enabled || !supply_good_cached) return;
  Serial1.write(frame, n);
  Serial1.flush();
}
void em_uart_set_baud(uint16_t raw) {
  if (!uart_enabled) return;
  const uint32_t target = (raw == EM_BAUD_FAST) ? 1500000u : 750000u;
  if (target == configured_baud) return;
  Serial1.flush();
  Serial1.end();
  configured_baud = target;
  Serial1.begin(configured_baud);
  rx_head = rx_tail = 0;
}
void em_uart_disable(void) {
  if (uart_enabled) Serial1.end();
  uart_enabled = false;
  pinMode(EM_PIN_TX, INPUT);
  pinMode(EM_PIN_RX, INPUT);
}
void em_uart_resume(void) {
  if (uart_enabled) return;
  rx_head = rx_tail = 0;
  Serial1.begin(configured_baud);
  uart_enabled = true;
}

void em_eic_init(void (*cs)(int), void (*vd)(void)) {
  cs_handler = cs;
  vd_handler = vd;
  callbacks_enabled = true;
  attachInterrupt(digitalPinToInterrupt(EM_PIN_BODY_CS), on_cs, CHANGE);
  attachInterrupt(digitalPinToInterrupt(EM_PIN_BODY_VD), on_vd, RISING);
}
void em_eic_pause(void) {
  callbacks_enabled = false;
  detachInterrupt(digitalPinToInterrupt(EM_PIN_BODY_CS));
  detachInterrupt(digitalPinToInterrupt(EM_PIN_BODY_VD));
}
void em_eic_resume(void) {
  if (!cs_handler || !vd_handler) return;
  callbacks_enabled = true;
  attachInterrupt(digitalPinToInterrupt(EM_PIN_BODY_CS), on_cs, CHANGE);
  attachInterrupt(digitalPinToInterrupt(EM_PIN_BODY_VD), on_vd, RISING);
}
void em_eic_sleep_until_cs_high(void) {
  // No low-power standby: USB must remain online for diagnostics.
  while (!digitalRead(EM_PIN_BODY_CS) && ukiyo_supply_ok()) delayMicroseconds(50);
}
int em_pin_level(uint8_t pin) { return digitalRead(pin); }

void delay_ms(uint32_t ms) { delay(ms); }
void delay_us(uint32_t us) { delayMicroseconds(us); }

// D0 has ADC, but is NOT five-volt tolerant. D0 must see 1/2 of LOGIC_VCC
// through a 100k/100k divider, not the bare red wire.
int ukiyo_supply_ok(void) {
  supply_good_cached = (analogRead(A0) >= 370);
  return supply_good_cached;
}
void ukiyo_gpio_lens_cs(int high) {
  if (!supply_good_cached) {
    pinMode(EM_PIN_LENS_CS, INPUT);
    return;
  }
  digitalWrite(EM_PIN_LENS_CS, high ? HIGH : LOW);
  pinMode(EM_PIN_LENS_CS, OUTPUT);
}
void ukiyo_gpio_body_cs_input(void) { pinMode(EM_PIN_BODY_CS, INPUT); }
void ukiyo_gpio_body_vd_input(void) { pinMode(EM_PIN_BODY_VD, INPUT); }
void ukiyo_gpio_tx_low(void) {
  digitalWrite(EM_PIN_TX, LOW);
}
void ukiyo_gpio_tx_output(void) {
  if (supply_good_cached) pinMode(EM_PIN_TX, OUTPUT);
}
void ukiyo_gpio_tx_unmux(void) { /* end() removed the UART peripheral mux */ }
void ukiyo_bus_abort(void) {
  em_eic_pause();
  em_uart_disable();
  supply_good_cached = false;
  pinMode(EM_PIN_LENS_CS, INPUT);
}

uint32_t ukiyo_bus_baud(void) { return configured_baud; }
uint8_t ukiyo_trace_count(void) { return trace_used; }
uint32_t ukiyo_trace_time(uint8_t i) { return i < trace_used ? trace_time[i] : 0; }
uint8_t ukiyo_trace_level(uint8_t i) { return i < trace_used ? trace_level[i] : 0; }
void ukiyo_trace_reset(void) { trace_used = 0; }
} // extern C
