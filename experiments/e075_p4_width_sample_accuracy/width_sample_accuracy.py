"""E075 host side: pull a capture of any PARLIO width and check, per channel,
that no sample went missing.

Plan and report: README.ja.md

The check is the interval between rising edges, not the duty: a fixed-duty
source keeps its duty even when samples are dropped (E036), so only the period
detects loss.
"""

import argparse
import io
import time
import zipfile

import serial

DUTIES = (32, 64, 96, 128, 160, 176, 192, 208)  # matches the firmware's kAllDuties


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


def write_sr(path: str, samples: bytes, samplerate_hz: int, channels: int) -> None:
    metadata = (
        "[global]\nsigrok version=0.5.2\n\n[device 1]\ncapturefile=logic-1\n"
        f"total probes={channels}\nsamplerate={samplerate_hz} Hz\ntotal analog=0\n"
        + "".join(f"probe{index + 1}=D{index}\n" for index in range(channels))
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
    parser.add_argument("--bytes", type=int, default=131072)
    parser.add_argument("--rate", type=int, default=160_000_000)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    port = serial.Serial(args.port, 115200, timeout=2)
    time.sleep(0.3)
    port.reset_input_buffer()
    port.write(f"C {args.bytes} {args.rate}\n".encode())
    port.flush()
    cap = _expect(port, b"CAP")
    fields = dict(item.split("=", 1) for item in cap.decode().split()[1:])
    if fields.get("status") != "0":
        raise SystemExit(f"capture failed: {cap!r}")
    lanes = int(fields["lanes"])
    packed_len = int(fields["bytes"])

    port.write(b"D\n")
    port.flush()
    expected = int(_expect(port, b"DUMP").decode().split("bytes=")[1])
    buffer = io.BytesIO()
    while buffer.tell() < expected:
        block = port.read(min(65536, expected - buffer.tell()))
        if not block:
            raise SystemExit(f"download stalled at {buffer.tell()} of {expected}")
        buffer.write(block)
    port.close()

    samples = unpack(buffer.getvalue(), lanes)
    period = args.rate / 100_000
    print(f"lanes={lanes} rate={args.rate} packed={packed_len} samples={len(samples)} "
          f"overflow={fields['overflow']} timeout={fields['timeout']}")
    exact = True
    for bit in range(lanes):
        mean, low, high, edges = rising_period(samples, bit)
        ok = low == high == round(period)
        exact = exact and ok
        high_pct = sum((value >> bit) & 1 for value in samples) / len(samples) * 100
        print(f"  D{bit}: duty {high_pct:6.2f}%  edges {edges:6d}  period {low}/{mean:.2f}/{high} "
              f"(expected {period:.0f}) {'OK' if ok else 'MISMATCH'}")
    if args.out:
        write_sr(args.out, samples, args.rate, lanes)
        print(f"  wrote {args.out}")
    print(f"  => {'sample-accurate' if exact else 'SAMPLES LOST'}")
    return 0 if exact else 1


if __name__ == "__main__":
    raise SystemExit(main())
