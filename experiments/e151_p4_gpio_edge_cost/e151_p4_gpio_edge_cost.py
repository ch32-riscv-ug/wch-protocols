"""E151: GPIO edge / sample cost on ESP32-P4 by drive method.

Plan and report: README.ja.md
Run:  uv run --env-file .env pytest e151_p4_gpio_edge_cost/e151_p4_gpio_edge_cost.py -s
"""

import re

LINE = re.compile(
    rb"(EDGE|READ|BIT) method=(\w+) pin=(\d+) \w+=(\d+) total_us=(\d+) ns_per_\w+=([\d.]+)")


def test_p4_gpio_edge_cost(dut):
    dut.write("R")
    dut.expect(re.compile(rb"# EXP E151 v1 git=\S+ probe=esp32p4_x035 target=none pins=\d+,\d+ cpu_mhz=\d+"),
               timeout=30)
    rows = {}
    for _ in range(3 * 9):
        m = dut.expect(LINE, timeout=30)
        kind, method = m.group(1).decode(), m.group(2).decode()
        rows.setdefault((kind, method), []).append(float(m.group(6)))
    dut.expect_exact("MEASURE END", timeout=10)
    for key in sorted(rows):
        print(f"E151 {key[0]:4s} {key[1]:13s} ns={rows[key]}")
    assert len(rows) == 9
    for kind in ("EDGE", "READ", "BIT"):
        hal = min(rows[(kind, "digitalWrite" if kind != "READ" else "digitalRead")])
        ll = max(rows[(kind, "gpio_ll")])
        assert hal > ll, f"{kind}: HAL {hal} ns not slower than gpio_ll {ll} ns"
