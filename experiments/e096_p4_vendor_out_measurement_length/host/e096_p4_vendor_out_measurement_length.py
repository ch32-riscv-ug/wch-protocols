"""Run E096 with the original DL-165 test's 256 KiB condition size."""

import re

ROW = re.compile(r"E091_OUT depth=(\d+) xfer=(\d+) bytes=(\d+) elapsed_us=(\d+) mbps=([\d.]+) .*?errors=(\d+).*?device_received=(\d+) device_bad=(\d+)")


def test_p4_vendor_out_measurement_length(dut):
    dut.expect_exact("E091_HOST_BEGIN")
    dut.expect(r"E091_OPEN out_ep=0x[0-9a-f]+ out_mps=512", timeout=90)
    rows = []
    for _ in range(20):
        m = dut.expect(ROW, timeout=120)
        rows.append([g.decode() if isinstance(g, bytes) else g for g in m.groups()])
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"
    assert all(int(r[2]) == 262144 and int(r[5]) == 0 and int(r[6]) == 262144 and int(r[7]) == 0 for r in rows)
    print(f"\nbest 256-KiB run = {max(float(r[4]) for r in rows):.3f} MB/s")
