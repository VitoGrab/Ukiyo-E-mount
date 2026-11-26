#include <Arduino.h>
#include <string.h>

#include "ukiyo-e-mount/camera_link.h"
#include "ukiyo-e-mount/techart_port.h"
#include "ukiyo-e-mount/version.h"

// Auto-arm the camera protocol before USB CDC; qualify LOGIC_VCC before driving outputs.
// USB diagnostics remain manually selectable.

namespace {

constexpr uint8_t kPinVd = 8;
constexpr uint8_t kPinBodyCs = 9;
constexpr uint8_t kPinLensCs = 10;
constexpr uint8_t kPinTx = 6;
constexpr uint8_t kPinRx = 7;
constexpr uint8_t kPinDetectFuture = 3;
char command[80] = {};
size_t command_used = 0;
bool banner_printed = false;

void safe_pins() {
  pinMode(kPinVd, INPUT);
  pinMode(kPinBodyCs, INPUT);
  pinMode(kPinLensCs, INPUT);
  pinMode(kPinTx, INPUT);
  pinMode(kPinRx, INPUT);
  pinMode(kPinDetectFuture, INPUT);
}

void print_status() {
  if (ukiyoemount::camera_link::active()) ukiyoemount::camera_link::status();
  else ukiyoemount::techart::status();
}

void handle_command(const char* cmd) {
  // Every command is echoed with a monotonic timestamp for log correlation.
  // millis() wraps after ~49.7 days; micros() after ~71.6 minutes.
  Serial.print("[t_ms="); Serial.print(millis());
  Serial.print(" t_us="); Serial.print(micros());
  Serial.print("] CMD="); Serial.println(cmd);

  // Short camera commands can be typed quickly before switching the body on.
  if (!strcmp(cmd, "h")) cmd = "help";
  else if (!strcmp(cmd, "l")) cmd = "camera listen";
  else if (!strcmp(cmd, "p")) cmd = "camera probe";
  else if (!strcmp(cmd, "s")) cmd = "camera status";
  else if (!strcmp(cmd, "t")) cmd = "camera trace";
  else if (!strcmp(cmd, "tr")) cmd = "camera trace clear";
  else if (!strcmp(cmd, "tm")) cmd = "camera trace mark";
  else if (!strcmp(cmd, "x")) cmd = "camera stop";
  else if (!strcmp(cmd, "e")) cmd = "camera emulate";
  if (!strcmp(cmd, "help")) {
    Serial.print("UKIYO-E-MOUNT "); Serial.println(UKIYO_PROJECT_VERSION_LABEL);
    Serial.println("Short camera commands: l=listen, p=probe, e=rearm, s=status, t=trace dump, tr=clear, tm=mark attempt, x=stop, h=help.");
    Serial.println("Auto emulation is armed at boot; x disables it until e or reset.");
    Serial.println("Commands: help, status, camera listen, camera probe, camera emulate, camera status, camera trace, camera stop");
    Serial.println("camera probe drives D10 only, never UART TX.");
  } else if (!strcmp(cmd, "camera status")) {
    print_status();
  } else if (!strcmp(cmd, "camera trace")) {
    if (ukiyoemount::camera_link::active()) ukiyoemount::camera_link::trace();
    else ukiyoemount::techart::trace();
  } else if (!strcmp(cmd, "camera trace clear")) {
    ukiyoemount::techart::clear_trace();
  } else if (!strcmp(cmd, "camera trace mark")) {
    ukiyoemount::techart::mark_trace();
  } else if (!strcmp(cmd, "camera stop")) {
    ukiyoemount::techart::stop();
    ukiyoemount::camera_link::stop();
  } else if (!strcmp(cmd, "camera emulate")) {
    if (ukiyoemount::camera_link::active()) {
      Serial.println("CAMERA_LINK_ACTIVE_USE_x_FIRST");
    } else {
      ukiyoemount::techart::arm();
    }
  } else if (!strcmp(cmd, "camera listen") || !strcmp(cmd, "camera probe")) {
    if (ukiyoemount::camera_link::active() || ukiyoemount::techart::active()) {
      Serial.println("CAMERA_ALREADY_ACTIVE_USE_camera_stop");
    } else {
      ukiyoemount::camera_link::start(!strcmp(cmd, "camera probe"));
    }
  } else if (!strcmp(cmd, "status")) {
    print_status();
  } else if (cmd[0]) {
    Serial.println("UNKNOWN_COMMAND (type help)");
  }
}

void poll_command() {
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (command_used) {
        command[command_used] = '\0';
        handle_command(command);
        command_used = 0;
      }
    } else if (ch >= 32 && ch <= 126) {
      if (command_used + 1 < sizeof(command)) {
        command[command_used++] = static_cast<char>(ch);
      } else {
        command_used = 0;
        Serial.println("COMMAND_TOO_LONG");
      }
    }
  }
}

}  // namespace

void setup() {
  const uint32_t sketch_entry_ms = millis();
  safe_pins();
  // Set the supply gate and persistent standby BEFORE initializing USB CDC.
  ukiyoemount::techart::boot_auto_arm(sketch_entry_ms, millis());
  Serial.begin(115200);  // No wait for Serial or USB host.
  // USB CDC configures the XIAO RX/TX LEDs; apply our selected LED policy afterwards.
  ukiyoemount::techart::status_leds_init();
  ukiyoemount::techart::record_setup_done(millis());
}

void loop() {
  // Service the camera first; USB and LED are lower-priority diagnostics.
  ukiyoemount::techart::poll();
  ukiyoemount::camera_link::poll();
  if (Serial && !banner_printed) {
    banner_printed = true;
    Serial.print("UKIYO-E-MOUNT "); Serial.print(UKIYO_PROJECT_VERSION_LABEL);
    Serial.println(" READY / AUTO-ARMED");
    Serial.println("Type h, l, p, e, s, t or x. x disables auto-arm; e re-enables.");
  }
  poll_command();
  ukiyoemount::techart::poll_trace();
  ukiyoemount::camera_link::poll();
}
