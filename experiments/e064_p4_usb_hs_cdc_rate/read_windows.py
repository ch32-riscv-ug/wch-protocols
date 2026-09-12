# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""E064 host-side reader. Runs on Windows through `uv run --script`.

Plan and report: README.ja.md

The port under test stays on the Windows side, so this script is launched from
the WSL harness with uv.exe and reports one JSON object on stdout. It writes the
start byte itself, which is what keeps the device from streaming before the
reader is listening.
"""

import argparse
import array
import json
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--bytes", type=int, required=True)
    parser.add_argument("--read-size", type=int, default=65536)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    # The device gates CDC writes on DTR.
    port.dtr = True
    port.rts = True
    port.timeout = args.timeout

    received = bytearray()
    stalled = False
    error = None
    elapsed = 0.0
    try:
        port.open()
        time.sleep(0.2)
        port.reset_input_buffer()
        port.write(b"G")
        port.flush()

        started = time.perf_counter()
        while len(received) < args.bytes:
            block = port.read(min(args.read_size, args.bytes - len(received)))
            if not block:
                stalled = True
                break
            received.extend(block)
        elapsed = time.perf_counter() - started
    except Exception as exc:  # noqa: BLE001 - reported, not raised, so the harness sees it
        error = f"{type(exc).__name__}: {exc}"
    finally:
        if port.is_open:
            port.close()

    # Verification is deliberately outside the timed window.
    mismatch_at = None
    if args.verify and not error:
        words = array.array("I")
        usable = len(received) - (len(received) % 4)
        words.frombytes(bytes(received[:usable]))
        if sys.byteorder != "little":
            words.byteswap()
        for index, value in enumerate(words):
            if value != index:
                mismatch_at = index * 4
                break

    result = {
        "port": args.port,
        "requested": args.bytes,
        "received": len(received),
        "elapsed_s": elapsed,
        "rate_mb_s": (len(received) / elapsed / 1e6) if elapsed > 0 else 0.0,
        "read_size": args.read_size,
        "stalled": stalled,
        "verified": bool(args.verify and not error),
        "mismatch_at": mismatch_at,
        "error": error,
    }
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
