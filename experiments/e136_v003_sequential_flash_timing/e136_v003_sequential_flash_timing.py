"""E136: characterize host-sequenced V003 flash operation timing."""

import re


def test_v003_sequential_flash_timing(dut):
    dut.write("?")
    dut.expect_exact("# EXP E136 v003-sequential-flash-timing", timeout=10)
    dut.expect_exact("READY commands=M", timeout=5)
    dut.write("M")
    cases = []
    while len(cases) < 4:
        match = dut.expect(
            re.compile(rb"CASE quiet_us=(\d+) pattern=(\d) restore=(\d) elapsed_ms=(\d+)"),
            timeout=120,
        )
        cases.append(tuple(int(value) for value in match.groups()))
        assert cases[-1][1:3] == (1, 1)
    dut.expect_exact("MEASURE END", timeout=10)
    assert [case[0] for case in cases] == [0, 50, 200, 1000]
