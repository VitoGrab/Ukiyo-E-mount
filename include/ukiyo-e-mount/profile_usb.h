#pragma once
namespace ukiyoemount { namespace profile_usb {
// True for a profile command, including malformed ones.
bool handle(const char* command);
void cancel();
void poll();
} }
