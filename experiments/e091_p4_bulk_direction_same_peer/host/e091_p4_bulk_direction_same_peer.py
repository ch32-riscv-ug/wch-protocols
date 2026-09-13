"""Run E091's same-peer P4 host-to-device bulk OUT sweep."""

import re


ROW = re.compile(
    r"E091_OUT depth=(\d+) xfer=(\d+) bytes=(\d+) elapsed_us=(\d+) mbps=([\d.]+) "
    r"completed=(\d+) errors=(\d+) queue_full=(\d+) empty_pct=(\d+) "
    r"device_received=(\d+) device_bad=(\d+) device_elapsed_us=(\d+)"
)

DEPTHS = (1, 2, 4, 8)
SIZES = (512, 2048, 8192, 16384, 32768)


def test_p4_bulk_direction_same_peer(dut):
    dut.expect_exact("E091_HOST_BEGIN")
    dut.expect(r"E091_OPEN out_ep=0x[0-9a-f]+ out_mps=512", timeout=90)

    rows = []
    for _ in range(len(DEPTHS) * len(SIZES)):
        match = dut.expect(ROW, timeout=120)
        rows.append([g.decode() if isinstance(g, bytes) else g for g in match.groups()])

    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"

    print("\ndepth  xfer     MB/s  completed  queue_full  empty%  device_us")
    for depth, xfer, _b, _us, mbps, completed, _errors, full, empty, _rx, _bad, dev_us in rows:
        print(f"{depth:>5} {xfer:>6}  {float(mbps):7.3f} {completed:>10} "
              f"{full:>11} {empty:>7} {dev_us:>10}")

    assert all(int(r[6]) == 0 for r in rows), "host transfers must complete without errors"
    assert all(int(r[9]) == int(r[2]) for r in rows), "device must receive every completed byte"
    assert all(int(r[10]) == 0 for r in rows), "device must see only the 0xaf payload"

    best = max(float(r[4]) for r in rows)
    print(f"\nbest same-peer bulk OUT = {best:.3f} MB/s")
