"""E122: exercise a real CH32V003 SWIO attachment from classic ESP32."""

import re


def test_esp32_v003_swio_attach(dut):
    dut.write("?")
    dut.expect_exact("# EXP E122 classic-esp32 gpio16->ch32v003-pd1 swio", timeout=10)
    dut.expect_exact("READY commands=A", timeout=5)
    dut.write("A")
    dut.expect_exact("ATTACH BEGIN", timeout=5)
    idle = dut.expect(re.compile(rb"IDLE gpio16=([01])"), timeout=5)
    print(f"\nE122: idle={idle.group(1).decode()}")

    lines = []
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
      lines.append(line)
      print(f"E122: {line}")
      if line.startswith("ATTACH "):
          break

    dut.expect_exact("ATTACH END", timeout=5)
    assert lines, "attach produced no result"
