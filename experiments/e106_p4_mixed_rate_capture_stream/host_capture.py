#!/usr/bin/env python3
"""Arm E106 and validate the live PARLIO -> codec -> USB stream."""

from __future__ import annotations

import argparse
import struct
import sys
import time

try:
    import usb1
except ImportError:
    sys.exit("run with `uv run --with libusb1 python host_capture.py`")

VID = 0x303A
PID = 0x4021
EP_OUT = 0x01
EP_IN = 0x81
WIRE_BLOCK_BYTES = 25


def from_gray(value: int) -> int:
    value ^= value >> 1
    value ^= value >> 2
    value ^= value >> 4
    return value & 0xFF


class Validator:
    def __init__(self) -> None:
        self.buffer = bytearray()
        self.bad = 0
        self.mirror_bad = 0
        self.examples: list[str] = []
        self.blocks = 0
        self.previous_binary: int | None = None

    def feed(self, data: bytes) -> None:
        self.buffer.extend(data)
        while len(self.buffer) >= WIRE_BLOCK_BYTES:
            block = self.buffer[:WIRE_BLOCK_BYTES]
            del self.buffer[:WIRE_BLOCK_BYTES]
            packed = int.from_bytes(block[:24], "little")
            fast = [(packed >> (3 * i)) & 7 for i in range(64)]
            slow = block[24]
            gray0 = fast[0] | ((slow & 0x1F) << 3)
            # Mirrored RX lanes 8..10 must equal lanes 0..2.
            if (slow >> 5) != fast[0]:
                self.mirror_bad += 1
            binary0 = from_gray(gray0)
            # E042 measured 3..5 RX samples per 12 MHz source symbol at this
            # asynchronous edge. Follow Gray order rather than requiring four
            # identical samples. A carry into bit 3 is invisible in low3, so a
            # later visible change may legitimately advance by two symbols.
            symbol = binary0
            failed_at: int | None = None
            for sample_index, value in enumerate(fast[1:], 1):
                if value == ((symbol ^ (symbol >> 1)) & 7):
                    continue
                for delta in (1, 2):
                    candidate = (symbol + delta) & 0xFF
                    if value == ((candidate ^ (candidate >> 1)) & 7):
                        symbol = candidate
                        break
                else:
                    self.bad += 1
                    failed_at = sample_index
                    break
            progress = (symbol - binary0) & 0xFF
            if not 14 <= progress <= 17:
                self.bad += 1
            if (failed_at is not None or not 14 <= progress <= 17) and len(self.examples) < 3:
                self.examples.append(
                    f"block={self.blocks} binary0={binary0} failed_at={failed_at} "
                    f"progress={progress} fast={''.join(format(v, 'x') for v in fast)}"
                )
            if self.previous_binary is not None:
                delta = (binary0 - self.previous_binary) & 0xFF
                if delta not in range(15, 18):
                    self.bad += 1
                    if len(self.examples) < 3:
                        self.examples.append(
                            f"block={self.blocks} previous={self.previous_binary} binary0={binary0} delta={delta}"
                        )
            self.previous_binary = binary0
            self.blocks += 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--periods", type=int, default=64,
                        help="number of 8192-block / 204800-byte periods")
    parser.add_argument("--transfer-size", type=int, default=1024 * 1024)
    parser.add_argument("--depth", type=int, default=8)
    parser.add_argument("--no-validate", action="store_true")
    parser.add_argument("--probe-bytes", type=int, default=0,
                        help="run USB-only EP probe instead of capture")
    parser.add_argument("--rate-mhz", type=float, default=32.0,
                        help="PARLIO base rate for capture")
    args = parser.parse_args()
    blocks = args.periods * 8192
    total = args.probe_bytes or blocks * WIRE_BLOCK_BYTES
    probe = args.probe_bytes > 0
    validator = Validator()
    captured = bytearray()

    with usb1.USBContext() as context:
        handle = context.openByVendorIDAndProductID(VID, PID, skip_on_error=True)
        if handle is None:
            sys.exit(f"no device {VID:04x}:{PID:04x}")
        with handle.claimInterface(0):
            rate_hz = round(args.rate_mhz * 1_000_000)
            command = ((b"EP" if probe else b"E6") + struct.pack(
                "<IQ", 0 if probe else rate_hz, total if probe else blocks
            ))
            assert handle.bulkWrite(EP_OUT, command, timeout=2000) == len(command)
            received = planned = short = 0
            error: str | None = None
            active: set[usb1.USBTransfer] = set()
            requested: dict[usb1.USBTransfer, int] = {}

            def submit(transfer: usb1.USBTransfer, size: int, *, new: bool = True) -> None:
                nonlocal planned
                transfer.setBulk(EP_IN, size, callback=complete, timeout=10000)
                requested[transfer] = size
                active.add(transfer)
                if new:
                    planned += size
                transfer.submit()

            def complete(transfer: usb1.USBTransfer) -> None:
                nonlocal received, short, error
                active.discard(transfer)
                if transfer.getStatus() != usb1.TRANSFER_COMPLETED:
                    error = f"transfer status={transfer.getStatus()} at {received}"
                    return
                actual = transfer.getActualLength()
                if actual < requested[transfer]:
                    short += 1
                data = bytes(transfer.getBuffer()[:actual])
                if not args.no_validate:
                    captured.extend(data)
                received += actual
                missing = requested[transfer] - actual
                if missing:
                    submit(transfer, missing, new=False)
                elif planned < total:
                    submit(transfer, min(args.transfer_size, total - planned))

            transfers = [handle.getTransfer() for _ in range(args.depth)]
            for transfer in transfers:
                if planned >= total:
                    break
                submit(transfer, min(args.transfer_size, total - planned))
            started = time.perf_counter()
            while active and error is None:
                context.handleEvents()
            elapsed = time.perf_counter() - started
            if error:
                raise RuntimeError(error)
            if not args.no_validate and not probe:
                validator.feed(captured)
            status = ""
            for _ in range(4):
                status = bytes(handle.bulkRead(EP_IN, 512, timeout=5000)).decode("ascii", "replace").strip()
                if status:
                    break
            rate_mbps = received * 8 / elapsed / 1e6
            if probe and not args.no_validate:
                pattern = bytes(range(256))
                probe_bad = any(captured[offset:offset + 256] != pattern
                                for offset in range(0, len(captured) - 255, 256))
                probe_bad |= captured[-(len(captured) % 256):] != pattern[:len(captured) % 256] if len(captured) % 256 else False
            else:
                probe_bad = False
            print(f"bytes={received} elapsed_s={elapsed:.6f} rate_mb_s={received / elapsed / 1e6:.3f} "
                  f"rate_mbps={rate_mbps:.3f} recommended_90pct_mbps={rate_mbps * 0.9:.3f} "
                  f"short={short} checked_blocks={validator.blocks} sequence_bad={validator.bad} "
                  f"mirror_bad={validator.mirror_bad} probe_bad={int(probe_bad)} {status}")
            for example in validator.examples:
                print(f"validation_example {example}")
            complete_stream = received == total and not validator.buffer
            device_clean = ((probe and f"bytes={total}" in status and not probe_bad) or
                            (not probe and "queue_overflow=0" in status and
                             "fifo_overflow=0" in status and "raw_sequence_bad=0" in status))
            return 0 if complete_stream and device_clean else 2


if __name__ == "__main__":
    raise SystemExit(main())
