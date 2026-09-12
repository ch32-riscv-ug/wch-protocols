"""E080: serve PulseView one continuous capture instead of stitched batches.

Plan and report: README.ja.md

E077 answered a "get" by capturing into PSRAM and sending, then capturing again
when the client wanted more -- and the seam between two captures is a real gap
in time, which showed up as a wrong period at every batch boundary. Here the
board streams while it captures (E078), so one "get" is one capture and there is
no seam to find.

The BeagleLogic protocol itself is E077's, imported rather than copied, so the
only thing that differs between the two experiments is where the bytes come
from.

    uv run --with libusb1 --with numpy python e080_p4_pulseview_gapless/bl_stream_server.py
    sigrok-cli --driver "beaglelogic:conn=tcp-raw/127.0.0.1/5556" \
      --channels P8_45,P8_46 --config samplerate=32m --samples 16000000 -o out.sr -O srzip
"""

import argparse
import os
import sys
import time

import numpy
import serial
import usb1

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "e077_p4_pulseview_over_ip"))

import bl_server  # noqa: E402  (path set above)

VID = 0x1209
PID = 0x0008
EP_IN = 0x81
LANES = 2
# The firmware's kStreamBytesMax. One "get" cannot ask for more than this, which
# is 268 M samples at two channels.
MAX_PACKED = 64 * 1024 * 1024


def expand(packed: bytes) -> bytes:
    """Packed 2-bit samples -> one byte per sample, bit n = channel n.

    Four times the bytes, and it has to keep up with 21 MB/s arriving, so this is
    numpy rather than E077's per-byte table lookup.
    """
    data = numpy.frombuffer(packed, dtype=numpy.uint8)
    out = numpy.empty(data.size * 4, dtype=numpy.uint8)
    out[0::4] = data & 3
    out[1::4] = (data >> 2) & 3
    out[2::4] = (data >> 4) & 3
    out[3::4] = (data >> 6) & 3
    return out.tobytes()


class StreamSource:
    """One `S <bytes> <rate>` per get, piped out as it arrives."""

    name = "stream"

    def __init__(self, port: str, depth: int = 4, size: int = 256 * 1024):
        self.depth = depth
        self.size = size
        self.context = usb1.USBContext()
        self.context.open()
        self.handle = self.context.openByVendorIDAndProductID(VID, PID)
        if self.handle is None:
            raise SystemExit(f"{VID:04x}:{PID:04x} is not attached (usbipd attach)")
        self.handle.claimInterface(0)
        self.serial = serial.Serial(port, 115200, timeout=2)
        time.sleep(0.3)
        self.serial.reset_input_buffer()

    def _expect(self, prefix: bytes, timeout: float = 60.0) -> str:
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.serial.readline()
            if line.startswith(prefix):
                return line.decode(errors="replace").strip()
        raise TimeoutError(f"no line starting with {prefix!r}")

    def _drain(self) -> None:
        try:
            while len(self.handle.bulkRead(EP_IN, 65536, 30)):
                pass
        except usb1.USBError:
            pass

    def stream(self, samples: int, rate_hz: int, sink) -> dict:
        """Capture `samples` samples at `rate_hz`, handing expanded blocks to
        `sink` as they arrive. Returns the device's own account of the run."""
        packed_total = min((samples * LANES + 7) // 8, MAX_PACKED)
        self._drain()
        self.serial.reset_input_buffer()
        self.serial.write(f"S {packed_total} {rate_hz}\n".encode())
        self.serial.flush()
        header = self._expect(b"STREAM")
        if "status=" in header:
            raise RuntimeError(f"stream rejected: {header}")

        state = {"received": 0, "sent": 0, "stalled": False}

        def on_done(transfer):
            if transfer.getStatus() != usb1.TRANSFER_COMPLETED:
                return
            length = transfer.getActualLength()
            if length:
                state["received"] += length
                if not state["stalled"]:
                    try:
                        sink(expand(bytes(transfer.getBuffer()[:length])))
                        state["sent"] += length * 4
                    except (BrokenPipeError, ConnectionResetError, TimeoutError):
                        # The client has what it wanted; let the device finish.
                        state["stalled"] = True
            if state["received"] < packed_total:
                transfer.submit()

        transfers = []
        for _ in range(self.depth):
            transfer = self.handle.getTransfer()
            transfer.setBulk(EP_IN, self.size, callback=on_done, timeout=10000)
            transfers.append(transfer)
        started = time.perf_counter()
        for transfer in transfers:
            transfer.submit()
        deadline = started + packed_total / 2e6 + 60
        while state["received"] < packed_total and time.perf_counter() < deadline:
            self.context.handleEvents()
        elapsed = time.perf_counter() - started
        for transfer in transfers:
            if transfer.isSubmitted():
                try:
                    transfer.cancel()
                except usb1.USBError:
                    pass
        while any(transfer.isSubmitted() for transfer in transfers):
            try:
                self.context.handleEvents()
            except usb1.USBError:
                break

        device = dict(item.split("=", 1) for item in self._expect(b"DONE").split()[1:])
        bl_server.log(f"  device: {packed_total} packed bytes in {elapsed:.3f} s "
                      f"= {state['received'] / elapsed / 1e6:.2f} MB/s, "
                      f"ring={device['ring_overflow']} fifo={device['fifo_overflow']} "
                      f"high_water={device['high_water']} stalls={device['stalls']}")
        return {"elapsed": elapsed, "packed": state["received"], "expanded": state["sent"], **device}


class StreamHandler(bl_server.Handler):
    def _stream(self) -> None:
        config = self.server.config
        unit_bytes = 1 if self.sampleunit == bl_server.SAMPLEUNIT_8_BITS else 2
        bl_server.log(f"streaming {config.samples} samples at {self.samplerate} Hz, {unit_bytes} byte/sample")

        def sink(block: bytes) -> None:
            if unit_bytes == 2:
                wide = bytearray(len(block) * 2)
                wide[0::2] = block
                block = bytes(wide)
            self.request.sendall(block)

        started = time.perf_counter()
        try:
            result = self.server.source.stream(config.samples, self.samplerate, sink)
        except Exception as error:
            bl_server.log(f"  stream failed: {error}")
            return
        bl_server.log(f"  sent {result['expanded']} bytes in {time.perf_counter() - started:.3f} s "
                      f"(one capture, no seams)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=5556)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--serial", default="/run/board-identify/by-id/esp32-p4-30eda0e31478")
    parser.add_argument("--samples", type=int, default=16_000_000,
                        help="samples one 'get' returns; the client's own limit decides when it stops")
    parser.add_argument("--depth", type=int, default=4)
    args = parser.parse_args()

    source = StreamSource(args.serial, args.depth)
    server = bl_server.Server((args.bind, args.port), StreamHandler)
    server.config = args
    server.source = source
    bl_server.log(f"listening on {args.bind}:{args.port} source=stream samples={args.samples}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
