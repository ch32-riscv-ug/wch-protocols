"""E082: spool the capture to a file as packed bytes, convert to .sr afterwards.

Plan and report: README.ja.md

E080 fed sigrok-cli live over TCP, and srzip's compression turned out to be the
thing that could not keep up -- a long capture lost samples at rates the board
handles easily. Nothing about the capture needs the file written while it runs,
so here the only work on the critical path is appending packed bytes to a file,
and the expansion and the zip happen once the board has finished.

    uv run --with libusb1 --with numpy python e082_p4_spool_then_convert/stream_to_sr.py \
        --rate 86000000 --samples 256000000 --out capture.sr
"""

import argparse
import os
import time
import zipfile

import numpy
import serial
import usb1

VID = 0x1209
PID = 0x0008
EP_IN = 0x81
LANES = 2
PWM_HZ = 100_000
MAX_PACKED = 64 * 1024 * 1024  # the firmware's kStreamBytesMax


def expect(port: serial.Serial, prefix: bytes, timeout: float = 120.0) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = port.readline()
        if line.startswith(prefix):
            return line.decode(errors="replace").strip()
    raise TimeoutError(f"no line starting with {prefix!r}")


def expand(packed: bytes) -> numpy.ndarray:
    data = numpy.frombuffer(packed, dtype=numpy.uint8)
    out = numpy.empty(data.size * 4, dtype=numpy.uint8)
    out[0::4] = data & 3
    out[1::4] = (data >> 2) & 3
    out[2::4] = (data >> 4) & 3
    out[3::4] = (data >> 6) & 3
    return out


def spool(port: serial.Serial, handle, context, packed_total: int, rate_hz: int,
          path: str, depth: int, size: int) -> tuple[float, dict]:
    """Pull `packed_total` bytes off the endpoint into `path`, unexpanded."""
    try:
        while len(handle.bulkRead(EP_IN, 65536, 30)):
            pass
    except usb1.USBError:
        pass
    port.reset_input_buffer()
    port.write(f"S {packed_total} {rate_hz}\n".encode())
    port.flush()
    header = expect(port, b"STREAM")
    if "status=" in header:
        raise RuntimeError(f"stream rejected: {header}")

    state = {"received": 0}
    with open(path, "wb", buffering=0) as sink:
        def on_done(transfer):
            if transfer.getStatus() != usb1.TRANSFER_COMPLETED:
                return
            length = transfer.getActualLength()
            if length:
                sink.write(transfer.getBuffer()[:length])
                state["received"] += length
            if state["received"] < packed_total:
                transfer.submit()

        transfers = []
        for _ in range(depth):
            transfer = handle.getTransfer()
            transfer.setBulk(EP_IN, size, callback=on_done, timeout=10000)
            transfers.append(transfer)
        started = time.perf_counter()
        for transfer in transfers:
            transfer.submit()
        deadline = started + packed_total / 2e6 + 60
        while state["received"] < packed_total and time.perf_counter() < deadline:
            context.handleEvents()
        elapsed = time.perf_counter() - started
        for transfer in transfers:
            if transfer.isSubmitted():
                try:
                    transfer.cancel()
                except usb1.USBError:
                    pass
        while any(transfer.isSubmitted() for transfer in transfers):
            try:
                context.handleEvents()
            except usb1.USBError:
                break

    device = dict(item.split("=", 1) for item in expect(port, b"DONE").split()[1:])
    return elapsed, {"received": state["received"], **device}


def convert(path: str, out: str, samplerate: int, chunk_bytes: int, compress: bool) -> float:
    """Packed spool file -> .sr, one chunk at a time so nothing is held whole."""
    packed_size = os.path.getsize(path)
    metadata = (
        "[global]\nsigrok version=0.5.2\n\n[device 1]\ncapturefile=logic-1\n"
        f"total probes={LANES}\nsamplerate={samplerate} Hz\ntotal analog=0\n"
        + "".join(f"probe{index + 1}=D{index}\n" for index in range(LANES))
        + "unitsize=1\n"
    )
    started = time.perf_counter()
    mode = zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
    with zipfile.ZipFile(out, "w", mode) as archive, open(path, "rb") as source:
        archive.writestr("version", "2")
        archive.writestr("metadata", metadata)
        index = 1
        written = 0
        while written < packed_size:
            block = source.read(chunk_bytes)
            if not block:
                break
            archive.writestr(f"logic-1-{index}", expand(block).tobytes())
            written += len(block)
            index += 1
    return time.perf_counter() - started


def check(path: str, rate_hz: int, head_bytes: int) -> tuple[bool, str]:
    """Rising-edge period on the head and tail of the spool, as in E074."""
    expected = round(rate_hz / PWM_HZ)
    size = os.path.getsize(path)
    worst_low, worst_high = expected, expected
    with open(path, "rb") as source:
        slices = [source.read(head_bytes)]
        if size > 2 * head_bytes:
            source.seek(size - head_bytes)
            slices.append(source.read(head_bytes))
    for packed in slices:
        samples = expand(packed)
        for bit in range(LANES):
            lane = (samples >> bit) & 1
            edges = numpy.flatnonzero((lane[1:] == 1) & (lane[:-1] == 0)) + 1
            if edges.size < 3:
                continue
            gaps = numpy.diff(edges)
            worst_low = min(worst_low, int(gaps.min()))
            worst_high = max(worst_high, int(gaps.max()))
    ok = worst_low == worst_high == expected
    return ok, f"{worst_low}/{worst_high} (expected {expected})"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/run/board-identify/by-id/esp32-p4-30eda0e31478")
    parser.add_argument("--rate", type=int, default=86_000_000)
    parser.add_argument("--samples", type=int, default=256_000_000)
    parser.add_argument("--out", default="")
    parser.add_argument("--spool", default="/tmp/e082_spool.bin")
    parser.add_argument("--depth", type=int, default=4)
    parser.add_argument("--size", type=int, default=256 * 1024)
    parser.add_argument("--chunk", type=int, default=4 * 1024 * 1024, help="packed bytes per .sr chunk")
    parser.add_argument("--compress", action="store_true", help="deflate the .sr (slower, smaller)")
    args = parser.parse_args()

    packed_total = (args.samples * LANES + 7) // 8
    if packed_total > MAX_PACKED:
        raise SystemExit(f"{args.samples} samples needs {packed_total} packed bytes; the firmware caps at {MAX_PACKED}")

    port = serial.Serial(args.port, 115200, timeout=2)
    time.sleep(0.3)
    port.reset_input_buffer()
    with usb1.USBContext() as context:
        handle = context.openByVendorIDAndProductID(VID, PID)
        if handle is None:
            raise SystemExit(f"{VID:04x}:{PID:04x} is not attached (usbipd attach)")
        handle.claimInterface(0)
        elapsed, device = spool(port, handle, context, packed_total, args.rate, args.spool, args.depth, args.size)
        handle.releaseInterface(0)
    port.close()

    print(f"spooled {device['received']} packed bytes in {elapsed:.3f} s "
          f"= {device['received'] / elapsed / 1e6:.2f} MB/s")
    print(f"  device: ring={device['ring_overflow']} fifo={device['fifo_overflow']} "
          f"high_water={device['high_water']} stalls={device['stalls']} waits={device['waits']}")
    ok, detail = check(args.spool, args.rate, 2 * 1024 * 1024)
    print(f"  period {detail} {'OK' if ok else 'MISMATCH'}")
    if args.out:
        took = convert(args.spool, args.out, args.rate, args.chunk, args.compress)
        print(f"converted to {args.out} in {took:.3f} s ({os.path.getsize(args.out) / 1e6:.1f} MB, "
              f"{'deflate' if args.compress else 'stored'})")
    clean = ok and device["ring_overflow"] == "0" and device["fifo_overflow"] == "0"
    print("=>", "sample-accurate" if clean else "SAMPLES LOST")
    return 0 if clean else 1


if __name__ == "__main__":
    raise SystemExit(main())
