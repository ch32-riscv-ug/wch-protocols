"""Reconcile imported H417 USB transactions, stdout snapshots and result JSON.

Offline evidence check only; this does not exercise target run control.
"""
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
RESULT = json.loads((ROOT / "ch32h417-core-select-2026-10-10.json").read_text())


def check(session):
    stem = f"ch32h417-core-select-run-{session}-2026-10-10"
    rows = [json.loads(line) for line in (ROOT / f"{stem}.ndjson").read_text().splitlines()]
    device = rows[1]["_device"]
    assert (device["vid"], device["pid"], device["serial"]) == (0x1A86, 0x8010, RESULT["probe_serial"])
    transfers = rows[2:]
    assert len(transfers) % 2 == 0
    for seq, row in enumerate(transfers):
        assert row["seq"] == seq and row["ok"]
        assert row["len"] == len(bytes.fromhex(row["data"]))
        assert row["chan"] == "cmd"
        assert (row["dir"], row["ep"]) == (("out", "0x01") if seq % 2 == 0 else ("in", "0x81"))
    pairs = [(bytes.fromhex(a["data"]), bytes.fromhex(b["data"])) for a, b in zip(transfers[::2], transfers[1::2])]
    assert pairs[:4] == [
        (bytes.fromhex(a), bytes.fromhex(b)) for a, b in [
            ("810d01ff", "820d01ff"), ("810d0101", "820d0402160200"),
            ("810c020103", "820c0101"), ("810d0102", "820d05c64170053d"),
        ]
    ]
    assert pairs[-1] == (bytes.fromhex("810d01ff"), bytes.fromhex("820d01ff"))
    snapshots, current, csr, busy, writes = [], None, None, False, []
    for request, reply in pairs[4:-1]:
        assert request[:3] == bytes.fromhex("810806") and reply[:3] == bytes.fromhex("820806")
        assert len(request) == len(reply) == 9
        addr, value, op = request[3], int.from_bytes(request[4:8], "big"), request[8]
        data = int.from_bytes(reply[4:8], "big")
        assert reply[3] == addr and reply[8] == 0, "DMI address/status mismatch"
        if op == 2:
            assert addr in (0x10, 0x16, 0x04, 0x17), "unexpected DMI write"
            if addr == 0x10:
                assert not busy and value in (1, 0x10001), "unexpected control/selection"
                writes.append(value)
            elif addr == 0x17:
                assert not busy and value >> 16 == 0x22, "not CSR read command"
                csr, busy = value & 0xFFFF, True
            continue
        assert op == 1
        if addr == 0x16:
            busy = bool(data & 0x1000)
            if csr is not None:
                assert data & 0x700 == 0, "abstract command error"
        if addr == 0x10:
            # Each full snapshot starts with DMCONTROL; selection readback is
            # followed by another DMCONTROL read before the next CSR snapshot.
            if current is None or current["csr"]:
                current = {"hart": (data >> 16) & 0x3FF, "dmi": {}, "csr": {}}
                snapshots.append(current)
        if addr in (0x10, 0x11, 0x12, 0x16) and current is not None and csr is None:
            current["dmi"][f"0x{addr:02x}"] = f"0x{data:08x}"
        if addr == 0x04:
            assert csr is not None and not busy
            current["csr"][f"0x{csr:03x}"] = f"0x{data:08x}"
            csr = None
    assert writes == [1, 0x10001, 1, 0x10001, 1, 1]
    expected = [r for r in RESULT["records"] if r["session"] == session]
    assert len(snapshots) == len(expected) == 5
    for actual, record in zip(snapshots, expected):
        assert actual == {k: record[k] for k in ("hart", "dmi", "csr")}, (actual, record)
    output = (ROOT / f"{stem}.txt").read_text()
    blocks = re.split(r"SNAPSHOT ([^\n]+)\n", output)
    assert len(blocks) == 11
    for i, record in enumerate(expected):
        assert blocks[2 * i + 1] == record["label"]
        block = blocks[2 * i + 2]
        for kind, key in (("DMI", "dmi"), ("CSR", "csr")):
            values = {f"0x{a}": f"0x{int(v):08x}" for a, v in re.findall(rf"{kind} ([0-9a-f]+) Ok\((\d+)\)", block)}
            assert values == record[key]
    assert re.findall(r"SELECT hart=(\d)", output) == ["1", "0", "1", "0"]
    assert output.endswith("RESTORE Ok(())\nDETACH Ok(())\n")
    print(f"session {session}: {len(transfers)} transfers, 5 snapshots, 25 CSR reads agree")


if __name__ == "__main__":
    assert RESULT["sessions"] == 2 and len(RESULT["records"]) == 10
    for session in (1, 2):
        check(session)
