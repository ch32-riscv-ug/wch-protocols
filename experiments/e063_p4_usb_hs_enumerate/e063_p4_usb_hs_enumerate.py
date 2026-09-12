"""E063: does the ESP32-P4 USB 2.0 OTG HS port enumerate, and at which speed.

Plan and report: README.ja.md

The console (USB-Serial-JTAG) is the DUT of the harness; the port under test is
on the Windows side and is reached through powershell.exe, so that attaching it
to WSL does not hide what Windows enumerates.
"""

import json
import re
import subprocess
import time
from pathlib import Path


BANNER = re.compile(rb"# EXP E063 v1 git=\S+ probe=esp32p4_usb target=none build=[^\r\n]+\r?\n")
ENV = re.compile(
    rb"ENV chip=(\S+) rev=(\d+) cores=(\d+) flash_size=(\d+) psram_found=(\d+) "
    rb"psram_size=(\d+) otg_periph=(\d+) usb_mode=(-?\d+) cdc_on_boot=(-?\d+)"
)
USB = re.compile(
    rb"USB vid=([0-9a-f]{4}) pid=([0-9a-f]{4}) serial=(\S+) product=(\S[^ ]*(?: \S+)*?) "
    rb"mounted=(\d+) suspended=(\d+) speed=(-?\d+) rx=(\d+) tx=(\d+) lines=(\d+)"
)

TEST_VID_PID = "VID_1209&PID_0002"
# tusb_speed_t
SPEED_NAMES = {0: "full", 1: "low", 2: "high"}
ECHO_ROUNDS = 3
HERE = Path(__file__).parent


def _powershell(*args: str) -> dict:
    """Run collect_windows.ps1 on the Windows side and parse its single JSON line."""
    script = subprocess.run(
        ["wslpath", "-w", str(HERE / "collect_windows.ps1")],
        capture_output=True, text=True, check=True,
    ).stdout.strip()
    completed = subprocess.run(
        ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, *args],
        capture_output=True, timeout=120,
    )
    stdout = completed.stdout.decode("utf-8", errors="replace")
    if completed.returncode != 0:
        stderr = completed.stderr.decode("utf-8", errors="replace")
        raise RuntimeError(f"collect_windows.ps1 failed: {stdout}\n{stderr}")
    return json.loads(stdout.strip().splitlines()[-1])


def _read_usb_status(dut) -> dict:
    dut.write("u")
    fields = dut.expect(USB, timeout=5).groups()
    return {
        "vid": fields[0].decode(),
        "pid": fields[1].decode(),
        "serial": fields[2].decode(),
        "product": fields[3].decode(),
        "mounted": int(fields[4]),
        "suspended": int(fields[5]),
        "speed": int(fields[6]),
        "rx": int(fields[7]),
        "tx": int(fields[8]),
        "lines": int(fields[9]),
    }


def test_p4_usb_hs_enumerate(dut):
    # P1: the console survives a build that puts TinyUSB on the OTG HS port.
    # The console reaches this host through usbipd, which can stall the CDC
    # stream for around 20 s, so the first line gets a generous window.
    dut.expect_exact("READY E063", timeout=45)
    dut.write("?")
    dut.expect(BANNER, timeout=5)
    env_fields = dut.expect(ENV, timeout=5).groups()
    env = {
        "chip": env_fields[0].decode(),
        "rev": int(env_fields[1]),
        "cores": int(env_fields[2]),
        "flash_size": int(env_fields[3]),
        "psram_found": int(env_fields[4]),
        "psram_size": int(env_fields[5]),
        "otg_periph": int(env_fields[6]),
        "usb_mode": int(env_fields[7]),
        "cdc_on_boot": int(env_fields[8]),
    }
    dut.expect(USB, timeout=5)
    assert env["chip"].startswith("ESP32-P4")
    assert env["otg_periph"] == 2
    # USB-OTG mode with the console left on the USB-Serial-JTAG.
    assert (env["usb_mode"], env["cdc_on_boot"]) == (0, 0)

    # P2: the device's own view. Mounting waits on the host, so poll.
    deadline = time.time() + 20
    status = _read_usb_status(dut)
    while not status["mounted"] and time.time() < deadline:
        time.sleep(1)
        status = _read_usb_status(dut)
    assert status["mounted"] == 1, f"OTG HS never mounted: {status}"
    assert (status["vid"], status["pid"]) == ("1209", "0002")
    device_speed = status["speed"]

    # P3: what Windows enumerated.
    windows = _powershell("-Action", "enumerate", "-Match", TEST_VID_PID)
    assert windows["Count"] > 0, f"Windows sees no {TEST_VID_PID}: {windows}"
    com_ports = windows["ComPorts"]
    assert com_ports, f"no COM port among the enumerated devnodes: {windows}"

    # P4: a round trip over the port Windows opened.
    echoes = []
    for index in range(ECHO_ROUNDS):
        payload = f"E063-{index}"
        echo = _powershell("-Action", "echo", "-ComPort", com_ports[0], "-Text", payload)
        echoes.append(echo)
        assert echo["Error"] is None, f"echo failed: {echo}"
        assert echo["Reply"].strip() == f"ECHO {payload}", f"unexpected reply: {echo}"

    # P5: the device agrees that the bytes went through.
    after = _read_usb_status(dut)
    assert after["rx"] > status["rx"] and after["tx"] > status["tx"]
    assert after["lines"] >= status["lines"] + ECHO_ROUNDS
    assert after["speed"] == device_speed

    print(f"\nE063 env={env}")
    print(f"E063 device_speed={device_speed} ({SPEED_NAMES.get(device_speed, 'unknown')})")
    print(f"E063 device_status_before={status}")
    print(f"E063 device_status_after={after}")
    print(f"E063 windows_com_ports={com_ports}")
    print(f"E063 windows_devices={json.dumps(windows['Devices'], ensure_ascii=False)}")
    print(f"E063 echoes={echoes}")
