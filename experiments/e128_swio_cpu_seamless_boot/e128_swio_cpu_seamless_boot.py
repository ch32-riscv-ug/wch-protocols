"""E128: execute the UIAP boot sequence on the V003 CPU from RAM."""

import re
import subprocess
import time

USBIPD = "/mnt/c/Program Files/usbipd-win/usbipd.exe"


def windows_b803():
    result = subprocess.run([USBIPD, "list"], capture_output=True, timeout=10)
    match = re.search(
        rb"(?m)^\s*(\S+)\s+1209:b803\s+.*?\s+(Attached|Shared|Not shared)\s*$",
        result.stdout,
    )
    return None if match is None else (match.group(1).decode(), match.group(2).decode())


def test_swio_cpu_seamless_boot(dut):
    dut.write("?")
    dut.expect_exact("# EXP E128 swio-cpu-seamless-boot", timeout=10)
    dut.expect_exact("READY commands=BR", timeout=5)
    if windows_b803() is not None:
        dut.write("R")
        dut.expect_exact("RESET RELEASE pin=23", timeout=5)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and windows_b803() is not None:
            time.sleep(0.25)
        assert windows_b803() is None

    started = time.monotonic()
    dut.write("B")
    for pattern in (
        rb"CPU BEGIN",
        rb"CPU INJECT name=nvic_reset words=6 status=0",
        rb"CPU DPC name=nvic_reset address=0x20000000 status=0",
        rb"CPU RESUME name=nvic_reset",
        rb"CPU INJECT name=prepare_boot words=36 status=0",
        rb"CPU DPC name=prepare_boot address=0x20000000 status=0",
        rb"CPU RESUME name=prepare_boot",
        rb"CPU USB_DETACH_WAIT ms=100",
        rb"CPU HWRESET ASSERT pin=23",
        rb"CPU HWRESET RELEASE pin=23",
        rb"CPU END",
    ):
        match = dut.expect(re.compile(pattern), timeout=20)
        print(f"E128: {match.group(0).decode()}")

    deadline = time.monotonic() + 90
    found = None
    while time.monotonic() < deadline:
        found = windows_b803()
        if found is not None:
            break
        time.sleep(0.5)
    assert found is not None, "Windows B803 did not appear within 90 s"
    print(f"E128: Windows B803 +{time.monotonic()-started:.3f}s busid={found[0]} state={found[1]}")
