"""E079 host side: read the same bulk IN endpoint with N URBs in flight and see
whether the ceiling moves.

Plan and report: README.ja.md

Every host script up to here used libusb's synchronous API, which submits one
transfer, waits for it, then submits the next -- the bus is idle in between.
This one keeps `depth` transfers outstanding using the async API, so the same
device is asked the same question with the gaps removed.

Run it against E076's firmware ("B <bytes> <source>", no capture in the way) or
E078's ("S <bytes> <rate>"); --command picks which.

    uv run --with libusb1 python e079_p4_host_urb_depth/urb_depth.py
"""

import argparse
import statistics
import time

import serial
import usb1

VID = 0x1209
PID = 0x0008
EP_IN = 0x81
PACKET = 512


def expect(port: serial.Serial, prefix: bytes, timeout: float = 45.0) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = port.readline()
        if line.startswith(prefix):
            return line.decode(errors="replace").strip()
    raise TimeoutError(f"no line starting with {prefix!r}")


class AsyncReader:
    """Keeps `depth` bulk IN transfers outstanding until `total` bytes arrive."""

    def __init__(self, handle, depth: int, size: int, total: int):
        self.handle = handle
        self.depth = depth
        self.size = size
        self.total = total
        self.received = 0
        self.errors = []

    def _on_done(self, transfer):
        status = transfer.getStatus()
        if status != usb1.TRANSFER_COMPLETED:
            self.errors.append(status)
            return
        self.received += transfer.getActualLength()
        if self.received < self.total:
            transfer.submit()

    def run(self, context, timeout: float = 30.0) -> float:
        transfers = []
        for _ in range(self.depth):
            transfer = self.handle.getTransfer()
            transfer.setBulk(EP_IN, self.size, callback=self._on_done, timeout=10000)
            transfers.append(transfer)
        started = time.perf_counter()
        for transfer in transfers:
            transfer.submit()
        deadline = started + timeout
        while self.received < self.total and time.perf_counter() < deadline:
            context.handleEvents()
            if self.errors:
                break
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
        return elapsed


def sync_read(handle, size: int, total: int) -> float:
    received = 0
    started = time.perf_counter()
    while received < total:
        received += len(handle.bulkRead(EP_IN, min(size, total - received), 10000))
    return time.perf_counter() - started


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/run/board-identify/by-id/esp32-p4-30eda0e31478")
    parser.add_argument("--bytes", type=int, default=4 * 1024 * 1024)
    parser.add_argument("--size", type=int, default=256 * 1024, help="bytes per URB")
    parser.add_argument("--depths", default="1,2,4,8")
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--command", choices=("B", "S"), default="B",
                        help="B = E076 bandwidth-only, S = E078 stream with capture")
    parser.add_argument("--rate", type=int, default=32_000_000, help="sample rate for the S command")
    args = parser.parse_args()

    port = serial.Serial(args.port, 115200, timeout=2)
    time.sleep(0.3)
    port.reset_input_buffer()

    def trigger() -> None:
        if args.command == "B":
            port.write(f"B {args.bytes} 1\n".encode())  # 1 = PSRAM, E076's real path
        else:
            port.write(f"S {args.bytes} {args.rate}\n".encode())
        port.flush()
        expect(port, b"STREAM" if args.command == "S" else b"DUMP")

    def settle(handle) -> None:
        try:
            while len(handle.bulkRead(EP_IN, 65536, 30)):
                pass
        except usb1.USBError:
            pass

    results: dict[str, list[float]] = {}
    with usb1.USBContext() as context:
        handle = context.openByVendorIDAndProductID(VID, PID)
        if handle is None:
            raise SystemExit(f"{VID:04x}:{PID:04x} is not attached (usbipd attach)")
        handle.claimInterface(0)

        for label in ["sync"] + [f"depth {d}" for d in args.depths.split(",")]:
            rates = []
            for _ in range(args.repeats):
                settle(handle)
                trigger()
                if label == "sync":
                    elapsed = sync_read(handle, args.size, args.bytes)
                    received = args.bytes
                else:
                    reader = AsyncReader(handle, int(label.split()[1]), args.size, args.bytes)
                    elapsed = reader.run(context)
                    received = reader.received
                    if reader.errors:
                        print(f"  {label}: transfer errors {reader.errors[:3]}")
                expect(port, b"SENT" if args.command == "B" else b"DONE")
                if received < args.bytes:
                    print(f"  {label}: short read {received} of {args.bytes}")
                    continue
                rates.append(received / elapsed / 1e6)
                print(f"  {label:9s} {rates[-1]:6.3f} MB/s")
            results[label] = rates

        handle.releaseInterface(0)
    port.close()

    print(f"\n{'':11s}{'mean':>8s}{'median':>8s}{'min':>8s}{'max':>8s}   transactions/microframe")
    for label, rates in results.items():
        if not rates:
            print(f"{label:11s}{'no data':>8s}")
            continue
        per_frame = statistics.mean(rates) * 1e6 / PACKET / 8000
        print(f"{label:11s}{statistics.mean(rates):8.2f}{statistics.median(rates):8.2f}"
              f"{min(rates):8.2f}{max(rates):8.2f}   {per_frame:5.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
