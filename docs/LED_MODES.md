# XIAO SAMD21 LED build modes

The XIAO has three GPIO-controlled, active-low LEDs and a separate hardware power LED.

| LED | Arduino pin | Debug build behaviour |
|---|---:|---|
| User | `LED_BUILTIN` (13) | Solid on while camera LOGIC_VCC is detected |
| RX | `PIN_LED_RXL` (12) | 100 ms pulse when the body handshake completes (0x0A answered) |
| TX | `PIN_LED_TXL` (11) | 300 ms pulse when a protocol/transport diagnostic counter reports an error |
| Power | Hardware-connected to 3V3 | Controlled by board power; not software-controllable |

The blue LEDs are status indicators, not traffic indicators. Normal frames, status traffic and keepalive messages do not produce LED activity.

The TX error indication is triggered by:
- malformed frames (length, terminator or checksum failure);
- dropped init requests when the request queue is full;
- unknown or incomplete records in the body's focus-instruction stream;
- receive-ring overflow reported by the UART layer.

Unknown init-message IDs are not treated as errors: the original adapter intentionally ignores many IDs, so lighting an error LED for every such request would create false alarms.

## Build targets

- `pio run -e seeed_xiao` — debug LED mode (default).
- `pio run -e seeed_xiao_production` — production mode; all GPIO-controlled LEDs are disabled.

Upload either build with the matching environment, for example:

- `pio run -e seeed_xiao -t upload`
- `pio run -e seeed_xiao_production -t upload`

The compile-time switch is `UKIYO_DEBUG_LEDS`. It defaults to `1`; the production environment defines it as `0`. Debug-only LED state and update code are removed by the preprocessor in production builds.

## USB CDC interaction

The Arduino SAMD USB CDC core also uses the XIAO RX/TX pins. The firmware therefore leaves those pins as inputs between status pulses. For a status indication, it briefly enables the corresponding active-low output and returns it to high impedance afterwards. Ordinary USB monitor traffic cannot independently illuminate the RX/TX LEDs.

The LED GPIO policy is applied after `Serial.begin()`, because USB CDC initialization configures the RX/TX pins. Production mode returns RX/TX to input/high-impedance and holds the User LED output HIGH. USB serial diagnostics remain available.

The separate hardware Power LED is not connected to an MCU GPIO. Cover it with an opaque insulating material if light leakage inside the adapter is undesirable; do not attempt to disable it in firmware.
