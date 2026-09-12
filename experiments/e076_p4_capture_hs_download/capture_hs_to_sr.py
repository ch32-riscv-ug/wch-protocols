"""E076 host side: capture on the board, pull it off over the OTG HS vendor
endpoint, and write a sigrok .sr.

Plan and report: README.ja.md

Same shape as E074 except the download leaves by USB HS bulk instead of the
full-speed console. The console still carries the control protocol, so the two
paths stay separate and the measurement is not mixed up with the commands.

Requires the HS port attached to this host:
    usbipd.exe attach --wsl --busid <n>
"""

import argparse
import time
import zipfile

import serial
import usb.core
import usb.util

VID = 0x1209
PID = 0x0008
EP_IN = 0x81


def _read_line(port: serial.Serial, timeout: float = 5.0) -> bytes:
    deadline = time.time() + timeout
    line = bytearray()
    while time.time() < deadline:
        chunk = port.read(1)
        if not chunk:
            continue
        if chunk == b"\n":
            return bytes(line)
        if chunk != b"\r":
            line.extend(chunk)
    raise TimeoutError(f"no line within {timeout}s (partial: {bytes(line)!r})")


def _expect(port: serial.Serial, prefix: bytes, timeout: float = 60.0) -> bytes:
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = _read_line(port, timeout=max(0.1, deadline - time.time()))
        if line.startswith(prefix):
            return line
    raise TimeoutError(f"no line starting with {prefix!r}")


def unpack(packed: bytes, lanes: int) -> bytes:
    if lanes == 8:
        return packed
    per_byte = 8 // lanes
    mask = (1 << lanes) - 1
    table = bytes(bytearray((v >> (lanes * i)) & mask for v in range(256) for i in range(per_byte)))
    out = bytearray(len(packed) * per_byte)
    for index, value in enumerate(packed):
        base = value * per_byte
        out[index * per_byte : (index + 1) * per_byte] = table[base : base + per_byte]
    return bytes(out)


def write_sr(path: str, samples: bytes, samplerate_hz: int, channels: int) -> None:
    metadata = (
        "[global]\nsigrok version=0.5.2\n\n[device 1]\ncapturefile=logic-1\n"
        f"total probes={channels}\nsamplerate={samplerate_hz} Hz\ntotal analog=0\n"
        + "".join(f"probe{i + 1}=D{i}\n" for i in range(channels))
        + "unitsize=1\n"
    )
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("version", "2")
        archive.writestr("metadata", metadata)
        archive.writestr("logic-1-1", samples)


def rising_period(samples: bytes, bit: int) -> tuple[float, int, int, int]:
    edges = []
    previous = (samples[0] >> bit) & 1
    for index in range(1, len(samples)):
        current = (samples[index] >> bit) & 1
        if current and not previous:
            edges.append(index)
        previous = current
    if len(edges) < 3:
        return (0.0, 0, 0, len(edges))
    gaps = [b - a for a, b in zip(edges, edges[1:])]
    return (sum(gaps) / len(gaps), min(gaps), max(gaps), len(edges))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/run/board-identify/by-id/esp32-p4-30eda0e31478")
    parser.add_argument("--bytes", type=int, default=4 * 1024 * 1024)
    parser.add_argument("--rate", type=int, default=160_000_000)
    parser.add_argument("--read-size", type=int, default=1024 * 1024)
    parser.add_argument("--lanes", type=int, default=2)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    device = usb.core.find(idVendor=VID, idProduct=PID)
    if device is None:
        raise SystemExit(f"{VID:04x}:{PID:04x} is not attached (usbipd attach)")
    try:
        device.get_active_configuration()
    except usb.core.USBError:
        device.set_configuration()
    usb.util.claim_interface(device, 0)

    port = serial.Serial(args.port, 115200, timeout=2)
    time.sleep(0.3)
    port.reset_input_buffer()

    port.write(f"C {args.bytes} {args.rate}\n".encode())
    port.flush()
    cap = _expect(port, b"CAP")
    fields = dict(item.split("=", 1) for item in cap.decode().split()[1:])
    if fields.get("status") != "0":
        raise SystemExit(f"capture failed: {cap!r}")
    packed_len = int(fields["bytes"])
    print(f"captured {packed_len} packed bytes, overflow={fields['overflow']} timeout={fields['timeout']} "
          f"in {int(fields['elapsed_us']) / 1000:.1f} ms")

    # Drain anything a previous round left in the endpoint.
    try:
        while True:
            if not len(device.read(EP_IN, 65536, 30)):
                break
    except usb.core.USBError:
        pass

    port.write(b"D\n")
    port.flush()
    expected = int(_expect(port, b"DUMP").decode().split("bytes=")[1])

    received = bytearray()
    started = time.perf_counter()
    while len(received) < expected:
        want = min(args.read_size, expected - len(received))
        block = device.read(EP_IN, want, 10000)
        if not len(block):
            raise SystemExit(f"download stalled at {len(received)} of {expected}")
        received.extend(block)
    elapsed = time.perf_counter() - started

    sent = _expect(port, b"SENT")
    port.close()
    usb.util.dispose_resources(device)

    samples = unpack(bytes(received), args.lanes)
    print(f"downloaded {len(received)} bytes in {elapsed:.3f} s = {len(received) / elapsed / 1e6:.3f} MB/s")
    print(f"  device: {sent.decode()}")
    print(f"expanded to {len(samples)} samples ({len(samples) / args.rate * 1e3:.3f} ms at {args.rate} Hz)")

    period = args.rate / 100_000
    exact = True
    for bit in range(args.lanes):
        mean, low, high, edges = rising_period(samples, bit)
        ok = low == high == round(period)
        exact = exact and ok
        duty = sum((v >> bit) & 1 for v in samples) / len(samples) * 100
        print(f"  D{bit}: duty {duty:6.2f}%  edges {edges:6d}  period {low}/{mean:.2f}/{high} "
              f"(expected {period:.0f}) {'OK' if ok else 'MISMATCH'}")
    if args.out:
        write_sr(args.out, samples, args.rate, args.lanes)
        print(f"wrote {args.out}")
    print(f"=> {'sample-accurate' if exact else 'SAMPLES LOST'}")
    return 0 if exact else 1


if __name__ == "__main__":
    raise SystemExit(main())
