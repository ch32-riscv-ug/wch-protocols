"""E124: request V003 BOOT mode over SWIO and observe HID re-enumeration."""

import re
import threading
import time

import usb.core

VID = 0x1209
PID = 0xB803


def find_hid():
    return usb.core.find(idVendor=VID, idProduct=PID)


def test_swio_boot_hid_reenumerate(dut):
    baseline = find_hid()
    assert baseline is not None, "baseline 1209:b803 is not enumerated"
    baseline_address = baseline.address
    print(f"\nE124: baseline bus={baseline.bus} address={baseline.address}")

    transitions = [(time.monotonic(), True, baseline.address)]
    stop = threading.Event()

    def watch_usb():
        last = True
        while not stop.is_set():
            device = find_hid()
            present = device is not None
            address = device.address if device is not None else None
            if present != last or (present and transitions[-1][2] != address):
                transitions.append((time.monotonic(), present, address))
                print(f"E124 USB present={int(present)} address={address}")
                last = present
            time.sleep(0.05)

    watcher = threading.Thread(target=watch_usb, daemon=True)
    watcher.start()
    started = time.monotonic()
    try:
        dut.write("?")
        dut.expect_exact("# EXP E124 swio-boot-hid-reenumerate", timeout=10)
        dut.expect_exact("READY commands=B", timeout=5)
        dut.write("B")
        dut.expect_exact("BOOT BEGIN", timeout=5)
        for pattern in (
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
        ):
            match = dut.expect(re.compile(pattern), timeout=15)
            print(f"E124: {match.group(0).decode()}")

        disappearance_deadline = time.monotonic() + 30
        while time.monotonic() < disappearance_deadline and not any(not p for _, p, _ in transitions):
            time.sleep(0.1)

        reappear_deadline = time.monotonic() + 90
        while time.monotonic() < reappear_deadline:
            missing_indices = [i for i, (_, present, _) in enumerate(transitions) if not present]
            if missing_indices:
                first_missing = missing_indices[0]
                if any(present for _, present, _ in transitions[first_missing + 1:]):
                    break
            time.sleep(0.1)
    finally:
        stop.set()
        watcher.join(timeout=2)

    relative = [(round(t - started, 3), present, address) for t, present, address in transitions]
    print(f"E124: transitions={relative}")
    missing_indices = [i for i, (_, present, _) in enumerate(transitions) if not present]
    assert missing_indices, "1209:b803 never disappeared within 30 s"
    first_missing = missing_indices[0]
    reappeared = [(t, address) for t, present, address in transitions[first_missing + 1:] if present]
    assert reappeared, "1209:b803 did not reappear within 90 s"
    device = find_hid()
    assert device is not None
    print(
        f"E124: re-enumerated bus={device.bus} address={device.address} "
        f"bcdDevice=0x{device.bcdDevice:04x} old_address={baseline_address}"
    )
