"""E067: what PARLIO capture and USB HS sending cost each other.

Plan and report: README.ja.md
"""

import json
import re
import statistics
import subprocess
import time
from pathlib import Path


BANNER = re.compile(rb"# EXP E067 v1 git=\S+ probe=esp32p4_usb target=none build=[^\r\n]+\r?\n")
ENV = re.compile(
    rb"ENV chip=(\S+) psram_found=(\d+) pattern_ready=(\d+) sink_ready=(\d+) ring_ready=(\d+) "
    rb"modes=(\d+) lanes=(\d+) usb_bytes=(\d+) ring_bytes=(\d+) arduino_core=(\d+) mounted=(\d+) speed=(-?\d+)"
)
CFG = re.compile(rb"CFG status=(\S+) mode=(\d+) rate_hz=(\d+) needs_go=(\d+)")
RUN = re.compile(
    rb"RUN mode=(\d+) name=(\S+) capture=(\d+) usb=(\d+) harvest_core=(-?\d+) usb_core=(-?\d+) "
    rb"rate_hz=(\d+) status=(-?\d+) usb_elapsed_us=(\d+) usb_written=(\d+) usb_short=(\d+) "
    rb"cap_elapsed_us=(\d+) cap_callback_bytes=(\d+) cap_copied=(\d+) cap_overflow=(\d+) cap_timeout=(\d+)"
)

TEST_VID_PID = "VID_1209&PID_0006"
MIB = 1024 * 1024
USB_BYTES = 4 * MIB - 100  # deliberately not a multiple of the 512 B bulk packet
SAMPLE_RATE_HZ = 32_000_000
LANES = 2
# usb_c0 ran first in the first two attempts and lost its last packets both
# times; running usb_c1 first separates "core 0" from "first transfer".
MODES = (1, 0, 2, 3, 4, 5, 6, 7)
REPEATS = 3

HERE = Path(__file__).parent
COLLECT_PS1 = HERE.parent / "e063_p4_usb_hs_enumerate" / "collect_windows.ps1"
READER = HERE.parent / "e064_p4_usb_hs_cdc_rate" / "read_windows.py"


def _win_path(path: Path) -> str:
    return subprocess.run(
        ["wslpath", "-w", str(path)], capture_output=True, text=True, check=True
    ).stdout.strip()


def _run_windows(command: list[str], timeout: float) -> dict:
    completed = subprocess.run(command, capture_output=True, timeout=timeout)
    stdout = completed.stdout.decode("utf-8", errors="replace")
    if completed.returncode != 0:
        stderr = completed.stderr.decode("utf-8", errors="replace")
        raise RuntimeError(f"{command[0]} failed: {stdout}\n{stderr}")
    return json.loads(stdout.strip().splitlines()[-1])


def _find_com_port() -> str:
    result = _run_windows(
        [
            "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", _win_path(COLLECT_PS1), "-Action", "enumerate", "-Match", TEST_VID_PID,
        ],
        timeout=120,
    )
    ports = result["ComPorts"]
    assert ports, f"Windows shows no COM port for {TEST_VID_PID}: {result}"
    return ports[0]


def _measure(dut, com_port: str, mode: int) -> dict:
    dut.write(f"C {mode} {SAMPLE_RATE_HZ}")
    cfg = dut.expect(CFG, timeout=10).groups()
    assert cfg[0] == b"ok", f"device rejected the request: {cfg}"
    needs_go = cfg[3] == b"1"

    host = None
    if needs_go:
        host = _run_windows(
            [
                "uv.exe", "run", "--script", _win_path(READER),
                "--port", com_port, "--bytes", str(USB_BYTES),
            ],
            timeout=300,
        )

    device = dut.expect(RUN, timeout=120).groups()
    usb_elapsed_us = int(device[8])
    cap_elapsed_us = int(device[11])
    cap_callback = int(device[12])
    cap_copied = int(device[13])

    return {
        "mode": int(device[0]),
        "name": device[1].decode(),
        "capture": int(device[2]),
        "usb": int(device[3]),
        "harvest_core": int(device[4]),
        "usb_core": int(device[5]),
        "status": int(device[7]),
        "usb_elapsed_us": usb_elapsed_us,
        "usb_written": int(device[9]),
        "usb_short": int(device[10]),
        "usb_device_mb_s": (USB_BYTES / (usb_elapsed_us / 1e6) / 1e6) if (needs_go and usb_elapsed_us) else None,
        "usb_host_mb_s": host["rate_mb_s"] if host else None,
        "usb_host_received": host["received"] if host else None,
        "usb_host_stalled": host["stalled"] if host else None,
        "cap_elapsed_us": cap_elapsed_us,
        "cap_callback_bytes": cap_callback,
        "cap_copied": cap_copied,
        "cap_overflow": int(device[14]),
        "cap_timeout": int(device[15]),
        "cap_mb_s": cap_copied / (cap_elapsed_us / 1e6) / 1e6 if cap_elapsed_us else None,
        "cap_missing_bytes": cap_callback - cap_copied,
        # The device reports every byte written while the host is short: the
        # tail of the transfer did not arrive. Counted, not asserted.
        "usb_tail_lost": (USB_BYTES - host["received"]) if host else None,
    }


def test_p4_usb_vs_capture_core(dut):
    dut.expect_exact("READY E067", timeout=45)
    dut.write("?")
    dut.expect(BANNER, timeout=5)
    env_fields = dut.expect(ENV, timeout=5).groups()
    env = {
        "chip": env_fields[0].decode(),
        "pattern_ready": int(env_fields[2]),
        "sink_ready": int(env_fields[3]),
        "ring_ready": int(env_fields[4]),
        "modes": int(env_fields[5]),
        "lanes": int(env_fields[6]),
        "arduino_core": int(env_fields[9]),
        "mounted": int(env_fields[10]),
        "speed": int(env_fields[11]),
    }
    assert env["pattern_ready"] == env["sink_ready"] == env["ring_ready"] == 1, f"buffers missing: {env}"
    assert env["speed"] == 2, f"the port under test is not high speed: {env}"
    assert env["modes"] == len(MODES), f"firmware has {env['modes']} modes, harness expects {len(MODES)}"
    assert env["lanes"] == LANES, f"unexpected lane count: {env}"

    deadline = time.time() + 45
    while True:
        try:
            com_port = _find_com_port()
            break
        except AssertionError:
            if time.time() > deadline:
                raise
            time.sleep(2)

    observations = []
    for mode in MODES:
        for _ in range(REPEATS):
            row = _measure(dut, com_port, mode)
            observations.append(row)
            assert row["status"] == 0, f"device reported an error: {row}"
            if row["usb"]:
                assert row["usb_written"] == USB_BYTES, f"short USB transfer: {row}"
                assert row["usb_short"] == 0, f"short writes: {row}"

    def median_of(name: str, key: str):
        values = [r[key] for r in observations if r["name"] == name and r[key] is not None]
        return statistics.median(values) if values else None

    names = [mode["name"] for mode in observations[:: REPEATS]]
    summary = {
        name: {
            "usb": median_of(name, "usb_device_mb_s"),
            "usb_host": median_of(name, "usb_host_mb_s"),
            "cap": median_of(name, "cap_mb_s"),
        }
        for name in names
    }

    print(f"\nE067 env={env}")
    print(f"E067 com_port={com_port}")
    print(f"E067 sample_rate_hz={SAMPLE_RATE_HZ} lanes={LANES} expected_capture_mb_s={SAMPLE_RATE_HZ * LANES / 8 / 1e6}")
    for name, values in summary.items():
        usb = f"{values['usb']:.2f}" if values["usb"] is not None else "-"
        cap = f"{values['cap']:.2f}" if values["cap"] is not None else "-"
        overflow = sum(r["cap_overflow"] for r in observations if r["name"] == name)
        missing = max((r["cap_missing_bytes"] for r in observations if r["name"] == name), default=0)
        tails = [r["usb_tail_lost"] for r in observations if r["name"] == name and r["usb_tail_lost"]]
        print(
            f"E067 mode {name}: usb(device) {usb} MB/s, capture {cap} MB/s, "
            f"cap_overflow {overflow}, cap_missing {missing}, usb_tail_lost {tails}"
        )
    for row in observations:
        print(f"E067 row={row}")
