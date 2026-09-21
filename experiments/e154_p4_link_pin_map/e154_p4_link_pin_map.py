"""E154: verify the declared 8-wire GPIO link between the two P4 boards.

Plan and report: README.ja.md
Run:  uv run --env-file .env pytest e154_p4_link_pin_map/e154_p4_link_pin_map.py -s --clean
"""

import os
import re

import pexpect

PINS = [int(p) for p in os.environ["TEST_P4_LINK_PINS"].split(",")]


def _hello(target, role):
    banner = re.compile(rb"# EXP E154 v1 git=\S+ role=" + role.encode() + rb" pins=[\d,]+ build=[^\r\n]+")
    for attempt in range(1, 4):
        target.write("?\n")
        try:
            target.expect(banner, timeout=5)
            return attempt
        except pexpect.exceptions.TIMEOUT:
            if attempt == 3:
                raise


def _read(target, pin):
    target.write(f"R{pin}\n")
    m = target.expect(re.compile(rb"READ (\d+)=(\d)"), timeout=5)
    assert int(m.group(1)) == pin
    return int(m.group(2))


def _scan(driver, reader):
    matrix = {}
    for out in PINS:
        driver.write(f"D{out}\n")
        driver.expect_exact(f"DRIVE {out}=1", timeout=5)
        matrix[out] = {p: _read(reader, p) for p in PINS}
        driver.write(f"L{out}\n")
        driver.expect_exact(f"DRIVE {out}=0", timeout=5)
    driver.write("Z\n")
    driver.expect_exact("RELEASED", timeout=5)
    return matrix


def _render(title, matrix):
    header = "     " + "".join(f"{p:>4}" for p in PINS)
    rows = [f"{out:>4} " + "".join(f"{matrix[out][i]:>4}" for i in PINS) for out in PINS]
    return "\n".join([f"\nE154 {title} (rows = driven, cols = read)", header, *rows])


def test_p4_link_pin_map(dut, peers):
    peer = peers["p4b"]
    print(f"\nE154: fixture answered after {_hello(dut, 'fixture')} trigger(s)")
    print(f"E154: peer answered after {_hello(peer, 'peer')} trigger(s)")
    forward = _scan(dut, peer)
    print(_render("fixture -> peer", forward))
    reverse = _scan(peer, dut)
    print(_render("peer -> fixture", reverse))
    fwd = [(o, i) for o in PINS for i in PINS if forward[o][i]]
    rev = [(o, i) for o in PINS for i in PINS if reverse[o][i]]
    print(f"E154 fixture->peer links: {fwd}")
    print(f"E154 peer->fixture links: {rev}")
    identity = [(p, p) for p in PINS]
    assert fwd == identity, f"fixture->peer is not the declared 1:1 map: {fwd}"
    assert rev == identity, f"peer->fixture is not the declared 1:1 map: {rev}"
