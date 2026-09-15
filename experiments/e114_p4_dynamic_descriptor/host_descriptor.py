#!/usr/bin/env python3
"""E114: send a channel descriptor, show the device's ACCEPT/REJECT reply, capture and verify.

Channels: --ch GPIO:MODE[/D[/phase|/pol]] repeated, in wire order.
  MODE = raw | hold | any | edge ; D = 1..128 (power of two)
  hold takes /phase (0..D-1); any and edge take /low or /high (active level).
Verification uses the internal loopback source (GPIO 2..9 carry Gray bits 0..7 of
an 8-bit counter that advances every 4 samples): the expected wire stream is
periodic over 1,024 samples (8 blocks), so the host finds the start phase from the
first block and compares every block byte-exact against the reference encoding.
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
    sys.exit("run with `uv run --with libusb1 python host_descriptor.py`")

VID = 0x303A
PID = 0x4021
EP_OUT = 0x01
EP_IN = 0x81
BLOCK = 128
MODES = {"raw": 0, "hold": 1, "any": 2, "edge": 3}


def parse_channel(spec: str) -> tuple[int, int, int, int, int]:
    parts = spec.split("/")
    gpio_s, mode_s = parts[0].split(":")
    gpio, mode = int(gpio_s), MODES[mode_s]
    d = int(parts[1]) if len(parts) > 1 else 1
    if d & (d - 1) or not 1 <= d <= 128:
        raise argparse.ArgumentTypeError(f"D must be a power of two in 1..128: {spec}")
    phase = 0
    active = 0
    if len(parts) > 2:
        if parts[2] in ("low", "high"):
            active = 1 if parts[2] == "high" else 0
        else:
            phase = int(parts[2])
    return gpio, mode, d.bit_length() - 1, phase, active


def gray(n: int) -> int:
    return (n ^ (n >> 1)) & 0xFF


class Reference:
    """Expected wire bytes for the loopback source, laid out the way the device does."""

    def __init__(self, channels: list[tuple[int, int, int, int, int]], reply: dict[str, str]) -> None:
        self.channels = channels
        self.width = int(reply["width"])
        self.passthrough = reply["passthrough"] == "1"
        self.fast = int(reply["fast"])
        self.wire = int(reply["wire_block_bytes"])
        self.lanes = [int(x) for x in reply["lanes"].split(",")]
        raw = [c for c in channels if c[1] == 0]
        dec = [c for c in channels if c[1] != 0]
        self.order = raw + dec  # lane index == position in this list
        assert len(self.order) == len(self.lanes)

    def lane_bits(self, n: int) -> int:
        """16-bit lane sample for counter value n: lane i carries GPIO self.lanes[i]."""
        g = gray(n & 0xFF)
        value = 0
        for lane, gpio in enumerate(self.lanes):
            bit = (g >> (gpio - 2)) & 1 if 2 <= gpio <= 9 else 0
            value |= bit << lane
        return value

    def block(self, n0: int, phase: int) -> bytes:
        """Wire block for samples i=0..127 where sample i has counter n0 + (i + phase) // 4."""
        samples = [self.lane_bits(n0 + (i + phase) // 4) for i in range(BLOCK)]
        if self.passthrough:
            bits = 0
            for i, s in enumerate(samples):
                bits |= (s & ((1 << self.width) - 1)) << (self.width * i)
            return bits.to_bytes(self.wire, "little")
        out = bytearray()
        F = self.fast
        if F:
            for g in range(16):
                acc = 0
                for k in range(8):
                    acc |= (samples[g * 8 + k] & ((1 << F) - 1)) << (F * k)
                out += acc.to_bytes(F, "little")
        acc = 0
        bits = 0
        for lane_index, (gpio, mode, log2d, phase_c, active) in enumerate(self.order):
            if mode == 0:
                continue
            lane = lane_index
            d = 1 << log2d
            for k in range(BLOCK // d):
                bucket = [(s >> lane) & 1 for s in samples[k * d:(k + 1) * d]]
                if mode == 1:
                    v, w = bucket[phase_c], 1
                elif mode == 2:
                    hit = any(b == active for b in bucket)
                    v, w = (active if hit else 1 - active), 1
                else:
                    edge = any(bucket[i - 1] != active and bucket[i] == active for i in range(1, len(bucket)))
                    v, w = bucket[-1] | (int(edge) << 1), 2
                acc |= v << bits
                bits += w
        if bits:
            out += acc.to_bytes((bits + 7) // 8, "little")
        while len(out) < self.wire:
            out.append(0)
        assert len(out) == self.wire, (len(out), self.wire)
        return bytes(out)

    def period(self, n0: int, phase: int) -> bytes:
        return b"".join(self.block((n0 + 32 * b) & 0xFF, phase) for b in range(8))

    def find_start(self, first: bytes) -> tuple[int, int] | None:
        for phase in range(4):
            for n0 in range(256):
                if self.block(n0, phase) == first:
                    return n0, phase
        return None


def read_line(handle, timeout_ms: int = 5000) -> str:
    for _ in range(4):
        try:
            data = bytes(handle.bulkRead(EP_IN, 2048, timeout=timeout_ms)).decode("ascii", "replace").strip()
        except usb1.USBErrorTimeout:
            data = ""
        if data:
            return data
    return ""


def fields_of(line: str) -> dict[str, str]:
    return dict(tok.split("=", 1) for tok in line.split() if "=" in tok)


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ch", action="append", type=parse_channel, required=True)
    ap.add_argument("--rate-mhz", type=float, default=40.0)
    ap.add_argument("--periods", type=int, default=160, help="8192-block periods")
    ap.add_argument("--budget-mbps", type=int, default=0, help="USB budget the device rejects against (0 = none)")
    ap.add_argument("--query", action="store_true", help="only ask for ACCEPT/REJECT and the layout")
    ap.add_argument("--internal", action="store_true")
    ap.add_argument("--no-check", action="store_true", help="flag 0x01: skip the device-side Gray check")
    ap.add_argument("--single-core", action="store_true")
    ap.add_argument("--no-codec-limit", action="store_true", help="flag 0x20: calibration, ignore the bench-derived codec limit")
    ap.add_argument("--no-pulldown", action="store_true", help="flag 0x40: leave undriven lane GPIOs floating")
    ap.add_argument("--no-validate", action="store_true", help="host: no start search and no byte comparison at all")
    ap.add_argument("--keep-mib", type=int, default=256, help="memory budget for transfers kept for post-capture verification")
    ap.add_argument("--transfer-size", type=int, default=1024 * 1024)
    ap.add_argument("--depth", type=int, default=8)
    ap.add_argument("--validate-every", type=int, default=1)
    args = ap.parse_args()
    flags = (0x01 if args.no_check else 0) | (0x10 if args.single_core else 0) | (0x20 if args.no_codec_limit else 0) | (0x40 if args.no_pulldown else 0)
    rate_hz = round(args.rate_mhz * 1e6)
    blocks = 0 if args.query else args.periods * 8192
    head = (b"Q" if args.query else b"I" if args.internal else b"E") + b"D"
    head += struct.pack("<IQBB", rate_hz, blocks, flags, len(args.ch)) + struct.pack("<H", args.budget_mbps)
    body = b"".join(struct.pack("<BBBB", gpio, mode, log2d, phase | (active << 7)) for gpio, mode, log2d, phase, active in args.ch)
    command = head + body

    with usb1.USBContext() as context:
        handle = context.openByVendorIDAndProductID(VID, PID, skip_on_error=True)
        if handle is None:
            sys.exit(f"no device {VID:04x}:{PID:04x}")
        with handle.claimInterface(0):
            assert handle.bulkWrite(EP_OUT, command, timeout=2000) == len(command)
            reply = read_line(handle)
            print(reply)
            f = fields_of(reply)
            if args.query or f.get("accept") != "1" or blocks == 0:
                return 0 if f.get("accept") == "1" else 3
            wire = int(f["wire_block_bytes"])
            total = blocks * wire
            ref = Reference(args.ch, f)
            if args.internal:
                started = time.perf_counter()
                status = read_line(handle, 120000)
                print(f"internal=1 elapsed_s={time.perf_counter() - started:.6f} {status}")
                sf = fields_of(status)
                ok = sf.get("encoded") == str(blocks) and sf.get("queue_overflow") == "0" and sf.get("raw_sequence_bad") == "0" and sf.get("aborted") == "0"
                return 0 if ok else 2

            received = planned = short = 0
            error: str | None = None
            active: set[usb1.USBTransfer] = set()
            requested: dict[usb1.USBTransfer, int] = {}
            expected: bytes | None = None
            expected_len = 8 * wire
            bad_blocks = 0
            checked_blocks = 0
            carry = bytearray()
            first_block: bytes | None = None
            start: tuple[int, int] | None = None
            sample_every = max(1, args.validate_every)
            transfer_index = 0
            work: "queue.Queue[tuple[int, bytes] | None]" = queue.Queue()
            skipped = 0
            last_data = [b""]

            def verify(offset: int, data: bytes) -> None:
                nonlocal bad_blocks, checked_blocks
                if expected is None:
                    return
                # Align to a block boundary within the periodic expectation.
                head_skip = (-offset) % wire
                data = data[head_skip:]
                offset += head_skip
                whole = len(data) - len(data) % wire
                pos = 0
                while pos < whole:
                    phase = (offset + pos) % expected_len
                    run = min(whole - pos, expected_len - phase)
                    if data[pos:pos + run] != expected[phase:phase + run]:
                        for b in range(0, run, wire):
                            if data[pos + b:pos + b + wire] != expected[phase + b:phase + b + wire]:
                                bad_blocks += 1
                    checked_blocks += run // wire
                    pos += run

            # Verification runs AFTER the capture. Doing it on a thread during the
            # capture starved URB resubmission (GIL): even one 1 MiB verify made the
            # device see 150-600 ms gaps in IN completions (E114 §4).
            kept_bytes = 0
            kept: list[tuple[int, bytes]] = []
            max_keep = args.keep_mib * 1024 * 1024

            def submit(transfer: usb1.USBTransfer, size: int, *, new: bool = True) -> None:
                nonlocal planned
                transfer.setBulk(EP_IN, size, callback=complete, timeout=10000)
                requested[transfer] = size
                active.add(transfer)
                if new:
                    planned += size
                transfer.submit()

            urb_times: list[tuple[float, int]] = []  # (perf_counter, bytes received so far) per completion

            def complete(transfer: usb1.USBTransfer) -> None:
                nonlocal received, short, error, expected, first_block, start, transfer_index, skipped, kept_bytes
                active.discard(transfer)
                urb_times.append((time.perf_counter(), received))
                if transfer.getStatus() != usb1.TRANSFER_COMPLETED:
                    error = f"transfer status={transfer.getStatus()} at {received}"
                    return
                actual = transfer.getActualLength()
                if actual < requested[transfer]:
                    short += 1
                data = bytes(transfer.getBuffer()[:actual])
                last_data[0] = data
                offset = received
                # Nothing but bookkeeping here: any Python work inside this callback
                # (the start-phase search took long enough) blocks URB resubmission and
                # the usbip transport then degrades to ~200 ms holes for the rest of
                # the run (E114 §4). The start search and all comparisons run after
                # the capture on the kept transfers.
                if not args.no_validate and (transfer_index == 0 or sample_every == 1 or transfer_index % sample_every == 0):
                    if kept_bytes + len(data) <= max_keep:
                        kept.append((offset, data))
                        kept_bytes += len(data)
                    else:
                        skipped += 1
                transfer_index += 1
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
            if kept and not args.no_validate and error is None:
                first_block = kept[0][1][:wire]
                start = ref.find_start(first_block)
                if start is None:
                    error = "first block does not match any start phase of the loopback source"
                    print(f"debug first_bytes={kept[0][1][:wire].hex()} kept0_offset={kept[0][0]} kept0_len={len(kept[0][1])} expect00={ref.period(0, 0)[:wire].hex()}")
                else:
                    expected = ref.period(*start)
                    for item in kept:
                        verify(*item)
            kept.clear()
            host_gaps = ""
            if len(urb_times) > 2:
                gaps = [(urb_times[k][0] - urb_times[k - 1][0], urb_times[k][1]) for k in range(1, len(urb_times))]
                gaps.sort(reverse=True)
                host_gaps = " host_urb_gaps_ms=" + ",".join(f"{g * 1000:.1f}@{at}" for g, at in gaps[:4])
                print(f"host_urb_completions={len(urb_times)}{host_gaps}")
            if error:
                for t in list(active):
                    try:
                        t.cancel()
                    except usb1.USBError:
                        pass
                deadline = time.monotonic() + 2.0
                while active and time.monotonic() < deadline:
                    context.handleEventsTimeout(0.2)
                status = ""
                marker = last_data[0].rfind(b"E114_STATUS")
                if marker >= 0:
                    status = last_data[0][marker:].split(b"\n", 1)[0].decode("ascii", "replace").strip()
                if not status:
                    status = read_line(handle, 3000)
                print(f"error={error} received={received} {status}")
                return 2
            status = read_line(handle)
            sf = fields_of(status)
            mbps = received * 8 / elapsed / 1e6
            ok = (received == total and bad_blocks == 0 and start is not None and sf.get("queue_overflow") == "0"
                  and sf.get("spill_overflow") == "0" and sf.get("raw_sequence_bad") == "0" and sf.get("aborted") == "0")
            print(f"ok={int(ok)} bytes={received} elapsed_s={elapsed:.6f} rate_mbps={mbps:.3f} start={start} "
                  f"checked_blocks={checked_blocks} bad_blocks={bad_blocks} skipped_transfers={skipped} short={short} {status}")
            return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
