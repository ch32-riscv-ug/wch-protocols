"""E008 wire: emit a SWIO write frame as pulse widths and check it.

Plan and report: README.ja.md

Run:  uv run --env-file .env pytest e008_wire_swio_frame/e008_wire_swio_frame.py
"""

import re
import statistics

import pexpect

TICK_HZ = 80_000_000
NS_PER_TICK = 1e9 / TICK_HZ
LO1, HI1 = 23, 83   # "1": low 287.5 ns, period 106 ticks = 1325 ns
LO0, HI0 = 71, 35   # "0": low 887.5 ns, same period
THRESHOLD_NS = 500  # anything longer than this is a zero
FRAME_BITS = 41     # start(1) + addr7 + rw(1) + data32
REPEATS = 3
MAX_TRIGGERS = 3

VECTORS = [
    ("DMSTATUS", 0x11, 0x00000000),
    ("DMCONTROL", 0x10, 0x80000001),
    ("all zero", 0x00, 0x00000000),
    ("all one", 0x7F, 0xFFFFFFFF),
    ("DMDATA0", 0x04, 0xA5C33C5A),
]


def _hello(target, role):
    banner = re.compile(rb"# EXP E008 v1 role=" + role.encode() + rb" line=\d+ build=[^\r\n]+")
    for attempt in range(1, MAX_TRIGGERS + 1):
        target.write("?\n")
        try:
            target.expect(banner, timeout=5)
            return attempt
        except pexpect.exceptions.TIMEOUT:
            if attempt == MAX_TRIGGERS:
                raise
    return MAX_TRIGGERS


def _init(target, direction):
    target.write(f"I{TICK_HZ},{direction}\n")
    match = target.expect(re.compile(rb"INIT (ok|fail) tick=\d+ dir=\d"), timeout=10)
    return match.group(1) == b"ok"


def expected_bits(addr, data):
    bits = [1]
    bits += [(addr >> i) & 1 for i in range(6, -1, -1)]
    bits += [1]
    bits += [(data >> i) & 1 for i in range(31, -1, -1)]
    assert len(bits) == FRAME_BITS
    return bits


def _round(sender, receiver, addr, data):
    receiver.write(f"C{FRAME_BITS}\n")
    receiver.expect_exact("ARMED", timeout=5)
    sender.write(f"S{addr},{data}\n")
    sender.expect(re.compile(rb"(SWIO|SWIOFAIL) n=\d+"), timeout=10)
    match = receiver.expect(re.compile(rb"(SYM n=(\d+) d=([^\r\n]+)|CAPFAIL)"), timeout=10)
    if match.group(2) is None:
        return None, []
    lows = []
    for sym in match.group(3).decode().split(";"):
        for part in sym.split(","):
            level, duration = part.split(":")
            if level == "0":
                lows.append(int(duration) * NS_PER_TICK)
    return int(match.group(2)), lows


def test_wire_swio_frame(dut, peers):
    """A SWIO write frame decodes back to the bits it was built from."""
    peer = peers["device"]
    print(f"\nE008: primary answered after {_hello(dut, 'primary')} trigger(s)")
    print(f"E008: peer answered after {_hello(peer, 'peer')} trigger(s)")
    assert _init(dut, 1) and _init(peer, 0), "rmtInit failed"
    dut.write(f"W{LO1},{HI1},{LO0},{HI0}\n")
    dut.expect_exact("WIDTHS set", timeout=5)

    ones, zeros, failures = [], [], []
    for name, addr, data in VECTORS:
        want = expected_bits(addr, data)
        ok = 0
        first = None
        for _ in range(REPEATS):
            n, lows = _round(dut, peer, addr, data)
            if n != FRAME_BITS or len(lows) != FRAME_BITS:
                first = first or f"symbols={n} lows={len(lows)}"
                continue
            got = [0 if w > THRESHOLD_NS else 1 for w in lows]
            for bit, width in zip(want, lows):
                (ones if bit else zeros).append(width)
            if got == want:
                ok += 1
            elif first is None:
                first = "".join(str(b) for b in got)
        status = "ok" if ok == REPEATS else "MISMATCH"
        print(f"E008: {name:<10} addr=0x{addr:02X} data=0x{data:08X}  {ok}/{REPEATS} {status}"
              + (f"  {first}" if first else ""))
        if ok != REPEATS:
            failures.append((name, first))

    def stats(xs):
        return f"min {min(xs):6.1f} / med {statistics.median(xs):6.1f} / max {max(xs):6.1f} ns  n={len(xs)}"

    print(f"\nE008: low pulse for bit 1 -> {stats(ones)}")
    print(f"E008: low pulse for bit 0 -> {stats(zeros)}")
    assert max(ones) < min(zeros), "the two widths overlap"
    assert not failures, f"frames did not decode: {failures}"
