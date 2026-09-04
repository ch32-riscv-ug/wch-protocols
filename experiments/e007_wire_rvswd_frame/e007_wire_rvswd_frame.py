"""E007 wire: emit an RVSWD host-phase frame and check it on the wire.

Plan and report: README.ja.md

Run:  uv run --env-file .env pytest e007_wire_rvswd_frame/e007_wire_rvswd_frame.py
"""

import re

import pexpect

HALF_US = 5  # E005 says 5 us per half is the reliable floor
FRAME_BITS = 42
REPEATS = 3
MAX_TRIGGERS = 3

# addr7, data32, op2 -- names follow protocols/riscv-debug-module.ja.md
VECTORS = [
    ("DMSTATUS read", 0x11, 0x00000000, 1),
    ("DMCONTROL write", 0x10, 0x80000001, 2),
    ("all zero", 0x00, 0x00000000, 0),
    ("all one", 0x7F, 0xFFFFFFFF, 3),
    ("DMDATA0 write", 0x04, 0xA5C33C5A, 2),
]


def _hello(target, role):
    banner = re.compile(rb"# EXP E007 v1 role=" + role.encode() + rb" clk=\d+ dio=\d+ build=[^\r\n]+")
    for attempt in range(1, MAX_TRIGGERS + 1):
        target.write("?\n")
        try:
            target.expect(banner, timeout=5)
            return attempt
        except pexpect.exceptions.TIMEOUT:
            if attempt == MAX_TRIGGERS:
                raise
    return MAX_TRIGGERS


def expected_hex(addr, data, op):
    """addr7 + data32 + op2 + parity1, MSB first; parity makes the ones count odd."""
    bits = [(addr >> i) & 1 for i in range(6, -1, -1)]
    bits += [(data >> i) & 1 for i in range(31, -1, -1)]
    bits += [(op >> i) & 1 for i in range(1, -1, -1)]
    bits.append(0 if sum(bits) % 2 else 1)
    assert len(bits) == FRAME_BITS
    padded = bits + [0] * (-len(bits) % 8)
    return bytes(
        int("".join(str(b) for b in padded[i : i + 8]), 2) for i in range(0, len(padded), 8)
    ).hex().upper()


def _round(sender, receiver, addr, data, op):
    receiver.write(f"A{FRAME_BITS}\n")
    receiver.expect_exact("ARMED", timeout=5)
    sender.write(f"F{addr},{data},{op},{HALF_US}\n")
    sender.expect_exact(f"FRAME bits={FRAME_BITS}", timeout=10)
    match = receiver.expect(re.compile(rb"(BITS=([0-9A-Fa-f]+)|TIMEOUT=(\d+))"), timeout=10)
    got = match.group(2)
    return got.decode().upper() if got else f"TIMEOUT({match.group(3).decode()})"


def test_wire_rvswd_frame(dut, peers):
    """The 42-bit host phase reaches the wire exactly as the spec describes."""
    peer = peers["device"]
    print(f"\nE007: primary answered after {_hello(dut, 'primary')} trigger(s)")
    print(f"E007: peer answered after {_hello(peer, 'peer')} trigger(s)")

    failures = []
    for name, addr, data, op in VECTORS:
        want = expected_hex(addr, data, op)
        got = [_round(dut, peer, addr, data, op) for _ in range(REPEATS)]
        ok = sum(1 for g in got if g == want)
        mark = "ok" if ok == REPEATS else "MISMATCH"
        print(f"E007: {name:<16} addr=0x{addr:02X} data=0x{data:08X} op={op}  "
              f"want {want}  got {got[0]}  {ok}/{REPEATS} {mark}")
        if ok != REPEATS:
            failures.append((name, want, got))

    assert not failures, f"frames did not match: {failures}"
