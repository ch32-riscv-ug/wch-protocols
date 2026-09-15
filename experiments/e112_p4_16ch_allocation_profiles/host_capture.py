#!/usr/bin/env python3
"""Arm E109 (E108 data path) and validate the live stream; soak-capable.

Same 16-byte command as E107. Flags: 0x01 skip the device-side Gray check,
0x02 force every stage through the PSRAM spill path.
"""

from __future__ import annotations

import argparse
import queue
import struct
import sys
import threading
import time

try:
    import usb1
except ImportError:
    sys.exit("run with `uv run --with libusb1 python host_capture.py`")

VID = 0x303A
PID = 0x4021
EP_OUT = 0x01
EP_IN = 0x81
def from_gray(value: int) -> int:
    value ^= value >> 1
    value ^= value >> 2
    value ^= value >> 4
    return value & 0xFF


PROFILES = {
    # name: (command letter, block samples, wire bytes, fast lanes, snapshot spec)
    # snapshot spec: (byte offset, bits per snapshot, lane base, snapshots per block)
    "legacy": ("6", 64, 25, 3, None),
    "eight": ("8", 64, 25, 3, None),
    "wide": ("W", 128, 53, 3, None),
    "four": ("F", 128, 70, 4, (64, 12, 4, 4)),
    "five": ("V", 128, 86, 5, (80, 11, 5, 4)),
}


class Validator:
    def __init__(self, width: int, wide_profile: bool = False, profile: str | None = None) -> None:
        self.width = width
        self.wide_profile = wide_profile
        self.profile = profile or ("wide" if wide_profile else ("eight" if width == 8 else "legacy"))
        _, self.block_samples, self.wire_block_bytes, self.fast_lanes, self.snapshot = PROFILES[self.profile]
        self.buffer = bytearray()
        self.bad = 0
        self.mirror_bad = 0
        self.examples: list[str] = []
        self.blocks = 0
        self.previous_binary: int | None = None

    def _feed_allocation_block(self, block: bytearray) -> None:
        """E112 profiles: N full lanes packed N bits/sample, then D32 snapshots of the upper lanes."""
        lanes = self.fast_lanes
        mask = (1 << lanes) - 1
        fast_bytes = self.block_samples * lanes // 8
        packed = int.from_bytes(block[:fast_bytes], "little")
        fast = [(packed >> (lanes * i)) & mask for i in range(self.block_samples)]
        offset, bits, lane_base, count = self.snapshot
        d = int.from_bytes(block[offset:offset + 6], "little")
        snaps = [(d >> (bits * k)) & ((1 << bits) - 1) for k in range(count)]
        step = self.block_samples // count  # samples between snapshots (32)
        # Lanes 8..15 mirror lanes 0..7: the mirrored low lanes must equal the fast
        # value at the snapshot sample, and the mirrored upper lanes the snapshot's own.
        upper = 8 - lane_base  # lanes lane_base..7 carried only in the snapshot
        for k, snap in enumerate(snaps):
            mirrored_low = (snap >> (8 - lane_base)) & mask
            if mirrored_low != fast[k * step]:
                self.mirror_bad += 1
            mirrored_upper = (snap >> (8 - lane_base + lanes)) & ((1 << upper) - 1)
            if mirrored_upper != (snap & ((1 << upper) - 1)):
                self.mirror_bad += 1
        gray0 = fast[0] | ((snaps[0] & ((1 << upper) - 1)) << lanes)
        binary0 = from_gray(gray0)
        symbol = binary0
        failed_at: int | None = None
        for sample_index, value in enumerate(fast[1:], 1):
            if value == ((symbol ^ (symbol >> 1)) & mask):
                continue
            for delta in (1, 2):
                candidate = (symbol + delta) & 0xFF
                if value == ((candidate ^ (candidate >> 1)) & mask):
                    symbol = candidate
                    break
            else:
                self.bad += 1
                failed_at = sample_index
                break
        # Snapshot k carries the full Gray code (fast + upper lanes) at sample k*step.
        for k, snap in enumerate(snaps[1:], 1):
            gray_k = fast[k * step] | ((snap & ((1 << upper) - 1)) << lanes)
            delta = (from_gray(gray_k) - binary0) & 0xFF
            expected = k * step // 4
            if not expected - 2 <= delta <= expected + 2:
                self.bad += 1
        progress = (symbol - binary0) & 0xFF
        progress_min, progress_max = (30, 34)
        if not progress_min <= progress <= progress_max:
            self.bad += 1
        if (failed_at is not None or not progress_min <= progress <= progress_max) and len(self.examples) < 3:
            self.examples.append(
                f"block={self.blocks} binary0={binary0} failed_at={failed_at} progress={progress} "
                f"fast={''.join(format(v, 'x') for v in fast[:32])}...")
        if self.previous_binary is not None:
            delta = (binary0 - self.previous_binary) & 0xFF
            if not progress_min <= delta <= progress_max:
                self.bad += 1
                if len(self.examples) < 3:
                    self.examples.append(
                        f"block={self.blocks} previous={self.previous_binary} binary0={binary0} delta={delta}")
        self.previous_binary = binary0
        self.blocks += 1

    def reset_chain(self) -> None:
        """Forget block alignment and the previous block: used when validating a sampled transfer."""
        self.buffer = bytearray()
        self.previous_binary = None

    def feed(self, data: bytes) -> None:
        self.buffer.extend(data)
        while len(self.buffer) >= self.wire_block_bytes:
            block = self.buffer[:self.wire_block_bytes]
            del self.buffer[:self.wire_block_bytes]
            if self.snapshot is not None:
                self._feed_allocation_block(block)
                continue
            fast_bytes = 48 if self.wide_profile else 24
            packed = int.from_bytes(block[:fast_bytes], "little")
            fast = [(packed >> (3 * i)) & 7 for i in range(self.block_samples)]
            if self.wide_profile:
                d8 = int.from_bytes(block[48:50], "little")
                d64 = int.from_bytes(block[50:53], "little")
                gray0 = fast[0] | ((d8 & 1) << 3)
                gray0 |= sum(((d64 >> ((lane - 4) * 2)) & 1) << lane for lane in range(4, 8))
                gray64 = fast[64] | (((d8 >> 8) & 1) << 3)
                gray64 |= sum(((d64 >> ((lane - 4) * 2 + 1)) & 1) << lane for lane in range(4, 8))
                for half, gray in enumerate((gray0, gray64)):
                    for lane in range(8, 16):
                        actual = (d64 >> ((lane - 4) * 2 + half)) & 1
                        if actual != ((gray >> (lane - 8)) & 1):
                            self.mirror_bad += 1
            else:
                slow = block[24]
                gray0 = fast[0] | ((slow & 0x1F) << 3)
                # 16-bit RX mirrors lanes 0..2; 8-bit RX defines these as padding.
                expected_upper = fast[0] if self.width == 16 else 0
                if (slow >> 5) != expected_upper:
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
            progress_min, progress_max = ((30, 34) if self.wide_profile else (14, 17))
            if not progress_min <= progress <= progress_max:
                self.bad += 1
            if (failed_at is not None or not progress_min <= progress <= progress_max) and len(self.examples) < 3:
                self.examples.append(
                    f"block={self.blocks} binary0={binary0} failed_at={failed_at} "
                    f"progress={progress} fast={''.join(format(v, 'x') for v in fast)}"
                )
            if self.previous_binary is not None:
                delta = (binary0 - self.previous_binary) & 0xFF
                if not progress_min <= delta <= progress_max:
                    self.bad += 1
                    if len(self.examples) < 3:
                        self.examples.append(
                            f"block={self.blocks} previous={self.previous_binary} binary0={binary0} delta={delta}"
                        )
            self.previous_binary = binary0
            self.blocks += 1


def cpu_summary(status: str) -> str:
    """Per-core busy fractions over the capture window from the status line."""
    fields = dict(token.split("=", 1) for token in status.split() if "=" in token)
    try:
        wall = int(fields.get("stat_wall_us") or fields["elapsed_us"])
        idle0 = int(fields["idle0_us"])
        idle1 = int(fields["idle1_us"])
        usbd = int(fields["usbd_us"])
    except (KeyError, ValueError):
        return "cpu: no run-time stats in status"
    if wall <= 0:
        return "cpu: empty window"
    parts = [f"cpu window_s={wall / 1e6:.3f}",
             f"core0_busy={100 * (1 - idle0 / wall):.1f}%",
             f"core1_busy={100 * (1 - idle1 / wall):.1f}%",
             f"usbd={100 * usbd / wall:.1f}%"]
    for key, label in (("usb_task_us", "usb_task"), ("spool_task_us", "spool_task"),
                       ("codec_task_us", "codec_task"), ("codec0_task_us", "codec0_task"),
                       ("codec1_task_us", "codec1_task")):
        if key in fields:
            parts.append(f"{label}={100 * int(fields[key]) / wall:.1f}%")
    try:
        spin = int(fields["spin_count"])
        spin_us = int(fields["spin_us"])
        calib = int(fields["spin_calib_per_s"])
        if spin and spin_us and calib:
            parts.append(f"core0_free_spin={100 * spin / (calib * spin_us / 1e6):.1f}%")
    except (KeyError, ValueError):
        pass
    for key in ("usb_core", "rx_isr_core", "flags", "workers", "chunk_bytes", "short_chunks", "stage_waits",
                "chunks0", "chunks1", "chunk_min", "chunk_max", "stage_wait_timeouts", "aborted", "codec_o2", "codec_prefetch", "direct_segments",
                "bounce_segments", "spill_bytes", "spill_high_water", "arm_failures", "completions"):
        if key in fields:
            parts.append(f"{key}={fields[key]}")
    return " ".join(parts)


def read_status(handle, attempts: int = 4, timeout_ms: int = 5000) -> str:
    """Read the device's status line, tolerating a late or missing line."""
    for _ in range(attempts):
        try:
            status = bytes(handle.bulkRead(EP_IN, 2048, timeout=timeout_ms)).decode("ascii", "replace").strip()
        except usb1.USBErrorTimeout:
            status = ""
        if status:
            return status
    return ""


def main() -> int:
    # The status line is printed as-is; a Windows console with a legacy code
    # page would otherwise raise on a stray replacement character.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--periods", type=int, default=64,
                        help="number of 8192-block / 204800-byte periods")
    parser.add_argument("--transfer-size", type=int, default=1024 * 1024)
    parser.add_argument("--depth", type=int, default=8)
    parser.add_argument("--no-validate", action="store_true")
    parser.add_argument("--keep-mib", type=int, default=768,
                        help="memory budget for sampled transfers kept for post-capture validation")
    parser.add_argument("--probe-bytes", type=int, default=0,
                        help="run USB-only EP probe instead of capture")
    parser.add_argument("--rate-mhz", type=float, default=32.0,
                        help="PARLIO base rate for capture")
    parser.add_argument("--width", type=int, choices=(8, 16), default=16,
                        help="PARLIO input width; 8 means 3 fast + 5 D=64")
    parser.add_argument("--internal", action="store_true",
                        help="benchmark capture/codec/PSRAM while discarding output")
    parser.add_argument("--wide-profile", action="store_true",
                        help="16ch profile: 3 full + 1 D8 + 12 D64, 128 samples/53 bytes")
    parser.add_argument("--profile", choices=("four", "five"), default=None,
                        help="E112 16ch allocation profiles: four = 4 full + 12 D32 (70 B), five = 5 full + 11 D32 (86 B)")
    parser.add_argument("--no-check", action="store_true",
                        help="flag 0x01: skip the device-side Gray sequence check")
    parser.add_argument("--no-direct", action="store_true",
                        help="flag 0x02: force every stage through the PSRAM spill path")
    parser.add_argument("--single-core", action="store_true",
                        help="flag 0x10: one codec worker on core 1 only (E108 layout) for comparison")
    parser.add_argument("--load-probe", action="store_true",
                        help="flag 0x04: run a priority-1 spin task on core 0 to measure ISR-inclusive free time")
    parser.add_argument("--flags", type=lambda v: int(v, 0), default=0,
                        help="raw flags byte, ORed with the switches above")
    parser.add_argument("--validate-every", type=int, default=1,
                        help="soak mode: fully validate every Nth 1 MiB transfer on a worker thread, count the rest")
    parser.add_argument("--drain", action="store_true",
                        help="read and discard stale device output before arming")
    args = parser.parse_args()
    flags = args.flags
    flags |= 0x01 if args.no_check else 0
    flags |= 0x02 if args.no_direct else 0
    flags |= 0x04 if args.load_probe else 0
    flags |= 0x10 if args.single_core else 0
    blocks = args.periods * 8192
    if args.wide_profile and args.width != 16:
        parser.error("--wide-profile requires --width 16")
    if args.profile:
        args.width = 16
    validator = Validator(args.width, args.wide_profile, args.profile)
    wire_block_bytes = validator.wire_block_bytes
    total = args.probe_bytes or blocks * wire_block_bytes
    probe = args.probe_bytes > 0
    captured = bytearray()

    with usb1.USBContext() as context:
        handle = context.openByVendorIDAndProductID(VID, PID, skip_on_error=True)
        if handle is None:
            sys.exit(f"no device {VID:04x}:{PID:04x}")
        with handle.claimInterface(0):
            if args.drain:
                # A previous run that ended in a host error may have left its
                # status line in the device TX FIFO. Opt-in: on Windows/WinUSB a
                # timed-out read right after enumeration desynchronised the pipe.
                stale = 0
                while True:
                    try:
                        stale += len(handle.bulkRead(EP_IN, 8192, timeout=200))
                    except usb1.USBErrorTimeout:
                        break
                if stale:
                    print(f"drained_stale_bytes={stale}")
            rate_hz = round(args.rate_mhz * 1_000_000)
            # E108 command: 2-byte prefix, uint32 rate, uint64 blocks, flags, 0.
            width_code = PROFILES[validator.profile][0].encode("ascii")
            prefix = b"EP" if probe else ((b"I" if args.internal else b"E") + width_code)
            command = (prefix + struct.pack(
                "<IQBB", 0 if probe else rate_hz, total if probe else blocks, flags, 0
            ))
            assert len(command) == 16
            assert handle.bulkWrite(EP_OUT, command, timeout=2000) == len(command)
            if args.internal:
                started = time.perf_counter()
                status = bytes(handle.bulkRead(EP_IN, 2048, timeout=60000)).decode("ascii", "replace").strip()
                elapsed = time.perf_counter() - started
                print(f"internal=1 elapsed_s={elapsed:.6f} {status}")
                print(cpu_summary(status))
                expected_profile = validator.profile if validator.profile in ("wide", "four", "five") else "legacy"
                clean = (f"width={args.width}" in status and f"profile={expected_profile}" in status and
                         "sink_only=1" in status and
                         f"encoded={blocks}" in status and "queue_overflow=0" in status and
                         "fifo_overflow=0" in status and "raw_sequence_bad=0" in status)
                return 0 if clean else 2
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

            sample_every = max(1, args.validate_every)
            last_data = [b""]
            samples: "queue.Queue[tuple[int, bytes] | None]" = queue.Queue()
            sampled = skipped = completed_transfers = 0
            worker_blocks = 0
            worker_lock = threading.Lock()

            # Sampled transfers are validated AFTER the capture, never on a thread
            # while URBs are in flight: Python work during the capture starves URB
            # resubmission and the usbip path then degrades to ~200 ms holes
            # (E114 §4). `kept` is bounded by --keep-mib; overflow counts as skipped.
            kept: list[tuple[int, bytes]] = []
            kept_bytes = 0
            max_keep = args.keep_mib * 1024 * 1024

            def validate_kept() -> None:
                nonlocal worker_blocks
                for start, data in kept:
                    phase = start % validator.wire_block_bytes
                    skip = (validator.wire_block_bytes - phase) % validator.wire_block_bytes
                    validator.reset_chain()
                    before = validator.blocks
                    validator.feed(data[skip:])
                    worker_blocks += validator.blocks - before
                kept.clear()

            def complete(transfer: usb1.USBTransfer) -> None:
                nonlocal received, short, error, sampled, skipped, completed_transfers, kept_bytes
                active.discard(transfer)
                if transfer.getStatus() != usb1.TRANSFER_COMPLETED:
                    error = f"transfer status={transfer.getStatus()} at {received}"
                    return
                actual = transfer.getActualLength()
                if actual < requested[transfer]:
                    short += 1
                data = bytes(transfer.getBuffer()[:actual])
                last_data[0] = data
                if not args.no_validate:
                    if sample_every == 1:
                        captured.extend(data)
                    elif completed_transfers % sample_every == 0:
                        # Validation is slower than the bus: keep at most a few
                        # transfers queued and count the ones we had to drop.
                        if kept_bytes + len(data) <= max_keep:
                            kept.append((received, data))
                            kept_bytes += len(data)
                            sampled += 1
                        else:
                            skipped += 1
                completed_transfers += 1
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
                # Cancel what is still in flight, then fetch the device's status so
                # the device-side reason (overflow, abort) is visible with the error.
                for transfer in list(active):
                    try:
                        transfer.cancel()
                    except usb1.USBError:
                        pass
                deadline = time.monotonic() + 2.0
                while active and time.monotonic() < deadline:
                    context.handleEventsTimeout(0.2)
                drained = bytearray()
                for _ in range(40):
                    try:
                        piece = handle.bulkRead(EP_IN, 65536, timeout=300)
                    except usb1.USBErrorTimeout:
                        break
                    if not piece:
                        break
                    drained.extend(piece)
                status = ""
                # The device's status line may already have arrived inside the
                # data stream (it is what ended the stream early).
                for blob in (bytes(captured[-8192:]), last_data[0][-8192:], bytes(drained)):
                    # The same profiles also run on the E114 firmware (free-list stages),
                    # whose status line is prefixed E114_STATUS.
                    marker = max(blob.rfind(b"E112_STATUS"), blob.rfind(b"E114_STATUS"))
                    if marker >= 0:
                        status = blob[marker:].split(b"\n", 1)[0].decode("ascii", "replace").strip()
                        break
                if not status:
                    status = read_status(handle, attempts=3, timeout_ms=3000)
                print(f"error={error} received={received} {status}")
                print(cpu_summary(status))
                return 2
            if not args.no_validate and not probe:
                if sample_every == 1:
                    validator.feed(captured)
                else:
                    validate_kept()
                    validator.buffer = bytearray()  # sampled tails are not whole blocks
            status = read_status(handle)
            rate_mbps = received * 8 / elapsed / 1e6
            if probe and not args.no_validate:
                pattern = bytes(range(256))
                probe_bad = any(captured[offset:offset + 256] != pattern
                                for offset in range(0, len(captured) - 255, 256))
                probe_bad |= captured[-(len(captured) % 256):] != pattern[:len(captured) % 256] if len(captured) % 256 else False
            else:
                probe_bad = False
            sampling = (f"validate_every={sample_every} sampled_transfers={sampled} skipped_samples={skipped} "
                        if sample_every > 1 else "")
            print(f"bytes={received} elapsed_s={elapsed:.6f} rate_mb_s={received / elapsed / 1e6:.3f} {sampling}"
                  f"rate_mbps={rate_mbps:.3f} recommended_90pct_mbps={rate_mbps * 0.9:.3f} "
                  f"short={short} checked_blocks={validator.blocks} sequence_bad={validator.bad} "
                  f"mirror_bad={validator.mirror_bad} probe_bad={int(probe_bad)} {status}")
            for example in validator.examples:
                print(f"validation_example {example}")
            print(cpu_summary(status))
            complete_stream = received == total and not validator.buffer
            device_clean = ((probe and f"bytes={total}" in status and not probe_bad) or
                            (not probe and "queue_overflow=0" in status and
                             "fifo_overflow=0" in status and "raw_sequence_bad=0" in status and
                             "arm_failures=0" in status))
            return 0 if complete_stream and device_clean else 2


if __name__ == "__main__":
    raise SystemExit(main())
