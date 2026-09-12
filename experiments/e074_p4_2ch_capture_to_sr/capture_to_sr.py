"""E074 host side: pull a two-channel PARLIO capture off the board and write it
as a sigrok .sr file.

Plan and report: README.ja.md

PARLIO packs two channels at two bits per sample, four samples to a byte, LSB
first. sigrok's native format wants one byte per sample with bit n holding
channel n, so the expansion is 4x and happens here rather than on the device --
the wire stays packed, which is the whole point of doing it this way.
"""

import argparse
import io
import time
import zipfile

import serial

LANES = 2
SAMPLES_PER_BYTE = 8 // LANES  # 4


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


def _expect(port: serial.Serial, prefix: bytes, timeout: float = 30.0) -> bytes:
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = _read_line(port, timeout=max(0.1, deadline - time.time()))
        if line.startswith(prefix):
            return line
    raise TimeoutError(f"no line starting with {prefix!r}")


def unpack(packed: bytes) -> bytes:
    """Packed 2-bit samples -> one byte per sample, bit 0 = D0, bit 1 = D1."""
    table = bytes(bytearray((value >> (2 * index)) & 0x03 for value in range(256) for index in range(SAMPLES_PER_BYTE)))
    out = bytearray(len(packed) * SAMPLES_PER_BYTE)
    for index, value in enumerate(packed):
        base = value * SAMPLES_PER_BYTE
        out[index * SAMPLES_PER_BYTE : (index + 1) * SAMPLES_PER_BYTE] = table[base : base + SAMPLES_PER_BYTE]
    return bytes(out)


def write_sr(path: str, samples: bytes, samplerate_hz: int, channels: int = LANES) -> None:
    metadata = (
        "[global]\n"
        "sigrok version=0.5.2\n"
        "\n"
        "[device 1]\n"
        "capturefile=logic-1\n"
        f"total probes={channels}\n"
        f"samplerate={samplerate_hz} Hz\n"
        "total analog=0\n"
        + "".join(f"probe{index + 1}=D{index}\n" for index in range(channels))
        + "unitsize=1\n"
    )
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("version", "2")
        archive.writestr("metadata", metadata)
        archive.writestr("logic-1-1", samples)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/run/board-identify/by-id/esp32-p4-30eda0e31478")
    parser.add_argument("--bytes", type=int, default=256 * 1024, help="packed bytes to capture")
    parser.add_argument("--rate", type=int, default=32_000_000, help="sample rate in Hz")
    parser.add_argument("--out", default="capture.sr")
    args = parser.parse_args()

    port = serial.Serial(args.port, 115200, timeout=2)
    time.sleep(0.3)
    port.reset_input_buffer()

    port.write(f"C {args.bytes} {args.rate}\n".encode())
    port.flush()
    cap = _expect(port, b"CAP")
    print(cap.decode())
    fields = dict(item.split("=", 1) for item in cap.decode().split()[1:])
    if fields.get("status") != "0":
        raise SystemExit(f"capture failed: {cap!r}")
    packed_len = int(fields["bytes"])

    port.write(b"D\n")
    port.flush()
    header = _expect(port, b"DUMP")
    expected = int(header.decode().split("bytes=")[1])
    if expected != packed_len:
        raise SystemExit(f"dump header disagrees: {expected} vs {packed_len}")

    buffer = io.BytesIO()
    started = time.perf_counter()
    while buffer.tell() < expected:
        block = port.read(min(65536, expected - buffer.tell()))
        if not block:
            raise SystemExit(f"download stalled at {buffer.tell()} of {expected}")
        buffer.write(block)
    elapsed = time.perf_counter() - started
    port.close()

    packed = buffer.getvalue()
    samples = unpack(packed)
    write_sr(args.out, samples, args.rate)

    # A fixed-duty source keeps its duty even if samples are lost (E036), so the
    # period between rising edges is the check that actually detects loss.
    def rising_period(bit: int) -> tuple[float, int, int]:
        edges = []
        previous = samples[0] >> bit & 1
        for index in range(1, len(samples)):
            current = samples[index] >> bit & 1
            if current and not previous:
                edges.append(index)
            previous = current
        if len(edges) < 3:
            return (0.0, 0, 0)
        gaps = [b - a for a, b in zip(edges, edges[1:])]
        return (sum(gaps) / len(gaps), min(gaps), max(gaps))

    ones = [sum((value >> bit) & 1 for value in samples) for bit in range(LANES)]
    print(f"downloaded {len(packed)} packed bytes in {elapsed:.3f} s ({len(packed) / elapsed / 1e6:.3f} MB/s)")
    print(f"expanded to {len(samples)} samples ({len(samples) / args.rate * 1e3:.3f} ms at {args.rate} Hz)")
    expected_period = args.rate / 100_000  # the on-board LEDC source runs at 100 kHz
    for index, count in enumerate(ones):
        mean, low, high = rising_period(index)
        print(
            f"  D{index}: high {count / len(samples) * 100:6.2f}%  "
            f"period mean {mean:8.2f} min {low} max {high} samples "
            f"(expected {expected_period:.0f})"
        )
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
