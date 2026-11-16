#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ukiyoemount {

// Lens identity payload, message 0x07, from Aylwin Wei's
// E-mount-impl-on-Techart-EA9/src/emount_packets.c (MIT).
// This is a protocol fixture, NOT the identity for the user's M42 lens.
// It is NOT intended to be transmitted to a camera in v0.1.0.
// Upstream has response pl[0]=01, pl[1]=03, Lens ID 0xC563.
constexpr uint8_t kTechartIdentityPayload[] = {
  0x01, 0x03, 0x08, 0x01, 0x00, 0x00, 0x22, 0x10,
  0xA0, 0x63, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x92, 0x86, 0x5E, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00
};
constexpr size_t kTechartIdentityPayloadSize = sizeof(kTechartIdentityPayload);

}  // namespace ukiyoemount
