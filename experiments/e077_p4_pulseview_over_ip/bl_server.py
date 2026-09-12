"""E077: speak BeagleLogic's TCP protocol so stock sigrok/PulseView can capture
from the ESP32-P4.

Plan and report: README.ja.md

libsigrok's beaglelogic driver accepts conn=tcp-raw/<host>/<port> and then talks
a handful of text commands over that socket. This serves them and, on "get",
streams raw samples back down the same socket -- one byte per sample when the
client asked for an 8-bit sample unit, two when it asked for 16.

The device end is E076's firmware: the console carries "C <bytes> <rate>" and
"D", and the packed capture comes back over the OTG HS vendor endpoint. PARLIO
packs two channels at two bits per sample, so the expansion to sigrok's
one-byte-per-sample layout happens here rather than on the wire.

    uv run python e077_p4_pulseview_over_ip/bl_server.py --source usb
    sigrok-cli --driver "beaglelogic:conn=tcp-raw/127.0.0.1/5556" --scan
"""

import argparse
import select
import socket
import socketserver
import sys
import threading
import time

DEFAULT_PORT = 5556
# Reported to the client for "memalloc". The driver refuses a sample limit that
# would not fit in it, and stops a one-shot capture once that many bytes have
# arrived, so it has to cover the deepest capture the board can hold.
BUFFER_SIZE = 256 * 1024 * 1024
BUFUNIT_SIZE = 4 * 1024 * 1024
SAMPLEUNIT_16_BITS = 0
SAMPLEUNIT_8_BITS = 1
# 160 MHz divided by an integer is what PARLIO can actually make, and the
# driver's own list stops at 100 MHz, so 80 MHz is the fastest rate both ends
# can express exactly.
DEFAULT_SAMPLERATE = 80_000_000

VID = 0x1209
PID = 0x0008
EP_IN = 0x81
LANES = 2
PWM_HZ = 100_000


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def unpack(packed: bytes, lanes: int) -> bytes:
    """Packed `lanes`-bit samples -> one byte per sample, bit n = channel n."""
    if lanes == 8:
        return packed
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


class SimSource:
    """Square waves at PWM_HZ, the same shape the board's LEDC source makes."""

    name = "sim"

    def __init__(self, lanes: int = LANES):
        self.lanes = lanes

    def capture(self, samples: int, rate_hz: int) -> bytes:
        period = max(2, round(rate_hz / PWM_HZ))
        duties = (0.25, 0.50, 0.75, 0.125)
        out = bytearray(samples)
        for lane in range(self.lanes):
            high = max(1, round(period * duties[lane % len(duties)]))
            bit = 1 << lane
            for index in range(samples):
                if index % period < high:
                    out[index] |= bit
        return bytes(out)


class UsbSource:
    """E076's path: the console triggers the capture, the HS vendor endpoint
    carries it back."""

    name = "usb"

    def __init__(self, port: str, lanes: int = LANES, read_size: int = 1024 * 1024):
        import serial  # imported here so --source sim needs neither pyserial nor pyusb
        import usb.core
        import usb.util

        self._usb_util = usb.util
        self.lanes = lanes
        self.read_size = read_size
        self.device = usb.core.find(idVendor=VID, idProduct=PID)
        if self.device is None:
            raise SystemExit(f"{VID:04x}:{PID:04x} is not attached (usbipd attach)")
        try:
            self.device.get_active_configuration()
        except usb.core.USBError:
            self.device.set_configuration()
        usb.util.claim_interface(self.device, 0)
        self.serial = serial.Serial(port, 115200, timeout=2)
        time.sleep(0.3)
        self.serial.reset_input_buffer()
        self.max_packed = None

    def _expect(self, prefix: bytes, timeout: float = 60.0) -> bytes:
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.serial.readline()
            if line.startswith(prefix):
                return line.strip()
        raise TimeoutError(f"no line starting with {prefix!r}")

    def _drain_endpoint(self) -> None:
        import usb.core

        try:
            while len(self.device.read(EP_IN, 65536, 30)):
                pass
        except usb.core.USBError:
            pass

    def capture(self, samples: int, rate_hz: int) -> bytes:
        packed_bytes = (samples * self.lanes + 7) // 8
        self.serial.reset_input_buffer()
        self.serial.write(f"C {packed_bytes} {rate_hz}\n".encode())
        self.serial.flush()
        fields = dict(item.split("=", 1) for item in self._expect(b"CAP").decode().split()[1:])
        if fields.get("status") != "0":
            raise RuntimeError(f"capture failed: {fields}")
        log(f"  device: {packed_bytes} packed bytes, overflow={fields['overflow']} "
            f"timeout={fields['timeout']} in {int(fields['elapsed_us']) / 1000:.1f} ms")

        self._drain_endpoint()
        self.serial.write(b"D\n")
        self.serial.flush()
        expected = int(self._expect(b"DUMP").decode().split("bytes=")[1])
        received = bytearray()
        started = time.perf_counter()
        while len(received) < expected:
            block = self.device.read(EP_IN, min(self.read_size, expected - len(received)), 10000)
            if not len(block):
                raise RuntimeError(f"download stalled at {len(received)} of {expected}")
            received.extend(block)
        elapsed = time.perf_counter() - started
        self._expect(b"SENT")
        log(f"  download: {len(received)} bytes in {elapsed:.3f} s = {len(received) / elapsed / 1e6:.2f} MB/s")
        return unpack(bytes(received), self.lanes)[:samples]


class Handler(socketserver.BaseRequestHandler):
    def setup(self) -> None:
        self.streaming = False
        self.samplerate = DEFAULT_SAMPLERATE
        self.sampleunit = SAMPLEUNIT_8_BITS
        self.triggerflags = 0
        self.buffersize = BUFFER_SIZE
        self.bufunitsize = BUFUNIT_SIZE
        self.request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def _reply(self, text: str) -> None:
        self.request.sendall(text.encode() + b"\n")

    def handle(self) -> None:
        log(f"connect from {self.client_address[0]}:{self.client_address[1]}")
        self.pending = b""
        while True:
            if b"\n" not in self.pending:
                try:
                    chunk = self.request.recv(4096)
                except (ConnectionResetError, TimeoutError):
                    break
                if not chunk:
                    break
                self.pending += chunk
            while b"\n" in self.pending:
                line, self.pending = self.pending.split(b"\n", 1)
                if not self._command(line.decode(errors="replace").strip()):
                    log("disconnect")
                    return
        log("disconnect")

    def _command(self, line: str) -> bool:
        if not line:
            return True
        log(f"<- {line!r}")
        parts = line.split()
        name, args = parts[0].lower(), parts[1:]

        getters = {
            "memalloc": lambda: self.buffersize,
            "samplerate": lambda: self.samplerate,
            "sampleunit": lambda: self.sampleunit,
            "triggerflags": lambda: self.triggerflags,
            "bufunitsize": lambda: self.bufunitsize,
        }
        if name == "version":
            self._reply("BeagleLogic 1.0")
            return True
        if name in getters:
            if not args:
                self._reply(str(getters[name]()))
                return True
            value = int(args[0])
            setattr(self, {"memalloc": "buffersize"}.get(name, name), value)
            self._reply("ok")
            return True
        if name == "get":
            self._stream()
            return True
        if name == "close":
            # Do not hang up here. The driver sends "close", then drains the
            # socket and only afterwards closes it from its side; closing first
            # leaves libsigrok spinning on a hung-up poll fd.
            self.streaming = False
            return True
        log(f"   (unknown command {name!r})")
        self._reply("error")
        return True

    def _stream(self) -> None:
        # The driver never tells the server how many samples it wants: real
        # BeagleLogic streams until the client says "close" or drops the socket.
        # So batches go out until one of those happens, and a client that asked
        # for less than a batch simply stops reading -- which breaks the send,
        # and that is the normal end of a capture rather than a fault.
        config = self.server.config
        unit_bytes = 1 if self.sampleunit == SAMPLEUNIT_8_BITS else 2
        log(f"streaming {config.samples}-sample batches at {self.samplerate} Hz, {unit_bytes} byte/sample")
        sent = 0
        started = time.perf_counter()
        for batch in range(config.max_batches):
            if self._client_finished(0.0):
                break
            try:
                data = self.server.source.capture(config.samples, self.samplerate)
            except Exception as error:
                log(f"  capture failed: {error}")
                break
            if unit_bytes == 2:
                wide = bytearray(len(data) * 2)
                wide[0::2] = data
                data = bytes(wide)
            try:
                self.request.sendall(data)
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                sent += len(data)
                log(f"  client stopped reading during batch {batch + 1}")
                break
            sent += len(data)
            # The client never says how many samples it wants -- it just stops
            # and sends "close" once it has them. Waiting for that here is what
            # keeps a satisfied client from costing another real capture.
            if self._client_finished(config.batch_gap):
                log(f"  client has what it needs after batch {batch + 1}")
                break
        log(f"  sent {sent} bytes in {time.perf_counter() - started:.3f} s")

    def _client_finished(self, timeout: float) -> bool:
        """True if the client asked to stop or went away. Anything else it sent
        is kept for the command loop."""
        ready, _, _ = select.select([self.request], [], [], timeout)
        if not ready:
            return False
        try:
            chunk = self.request.recv(4096)
        except (ConnectionResetError, TimeoutError):
            return True
        if not chunk:
            return True
        self.pending += chunk
        return b"close" in chunk.lower()


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--source", choices=("sim", "usb"), default="usb")
    parser.add_argument("--serial", default="/run/board-identify/by-id/esp32-p4-30eda0e31478")
    parser.add_argument("--lanes", type=int, default=LANES)
    parser.add_argument(
        "--samples",
        type=int,
        default=1_000_000,
        help="how many samples a 'get' returns; the client's own limit decides when it stops",
    )
    parser.add_argument("--max-batches", type=int, default=64, help="batches one 'get' will send at most")
    parser.add_argument("--batch-gap", type=float, default=0.5,
                        help="seconds to wait for the client to say 'close' before capturing again")
    parser.add_argument("--once", action="store_true", help="serve one connection and exit")
    args = parser.parse_args()

    source = SimSource(args.lanes) if args.source == "sim" else UsbSource(args.serial, args.lanes)
    server = Server((args.bind, args.port), Handler)
    server.config = args
    server.source = source
    server.lock = threading.Lock()
    log(f"listening on {args.bind}:{args.port} source={source.name} lanes={args.lanes} samples={args.samples}")
    log(f'sigrok-cli --driver "beaglelogic:conn=tcp-raw/{args.bind}/{args.port}:numchannels=8" --scan')
    try:
        if args.once:
            server.handle_request()
        else:
            server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
