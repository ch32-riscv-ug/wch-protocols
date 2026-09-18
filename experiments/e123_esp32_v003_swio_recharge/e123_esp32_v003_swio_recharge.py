"""E123: retry real CH32V003 SWIO with the reference HIGH recharge."""

import re


def test_esp32_v003_swio_recharge(dut):
    dut.write("?")
    dut.expect_exact("# EXP E123 classic-esp32 swio-high-recharge", timeout=10)
    dut.expect_exact("READY commands=A", timeout=5)
    dut.write("A")
    dut.expect_exact("ATTACH BEGIN", timeout=5)
    idle = dut.expect(re.compile(rb"IDLE gpio16=([01])"), timeout=5)
    print(f"\nE123: idle={idle.group(1).decode()}")
    while True:
        match = dut.expect(
            [
                re.compile(rb"TRY coefficient=.*"),
                re.compile(rb"DMSTATUS .*"),
                re.compile(rb"DMHARTINFO .*"),
                re.compile(rb"ATTACH (?:OK|FAIL|STOP).*"),
            ],
            timeout=10,
        )
        line = match.group(0).decode()
        print(f"E123: {line}")
        if line.startswith("ATTACH "):
            break
    dut.expect_exact("ATTACH END", timeout=5)
