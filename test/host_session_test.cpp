#include "ukiyo-e-mount/session.h"
#include "ukiyo-e-mount/frame.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace ukiyoemount;

static void one_session(const uint8_t* ids, size_t count) {
  LensSession s;
  uint8_t req[kMaxFrame] = {};
  uint8_t rep[kMaxFrame] = {};
  uint8_t payload[32] = {};
  for (size_t i = 0; i < count; ++i) {
    size_t rn = 0, sn = 0;
    memset(payload, 0, sizeof(payload));
    if (ids[i] == 0x0C) payload[0] = 0x02;
    assert(encode(0x02, 0, ids[i], payload, request_payload_length(ids[i]),
                  req, sizeof(req), rn));
    assert(s.accept(req, rn, rep, sizeof(rep), sn) == SessionResult::Ok);
    FrameView v = {};
    assert(decode(rep, sn, v) == ParseError::Ok);
    assert(v.id == ids[i] && v.message_class == 2 && v.sequence == 0);
    if (ids[i] == 0x01) {
      assert(sn == 41 && rep[sn - 3] == 0x9F && rep[sn - 2] == 0x03);
    }
    if (ids[i] == 0x07) {
      assert(sn == 43 && rep[sn - 3] == 0x11 && rep[sn - 2] == 0x04);
      assert(v.payload[9] == 0x63 && v.payload[10] == 0xC5);
    }
    if (ids[i] == 0x08) {
      assert(sn == 210 && v.payload_size == 201);
      assert(rep[sn - 3] == 0x79 && rep[sn - 2] == 0x0F);
    }
    if (ids[i] == 0x0C) assert(v.payload_size == 1 && v.payload[0] == 1);
    if (ids[i] == 0x3F) {
      assert(v.payload_size == 65 && v.payload[0] == 0);
      assert(memcmp(v.payload + 1, "TECHART LM-EA9", 14) == 0);
      assert(rep[sn - 3] == 0x3B && rep[sn - 2] == 0x04);
    }
    if (ids[i] == 0x3D) {
      assert(v.payload_size == 63 && v.payload[0] == 1);
    }
    if (ids[i] == 0x0A) assert(memcmp(v.payload, payload, 16) == 0);
    assert(s.accepted() == i + 1);
  }
  assert(s.ready() && s.baud() == 1500000u);
  size_t rn = 0, sn = 0;
  const uint8_t shutdown[] = {0x45, 0x12, 0x23};
  assert(encode(0x02, 7, 0x16, shutdown, sizeof(shutdown),
                req, sizeof(req), rn));
  assert(s.accept(req, rn, rep, sizeof(rep), sn) == SessionResult::Ok);
  assert(sn == rn && !memcmp(req, rep, rn));
  assert(s.step() == SessionStep::Shutdown);
  assert(s.accept(req, rn, rep, sizeof(rep), sn) == SessionResult::Ok);
  assert(sn == rn && !memcmp(req, rep, rn));
}

static void test_rejections() {
  LensSession s;
  uint8_t req[kMaxFrame] = {}, rep[kMaxFrame] = {};
  size_t rn = 0, sn = 99;
  const uint8_t x[] = {0};
  assert(encode(2, 0, 7, x, 1, req, sizeof(req), rn));
  assert(s.accept(req, rn, rep, sizeof(rep), sn) == SessionResult::UnexpectedMessage);
  assert(sn == 0 && s.accepted() == 0);
  req[6] ^= 1;
  assert(s.accept(req, rn, rep, sizeof(rep), sn) == SessionResult::InvalidFrame);
  assert(s.accepted() == 0);
  req[6] ^= 1;
  assert(encode(1, 0, 1, x, 1, req, sizeof(req), rn));
  assert(s.accept(req, rn, rep, sizeof(rep), sn) == SessionResult::WrongClass);
  assert(s.step() == SessionStep::Cold);
  assert(encode(2, 0, 1, x, 1, req, sizeof(req), rn));
  assert(s.accept(req, rn, rep, sizeof(rep), sn) == SessionResult::WrongPayloadSize);
  uint8_t p32[32] = {};
  assert(encode(2, 0, 1, p32, 32, req, sizeof(req), rn));
  assert(s.accept(req, rn, rep, 5, sn) == SessionResult::OutputTooSmall);
  assert(s.step() == SessionStep::Cold);
  // CS transfer-window padding does not invalidate the inner frame.
  req[rn] = 0x00;
  req[rn + 1] = 0xA5;
  assert(s.accept(req, rn + 2, rep, sizeof(rep), sn) == SessionResult::Ok);
  assert(s.step() == SessionStep::Identity);
}

static void test_optional_baud() {
  LensSession s;
  const uint8_t ids[] = {0x01, 0x07, 0x0B, 0x08, 0x09, 0x0D, 0x10, 0x0A};
  uint8_t req[kMaxFrame] = {}, rep[kMaxFrame] = {};
  uint8_t data[32] = {};
  for (size_t i = 0; i < sizeof(ids); ++i) {
    size_t rn = 0, sn = 0;
    assert(encode(2, 0, ids[i], data, request_payload_length(ids[i]),
                  req, sizeof(req), rn));
    assert(s.accept(req, rn, rep, sizeof(rep), sn) == SessionResult::Ok);
  }
  assert(s.ready() && s.baud() == 750000u);
  std::puts("HOST_SESSION_NO_BAUD_SWITCH=PASS");
}

int main() {
  const uint8_t a6000[] = {0x01, 0x07, 0x0C, 0x0B,
                             0x08, 0x09, 0x0D, 0x10, 0x0A};
  const uint8_t a9ii[] = {0x01, 0x07, 0x0C, 0x0B, 0x3F, 0x3D,
                            0x08, 0x09, 0x0D, 0x10, 0x0A};
  one_session(a6000, sizeof(a6000));
  std::puts("HOST_SESSION_A6000=PASS");
  one_session(a9ii, sizeof(a9ii));
  std::puts("HOST_SESSION_A9II=PASS");
  test_optional_baud();
  test_rejections();
  std::puts("HOST_SESSION_NEGATIVE=PASS");
  std::puts("HOST_SESSION_TEST=PASS");
}
