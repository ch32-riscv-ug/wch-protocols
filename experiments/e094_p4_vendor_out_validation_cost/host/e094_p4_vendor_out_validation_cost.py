"""Run E094 with the device payload checker disabled."""

import re

ROW = re.compile(
    r"E091_OUT depth=(\d+) xfer=(\d+) bytes=(\d+) elapsed_us=(\d+) mbps=([\d.]+) "
    r"completed=(\d+) errors=(\d+) queue_full=(\d+) empty_pct=(\d+) "
    r"device_received=(\d+) device_bad=(\d+) device_elapsed_us=(\d+)"
)


def test_p4_vendor_out_validation_cost(dut):
    dut.expect_exact("E091_HOST_BEGIN")
    dut.expect(r"E091_OPEN out_ep=0x[0-9a-f]+ out_mps=512", timeout=90)
    rows = []
    for _ in range(20):
        match = dut.expect(ROW, timeout=120)
        rows.append([g.decode() if isinstance(g, bytes) else g for g in match.groups()])
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"
    assert all(int(r[6]) == 0 and int(r[9]) == int(r[2]) for r in rows)
    print(f"\nbest checker-off run = {max(float(r[4]) for r in rows):.3f} MB/s")
