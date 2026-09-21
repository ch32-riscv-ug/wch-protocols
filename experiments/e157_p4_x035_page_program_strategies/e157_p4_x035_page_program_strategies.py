"""E157: 256-byte page program strategies on the dedicated-GPIO RVSWD PHY (destructive, last 2 KiB).

Plan and report: README.ja.md
Run:  uv run --env-file .env pytest e157_p4_x035_page_program_strategies/e157_p4_x035_page_program_strategies.py -s --clean
"""

import re
import time

PAGE = re.compile(rb"PAGE strategy=(\w+) page=0x([0-9a-f]{8}) erase_us=(\d+) program_us=(\d+) verify_us=(\d+) mismatch=(\d+) dmi=(\d+) retries=(\d+) ok=(\d)")
SUMMARY = re.compile(rb"SUMMARY strategy=(\w+) pages=8 program_us_min=(\d+) program_us_median=(\d+) program_us_max=(\d+) mismatch_total=(\d+)")


def test_p4_x035_page_program_strategies(dut):
    seed = int(time.time()) % 1000
    dut.write(f"P{seed}")
    dut.expect(re.compile(rb"# EXP E157 v1 git=\S+ probe=esp32p4_x035 target0=ch32x035f8u6"), timeout=30)
    for _ in range(5):
        m = dut.expect(re.compile(rb"SETTLE half_ns=(\d+) reads=10000 parity_bad=(\d+) good_value=0x([0-9a-f]{8}) bad_samples=(\S+)"), timeout=30)
        print(f"E157 settle half_ns={int(m.group(1)):4d} parity_bad={int(m.group(2)):5d}/10000 good=0x{m.group(3).decode()} bad={m.group(4).decode()}")
    sel = dut.expect(re.compile(rb"SELECTED half_ns=(\d+) found=(\d)"), timeout=10)
    print(f"E157 selected half_ns={sel.group(1).decode()} found={sel.group(2).decode()}")
    dut.expect_exact("HALTED unlocked", timeout=15)
    summaries = {}
    for _ in range(2):
        for _ in range(8):
            m = dut.expect(PAGE, timeout=60)
            print(f"E157 {m.group(1).decode():16s} page=0x{m.group(2).decode()} erase={int(m.group(3)):6d} us "
                  f"program={int(m.group(4)):7d} us verify={int(m.group(5)):6d} us mismatch={m.group(6).decode()} "
                  f"dmi={int(m.group(7)):5d} retries={m.group(8).decode()} ok={m.group(9).decode()}")
        s = dut.expect(SUMMARY, timeout=10)
        summaries[s.group(1).decode()] = tuple(int(s.group(i)) for i in range(2, 6))
        print(f"E157 SUMMARY {s.group(1).decode()}: program min/median/max us = {s.group(2).decode()}/{s.group(3).decode()}/"
              f"{s.group(4).decode()} mismatch_total={s.group(5).decode()}")
    resume = dut.expect(re.compile(rb"RESUME ok=(\d)"), timeout=10)
    dut.expect_exact("MEASURE END", timeout=5)
    print(f"E157 resume ok={resume.group(1).decode()}  seed={seed}")
    assert resume.group(1) == b"1"
    assert summaries["word_dmi"][3] == 0, "reference strategy must verify"
