#include "ukiyo-e-mount/frame.h"
#include "ukiyo-e-mount/techart_sample.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  using namespace ukiyoemount;
  static_assert(kTechartIdentityPayloadSize == 34, "0x07 reply must have 34 payload bytes");
  uint8_t frame[kMaxFrame] = {};
  size_t size = 0;
  assert(encode(0x02, 0, 0x07, kTechartIdentityPayload,
                kTechartIdentityPayloadSize, frame, sizeof(frame), size));
  assert(size == 43);
  assert(frame[0] == 0xF0 && frame[1] == 0x2B && frame[2] == 0);
  assert(frame[size - 1] == 0x55);
  // Techart stores the same canonical frame with checksum 0x0411.
  assert(frame[size - 3] == 0x11 && frame[size - 2] == 0x04);
  FrameView v = {};
  assert(decode(frame, size, v) == ParseError::Ok);
  assert(v.id == 0x07 && v.payload_size == 34);
  assert(v.payload[9] == 0x63 && v.payload[10] == 0xC5);
  // Excess 0x00 transfer-window padding is not part of the frame.
  frame[size] = 0;
  frame[size + 1] = 0;
  assert(decode(frame, size + 2, v) == ParseError::Ok);
  assert(v.trailing_size == 2);
  const uint8_t saved = frame[10];
  frame[10] ^= 0x40;
  assert(decode(frame, size, v) == ParseError::InvalidChecksum);
  frame[10] = saved;
  assert(decode(frame, size - 1, v) == ParseError::Truncated);
  frame[size - 1] = 0;
  assert(decode(frame, size, v) == ParseError::InvalidTerminator);
  frame[size - 1] = 0x55;
  frame[0] = 0;
  assert(decode(frame, size, v) == ParseError::InvalidStart);
  frame[0] = 0xF0;
  assert(decode(frame, size, v) == ParseError::Ok);
  assert(!encode(2,0,7,kTechartIdentityPayload,34,frame,9,size));
  std::puts("HOST_FRAME_TEST=PASS");
}
