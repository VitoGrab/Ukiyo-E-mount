#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ukiyo-e-mount/frame.h"

namespace ukiyoemount {

// Pure-C++ dry-run protocol state machine. This class owns NO hardware and
// NEVER touches UART, GPIO or camera power. Payloads are Techart fixtures,
// NOT a manual-lens capability declaration suitable for a real camera.
enum class SessionStep : uint8_t {
  Cold,
  Identity,
  BaudOrSettings,
  Settings,
  OptionalNameOrDescriptor,
  OptionalMetadataOrDescriptor,
  Descriptor,
  Startup,
  Sync,
  ReadyCheck,
  FinalAck,
  Running,
  Shutdown
};

enum class SessionResult : uint8_t {
  Ok,
  InvalidFrame,
  WrongClass,
  WrongPayloadSize,
  UnexpectedMessage,
  UnsupportedMessage,
  OutputTooSmall
};

const char* session_result_string(SessionResult value);
const char* session_step_string(SessionStep value);

class LensSession {
 public:
  LensSession();
  void reset();

  // Each call is one completed chip-select transfer window. Extra padding
  // after the frame terminator is allowed by the frame decoder.
  SessionResult accept(const uint8_t* window, size_t window_size,
                       uint8_t* reply, size_t reply_capacity, size_t& reply_size);

  SessionStep step() const { return step_; }
  uint32_t baud() const { return baud_; }  // virtual negotiated speed ONLY
  uint16_t accepted() const { return accepted_; }
  bool ready() const { return step_ == SessionStep::Running; }

 private:
  bool expected(uint8_t id) const;
  void advance(uint8_t id);
  SessionStep step_;
  uint32_t baud_;
  uint16_t accepted_;
};

// An individual, source-derived synthetic request, never a capture.
size_t request_payload_length(uint8_t id);

}  // namespace ukiyoemount
