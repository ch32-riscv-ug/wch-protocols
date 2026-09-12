# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""E068 host-side reader: read a transfer and, if the tail is missing, find out
whether it is lost or merely stuck.

Plan and report: README.ja.md

Three phases, so the report can say which one produced the missing bytes:
  1. read the expected count
  2. if short, keep reading for a while without touching the device
  3. if still short, prod the device with 'T' and keep reading
"""

import argparse
import json
import time

import serial

TERMINATOR = bytes([0xE0, 0x68] * 8)


def _drain(port: serial.Serial, want: int, deadline: float) -> bytes:
    """Read up to `want` bytes until they arrive or the port goes quiet."""
    collected = bytearray()
    while len(collected) < want and time.perf_counter() < deadline:
        block = port.read(min(65536, want - len(collected)))
        if not block:
            break
        collected.extend(block)
    return bytes(collected)


def _expected_prefix(length: int) -> bytes:
    words = (length + 3) // 4
    return b"".join(index.to_bytes(4, "little") for index in range(words))[:length]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--bytes", type=int, required=True)
    parser.add_argument("--read-timeout", type=float, default=2.0)
    parser.add_argument("--wait-seconds", type=float, default=2.0)
    parser.add_argument("--prod-seconds", type=float, default=3.0)
    args = parser.parse_args()

    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.dtr = True
    port.rts = True
    port.timeout = args.read_timeout

    stream = bytearray()
    phase1 = phase2 = phase3 = 0
    prodded = False
    elapsed = 0.0
    error = None
    try:
        port.open()
        time.sleep(0.2)
        port.reset_input_buffer()
        port.write(b"G")
        port.flush()

        started = time.perf_counter()
        block = _drain(port, args.bytes, started + 120)
        elapsed = time.perf_counter() - started
        stream.extend(block)
        phase1 = len(block)

        if phase1 < args.bytes:
            # Nothing is sent to the device here: if the tail appears now, time
            # alone was enough.
            block = _drain(port, args.bytes - len(stream), time.perf_counter() + args.wait_seconds)
            stream.extend(block)
            phase2 = len(block)

        if len(stream) < args.bytes:
            prodded = True
            port.write(b"T")
            port.flush()
            block = _drain(
                port, args.bytes - len(stream) + len(TERMINATOR), time.perf_counter() + args.prod_seconds
            )
            stream.extend(block)
            phase3 = len(block)
    except Exception as exc:  # noqa: BLE001 - reported, not raised
        error = f"{type(exc).__name__}: {exc}"
    finally:
        if port.is_open:
            port.close()

    data = bytes(stream)
    terminator_at = data.find(TERMINATOR)
    body = data[:terminator_at] if terminator_at >= 0 else data

    expected = _expected_prefix(len(body))
    mismatch_at = None
    if body != expected:
        for index, (got, want) in enumerate(zip(body, expected)):
            if got != want:
                mismatch_at = index
                break

    result = {
        "port": args.port,
        "requested": args.bytes,
        "phase1_bytes": phase1,
        "phase2_bytes": phase2,
        "phase3_bytes": phase3,
        "prodded": prodded,
        "body_bytes": len(body),
        "short_by": args.bytes - len(body),
        "short_by_packets": (args.bytes - len(body)) / 512,
        "terminator_seen": terminator_at >= 0,
        "body_mismatch_at": mismatch_at,
        "elapsed_s": elapsed,
        "rate_mb_s": (phase1 / elapsed / 1e6) if elapsed > 0 else 0.0,
        "error": error,
    }
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
