#!/usr/bin/env python3
"""Validate a sustained E105 mixed-rate stream over native Linux/WSL libusb."""

from __future__ import annotations

import argparse
import struct
import sys
import time

try:
    import usb1
except ImportError:
    sys.exit("run with `uv run --with libusb1 python host_usb.py`")

VID = 0x303A
PID = 0x4021
EP_OUT = 0x01
EP_IN = 0x81
BLOCK_SAMPLES = 64
WIRE_BLOCK_BYTES = 25
STREAM_BLOCKS = 512
STREAM_BYTES = STREAM_BLOCKS * WIRE_BLOCK_BYTES


def make_block(block: int) -> bytes:
    output = bytearray(WIRE_BLOCK_BYTES)
    accumulator = 0
    bit_count = 0
    destination = 0
    for sample in range(BLOCK_SAMPLES):
        fast = (
            ((sample & 1) << 0)
            | ((((sample >> 1) ^ block) & 1) << 1)
            | ((((sample >> 0) ^ (sample >> 1) ^ (sample >> 2)
                  ^ (sample >> 3) ^ (sample >> 4) ^ (sample >> 5) ^ block) & 1) << 2)
        )
        accumulator |= fast << bit_count
        bit_count += 3
        while bit_count >= 8:
            output[destination] = accumulator & 0xFF
            destination += 1
            accumulator >>= 8
            bit_count -= 8
    assert destination == 24 and bit_count == 0
    slow = block & 0xFF
    output[24] = slow
    return bytes(output)


PATTERN = b"".join(make_block(block) for block in range(STREAM_BLOCKS))


def expected(offset: int, length: int) -> bytes:
    phase = offset % len(PATTERN)
    repeats = (phase + length + len(PATTERN) - 1) // len(PATTERN)
    return (PATTERN * repeats)[phase : phase + length]


def query_status(handle) -> str:
    handle.bulkWrite(EP_OUT, b"EQ", timeout=2000)
    return bytes(handle.bulkRead(EP_IN, 512, timeout=5000)).decode("ascii", "replace").strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chunks", type=int, default=5000,
                        help=f"number of {STREAM_BYTES}-byte device chunks")
    parser.add_argument("--transfer-size", type=int, default=1024 * 1024)
    parser.add_argument("--depth", type=int, default=8)
    parser.add_argument("--no-validate", action="store_true")
    args = parser.parse_args()
    total = args.chunks * STREAM_BYTES

    with usb1.USBContext() as context:
        handle = context.openByVendorIDAndProductID(VID, PID, skip_on_error=True)
        if handle is None:
            sys.exit(f"no device {VID:04x}:{PID:04x}")
        with handle.claimInterface(0):
            command = b"E5" + struct.pack("<Q", total)
            assert handle.bulkWrite(EP_OUT, command, timeout=2000) == len(command)
            received = 0
            bad = 0
            short = 0
            error: str | None = None
            stopping = False
            data_finished_at: float | None = None
            active: set[usb1.USBTransfer] = set()
            requested: dict[usb1.USBTransfer, int] = {}
            planned = 0

            def submit(
                transfer: usb1.USBTransfer, size: int, *, newly_planned: bool = True
            ) -> None:
                nonlocal planned
                transfer.setBulk(EP_IN, size, callback=completed, timeout=10000)
                requested[transfer] = size
                active.add(transfer)
                if newly_planned:
                    planned += size
                transfer.submit()

            def completed(transfer: usb1.USBTransfer) -> None:
                nonlocal received, bad, short, error, stopping, data_finished_at
                active.discard(transfer)
                status_code = transfer.getStatus()
                if status_code == usb1.TRANSFER_CANCELLED and stopping:
                    return
                if status_code != usb1.TRANSFER_COMPLETED:
                    error = f"IN transfer status={status_code} at {received}"
                    return
                actual = transfer.getActualLength()
                if actual < requested[transfer]:
                    short += 1
                data = bytes(transfer.getBuffer()[:actual])
                if not args.no_validate and data != expected(received, actual):
                    bad += 1
                received += actual
                missing = requested[transfer] - actual
                if received >= total:
                    data_finished_at = time.perf_counter()
                    stopping = True
                    for pending in tuple(active):
                        pending.cancel()
                elif not stopping and missing:
                    submit(transfer, missing, newly_planned=False)
                elif not stopping and planned < total:
                    submit(transfer, min(args.transfer_size, total - planned))

            transfers = [handle.getTransfer() for _ in range(args.depth)]
            for transfer in transfers:
                if planned >= total:
                    break
                submit(transfer, min(args.transfer_size, total - planned))
            started = time.perf_counter()
            while active and error is None:
                context.handleEvents()
            cleanup_finished = time.perf_counter()
            assert data_finished_at is not None
            elapsed = data_finished_at - started
            cleanup = cleanup_finished - data_finished_at
            if error is not None:
                raise RuntimeError(error)
            status = query_status(handle)
            print(
                f"bytes={received} elapsed_s={elapsed:.6f} rate_mb_s={received / elapsed / 1e6:.3f} "
                f"cleanup_s={cleanup:.6f} depth={args.depth} short={short} bad={bad} {status}"
            )
    return 0 if bad == 0 and f"bytes={total}" in status else 2


if __name__ == "__main__":
    raise SystemExit(main())
