"""Run E090's host sweep against precomputed direct TX with memcpy retained."""

import re

ROW = re.compile(r"VENDOR_BULK_IN_THROUGHPUT .*?bytes=(\d+).*?bad=(\d+).*?mbps=([\d.]+).*?errors=(\d+)")


def test_p4_vendor_in_precomputed_with_copy(dut):
    dut.expect_exact("vendor_bulk_in_throughput test start")
    rows = []
    for _ in range(17):
        match = dut.expect(ROW, timeout=120)
        rows.append([g.decode() if isinstance(g, bytes) else g for g in match.groups()])
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"
    assert all(int(r[0]) >= 1048576 and int(r[1]) == 0 and int(r[3]) == 0 for r in rows)
    print(f"\nbest precomputed-copy run = {max(float(r[2]) for r in rows):.3f} MB/s")
