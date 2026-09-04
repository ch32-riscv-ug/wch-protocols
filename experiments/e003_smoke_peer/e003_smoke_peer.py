"""E003 peer environment check: discover the wiring between the two boards.

Plan and report: README.ja.md

Run:  uv run --env-file .env pytest e003_smoke_peer/e003_smoke_peer.py
"""

import os
import re

import pexpect

PINS = [int(p) for p in os.environ.get("TEST_PEER_PINS", "17,18,19,20,21").split(",")]
MAX_TRIGGERS = 3


def _hello(target, role):
    """Ask until answered; never wait a fixed time (rules section 7-10)."""
    banner = re.compile(rb"# EXP E003 v2 role=" + role.encode() + rb" pins=[\d,]+ build=[^\r\n]+")
    for attempt in range(1, MAX_TRIGGERS + 1):
        target.write("?\n")
        try:
            target.expect(banner, timeout=5)
            return attempt
        except pexpect.exceptions.TIMEOUT:
            if attempt == MAX_TRIGGERS:
                raise
    return MAX_TRIGGERS


def _read(target, pin):
    target.write(f"R{pin}\n")
    match = target.expect(re.compile(rb"READ (\d+)=(\d)"), timeout=5)
    assert int(match.group(1)) == pin, "reply is for another pin"
    return int(match.group(2))


def _scan(driver, reader):
    """Drive each candidate pin on one board, read them all on the other."""
    matrix = {}
    for out_pin in PINS:
        driver.write(f"D{out_pin}\n")
        driver.expect_exact(f"DRIVE {out_pin}=1", timeout=5)
        matrix[out_pin] = {p: _read(reader, p) for p in PINS}
        driver.write(f"L{out_pin}\n")
        driver.expect_exact(f"DRIVE {out_pin}=0", timeout=5)
    driver.write("Z\n")
    driver.expect_exact("RELEASED", timeout=5)
    return matrix


def _render(title, matrix):
    header = "    " + "".join(f"{p:>4}" for p in PINS)
    rows = [f"{out:>3} " + "".join(f"{matrix[out][i]:>4}" for i in PINS) for out in PINS]
    return "\n".join([f"\n{title} (rows = driven, cols = read)", header, *rows])


def test_smoke_peer(dut, peers):
    """Both boards run together; find which pins are actually connected."""
    peer = peers["device"]
    print(f"\nE003: primary answered after {_hello(dut, 'primary')} trigger(s)")
    print(f"E003: peer answered after {_hello(peer, 'peer')} trigger(s)")

    forward = _scan(dut, peer)
    print(_render("primary -> peer", forward))
    reverse = _scan(peer, dut)
    print(_render("peer -> primary", reverse))

    links = [(o, i) for o in PINS for i in PINS if forward[o][i] == 1]
    back = [(o, i) for o in PINS for i in PINS if reverse[o][i] == 1]
    print(f"\nprimary -> peer links: {links}")
    print(f"peer -> primary links: {back}")

    assert links or back, f"no connection found between the boards on pins {PINS}"
