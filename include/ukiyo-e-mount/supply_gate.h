#pragma once
#include <stdint.h>

namespace ukiyoemount {

// Monotonic power qualification for the 100k/100k LOGIC_VCC sense divider.
// Pure C++ so it can be regression-tested without Arduino or the camera.
// No digital camera-facing GPIO may be driven until good() returns true.
class SupplyGate {
 public:
  static constexpr uint16_t kOnAdc = 370;       // ~2.39 V on Sony contact 5
  static constexpr uint16_t kOffAdc = 310;      // ~2.00 V, hysteresis
  static constexpr uint32_t kOnDebounceUs = 3000;
  static constexpr uint32_t kOffDebounceUs = 12000;

  void reset() {
    good_ = false;
    transitioning_ = false;
    pending_good_ = false;
    pending_since_us_ = 0;
    raw_ = 0;
    min_raw_ = 1023;
    max_raw_ = 0;
    rises_ = falls_ = 0;
    last_on_us_ = last_off_us_ = 0;
  }

  bool update(uint16_t adc, uint32_t now_us) {
    raw_ = adc;
    if (adc < min_raw_) min_raw_ = adc;
    if (adc > max_raw_) max_raw_ = adc;

    // Hysteresis means samples in the dead band preserve the current state.
    const bool requested = good_ ? adc < kOffAdc : adc >= kOnAdc;
    if (!requested) {
      transitioning_ = false;
      return good_;
    }
    const bool next_good = !good_;
    if (!transitioning_ || pending_good_ != next_good) {
      transitioning_ = true;
      pending_good_ = next_good;
      pending_since_us_ = now_us;
      return good_;
    }
    const uint32_t threshold = next_good ? kOnDebounceUs : kOffDebounceUs;
    if (static_cast<uint32_t>(now_us - pending_since_us_) >= threshold) {
      good_ = next_good;
      transitioning_ = false;
      if (good_) {
        ++rises_;
        last_on_us_ = now_us;
      } else {
        ++falls_;
        last_off_us_ = now_us;
      }
    }
    return good_;
  }

  bool good() const { return good_; }
  uint16_t raw() const { return raw_; }
  uint16_t min_raw() const { return min_raw_; }
  uint16_t max_raw() const { return max_raw_; }
  uint32_t rises() const { return rises_; }
  uint32_t falls() const { return falls_; }
  uint32_t last_on_us() const { return last_on_us_; }
  uint32_t last_off_us() const { return last_off_us_; }

 private:
  bool good_ = false;
  bool transitioning_ = false;
  bool pending_good_ = false;
  uint32_t pending_since_us_ = 0;
  uint16_t raw_ = 0;
  uint16_t min_raw_ = 1023;
  uint16_t max_raw_ = 0;
  uint32_t rises_ = 0;
  uint32_t falls_ = 0;
  uint32_t last_on_us_ = 0;
  uint32_t last_off_us_ = 0;
};

}  // namespace ukiyoemount
