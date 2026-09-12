"""E078 host side: sweep the sample rate while the board streams the capture out
live, and find where it stops keeping up.

Plan and report: README.ja.md

The reader keeps several bulk IN transfers outstanding. With one at a time the
bus idles between URBs -- 18.64 against 22.68 MB/s on this path (E079) -- and
that gap would show up as the device falling behind, which is exactly the thing
being measured. So the host must not be the slow end here.

    uv run --with libusb1 python e078_p4_continuous_stream/stream_rate_sweep.py
"""

import argparse
import time

import serial
import usb1

VID = 0x1209
PID = 0x0008
EP_IN = 0x81
LANES = 2
PWM_HZ = 100_000


def expect(port: serial.Serial, prefix: bytes, timeout: float = 60.0) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = port.readline()
        if line.startswith(prefix):
            return line.decode(errors="replace").strip()
    raise TimeoutError(f"no line starting with {prefix!r}")


def unpack(packed: bytes, lanes: int) -> bytes:
    per_byte = 8 // lanes
    mask = (1 << lanes) - 1
    table = bytes(
        bytearray((value >> (lanes * index)) & mask for value in range(256) for index in range(per_byte))
    )
    out = bytearray(len(packed) * per_byte)
    for index, value in enumerate(packed):
        base = value * per_byte
        out[index * per_byte : (index + 1) * per_byte] = table[base : base + per_byte]
    return bytes(out)


def rising_period(samples: bytes, bit: int) -> tuple[int, float, int, int]:
    edges = []
    previous = (samples[0] >> bit) & 1
    for index in range(1, len(samples)):
        current = (samples[index] >> bit) & 1
        if current and not previous:
            edges.append(index)
        previous = current
    gaps = [b - a for a, b in zip(edges, edges[1:])]
    if not gaps:
        return (0, 0.0, 0, len(edges))
    return (min(gaps), sum(gaps) / len(gaps), max(gaps), len(edges))


class StreamReader:
    """Keeps `depth` bulk IN transfers outstanding until `total` bytes arrive."""

    def __init__(self, handle, depth: int, size: int, total: int):
        self.handle = handle
        self.depth = depth
        self.size = size
        self.total = total
        self.chunks: list[bytes] = []
        self.received = 0
        self.errors: list[int] = []
        # Cancelling the leftovers at the end of a run reports every one of them
        # as cancelled; those are not failures of the run.
        self.closing = False

    def _on_done(self, transfer):
        status = transfer.getStatus()
        if status != usb1.TRANSFER_COMPLETED:
            if not self.closing:
                self.errors.append(status)
            return
        length = transfer.getActualLength()
        if length:
            self.chunks.append(bytes(transfer.getBuffer()[:length]))
            self.received += length
        if self.received < self.total:
            transfer.submit()

    def run(self, context, timeout: float) -> float:
        transfers = []
        for _ in range(self.depth):
            transfer = self.handle.getTransfer()
            transfer.setBulk(EP_IN, self.size, callback=self._on_done, timeout=10000)
            transfers.append(transfer)
        started = time.perf_counter()
        for transfer in transfers:
            transfer.submit()
        deadline = started + timeout
        while self.received < self.total and time.perf_counter() < deadline and not self.errors:
            context.handleEvents()
        elapsed = time.perf_counter() - started
        self.closing = True
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
        return elapsed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/run/board-identify/by-id/esp32-p4-30eda0e31478")
    parser.add_argument("--rates", default="16,32,48,64,80,96", help="MHz")
    parser.add_argument("--bytes", type=int, default=32 * 1024 * 1024, help="packed bytes per run")
    parser.add_argument("--depth", type=int, default=4)
    parser.add_argument("--size", type=int, default=256 * 1024, help="bytes per URB")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--verify-bytes", type=int, default=4 * 1024 * 1024,
                        help="how much of the head to expand and period-check")
    args = parser.parse_args()

    port = serial.Serial(args.port, 115200, timeout=2)
    time.sleep(0.3)
    port.reset_input_buffer()

    with usb1.USBContext() as context:
        handle = context.openByVendorIDAndProductID(VID, PID)
        if handle is None:
            raise SystemExit(f"{VID:04x}:{PID:04x} is not attached (usbipd attach)")
        handle.claimInterface(0)

        print(f"{'rate':>7} {'run':>4} {'MB/s':>7} {'ring':>6} {'fifo':>6} {'high water':>11} "
              f"{'stalls':>7} {'waits':>7} {'short':>6} {'period min/max':>16}  verdict")
        for mhz in [int(value) for value in args.rates.split(",")]:
            rate = mhz * 1_000_000
            for repeat in range(args.repeats):
                try:
                    while len(handle.bulkRead(EP_IN, 65536, 30)):
                        pass
                except usb1.USBError:
                    pass
                port.reset_input_buffer()
                port.write(f"S {args.bytes} {rate}\n".encode())
                port.flush()
                header = expect(port, b"STREAM")
                if "status=" in header:
                    print(f"{mhz:6d}M {repeat + 1:4d}  rejected: {header}")
                    continue

                reader = StreamReader(handle, args.depth, args.size, args.bytes)
                # Generous: a rate the board cannot sustain still has to finish.
                elapsed = reader.run(context, timeout=args.bytes / 2e6 + 30)
                done = dict(item.split("=", 1) for item in expect(port, b"DONE").split()[1:])

                if reader.received < args.bytes:
                    print(f"{mhz:6d}M {repeat + 1:4d}  short: {reader.received} of {args.bytes}"
                          f"{' errors ' + str(reader.errors[:3]) if reader.errors else ''}"
                          f"  ring={done['ring_overflow']} fifo={done['fifo_overflow']}")
                    continue
                data = b"".join(reader.chunks)
                # Head and tail both: a stream that starts fine and falls behind
                # later loses its samples at the end, and checking only the head
                # would call that a pass.
                expected = round(rate / PWM_HZ)
                worst_low, worst_high = expected, expected
                for slice_ in (data[: args.verify_bytes], data[-args.verify_bytes :]):
                    samples = unpack(slice_, LANES)
                    for bit in range(LANES):
                        low, _, high, _ = rising_period(samples, bit)
                        worst_low = min(worst_low, low)
                        worst_high = max(worst_high, high)
                clean = (worst_low == worst_high == expected
                         and done["ring_overflow"] == "0" and done["fifo_overflow"] == "0"
                         and reader.received == args.bytes)
                short_urbs = sum(1 for chunk in reader.chunks if len(chunk) < args.size)
                print(f"{mhz:6d}M {repeat + 1:4d} {reader.received / elapsed / 1e6:7.2f} "
                      f"{done['ring_overflow']:>6} {done['fifo_overflow']:>6} {done['high_water']:>11} "
                      f"{done['stalls']:>7} {done['waits']:>7} {short_urbs:>6d} {worst_low:7d}/{worst_high:<8d} "
                      f"{'OK' if clean else 'KEEPS UP NO LONGER'}")
        handle.releaseInterface(0)
    port.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
