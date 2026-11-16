#include "ukiyo-e-mount/frame.h"

namespace ukiyoemount {

uint16_t checksum(const uint8_t* frame, size_t declared_size) {
  uint32_t sum = 0;
  // Include length, class, sequence, ID and payload; omit F0, checksum, 55.
  for (size_t i = 1; i < declared_size - 3; ++i) sum += frame[i];
  return static_cast<uint16_t>(sum & 0xFFFFu);
}

ParseError decode(const uint8_t* window, size_t available, FrameView& out) {
  out = {};
  if (!window || available < 9) return ParseError::TooShort;
  if (window[0] != kFrameStart) return ParseError::InvalidStart;
  const size_t declared = static_cast<size_t>(window[1]) |
                          (static_cast<size_t>(window[2]) << 8);
  if (declared < 9 || declared > kMaxFrame) return ParseError::InvalidLength;
  if (available < declared) return ParseError::Truncated;
  if (window[declared - 1] != kFrameEnd) return ParseError::InvalidTerminator;
  const uint16_t received = static_cast<uint16_t>(window[declared - 3]) |
                            (static_cast<uint16_t>(window[declared - 2]) << 8);
  if (received != checksum(window, declared)) return ParseError::InvalidChecksum;
  out.message_class = window[3];
  out.sequence = window[4];
  out.id = window[5];
  out.payload = window + 6;
  out.payload_size = declared - 9;
  out.declared_size = declared;
  out.trailing_size = available - declared;
  return ParseError::Ok;
}

bool encode(uint8_t message_class, uint8_t sequence, uint8_t id,
            const uint8_t* payload, size_t payload_size,
            uint8_t* target, size_t capacity, size_t& written) {
  written = 0;
  if (!target || (!payload && payload_size)) return false;
  if (payload_size > kMaxFrame - 9) return false;
  const size_t total = payload_size + 9;
  if (capacity < total) return false;
  target[0] = kFrameStart;
  target[1] = static_cast<uint8_t>(total & 0xFFu);
  target[2] = static_cast<uint8_t>((total >> 8) & 0xFFu);
  target[3] = message_class;
  target[4] = sequence;
  target[5] = id;
  for (size_t i = 0; i < payload_size; ++i) target[6 + i] = payload[i];
  const uint16_t cksum = checksum(target, total);
  target[total - 3] = static_cast<uint8_t>(cksum & 0xFFu);
  target[total - 2] = static_cast<uint8_t>(cksum >> 8);
  target[total - 1] = kFrameEnd;
  written = total;
  return true;
}

const char* error_string(ParseError error) {
  switch (error) {
    case ParseError::Ok: return "OK";
    case ParseError::TooShort: return "TOO_SHORT";
    case ParseError::InvalidStart: return "BAD_START";
    case ParseError::InvalidLength: return "BAD_LENGTH";
    case ParseError::Truncated: return "TRUNCATED";
    case ParseError::InvalidTerminator: return "BAD_TERMINATOR";
    case ParseError::InvalidChecksum: return "BAD_CHECKSUM";
  }
  return "UNKNOWN";
}

}  // namespace ukiyoemount
