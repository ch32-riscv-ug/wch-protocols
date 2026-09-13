#!/usr/bin/env python3
"""Measure sustained WinUSB bulk OUT and IN against the E104 P4 firmware."""

from __future__ import annotations

import argparse
import statistics
import struct
import sys
import time
from pathlib import Path

try:
    import usb.backend.libusb1
    import usb.core
    import usb.util
except ImportError:
    sys.exit("run with `uv run --with pyusb python host_windows.py`")

VID = 0x303A
PID = 0x4021
USB_SPEED_HIGH = 3
BASE_PATTERN = bytes(range(256))


def make_backend(dll: Path | None):
    if dll is None:
        try:
            import libusb
        except ImportError:
            sys.exit("install the DLL provider too: `uv run --with pyusb --with libusb ...`")
        dll_name = libusb.dll._name
    else:
        dll_name = str(dll)
    backend = usb.backend.libusb1.get_backend(find_library=lambda _: dll_name)
    if backend is None:
        sys.exit(f"could not load libusb backend DLL: {dll_name}")
    return backend


def open_device(backend):
    device = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
    if device is None:
        sys.exit(f"no WinUSB device {VID:04x}:{PID:04x}")
    try:
        configuration = device.get_active_configuration()
    except usb.core.USBError:
        device.set_configuration()
        configuration = device.get_active_configuration()

    interface = next(
        (candidate for candidate in configuration if candidate.bInterfaceClass == 0xFF),
        None,
    )
    if interface is None:
        sys.exit("no vendor interface")
    usb.util.claim_interface(device, interface.bInterfaceNumber)
    endpoint_in = usb.util.find_descriptor(
        interface,
        custom_match=lambda ep: usb.util.endpoint_type(ep.bmAttributes)
        == usb.util.ENDPOINT_TYPE_BULK
        and usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN,
    )
    endpoint_out = usb.util.find_descriptor(
        interface,
        custom_match=lambda ep: usb.util.endpoint_type(ep.bmAttributes)
        == usb.util.ENDPOINT_TYPE_BULK
        and usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_OUT,
    )
    if endpoint_in is None or endpoint_out is None:
        sys.exit("no vendor bulk IN/OUT endpoint pair")
    return device, endpoint_in, endpoint_out


def expected_bytes(offset: int, length: int) -> bytes:
    phase = offset & 0xFF
    repeats = (phase + length + 255) // 256
    return (BASE_PATTERN * repeats)[phase : phase + length]


def drain(endpoint_in) -> None:
    try:
        while endpoint_in.read(65536, timeout=20):
            pass
    except usb.core.USBError:
        pass


def command(endpoint_out, direction: bytes, total: int) -> None:
    packet = direction + struct.pack("<Q", total)
    written = endpoint_out.write(packet, timeout=2000)
    if written != len(packet):
        raise RuntimeError(f"short command write: {written}/{len(packet)}")


def query_status(endpoint_in, endpoint_out) -> str:
    written = endpoint_out.write(b"EQ", timeout=2000)
    if written != 2:
        raise RuntimeError(f"short status query: {written}/2")
    return bytes(endpoint_in.read(4096, timeout=5000)).decode("ascii", "replace").strip()


def run_out(endpoint_in, endpoint_out, total: int, write_size: int) -> dict:
    drain(endpoint_in)
    command(endpoint_out, b"E0", total)
    payload = BASE_PATTERN * ((write_size + 255) // 256)
    sent = 0
    started = time.perf_counter()
    while sent < total:
        want = min(write_size, total - sent)
        block = payload[:want] if (sent & 0xFF) == 0 else expected_bytes(sent, want)
        written = endpoint_out.write(block, timeout=10000)
        if written <= 0:
            raise RuntimeError(f"empty OUT write at {sent}")
        sent += written
    elapsed = time.perf_counter() - started
    status = query_status(endpoint_in, endpoint_out)
    return {
        "direction": "OUT",
        "bytes": sent,
        "elapsed_s": elapsed,
        "rate_mb_s": sent / elapsed / 1e6,
        "host_bad": 0,
        "status": status,
    }


def run_in(endpoint_in, endpoint_out, total: int, read_size: int) -> dict:
    drain(endpoint_in)
    command(endpoint_out, b"E1", total)
    received = 0
    bad_blocks = 0
    started = time.perf_counter()
    while received < total:
        want = min(read_size, total - received)
        block = bytes(endpoint_in.read(want, timeout=10000))
        if not block:
            raise RuntimeError(f"empty IN read at {received}")
        if block != expected_bytes(received, len(block)):
            bad_blocks += 1
        received += len(block)
    elapsed = time.perf_counter() - started
    status = query_status(endpoint_in, endpoint_out)
    return {
        "direction": "IN",
        "bytes": received,
        "elapsed_s": elapsed,
        "rate_mb_s": received / elapsed / 1e6,
        "host_bad": bad_blocks,
        "status": status,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--megabytes", type=int, default=512)
    parser.add_argument("--io-size", type=int, default=1024 * 1024)
    parser.add_argument(
        "--libusb-dll",
        type=Path,
        default=None,
        help="libusb-1.0.dll path; default uses the PyPI libusb package",
    )
    arguments = parser.parse_args()
    total = arguments.megabytes * 1024 * 1024

    backend = make_backend(arguments.libusb_dll)
    device, endpoint_in, endpoint_out = open_device(backend)
    speed = getattr(device, "speed", None)
    print(
        f"device={VID:04x}:{PID:04x} speed={speed} "
        f"({'high' if speed == USB_SPEED_HIGH else 'NOT-HIGH'}) "
        f"in=0x{endpoint_in.bEndpointAddress:02x}/mps={endpoint_in.wMaxPacketSize} "
        f"out=0x{endpoint_out.bEndpointAddress:02x}/mps={endpoint_out.wMaxPacketSize} "
        f"bytes_per_direction={total} io_size={arguments.io_size}"
    )

    rows: list[dict] = []
    try:
        for run in range(1, arguments.runs + 1):
            for runner in (run_out, run_in):
                row = runner(endpoint_in, endpoint_out, total, arguments.io_size)
                rows.append(row)
                print(
                    f"run={run} dir={row['direction']} bytes={row['bytes']} "
                    f"elapsed_s={row['elapsed_s']:.6f} host_mb_s={row['rate_mb_s']:.3f} "
                    f"host_bad={row['host_bad']} {row['status']}",
                    flush=True,
                )
    finally:
        usb.util.dispose_resources(device)

    rates = {
        direction: [row["rate_mb_s"] for row in rows if row["direction"] == direction]
        for direction in ("OUT", "IN")
    }
    for direction in ("OUT", "IN"):
        values = rates[direction]
        print(
            f"summary dir={direction} median={statistics.median(values):.3f} "
            f"min={min(values):.3f} max={max(values):.3f} n={len(values)}"
        )
    print(f"summary out_in_ratio={statistics.median(rates['OUT']) / statistics.median(rates['IN']):.3f}")
    return 0 if all(row["host_bad"] == 0 and "bad=0" in row["status"] for row in rows) else 2


if __name__ == "__main__":
    raise SystemExit(main())
