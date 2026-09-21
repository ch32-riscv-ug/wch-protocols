"""E152: RVSWD clock ceiling against the fixture X035 with gpio_ll bit-bang.

Plan and report: README.ja.md
Run:  uv run --env-file .env pytest e152_p4_x035_rvswd_clock_ceiling/e152_p4_x035_rvswd_clock_ceiling.py -s
"""

import re

LINE = re.compile(
    rb"SWEEP mode=(od|pp) half_ns=(\d+) half_cycles=(\d+) reads=1000 parity_ok=(\d+) value_match=(\d+) "
    rb"dmstatus=0x([0-9a-f]{8}) ctrl_reads=200 ctrl_ok=(\d+) writes=200 write_ok=(\d+) ns_per_read=(\d+)")


def test_p4_x035_rvswd_clock_ceiling(dut):
    dut.write("S")
    dut.expect(re.compile(rb"# EXP E152 v1 git=\S+ probe=esp32p4_x035 target0=ch32x035f8u6 swdio=\d+ swclk=\d+"),
               timeout=30)
    rows = []
    for _ in range(20):
        m = dut.expect(LINE, timeout=60)
        rows.append({
            "mode": m.group(1).decode(), "half_ns": int(m.group(2)), "half_cycles": int(m.group(3)),
            "parity_ok": int(m.group(4)), "value_match": int(m.group(5)), "dmstatus": m.group(6).decode(),
            "ctrl_ok": int(m.group(7)), "write_ok": int(m.group(8)), "ns_per_read": int(m.group(9)),
        })
    dut.expect_exact("SWEEP END lines=Hi-Z", timeout=10)
    print("\nE152 mode half_ns parity value ctrl write ns/read  kbit/s(52bit)")
    for r in rows:
        kbps = 52 * 1e6 / r["ns_per_read"] if r["ns_per_read"] else 0
        print(f"E152 {r['mode']}  {r['half_ns']:5d} {r['parity_ok']:4d}/1000 {r['value_match']:4d}/1000 "
              f"{r['ctrl_ok']:3d}/200 {r['write_ok']:3d}/200 {r['ns_per_read']:7d} {kbps:8.0f} dmstatus=0x{r['dmstatus']}")
    for mode in ("od", "pp"):
        ok = [r["half_ns"] for r in rows if r["mode"] == mode and r["parity_ok"] == 1000 and
              r["value_match"] == 1000 and r["ctrl_ok"] == 200 and r["write_ok"] == 200]
        print(f"E152 {mode}: all-pass half_ns = {sorted(ok)}  min = {min(ok) if ok else None}")
        sanity = [r for r in rows if r["mode"] == mode and r["half_ns"] == 2000][0]
        assert sanity["parity_ok"] == 1000 and sanity["ctrl_ok"] == 200, f"{mode}: half 2000 ns must pass (E142)"
