#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ukiyoemount {

constexpr size_t kMaxFrame = 256;
constexpr uint8_t kFrameStart = 0xF0;
constexpr uint8_t kFrameEnd = 0x55;

struct FrameView {
  uint8_t message_class;
  uint8_t sequence;
  uint8_t id;
  const uint8_t* payload;
  size_t payload_size;
  size_t declared_size;
  size_t trailing_size;
};

enum class ParseError : uint8_t {
  Ok,
  TooShort,
  InvalidStart,
  InvalidLength,
  Truncated,
  InvalidTerminator,
  InvalidChecksum
};

uint16_t checksum(const uint8_t* frame, size_t declared_size);
ParseError decode(const uint8_t* window, size_t available, FrameView& out);
bool encode(uint8_t message_class, uint8_t sequence, uint8_t id,
            const uint8_t* payload, size_t payload_size,
            uint8_t* target, size_t capacity, size_t& written);
const char* error_string(ParseError error);

}  // namespace ukiyoemount
