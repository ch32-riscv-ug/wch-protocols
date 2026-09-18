"""E125: establish user mode, then request and observe B803 boot HID."""

import re
import subprocess
import time

import usb.core

VID, PID = 0x1209, 0xB803
USBIPD = "/mnt/c/Program Files/usbipd-win/usbipd.exe"


def find_boot_hid():
    return usb.core.find(idVendor=VID, idProduct=PID)


def windows_boot_hid():
    result = subprocess.run(
        [USBIPD, "list"], capture_output=True, timeout=10, check=False
    )
    match = re.search(
        rb"(?m)^\s*(\S+)\s+1209:b803\s+.*?\s+(Attached|Shared|Not shared)\s*$",
        result.stdout,
    )
    if match is None:
        return None
    return match.group(1).decode(), match.group(2).decode()


def expect_lines(dut, patterns, timeout=15):
    for pattern in patterns:
        match = dut.expect(re.compile(pattern), timeout=timeout)
        print(f"E125: {match.group(0).decode()}")


def wait_windows_absent(timeout, stable=1.0):
    deadline = time.monotonic() + timeout
    absent_since = None
    while time.monotonic() < deadline:
        if windows_boot_hid() is None:
            absent_since = absent_since or time.monotonic()
            if time.monotonic() - absent_since >= stable:
                return absent_since
        else:
            absent_since = None
        time.sleep(0.1)
    return None


def wait_windows_present(timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        device = windows_boot_hid()
        if device is not None:
            return time.monotonic(), device
        time.sleep(0.5)
    return None, None


def wait_wsl_present(timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        device = find_boot_hid()
        if device is not None:
            return time.monotonic(), device
        time.sleep(0.5)
    return None, None


def test_swio_user_to_boot_hid(dut):
    baseline_windows = windows_boot_hid()
    assert baseline_windows is not None, "Windows baseline 1209:b803 is not enumerated"
    baseline = find_boot_hid()
    assert baseline is not None, "WSL baseline 1209:b803 is not attached"
    started = time.monotonic()
    print(
        f"\nE125: baseline Windows busid={baseline_windows[0]} state={baseline_windows[1]} "
        f"WSL bus={baseline.bus} address={baseline.address}"
    )

    dut.write("?")
    dut.expect_exact("# EXP E125 swio-user-to-boot-hid", timeout=10)
    dut.expect_exact("READY commands=U,B", timeout=5)
    dut.write("U")
    dut.expect_exact("USER BEGIN", timeout=5)
    expect_lines(dut, (
        rb"USER IDLE gpio16=1",
        rb"USER ATTACH status=0 DMCFGR=0x5aa5[0-9a-fA-F]{4}",
        rb"USER HALT status=0 DMSTATUS=0x[0-9a-fA-F]{8}",
        rb"USER WRITER status=0",
        rb"USER WRITE BOOT_KEY1 status=0",
        rb"USER WRITE BOOT_KEY2 status=0",
        rb"USER WRITE USER_MODE status=0",
        rb"USER RESET REQUEST",
        rb"USER RESET SENT",
        rb"USER END",
    ))

    absent_at = wait_windows_absent(60, stable=1.0)
    assert absent_at is not None, "Windows 1209:b803 did not disappear in user mode within 60 s"
    print(f"E125: Windows B803 absent at +{absent_at - started:.3f}s and stable for 1s")

    dut.write("B")
    dut.expect_exact("BOOT BEGIN", timeout=5)
    expect_lines(dut, (
        rb"IDLE gpio16=1",
        rb"ATTACH status=0 DMCFGR=0x5aa5[0-9a-fA-F]{4}",
        rb"HALT status=0 DMSTATUS=0x[0-9a-fA-F]{8}",
        rb"WRITER status=0",
        rb"WRITE BOOT_KEY1 .* status=0",
        rb"WRITE BOOT_KEY2 .* status=0",
        rb"WRITE BOOT_MODE .* status=0",
        rb"WRITE RESET_FLAGS_CLEAR .* status=0",
        rb"RESET REQUEST .*",
        rb"RESET SENT",
        rb"BOOT END",
    ))

    present_at, windows_device = wait_windows_present(90)
    assert windows_device is not None, "Windows 1209:b803 did not appear within 90 s after boot reset"
    print(
        f"E125: Windows B803 present at +{present_at - started:.3f}s "
        f"busid={windows_device[0]} state={windows_device[1]}"
    )
    _, wsl_device = wait_wsl_present(30)
    if wsl_device is None:
        print("E125: WSL auto-attach not observed within additional 30s")
    else:
        print(
            f"E125: WSL attached bus={wsl_device.bus} address={wsl_device.address} "
            f"bcdDevice=0x{wsl_device.bcdDevice:04x}"
        )
