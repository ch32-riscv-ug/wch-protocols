"""E159: DMCONTROL write order vs. the X035 hart parking at the reset vector after ndmreset.

Plan and report: README.ja.md
Run:  uv run --env-file .env pytest e159_p4_x035_ndmreset_order/e159_p4_x035_ndmreset_order.py -s --clean
Env:  E159_CYCLES (default 100 per variant)
"""

import os
import re

SUMMARY = re.compile(rb"SUMMARY variant=(\w+) cycles=(\d+) parked=(\d+) halt_fail=(\d+) sample_fail=(\d+) seq_fail=(\d+) release_fail=(\d+) halted_fail=(\d+) resume_fail=(\d+)")


def test_p4_x035_ndmreset_order(dut):
    cycles = int(os.environ.get("E159_CYCLES", "100"))
    dut.write(f"P{cycles}")
    dut.expect(re.compile(rb"# EXP E159 v1 git=\S+ probe=esp32p4_x035 target0=ch32x035f8u6"), timeout=30)
    sel = dut.expect(re.compile(rb"SELECTED half_ns=(\d+) found=(\d)"), timeout=30)
    print(f"\nE159 selected half_ns={sel.group(1).decode()} found={sel.group(2).decode()}")
    results = {}
    retries = dut.expect(re.compile(rb"RETRIES total=(\d+) dmi=(\d+)"), timeout=900)
    print(f"E159 dmi={retries.group(2).decode()} parity_retries={retries.group(1).decode()}")
    hist = dut.expect(re.compile(rb"HALF_HIST 0=(\d+) 25=(\d+) 50=(\d+) 100=(\d+) 200=(\d+) 500=(\d+) none=(\d+)"), timeout=10)
    print("E159 half selected per attach: " + " ".join(f"{k}={hist.group(i).decode()}" for i, k in enumerate(("0", "25", "50", "100", "200", "500", "none"), 1)))
    for _ in range(7):
        m = dut.expect(SUMMARY, timeout=600)
        name = m.group(1).decode()
        results[name] = tuple(int(m.group(i)) for i in range(2, 10))
        r = results[name]
        print(f"E159 {name:18s} parked={r[1]:3d}/{r[0]} halt_fail={r[2]} sample_fail={r[3]} seq_fail={r[4]} "
              f"(release={r[5]} halted={r[6]} resume={r[7]})")
    resume = dut.expect(re.compile(rb"RESUME ok=(\d)"), timeout=10)
    dut.expect_exact("MEASURE END", timeout=5)
    print(f"E159 resume ok={resume.group(1).decode()}")
    assert len(results) == 7
    assert resume.group(1) == b"1"
