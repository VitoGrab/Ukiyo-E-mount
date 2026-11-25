# Experimental Sony a6000 shutdown and short wake

Status: **unvalidated experiment** based on the diagnostic branch.
No build, memory budget or physical camera timing validation has been
completed. Keep the known-good v0.4.2 firmware available for rollback.

## Goal

After a camera 0x16 shutdown request, lower lens TX (XIAO D6) rather than
leaving UART idle HIGH. The earlier capture showed BODY_CS HIGH for about
2.02 seconds after a failed wake attempt. Native SEL55F18Z and
SELP18105G wake within roughly 2-4 seconds in the user's tests.

## State machine

1. Reply to 0x16 and flush TX.
2. Disable UART; drive lens TX LOW and lens CS LOW only while the
   validated LOGIC_VCC gate reports power.
3. Enter SUSPENDED and listen for BODY_CS rising on D9.
4. Assert lens CS HIGH immediately from the rising-edge IRQ, without
   USB logs or UART initialization in the interrupt.
5. When body TX (XIAO D7) is HIGH, resume UART at the previously
   negotiated speed (normally 1.5 Mbaud).
6. On BODY_CS falling, hold 200 us, lower lens CS, restore protocol
   interrupts and session-local state. Expect a short handshake
   (possibly starting with 0x0B), without forcing 0x01.
7. A qualified loss of LOGIC_VCC aborts the wake session and re-arms
   cold initialization. A stalled pin handshake times out after 2 s
   and returns to suspended wake monitoring.

The upstream blocking em_sleep_until_wake() is not called. All
wire-level timing assumptions remain subject to camera testing.

## Diagnostics

- `tr`: clear/arm post-0x16 event buffer
- `tm`: manual marker before a shutter attempt
- `s`: mode, wake success/retry/cold restart counters
- `t`: trace available **even during RUNNING**, using short,
  throttled USB CDC writes in the main loop

CDC availableForWrite() is not a strict guarantee of nonblocking output;
avoid concurrent trace dumping if timing-sensitive communication fails.

## Tests

Run `pio run -e seeed_xiao` locally and check RAM/flash sizes before
flashing. Test: cold initialization, repeated sleep / half-press wake
(at 2 s, 4 s, 10 s), full power-off/on, remount, USB detached/reconnected,
MF and EXIF regressions. Capture `s` and `t` around each failure.
Compare whether waking begins with 0x0B or a full 0x01.

**Do not merge until hardware validation**. If the camera hangs, switch
it off, disconnect the adapter and revert to the known-good firmware.
Do not modify wiring or bridge body power contacts as part of this test.
