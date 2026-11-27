# Extend XIAO SAM-BA port discovery and avoid a full application erase.
#
# Loaded as a post extra_script: PlatformIO has already installed its
# WaitForNewSerialPort method, but the upload action has not run yet.
Import("env")

import sys
from time import monotonic, sleep

from serial import Serial, SerialException
from platformio.public import list_serial_ports

WAIT_TIMEOUT_SECONDS = 15.0
SAME_PORT_FALLBACK_SECONDS = 3.0
POLL_INTERVAL_SECONDS = 0.25


def wait_for_new_serial_port(env, before):
    previous_ports = [item["port"] for item in before]
    previous_snapshot = set(previous_ports)
    previous_port = env.subst("$UPLOAD_PORT")
    new_port = None
    current_ports = previous_ports
    started = monotonic()
    deadline = started + WAIT_TIMEOUT_SECONDS
    saw_previous_port_disappear = previous_port not in previous_snapshot

    print(
        "Waiting for the XIAO SAM-BA upload port "
        "(up to %.0f seconds)..." % WAIT_TIMEOUT_SECONDS
    )

    while monotonic() < deadline and new_port is None:
        entries = list_serial_ports()
        current_ports = [item["port"] for item in entries]

        for port in current_ports:
            if port not in previous_snapshot:
                new_port = port
                break

        if previous_port not in current_ports:
            saw_previous_port_disappear = True
        previous_snapshot = set(current_ports)
        if (
            new_port is None
            and previous_port in current_ports
            and not saw_previous_port_disappear
            and monotonic() - started >= SAME_PORT_FALLBACK_SECONDS
        ):
            # No USB disconnect/re-enumeration was observed. Try the original
            # port now; the upload retry task will issue a fresh 1200-baud touch
            # if the application port was not actually switched to SAM-BA.
            new_port = previous_port
        if new_port is None:
            sleep(POLL_INTERVAL_SECONDS)

    if new_port is None and previous_port in current_ports:
        # Some systems reuse the same tty name after USB re-enumeration.
        new_port = previous_port

    if not new_port:
        sys.stderr.write(
            "ERROR: XIAO bootloader port did not appear within %.0f seconds. "
            "Check the USB connection or enter bootloader mode with the "
            "documented double-reset procedure.\n" % WAIT_TIMEOUT_SECONDS
        )
        env.Exit(1)

    try:
        serial_port = Serial(new_port)
        serial_port.close()
    except SerialException:
        # The USB device has appeared but its CDC endpoint is not ready yet.
        sleep(1)

    return new_port


# BOSSA's D2xNvmFlash auto-erase normally erases only rows being written
# when no explicit --erase is requested. The standard PlatformIO SAM-BA
# flags include --erase, which also destroys our reserved profile region.
# Keep the original write/verify/reset/USB flags and port-discovery action.
if env.subst("$UPLOAD_PROTOCOL") == "sam-ba":
    flags = list(env["UPLOADERFLAGS"])
    filtered = [flag for flag in flags if flag not in ("--erase", "-e")]
    env.Replace(UPLOADERFLAGS=filtered)
    print("UKIYO_BOSSA_FULL_ERASE=DISABLED")

env.AddMethod(wait_for_new_serial_port, "WaitForNewSerialPort")
