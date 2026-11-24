# A6000 quick wake diagnostic capture (v0.4.2)

This is an **instrumentation-only** branch based on main commit
\`d48aeb9\`. It does not fix the shutdown handshake, change the working
manual-focus profile, or alter the autonomous state machine.

## Hypothesis

The responder acknowledges \`0x16\` and marks shutdown, but no caller
registers \`em_set_powerdown_handler()\`. In that state the current
\`em_send_id()\` intentionally ignores all initialization requests except
\`0x01\`. A short wake may start with \`0x0B\` and therefore hang. A later
camera-controlled rail drop can reset the session and produce a full
\`0x01\` handshake instead.

This is a hypothesis to verify, not a confirmed a6000 wake trace.

## Event capture

- Each valid body frame except \`0x0A\` is recorded with timestamp, declared
  length, class, ID and the first 12 bytes.
- Checksum failures, malformed frames and truncated candidates are recorded,
  but ordinary padding bytes are not; existing \`RX_SKIPPED_BYTES\` counts them.
- Recognizable \`0x0A\` headers are filtered even when malformed.
- ADC supply-detector transitions are recorded with the qualifying ADC10
  sample (in the event's \`LEN_OR_ADC\` field).
- All recording takes place in a bounded 128-entry RAM ring with **no USB
  printing from the frame parser or interrupts**.
- On the first valid \`0x16\`, capture continues for 64 further events and
  freezes. This retains prior activity and the first attempted wake.
- The trace records **12 bytes of each frame, not full payloads**. It is
  intended to identify unexpected IDs and failure reasons with minimal RAM
  cost. If it proves insufficient, use a controlled full-frame dump in a
  later experiment.
- \`KIND\`: 1 valid; 2 malformed/invalid length/terminator; 3 bad checksum;
  4 truncated bytes; 5 supply detected; 6 supply detector fell.
- \`millis()\` timestamps wrap after approximately 49.7 days; the existing
  supply gate's \`micros()\` timestamps wrap after approximately 71.6 minutes.
  Interpret both as wraparound counters, not absolute time.

## Test procedure

1. Build and flash this branch on XIAO. Keep the Sony a6000 on normal
   camera power and use the verified manual-lens setup.
2. Open USB serial monitor if wanted; the monitor is optional for capture.
3. Run \`tr\` (or \`camera trace clear\`) once before each attempt.
4. With camera awake, let it enter normal power-save sleep.
5. Half-press the shutter **quickly** after entering sleep. Record whether
   the camera recovers or locks up.
6. After the attempt, run \`s\` to collect the status and \`t\` to dump trace.
   Avoid dumping while the camera is actively exchanging frames: printing
   hundreds of lines at 115200 baud can disturb timing. Firmware defers
   \`t\` when it considers a session active and not shut down.
7. Copy the raw dump, including \`EVT\` lines and \`SHUTDOWN_*\` values.
   If frozen, run \`tr\` to re-arm the logger for another attempt.
8. Repeat after a **long** sleep and compare the first wake frame. A frame
   beginning with \`0x0B\` vs \`0x01\` is the key discriminator.

## Safety and limits

- Do not force GPIO outputs to simulate sleep, or bridge camera power pins.
- Do not treat ADC supply changes as sufficient evidence of session changes.
  The gate requires ADC10 below 310 for at least 12 ms to report power loss;
  it does **not** automatically reinitialize A0 during camera wake.
- No new shutdown handler or wake-recovery logic is enabled by this branch.
  The camera can still lock up until disconnected or power-cycled.
- This branch is a research instrument, not a release. Verify compilation
  and memory headroom on the actual PlatformIO/XIAO toolchain before camera
  testing. Preserve the known-good main firmware for rollback.
