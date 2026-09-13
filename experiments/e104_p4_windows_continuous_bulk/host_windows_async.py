#!/usr/bin/env python3
"""Queue multiple native Windows/libusb transfers for the E104 sustained test."""

from __future__ import annotations

import argparse
import statistics
import struct
import sys
import time

try:
    import usb1
except ImportError:
    sys.exit("run with `uv run --with libusb1 python host_windows_async.py`")

VID = 0x303A
PID = 0x4021
EP_OUT = 0x01
EP_IN = 0x81
BASE_PATTERN = bytes(range(256))


def expected_bytes(offset: int, length: int) -> bytes:
    phase = offset & 0xFF
    repeats = (phase + length + 255) // 256
    return (BASE_PATTERN * repeats)[phase : phase + length]


def send_command(handle, direction: bytes, total: int) -> None:
    packet = direction + struct.pack("<Q", total)
    written = handle.bulkWrite(EP_OUT, packet, timeout=2000)
    if written != len(packet):
        raise RuntimeError(f"short command write: {written}/{len(packet)}")


def query_status(handle) -> str:
    if handle.bulkWrite(EP_OUT, b"EQ", timeout=2000) != 2:
        raise RuntimeError("short status query")
    return bytes(handle.bulkRead(EP_IN, 4096, timeout=5000)).decode("ascii", "replace").strip()


def run_out(context, handle, total: int, transfer_size: int, depth: int) -> dict:
    send_command(handle, b"E0", total)
    payload = BASE_PATTERN * ((transfer_size + 255) // 256)
    transfers = [handle.getTransfer() for _ in range(depth)]
    active: set[usb1.USBTransfer] = set()
    requested: dict[usb1.USBTransfer, int] = {}
    planned = 0
    sent = 0
    callbacks = 0
    short = 0
    error: str | None = None

    def submit(transfer, size: int, *, newly_planned: bool) -> None:
        nonlocal planned
        transfer.setBulk(EP_OUT, payload[:size], callback=completed, timeout=10000)
        requested[transfer] = size
        active.add(transfer)
        if newly_planned:
            planned += size
        transfer.submit()

    def completed(transfer) -> None:
        nonlocal sent, callbacks, short, error
        active.discard(transfer)
        callbacks += 1
        status = transfer.getStatus()
        actual = transfer.getActualLength()
        wanted = requested[transfer]
        if status != usb1.TRANSFER_COMPLETED:
            error = f"OUT transfer status={status} at {sent}"
            return
        sent += actual
        if actual < wanted:
            short += 1
            submit(transfer, wanted - actual, newly_planned=False)
        elif planned < total:
            submit(transfer, min(transfer_size, total - planned), newly_planned=True)

    for transfer in transfers:
        if planned >= total:
            break
        submit(transfer, min(transfer_size, total - planned), newly_planned=True)
    started = time.perf_counter()
    while active and error is None:
        context.handleEvents()
    elapsed = time.perf_counter() - started
    if error is not None:
        raise RuntimeError(error)
    status = query_status(handle)
    return {
        "direction": "OUT",
        "bytes": sent,
        "elapsed_s": elapsed,
        "rate_mb_s": sent / elapsed / 1e6,
        "host_bad": 0,
        "callbacks": callbacks,
        "short": short,
        "status": status,
    }


def run_in(context, handle, total: int, transfer_size: int, depth: int) -> dict:
    send_command(handle, b"E1", total)
    transfers = [handle.getTransfer() for _ in range(depth)]
    active: set[usb1.USBTransfer] = set()
    received = 0
    callbacks = 0
    short = 0
    bad = 0
    stopping = False
    error: str | None = None

    def submit(transfer) -> None:
        transfer.setBulk(EP_IN, transfer_size, callback=completed, timeout=10000)
        active.add(transfer)
        transfer.submit()

    def completed(transfer) -> None:
        nonlocal received, callbacks, short, bad, stopping, error
        active.discard(transfer)
        status = transfer.getStatus()
        if status == usb1.TRANSFER_CANCELLED and stopping:
            return
        if status != usb1.TRANSFER_COMPLETED:
            error = f"IN transfer status={status} at {received}"
            return
        callbacks += 1
        actual = transfer.getActualLength()
        if actual < transfer_size:
            short += 1
        block = bytes(transfer.getBuffer()[:actual])
        if block != expected_bytes(received, actual):
            bad += 1
        received += actual
        if received >= total:
            stopping = True
            for pending in tuple(active):
                pending.cancel()
        elif not stopping:
            submit(transfer)

    for transfer in transfers:
        submit(transfer)
    started = time.perf_counter()
    while active and error is None:
        context.handleEvents()
    elapsed = time.perf_counter() - started
    if error is not None:
        raise RuntimeError(error)
    status = query_status(handle)
    return {
        "direction": "IN",
        "bytes": received,
        "elapsed_s": elapsed,
        "rate_mb_s": received / elapsed / 1e6,
        "host_bad": bad,
        "callbacks": callbacks,
        "short": short,
        "status": status,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--megabytes", type=int, default=512)
    parser.add_argument("--transfer-size", type=int, default=1024 * 1024)
    parser.add_argument("--depth", type=int, default=8)
    arguments = parser.parse_args()
    total = arguments.megabytes * 1024 * 1024

    rows: list[dict] = []
    with usb1.USBContext() as context:
        handle = context.openByVendorIDAndProductID(VID, PID, skip_on_error=True)
        if handle is None:
            sys.exit(f"no WinUSB device {VID:04x}:{PID:04x}")
        with handle.claimInterface(0):
            device = handle.getDevice()
            print(
                f"device={VID:04x}:{PID:04x} speed={device.getDeviceSpeed()} "
                f"bytes_per_direction={total} transfer_size={arguments.transfer_size} "
                f"depth={arguments.depth}"
            )
            for run in range(1, arguments.runs + 1):
                for runner in (run_out, run_in):
                    row = runner(
                        context, handle, total, arguments.transfer_size, arguments.depth
                    )
                    rows.append(row)
                    print(
                        f"run={run} dir={row['direction']} bytes={row['bytes']} "
                        f"elapsed_s={row['elapsed_s']:.6f} host_mb_s={row['rate_mb_s']:.3f} "
                        f"callbacks={row['callbacks']} short={row['short']} "
                        f"host_bad={row['host_bad']} {row['status']}",
                        flush=True,
                    )

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
