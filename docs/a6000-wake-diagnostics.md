# A6000 post-0x16 wake diagnostic capture (v0.4.2)

This is an **instrumentation-only** branch based on main commit
`d48aeb9`. It does not fix shutdown handling, change manual-focus
responses, or alter the autonomous state machine.

## Why this revision

The first test showed mostly normal class 0x01 / IDs 0x03 and 0x04,
then shutdown 0x16. A second trace showed an ADC-qualified OFF/ON
transition followed by a new full handshake starting at 0x01;
the observed 0x0B belonged **inside that full handshake**, not to a
confirmed short wake. The user also observed the camera resume after
about 2-4 seconds with native Sony lenses but require a much longer
wait with Ukiyo.

The earlier diagnostic ring was filled by repetitive live requests.
This version only starts capturing at 0x16 and records physical bus
activity that can precede UART traffic.

## Capture format

- **EVT 0** is the first valid init-class 0x16 seen after `tr`.
- Ignore valid or recognizably malformed 0x0A, and **valid** normal-class
  0x03/0x04. Corrupted 0x03/0x04 candidates remain visible. No
  pre-shutdown traffic remains in the dump.
- Record other valid frames, checksum failures, malformed headers,
  truncated candidates and qualified ADC supply transitions.
- After 0x16, capture the **first 32 BODY_CS edges** with microsecond
  timestamps and a snapshot of exposed pin levels; count but omit further
  edges so the buffer still has room for subsequent events.
- Poll pin levels passively every 20 ms in the main loop and record changes
  except VD-only (frame-sync) transitions; write an idle snapshot every 2 s
  even if no bus edge occurs.
- Retain up to 128 events after shutdown; freeze on a successful-looking
  resume sequence once a 0x01 or 0x0B has been received followed by 0x10.
  A full buffer also freezes. Capture is otherwise allowed to remain
  active through multiple failed user attempts.
- No USB I/O from the protocol ISR; no test-only GPIO writes, pin direction
  changes, or injection of synthetic E-mount messages.

`KIND` is:
1 valid frame; 2 malformed/length/terminator; 3 bad checksum;
4 truncated; 5 supply ON; 6 supply OFF; 7 BODY_CS rising;
8 BODY_CS falling; 9 pin mask changed; 10 idle/heartbeat; 11 user marker.

For kinds 7-10, `PINS` is a bitmask of read levels (1=HIGH):
`1` BODY_CS; `2` BODY_VD; `4` XIAO RX receiving body TX;
`8` XIAO TX feeding body RX; `16` LENS_CS.
`T_MS` and `T_US` are MCU uptime counters with wraparound.
For supply kinds 5/6, `LEN_OR_ADC` means the qualified ADC10 sample.

## Test procedure

1. Build on XIAO with PlatformIO; inspect RAM/flash usage and retain a
   known-good firmware for rollback. Do not merge this branch into main
   before validation.
2. In the serial monitor, send `tr` to clear and arm the capture.
3. Let the a6000 enter power-save sleep. The first valid 0x16 begins
   the trace automatically. No monitor is required for collection.
4. Half-press the shutter several times soon after screen-off and
   during the next 5-15 seconds. Note when each attempt happened.
   Optional: use `tm` before a press to insert a USER_MARK event.
   `tm` is a manual timestamp; it is **not** shutter detection.
5. Once the camera recovers or remains stuck, send `s` and `t`
   when bus activity has stopped. Prefer full text logs over a cropped
   screenshot; preserve all `EVT` lines.
6. Compare whether BODY_CS edge pairs or XIAO RX pin changes occurred
   before any 0x01 / 0x0B, and whether TX remains high after 0x16.
7. Use `tr` to reset for the next wake trial. Capture auto-freezes
   after the next 0x10 following a resumed 0x01/0x0B or when full.

The camera shutter switch is not wired to XIAO; **if the body does not
alter a monitored E-mount line when pressed, attempts cannot be
inferred directly from trace events.** The 20-ms sampling misses
brief pin pulses; BODY_CS is timestamped through the existing IRQ
while that IRQ remains enabled. During supply-loss fault recovery,
the protocol abort disables EIC callbacks, so some edges may be missed.
A logic analyzer would provide stronger electrical evidence.

## Working hypothesis and safety

The responder acknowledges 0x16 but does not register
`em_set_powerdown_handler()`; the body may therefore await the lens
TX/RXD-low shutdown indication before it initiates wake. Without the
handler, the protocol also ignores init requests other than 0x01
while `em_shutdown_acked` is set. The hypothesis is unverified on
a6000; **do not conclude that accepting 0x0B by itself is a fix**.

Neither voltage measurements nor long-term power draw can be inferred
from the pin logic snapshots. Do not short contacts or alter external
power wiring during this capture. The original power gate and existing
protocol responses are unchanged.
