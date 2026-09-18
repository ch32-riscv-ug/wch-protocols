"""E131: build, flash, run, and leave an Arduino CH32V003 fixture via SWIO."""

import pathlib
import re
import struct
import subprocess
import tempfile
import time
import zlib

USBIPD = "/mnt/c/Program Files/usbipd-win/usbipd.exe"
FQBN = "UIAP:ch32v:CH32V00x_EVT:pnum=CH32V003V1DOT4,clock=48MHz_HSI"
NM = "/home/mt/.arduino15/packages/UIAP/tools/riscv-none-embed-gcc/8.2.0/bin/riscv-none-embed-nm"
FLASH_BASE = 0x08000000
PAGE_SIZE = 64
MARKER_VALUE = 0xE131B007


def windows_b803():
    result = subprocess.run([USBIPD, "list"], capture_output=True, timeout=10)
    match = re.search(
        rb"(?m)^\s*(\S+)\s+1209:b803\s+.*?\s+(Attached|Shared|Not shared)\s*$",
        result.stdout,
    )
    return None if match is None else (match.group(1).decode(), match.group(2).decode())


def build_fixture(output_dir):
    fixture = pathlib.Path(__file__).parent / "fixture_v003"
    subprocess.run(
        [
            "arduino-cli", "compile", "--fqbn", FQBN,
            "--output-dir", str(output_dir), str(fixture),
        ],
        check=True,
        timeout=120,
    )
    binary = next(output_dir.glob("*.bin")).read_bytes()
    elf = next(output_dir.glob("*.elf"))
    symbols = subprocess.run(
        [NM, "-n", str(elf)], capture_output=True, check=True, text=True, timeout=10
    ).stdout
    match = re.search(r"(?m)^([0-9a-fA-F]+)\s+\S\s+e131_marker$", symbols)
    assert match is not None
    return binary, int(match.group(1), 16)


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
    body = struct.pack("<I", address) + data
    packet = b"W" + body + struct.pack("<I", zlib.crc32(body))
    for attempt in range(1, 4):
        dut.write(packet)
        match = dut.expect(
            [
                re.compile(rb"FLASH OK address=0x%08x" % address),
                re.compile(rb"FLASH STOP reason=(?!bad_address).*"),
            ],
            timeout=60,
        )
        if match.group(0).startswith(b"FLASH OK"):
            return
        print(
            f"E131: page retry attempt={attempt} address=0x{address:08x} "
            f"reason={match.group(0).decode(errors='replace')}"
        )
    raise AssertionError(f"page retry exhausted at 0x{address:08x}")


def test_v003_arduino_image_swio(dut):
    with tempfile.TemporaryDirectory(prefix="e131-v003-") as directory:
        image, marker_address = build_fixture(pathlib.Path(directory))
    padded = image + b"\xff" * (-len(image) % PAGE_SIZE)
    print(
        f"E131: image={len(image)} padded={len(padded)} "
        f"pages={len(padded)//PAGE_SIZE} marker=0x{marker_address:08x}"
    )

    dut.write("?")
    dut.expect_exact("READY commands=NBRHSWV", timeout=10)
    dut.write("N")
    dut.expect_exact("NORMALIZE END", timeout=40)
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline and windows_b803() is not None:
        time.sleep(0.25)
    assert windows_b803() is None

    started = time.monotonic()
    for offset in range(0, len(padded), PAGE_SIZE):
        write_page(dut, FLASH_BASE + offset, padded[offset : offset + PAGE_SIZE])
    print(f"E131: flash verify PASS pages={len(padded)//PAGE_SIZE}")

    dut.write("N")
    dut.expect_exact("NORMALIZE END", timeout=40)
    time.sleep(0.25)
    marker_page = marker_address & ~(PAGE_SIZE - 1)
    ram = read_page(dut, marker_page)
    marker_offset = marker_address - marker_page
    actual = struct.unpack_from("<I", ram, marker_offset)[0]
    assert actual == MARKER_VALUE, f"setup marker mismatch: 0x{actual:08x}"
    print(f"E131: setup marker PASS value=0x{actual:08x}")

    dut.write("B")
    dut.expect_exact("SWIO BOOT END", timeout=40)
    deadline = time.monotonic() + 90
    found = None
    while time.monotonic() < deadline:
        found = windows_b803()
        if found is not None:
            break
        time.sleep(0.25)
    assert found is not None
    print(
        f"E131: end-to-end PASS elapsed={time.monotonic()-started:.3f}s "
        f"busid={found[0]} state={found[1]}"
    )
