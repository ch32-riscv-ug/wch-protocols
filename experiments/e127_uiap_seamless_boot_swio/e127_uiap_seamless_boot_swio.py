"""E127: reproduce UIAP's Seamless Switch from SWIO and watch Windows USB."""

import re
import subprocess
import time

import usb.core

USBIPD = "/mnt/c/Program Files/usbipd-win/usbipd.exe"


def windows_b803():
    result = subprocess.run([USBIPD, "list"], capture_output=True, timeout=10)
    match = re.search(
        rb"(?m)^\s*(\S+)\s+1209:b803\s+.*?\s+(Attached|Shared|Not shared)\s*$",
        result.stdout,
    )
    return None if match is None else (match.group(1).decode(), match.group(2).decode())


def test_uiap_seamless_boot_swio(dut):
    dut.write("?")
    dut.expect_exact("# EXP E127 uiap-seamless-boot-swio", timeout=10)
    dut.expect_exact("READY commands=BR", timeout=5)

    if windows_b803() is not None:
        dut.write("R")
        dut.expect_exact("RESET ASSERT pin=23", timeout=5)
        dut.expect_exact("RESET RELEASE pin=23", timeout=5)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and windows_b803() is not None:
            time.sleep(0.25)
        assert windows_b803() is None, "B803 remained after baseline reset"

    started = time.monotonic()
    dut.write("B")
    dut.expect_exact("SEAMLESS BEGIN", timeout=5)

    required = (
        rb"MODE ATTACH status=0 DMCFGR=0x5aa5[0-9a-fA-F]{4}",
        rb"MODE HALT status=0 DMSTATUS=0x[0-9a-fA-F]{8}",
        rb"MODE WRITER status=0",
        rb"SEAMLESS STATR_BEFORE status=0 value=0x[0-9a-fA-F]{8}",
    )
    for pattern in required:
        match = dut.expect(re.compile(pattern), timeout=15)
        print(f"E127: {match.group(0).decode()}")

    for pattern in (
        rb"SEAMLESS BOOT_MODE_WRITE status=0",
        rb"SEAMLESS STATR_AFTER status=0 value=0x[0-9a-fA-F]{8}",
        rb"PD4 READBACK APB2PCENR=0x[0-9a-fA-F]{8} CFGLR=0x[0-9a-fA-F]{8} OUTDR=0x[0-9a-fA-F]{8}",
        rb"SEAMLESS USB_DETACH_BEGIN ms=100",
        rb"SEAMLESS HWRESET ASSERT pin=23",
        rb"SEAMLESS HWRESET RELEASE pin=23",
        rb"SEAMLESS END",
    ):
        match = dut.expect(re.compile(pattern), timeout=20)
        print(f"E127: {match.group(0).decode()}")

    deadline = time.monotonic() + 90
    windows = None
    while time.monotonic() < deadline:
        windows = windows_b803()
        if windows is not None:
            break
        time.sleep(0.5)
    assert windows is not None, "Windows B803 did not appear within 90 s"
    print(
        f"E127: Windows B803 at +{time.monotonic() - started:.3f}s "
        f"busid={windows[0]} state={windows[1]}"
    )

    deadline = time.monotonic() + 30
    device = None
    while time.monotonic() < deadline:
        device = usb.core.find(idVendor=0x1209, idProduct=0xb803)
        if device is not None:
            break
        time.sleep(0.5)
    if device is None:
        print("E127: WSL auto-attach not observed within additional 30s")
    else:
        print(
            f"E127: WSL bus={device.bus} address={device.address} "
            f"bcdDevice=0x{device.bcdDevice:04x}"
        )
