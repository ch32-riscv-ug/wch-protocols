"""E069: how fast PSRAM-free data reaches the host over a vendor bulk endpoint.

Plan and report: README.ja.md

The host side runs in WSL over usbipd rather than on Windows, because Windows
never bound WinUSB to this device (see the report). usbip only adds overhead, so
every number here is a lower bound on what the path can do.

Requires the HS port attached to WSL:
    usbipd.exe bind --busid <n>     (administrator, once)
    usbipd.exe attach --wsl --busid <n>
"""

import array
import statistics
import time

import pytest
import serial
import usb.core
import usb.util


TEST_VID = 0x1209
TEST_PID = 0x0008
USB_BYTES = 4 * 1024 * 1024
PATTERN_PERIOD = 64 * 1024
EP_IN = 0x81
READ_SIZES = (4096, 65536, 262144, 1048576, 2097152, 4194304)
REPEATS = 3


def _expected() -> array.array:
    words = PATTERN_PERIOD // 4
    return array.array("I", [index % words for index in range(USB_BYTES // 4)])


def _open_device():
    device = usb.core.find(idVendor=TEST_VID, idProduct=TEST_PID)
    if device is None:
        pytest.skip(f"{TEST_VID:04x}:{TEST_PID:04x} not attached to this host (usbipd attach)")
    try:
        device.get_active_configuration()
    except usb.core.USBError:
        device.set_configuration()
    usb.util.claim_interface(device, 0)
    return device


def _transfer(device, console, read_size: int, expected: array.array) -> dict:
    # Leave nothing from a previous round in the pipe.
    try:
        while True:
            if not len(device.read(EP_IN, 65536, 30)):
                break
    except usb.core.USBError:
        pass

    console.reset_input_buffer()
    console.write(b"C\n")
    console.flush()

    received = bytearray()
    error = None
    started = time.perf_counter()
    while len(received) < USB_BYTES:
        want = min(read_size, USB_BYTES - len(received))
        try:
            block = device.read(EP_IN, want, 8000)
        except usb.core.USBError as exc:
            error = f"read error at {len(received)}: {exc}"
            break
        if not len(block):
            error = f"empty read at {len(received)}"
            break
        received.extend(block)
    elapsed = time.perf_counter() - started

    mismatch_at = None
    if error is None:
        got = array.array("I")
        got.frombytes(bytes(received))
        if got != expected:
            for index, (a, b) in enumerate(zip(got, expected)):
                if a != b:
                    mismatch_at = index * 4
                    break

    deadline = time.time() + 3
    device_line = ""
    while time.time() < deadline:
        line = console.readline()
        if b"SEND" in line:
            device_line = line.decode(errors="replace").strip()
            break

    return {
        "read_size": read_size,
        "received": len(received),
        "elapsed_s": elapsed,
        "rate_mb_s": (len(received) / elapsed / 1e6) if elapsed > 0 else 0.0,
        "mismatch_at": mismatch_at,
        "error": error,
        "device": device_line,
    }


def test_p4_hs_vendor_bulk_rate(dut):
    dut.expect_exact("READY E069", timeout=45)

    device = _open_device()
    expected = _expected()
    console = serial.Serial(dut.serial.port, 115200, timeout=2)
    try:
        observations = []
        for read_size in READ_SIZES:
            for _ in range(REPEATS):
                row = _transfer(device, console, read_size, expected)
                observations.append(row)
                # A stalled transfer is an observation, not a failure
                # (README.ja.md section 7-6).
    finally:
        console.close()
        usb.util.dispose_resources(device)

    medians = {}
    for read_size in READ_SIZES:
        rates = [r["rate_mb_s"] for r in observations if r["read_size"] == read_size and r["error"] is None]
        medians[read_size] = statistics.median(rates) if rates else None

    mismatches = [r for r in observations if r["mismatch_at"] is not None]
    errors = [r for r in observations if r["error"]]

    print(f"\nE069 median_mb_s_by_read_size={medians}")
    print(f"E069 transfers={len(observations)} mismatches={len(mismatches)} errors={len(errors)}")
    for row in observations:
        print(f"E069 row={row}")

    assert any(value for value in medians.values()), "no transfer completed"
