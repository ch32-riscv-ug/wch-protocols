"""Run E092's multi-packet vendor OUT arm sweep."""

import re


ROW = re.compile(
    r"E091_OUT depth=(\d+) xfer=(\d+) bytes=(\d+) elapsed_us=(\d+) mbps=([\d.]+) "
    r"completed=(\d+) errors=(\d+) queue_full=(\d+) empty_pct=(\d+) "
    r"device_received=(\d+) device_bad=(\d+) device_elapsed_us=(\d+)"
)

DEPTHS = (1, 2, 4, 8)
SIZES = (512, 2048, 8192, 16384, 32768)


def test_p4_vendor_out_arm_size(dut):
    dut.expect_exact("E091_HOST_BEGIN")
    dut.expect(r"E091_OPEN out_ep=0x[0-9a-f]+ out_mps=512", timeout=90)

    rows = []
    for _ in range(len(DEPTHS) * len(SIZES)):
        match = dut.expect(ROW, timeout=120)
        rows.append([g.decode() if isinstance(g, bytes) else g for g in match.groups()])

    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"
    assert all(int(r[6]) == 0 for r in rows)
    assert all(int(r[9]) == int(r[2]) for r in rows)
    assert all(int(r[10]) == 0 for r in rows)

    print("\ndepth  xfer     MB/s  completed  queue_full  empty%  device_us")
    for depth, xfer, _b, _us, mbps, completed, _errors, full, empty, _rx, _bad, dev_us in rows:
        print(f"{depth:>5} {xfer:>6}  {float(mbps):7.3f} {completed:>10} "
              f"{full:>11} {empty:>7} {dev_us:>10}")
    print(f"\nbest 8192-byte-arm bulk OUT = {max(float(r[4]) for r in rows):.3f} MB/s")
