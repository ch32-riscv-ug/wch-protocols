"""E156: flash read strategies on the dedicated-GPIO RVSWD PHY.

Plan and report: README.ja.md
Run:  uv run --env-file .env pytest e156_p4_x035_flash_read_strategies/e156_p4_x035_flash_read_strategies.py -s --clean
"""

import re

READ = re.compile(rb"READ strategy=(\w+) gap_us=(\d+) bytes=(\d+) us=(\d+) dmi=(\d+) retries=(\d+) errors=(\d+) cmderr=(\d+) crc32=0x([0-9a-f]{8})")


def test_p4_x035_flash_read_strategies(dut):
    dut.write("R")
    dut.expect(re.compile(rb"# EXP E156 v1 git=\S+ probe=esp32p4_x035 target0=ch32x035f8u6"), timeout=30)
    dut.expect_exact("HALTED", timeout=10)
    for _ in range(9):
        m = dut.expect(re.compile(rb"HALTED_SWEEP half_ns=(\d+) dmstatus_parity=(\d+) dmstatus_match=(\d+) dmstatus=0x([0-9a-f]{8}) "
                                  rb"abstractcs_parity=(\d+) abstractcs_match=(\d+) abstractcs=0x([0-9a-f]{8})"), timeout=30)
        print(f"E156 halted half_ns={int(m.group(1)):5d} dmstatus parity/match={m.group(2).decode()}/{m.group(3).decode()} "
              f"(0x{m.group(4).decode()}) abstractcs parity/match={m.group(5).decode()}/{m.group(6).decode()} (0x{m.group(7).decode()})")
    sel = dut.expect(re.compile(rb"SELECTED half_ns=(\d+) found=(\d)"), timeout=10)
    print(f"E156 selected half_ns={sel.group(1).decode()} found={sel.group(2).decode()}")
    rows = {}
    for _ in range(7):
        m = dut.expect(READ, timeout=120)
        key = m.group(1).decode() + (f"_gap{int(m.group(2))}" if m.group(1) == b"autoexec_nopoll" else "")
        rows[key] = dict(bytes=int(m.group(3)), us=int(m.group(4)), dmi=int(m.group(5)), retries=int(m.group(6)),
                         errors=int(m.group(7)), cmderr=int(m.group(8)), crc=m.group(9).decode())
    busy = dut.expect(re.compile(rb"BUSY samples=100 min_us=(\d+) median_us=(\d+) max_us=(\d+)"), timeout=30)
    resume = dut.expect(re.compile(rb"RESUME ok=(\d)"), timeout=10)
    dut.expect_exact("MEASURE END", timeout=5)
    for name, r in rows.items():
        kbs = r["bytes"] / r["us"] * 1000
        print(f"E156 {name:22s} {r['us'] / 1e6:7.3f} s  {kbs:7.1f} kB/s  dmi={r['dmi']:6d} retries={r['retries']:6d} "
              f"errors={r['errors']} cmderr={r['cmderr']} crc32=0x{r['crc']}")
    print(f"E156 busy min/median/max us = {busy.group(1).decode()}/{busy.group(2).decode()}/{busy.group(3).decode()}")
    print(f"E156 resume ok={resume.group(1).decode()}")
    assert resume.group(1) == b"1", "target did not resume"
    assert rows["scalar"]["errors"] == 0 and rows["autoexec_poll"]["errors"] == 0
    assert rows["scalar"]["crc"] == rows["autoexec_poll"]["crc"], "reader mismatch"
    # nopoll is the hypothesis under test: report, and only fail if the harness reference itself broke.
