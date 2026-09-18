"""E129: enter the UIAP bootloader using only PD1/SWIO."""

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


def test_swio_only_cpu_boot(dut):
    dut.write("?")
    dut.expect_exact("# EXP E129 swio-only-cpu-boot", timeout=10)
    dut.expect_exact("READY commands=NBRHSWV", timeout=5)

    dut.write("N")
    dut.expect_exact("NORMALIZE BEGIN", timeout=5)
    for pattern in (
        rb"CPU INJECT name=normalize_user_reset words=37 status=0",
        rb"CPU DPC name=normalize_user_reset address=0x20000000 status=0",
        rb"CPU RESUME name=normalize_user_reset",
        rb"NORMALIZE END",
    ):
        dut.expect(re.compile(pattern), timeout=20)

    deadline = time.monotonic() + 30
    while time.monotonic() < deadline and windows_b803() is not None:
        time.sleep(0.25)
    assert windows_b803() is None, "normalize software reset did not leave boot HID"

    started = time.monotonic()
    dut.write("B")
    for pattern in (
        rb"SWIO BOOT BEGIN",
        rb"MODE ATTACH status=0 DMCFGR=0x5aa5[0-9a-fA-F]{4}",
        rb"CPU INJECT name=prepare_boot_and_reset words=61 status=0",
        rb"CPU DPC name=prepare_boot_and_reset address=0x20000000 status=0",
        rb"CPU RESUME name=prepare_boot_and_reset",
        rb"SWIO CPU_RESET_ARMED delay_loops=5000000",
        rb"SWIO BOOT END",
    ):
        match = dut.expect(re.compile(pattern), timeout=20)
        print(f"E129: {match.group(0).decode()}")

    deadline = time.monotonic() + 90
    found = None
    while time.monotonic() < deadline:
        found = windows_b803()
        if found is not None:
            break
        time.sleep(0.25)
    assert found is not None, "Windows B803 did not appear within 90 s"
    print(f"E129: Windows B803 +{time.monotonic()-started:.3f}s busid={found[0]} state={found[1]}")
