"""E130: destructive-then-restored CH32V003 64-byte flash round trip."""

import re
import struct
import subprocess
import time
import zlib

USBIPD = "/mnt/c/Program Files/usbipd-win/usbipd.exe"
PAGE_ADDRESS = 0x08003FC0
PAGE_SIZE = 64


def windows_b803():
    result = subprocess.run([USBIPD, "list"], capture_output=True, timeout=10)
    match = re.search(
        rb"(?m)^\s*(\S+)\s+1209:b803\s+.*?\s+(Attached|Shared|Not shared)\s*$",
        result.stdout,
    )
    return None if match is None else (match.group(1).decode(), match.group(2).decode())


def read_page(dut, address):
    dut.write(b"V" + struct.pack("<I", address))
    match = dut.expect(
        re.compile(
            rb"READ PAGE address=0x%08x((?: [0-9a-fA-F]{8}){16})" % address
        ),
        timeout=40,
    )
    dut.expect_exact("READ OK", timeout=10)
    words = [int(item, 16) for item in match.group(1).split()]
    return b"".join(struct.pack("<I", word) for word in words)


def write_page(dut, address, data):
    assert len(data) == PAGE_SIZE
    body = struct.pack("<I", address) + data
    packet = b"W" + body + struct.pack("<I", zlib.crc32(body))
    for attempt in range(1, 4):
        dut.write(packet)
        match = dut.expect(
            [
                re.compile(rb"FLASH OK address=0x%08x" % address),
                re.compile(rb"FLASH STOP reason=packet_crc actual=0x[0-9a-fA-F]{8} expected=0x[0-9a-fA-F]{8}"),
            ],
            timeout=60,
        )
        if match.group(0).startswith(b"FLASH OK"):
            return
        print(f"E130: packet CRC retry {attempt}")
    raise AssertionError("flash packet CRC failed three times")


def test_v003_swio_flash_roundtrip(dut):
    dut.write("?")
    dut.expect_exact("# EXP E129 swio-only-cpu-boot", timeout=10)
    dut.expect_exact("READY commands=NBRHSWV", timeout=5)

    dut.write("N")
    dut.expect_exact("NORMALIZE END", timeout=30)
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline and windows_b803() is not None:
        time.sleep(0.25)
    assert windows_b803() is None, "normalize reset did not reach user mode"

    original = read_page(dut, PAGE_ADDRESS)
    pattern = bytes((0x5A + 37 * index) & 0xFF for index in range(PAGE_SIZE))
    assert original != pattern
    print(f"E130: backup={original.hex()}")

    try:
        write_page(dut, PAGE_ADDRESS, pattern)
        assert read_page(dut, PAGE_ADDRESS) == pattern
        print("E130: pattern verify PASS")
    finally:
        write_page(dut, PAGE_ADDRESS, original)
        assert read_page(dut, PAGE_ADDRESS) == original
        print("E130: restore verify PASS")

    started = time.monotonic()
    dut.write("B")
    dut.expect_exact("SWIO BOOT END", timeout=40)
    deadline = time.monotonic() + 90
    found = None
    while time.monotonic() < deadline:
        found = windows_b803()
        if found is not None:
            break
        time.sleep(0.25)
    assert found is not None, "B803 did not appear after restored-flash boot"
    print(
        f"E130: boot PASS +{time.monotonic()-started:.3f}s "
        f"busid={found[0]} state={found[1]}"
    )
