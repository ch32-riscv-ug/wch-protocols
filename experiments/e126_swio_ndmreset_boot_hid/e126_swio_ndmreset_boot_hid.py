"""E126: switch user/boot mode using DMI ndmreset and watch Windows USB."""

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


def wait_state(present, timeout, stable=1.0):
    deadline, since = time.monotonic() + timeout, None
    while time.monotonic() < deadline:
        matches = (windows_b803() is not None) == present
        if matches:
            since = since or time.monotonic()
            if time.monotonic() - since >= stable:
                return since, windows_b803()
        else:
            since = None
        time.sleep(0.5)
    return None, None


def expect_mode(dut, requested):
    mode = "USER" if requested == 0 else "BOOT"
    dut.expect_exact(f"{mode} BEGIN", timeout=5)
    patterns = (
        rb"MODE ATTACH status=0 DMCFGR=0x5aa5[0-9a-fA-F]{4}",
        rb"MODE HALT status=0 DMSTATUS=0x[0-9a-fA-F]{8}",
        rb"MODE WRITER status=0",
        rb"MODE WRITE BOOT_KEY1 status=0",
        rb"MODE WRITE BOOT_KEY2 status=0",
        fr"MODE WRITE value=0x{requested:08x} status=0".encode(),
        rb"MODE READBACK status=0 STATR=0x[0-9a-fA-F]{8}",
        rb"NDMRESET ASSERT",
        rb"NDMRESET RELEASE",
        fr"{mode} END".encode(),
    )
    for pattern in patterns:
        match = dut.expect(re.compile(pattern), timeout=15)
        print(f"E126: {match.group(0).decode()}")


def test_swio_ndmreset_boot_hid(dut):
    baseline = windows_b803()
    assert baseline is not None, "Windows baseline B803 missing"
    started = time.monotonic()
    print(f"\nE126: baseline busid={baseline[0]} state={baseline[1]}")
    dut.write("?")
    dut.expect_exact("# EXP E126 swio-ndmreset-boot-hid", timeout=10)
    dut.expect_exact("READY commands=U,B", timeout=5)

    dut.write("U")
    expect_mode(dut, 0)
    absent_at, _ = wait_state(False, 60)
    assert absent_at is not None, "Windows B803 did not disappear after user ndmreset"
    print(f"E126: Windows B803 absent at +{absent_at - started:.3f}s")

    dut.write("B")
    expect_mode(dut, 0x4000)
    present_at, device = wait_state(True, 90)
    assert present_at is not None, "Windows B803 did not return after boot ndmreset"
    print(
        f"E126: Windows B803 returned at +{present_at - started:.3f}s "
        f"busid={device[0]} state={device[1]}"
    )

    deadline = time.monotonic() + 30
    wsl = None
    while time.monotonic() < deadline:
        wsl = usb.core.find(idVendor=0x1209, idProduct=0xb803)
        if wsl is not None:
            break
        time.sleep(0.5)
    if wsl is None:
        print("E126: WSL auto-attach not observed within additional 30s")
    else:
        print(
            f"E126: WSL bus={wsl.bus} address={wsl.address} "
            f"bcdDevice=0x{wsl.bcdDevice:04x}"
        )
