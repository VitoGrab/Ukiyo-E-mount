#pragma once
#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <string>
#include <sstream>

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
#define HEX 16
#define LED_BUILTIN 13
#define CHANGE 3
#define RISING 4
namespace mock { extern void (*irq_handlers[32])(); }
inline int digitalPinToInterrupt(uint8_t pin) { return pin; }
inline void attachInterrupt(int pin, void (*fn)(), int) { mock::irq_handlers[pin] = fn; }
inline void detachInterrupt(int pin) { mock::irq_handlers[pin] = nullptr; }
inline void noInterrupts() {}
inline void interrupts() {}


namespace mock {
extern int pins[32];
extern int modes[32];
extern bool links[4]; // D1->D8, D2->D9, D10->D3, D6->D7
extern uint32_t tick;
void reset();
}

inline void pinMode(uint8_t p, int v) { mock::modes[p] = v; }
inline void digitalWrite(uint8_t p, int v) { mock::pins[p] = v; }
inline int digitalRead(uint8_t p) {
  if (p == 8 && mock::links[0]) return mock::pins[1];
  if (p == 9 && mock::links[1]) return mock::pins[2];
  if (p == 3 && mock::links[2]) return mock::pins[10];
  return mock::modes[p] == INPUT_PULLUP ? HIGH : mock::pins[p];
}
inline uint32_t millis() { return ++mock::tick; }
inline uint32_t micros() { return ++mock::tick * 1000u; }
inline void delayMicroseconds(unsigned) { ++mock::tick; }
inline void delay(unsigned v) { mock::tick += v; }

struct MockSerial {
  std::string log;
  void begin(uint32_t) {}
  void end() {}
  void flush() {}
  void print(const char* str) { log += str; }
  void print(char c) { log += c; }
  template <typename T> void print(T x, int radix = 10) {
    std::ostringstream os;
    if (radix == HEX) os << std::hex << std::uppercase;
    os << static_cast<unsigned long long>(x);
    log += os.str();
  }
  void println(const char* str) { print(str); log += '\n'; }
  template <typename T> void println(T x, int radix = 10) { print(x, radix); log += '\n'; }
  void println() { log += '\n'; }
  int available() { return 0; }
  int read() { return -1; }
  explicit operator bool() const { return true; }
};
struct MockUart {
  bool begun = false;
  uint32_t baud = 0;
  std::deque<uint8_t> queue;
  void begin(uint32_t b) { begun = true; baud = b; }
  void end() { begun = false; queue.clear(); }
  void flush() {}
  size_t write(uint8_t b) {
    if (!begun) return 0;
    if (mock::links[3]) queue.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) write(data[i]);
    return size;
  }
  int available() { return static_cast<int>(queue.size()); }
  int read() {
    if (queue.empty()) return -1;
    int b = queue.front(); queue.pop_front(); return b;
  }
};
extern MockSerial Serial;
extern MockUart Serial1;
