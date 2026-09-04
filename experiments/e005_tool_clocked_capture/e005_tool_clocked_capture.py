"""E005 tool: clocked bit capture over the peer bench's two wires.

Plan and report: README.ja.md

Run:  uv run --env-file .env pytest e005_tool_clocked_capture/e005_tool_clocked_capture.py
"""

import re

import pexpect

PATTERN = bytes([0xA5, 0x5A, 0xC3, 0x3C])
BITS = 64
HALF_US = [20, 10, 5, 2, 1, 0]
REPEATS = 3
MAX_TRIGGERS = 3


def _hello(target, role):
    banner = re.compile(rb"# EXP E005 v1 role=" + role.encode() + rb" clk=\d+ dio=\d+ build=[^\r\n]+")
    for attempt in range(1, MAX_TRIGGERS + 1):
        target.write("?\n")
        try:
            target.expect(banner, timeout=5)
            return attempt
        except pexpect.exceptions.TIMEOUT:
            if attempt == MAX_TRIGGERS:
                raise
    return MAX_TRIGGERS


def _expected_hex(bits):
    data = (PATTERN * (bits // 8 // len(PATTERN) + 1))[: bits // 8]
    return data.hex().upper()


def _one_round(sender, receiver, half_us):
    receiver.write(f"A{BITS}\n")
    receiver.expect_exact("ARMED", timeout=5)
    sender.write(f"T{half_us},{BITS}\n")
    sender.expect_exact(f"SENT bits={BITS} half={half_us}", timeout=10)
    match = receiver.expect(re.compile(rb"(BITS=([0-9A-Fa-f]+)|TIMEOUT=(\d+))"), timeout=10)
    got = match.group(2)
    return got.decode().upper() if got else f"TIMEOUT({match.group(3).decode()})"


def test_tool_clocked_capture(dut, peers):
    """Find the shortest clock half-period that still round-trips the pattern."""
    peer = peers["device"]
    print(f"\nE005: primary answered after {_hello(dut, 'primary')} trigger(s)")
    print(f"E005: peer answered after {_hello(peer, 'peer')} trigger(s)")

    expected = _expected_hex(BITS)
    print(f"E005: expected {expected}")

    results = {}
    for half in HALF_US:
        outcomes = [_one_round(dut, peer, half) for _ in range(REPEATS)]
        ok = sum(1 for o in outcomes if o == expected)
        results[half] = (ok, outcomes)
        rate = "n/a" if half == 0 else f"{1000 / (2 * half):.0f} kbps"
        print(f"E005: half={half:>2} us  ok={ok}/{REPEATS}  ({rate})  {outcomes[0]}")

    passing = [h for h, (ok, _) in results.items() if ok == REPEATS]
    print(f"\nE005: half-periods that round-trip 3/3: {passing}")
    assert passing, f"no half-period round-tripped: {results}"
