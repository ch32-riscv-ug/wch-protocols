"""E133: capture SWIO timing and separate DMI write/read reliability."""

import re


def test_v003_swio_rmt_timing(dut):
    dut.write("C")
    read_results = {}
    for _ in range(7):
        result = dut.expect(
            re.compile(
                rb"RESULT coefficient=(\d+) captured_status=(-?\d+) "
                rb"captured=0x([0-9a-f]+) plain_status=(-?\d+) plain=0x([0-9a-f]+)"
            ),
            timeout=10,
        )
        coefficient = int(result.group(1))
        read_results[coefficient] = int(result.group(3), 16)
        capture = dut.expect(
            re.compile(rb"CAPTURE coefficient=%d symbols=(\d+) tick_ps=12500" % coefficient),
            timeout=5,
        )
        assert int(capture.group(1)) >= 40
    dut.expect_exact("CAPTURE END", timeout=5)
    assert set(read_results) == set(range(8, 15))
    assert read_results[8] == 0x5AA50401

    dut.write("W")
    write_results = {}
    for _ in range(7):
        result = dut.expect(
            re.compile(
                rb"WRITE_RESULT coefficient=(\d+) verify_c8=(\d+)/100 "
                rb"verify_same=(\d+)/100"
            ),
            timeout=10,
        )
        write_results[int(result.group(1))] = (
            int(result.group(2)), int(result.group(3)))
    dut.expect_exact("WRITE END", timeout=5)
    assert set(write_results) == set(range(8, 15))
    assert write_results[8][0] >= 95
