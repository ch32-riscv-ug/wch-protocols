"""E006 tool: pulse width generation and capture on one wire, using RMT.

Plan and report: README.ja.md

Run:  uv run --env-file .env pytest e006_tool_pulse_capture/e006_tool_pulse_capture.py
"""

import re
import statistics

import pexpect

TICK_CANDIDATES_HZ = [80_000_000, 40_000_000, 20_000_000, 10_000_000]
WIDTHS_NS = [250, 290, 500, 890, 2000]
HIGH_NS = 1000
PULSES = 8
REPEATS = 3
MAX_TRIGGERS = 3


def _hello(target, role):
    banner = re.compile(rb"# EXP E006 v1 role=" + role.encode() + rb" line=\d+ build=[^\r\n]+")
    for attempt in range(1, MAX_TRIGGERS + 1):
        target.write("?\n")
        try:
            target.expect(banner, timeout=5)
            return attempt
        except pexpect.exceptions.TIMEOUT:
            if attempt == MAX_TRIGGERS:
                raise
    return MAX_TRIGGERS


def _init(target, tick_hz, direction):
    target.write(f"I{tick_hz},{direction}\n")
    match = target.expect(re.compile(rb"INIT (ok|fail) tick=(\d+) dir=(\d)"), timeout=10)
    return match.group(1) == b"ok"


def _capture_round(sender, receiver, lo_ticks, hi_ticks):
    receiver.write(f"C{PULSES}\n")
    receiver.expect_exact("ARMED", timeout=5)
    sender.write(f"P{lo_ticks},{hi_ticks},{PULSES}\n")
    sender.expect(re.compile(rb"(SENT|SENDFAIL) n=\d+"), timeout=10)
    match = receiver.expect(re.compile(rb"(SYM n=(\d+) d=([^\r\n]+)|CAPFAIL)"), timeout=10)
    if match.group(2) is None:
        return None, []
    n = int(match.group(2))
    lows = []
    for sym in match.group(3).decode().split(";"):
        for part in sym.split(","):
            level, duration = part.split(":")
            if level == "0":
                lows.append(int(duration))
    return n, lows


def test_tool_pulse_capture(dut, peers):
    """Find the RMT resolution and how accurately short pulses come back."""
    peer = peers["device"]
    print(f"\nE006: primary answered after {_hello(dut, 'primary')} trigger(s)")
    print(f"E006: peer answered after {_hello(peer, 'peer')} trigger(s)")

    tick_hz = None
    for candidate in TICK_CANDIDATES_HZ:
        if _init(dut, candidate, 1) and _init(peer, candidate, 0):
            tick_hz = candidate
            break
        print(f"E006: rmtInit rejected {candidate / 1e6:.0f} MHz")
    assert tick_hz, "no tick frequency accepted by rmtInit"
    ns_per_tick = 1e9 / tick_hz
    print(f"E006: tick = {tick_hz / 1e6:.0f} MHz ({ns_per_tick:.2f} ns/tick)")

    hi_ticks = max(1, round(HIGH_NS / ns_per_tick))
    print(f"\nE006: nominal -> measured low pulse (n={PULSES} x {REPEATS})")
    measured = {}
    for want_ns in WIDTHS_NS:
        lo_ticks = max(1, round(want_ns / ns_per_tick))
        samples, counts = [], []
        for _ in range(REPEATS):
            n, lows = _capture_round(dut, peer, lo_ticks, hi_ticks)
            counts.append(n)
            samples += [d * ns_per_tick for d in lows]
        if samples:
            measured[want_ns] = (min(samples), statistics.median(samples), max(samples))
            lo, med, hi = measured[want_ns]
            print(f"E006: {want_ns:>5} ns (={lo_ticks:>3} tick) -> "
                  f"min {lo:6.1f} / med {med:6.1f} / max {hi:6.1f} ns   symbols {counts}")
        else:
            measured[want_ns] = None
            print(f"E006: {want_ns:>5} ns (={lo_ticks:>3} tick) -> NOTHING CAPTURED  symbols {counts}")

    short, long = measured.get(290), measured.get(890)
    assert short and long, "the two SWIO-relevant widths were not both captured"
    assert short[2] < long[0], (
        f"290 ns and 890 ns overlap: short max {short[2]:.1f} >= long min {long[0]:.1f}"
    )
    print(f"\nE006: 290 ns and 890 ns are separable "
          f"(short max {short[2]:.1f} ns < long min {long[0]:.1f} ns)")
