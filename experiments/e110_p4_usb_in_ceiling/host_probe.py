#!/usr/bin/env python3
"""E110: measure device -> host bulk IN throughput against the HS bulk ceiling.

The device streams a 256-periodic pattern zero-copy. Sweep the device transfer
length (--xfer), arm ring depth (--arm-depth), the host URB size
(--transfer-size) and URB depth (--depth); every run prints one line with the
host rate, the fraction of the theoretical 13 packets/microframe, and the
device-side timing (transfer duration, re-arm gap).
"""

from __future__ import annotations

import argparse
import struct
import sys
import time

try:
    import usb1
except ImportError:
    sys.exit("run with `uv run --with libusb1 python host_probe.py`")

VID = 0x303A
PID = 0x4021
EP_OUT = 0x01
EP_IN = 0x81
THEORY_MB_S = 512 * 13 * 8000 / 1e6  # 53.248 MB/s: 13 bulk packets per 125 us microframe


def read_status(handle, attempts: int = 4, timeout_ms: int = 5000) -> str:
    for _ in range(attempts):
        try:
            status = bytes(handle.bulkRead(EP_IN, 2048, timeout=timeout_ms)).decode("ascii", "replace").strip()
        except usb1.USBErrorTimeout:
            status = ""
        if status:
            return status
    return ""


def one_run(handle, args, flags: int) -> tuple[bool, str]:
    total = args.bytes
    if not args.no_command:
        command = b"EP" + struct.pack("<IQBB", args.xfer, total, flags, 0)
        assert handle.bulkWrite(EP_OUT, command, timeout=2000) == len(command)
    received = planned = short = 0
    error: str | None = None
    active: set[usb1.USBTransfer] = set()
    requested: dict[usb1.USBTransfer, int] = {}
    pattern = bytes(range(256))
    bad = 0
    tail = bytearray()

    def check(data: bytes) -> None:
        nonlocal bad, tail
        tail.extend(data)
        whole = len(tail) - len(tail) % 256
        chunk = bytes(tail[:whole])
        del tail[:whole]
        expected = pattern * (whole // 256)
        if chunk != expected:
            bad += sum(1 for offset in range(0, whole, 256) if chunk[offset:offset + 256] != pattern)

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
        if not args.no_validate:
            check(bytes(transfer.getBuffer()[:actual]))
        received += actual
        missing = requested[transfer] - actual
        if missing:
            submit(transfer, missing, new=False)
        elif planned < total:
            submit(transfer, min(args.transfer_size, total - planned))

    transfers = [handle.getTransfer() for _ in range(args.depth)]
    started = time.perf_counter()
    for transfer in transfers:
        if planned >= total:
            break
        submit(transfer, min(args.transfer_size, total - planned))
    while active and error is None:
        args.context.handleEvents()
    elapsed = time.perf_counter() - started
    if error:
        # Leave the device drained so the next run does not start on stale bytes.
        for _ in range(10):
            try:
                handle.bulkRead(EP_IN, 65536, timeout=200)
            except usb1.USBErrorTimeout:
                break
        return False, error
    # --no-command: the device streams on its own (e.g. the EspUsbDevice writeDirect()
    # prototype) and prints no status line; only the host-side numbers are reported.
    status = "" if args.no_command else read_status(handle)
    mb_s = received / elapsed / 1e6
    fields = dict(token.split("=", 1) for token in status.split() if "=" in token)
    device_us = int(fields.get("active_us", "0") or 0)
    device_mb_s = received / device_us if device_us else 0.0
    completions = int(fields.get("completions", "0") or 0)
    dur_sum = int(fields.get("dur_sum_us", "0") or 0)
    idle_us = device_us - dur_sum if device_us else 0
    ok = received == total and bad == 0 and (args.no_command or (f"bytes={total}" in status and "arm_failures=0" in status))
    line = (f"ok={int(ok)} xfer={args.xfer} arm_depth={args.arm_depth} chain={int(not args.task_rearm)} "
            f"urb={args.transfer_size} depth={args.depth} bytes={received} elapsed_s={elapsed:.6f} "
            f"host_mb_s={mb_s:.3f} host_mbps={mb_s * 8:.1f} pct_theory={100 * mb_s / THEORY_MB_S:.1f} "
            f"pkt_per_uframe={mb_s * 1e6 / 512 / 8000:.2f} device_mb_s={device_mb_s:.3f} "
            f"short={short} pattern_bad={bad} completions={completions} "
            f"dur_avg_us={fields.get('dur_avg_us', '?')} dur_min_us={fields.get('dur_min_us', '?')} dur_max_us={fields.get('dur_max_us', '?')} "
            f"gap_avg_us={fields.get('gap_avg_us', '?')} gap_max_us={fields.get('gap_max_us', '?')} gap_count={fields.get('gap_count', '?')} "
            f"ep_idle_pct={100 * idle_us / device_us if device_us else 0:.1f} usbd_us={fields.get('usbd_us', '?')} "
            f"idle0_us={fields.get('idle0_us', '?')} spin_count={fields.get('spin_count', '?')} spin_us={fields.get('spin_us', '?')} "
            f"spin_calib_per_s={fields.get('spin_calib_per_s', '?')} gahbcfg={fields.get('gahbcfg', '?')}")
    try:
        spin = int(fields["spin_count"]); spin_us = int(fields["spin_us"]); calib = int(fields["spin_calib_per_s"])
        if spin and spin_us and calib:
            line += f" core0_free_spin={100 * spin / (calib * spin_us / 1e6):.1f}"
    except (KeyError, ValueError):
        pass
    return ok, line


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xfer", type=int, default=27136, help="device transfer length (multiple of 512, <= 65024)")
    parser.add_argument("--arm-depth", type=int, choices=(1, 2, 3, 4), default=4)
    parser.add_argument("--task-rearm", action="store_true", help="re-arm from the task instead of the TX-complete callback")
    parser.add_argument("--load-probe", action="store_true", help="spin task on core 0 for ISR-inclusive load")
    parser.add_argument("--bytes", type=int, default=64_000_000)
    parser.add_argument("--transfer-size", type=int, default=1024 * 1024, help="host URB size")
    parser.add_argument("--depth", type=int, default=8, help="host URBs in flight")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--no-validate", action="store_true")
    parser.add_argument("--no-command", action="store_true",
                        help="device streams by itself (no EP command, no status line): report host-side short/pattern counts only")
    args = parser.parse_args()
    if args.xfer % 512 or not 512 <= args.xfer <= 65024:
        parser.error("--xfer must be a multiple of 512 in [512, 65024]")
    flags = (args.arm_depth - 1) | (0x04 if args.load_probe else 0) | (0x08 if args.task_rearm else 0)
    failures = 0
    with usb1.USBContext() as context:
        args.context = context
        handle = context.openByVendorIDAndProductID(VID, PID, skip_on_error=True)
        if handle is None:
            sys.exit(f"no device {VID:04x}:{PID:04x}")
        with handle.claimInterface(0):
            for run in range(args.repeat):
                ok, line = one_run(handle, args, flags)
                print(f"run={run + 1} {line}")
                if not ok:
                    failures += 1
    return 0 if failures == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
