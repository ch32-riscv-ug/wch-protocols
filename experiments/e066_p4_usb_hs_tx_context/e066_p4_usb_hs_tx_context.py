"""E066: does the sender's execution context set the CDC download rate.

Plan and report: README.ja.md
"""

import json
import re
import statistics
import subprocess
import time
from pathlib import Path


BANNER = re.compile(rb"# EXP E066 v1 git=\S+ probe=esp32p4_usb target=none build=[^\r\n]+\r?\n")
ENV = re.compile(
    rb"ENV chip=(\S+) psram_found=(\d+) pattern_ready=(\d+) modes=(\d+) max_priorities=(\d+) "
    rb"arduino_core=(\d+) cdc_tx_bufsize=(\d+) mounted=(\d+) speed=(-?\d+)"
)
CFG = re.compile(rb"CFG status=(\S+) bytes=(\d+) chunk=(\d+) mode=(\d+)")
SEND = re.compile(
    rb"SEND mode=(\d+) name=(\S+) priority=(\d+) core=(-?\d+) ran_on=(-?\d+) bytes=(\d+) chunk=(\d+) "
    rb"written=(\d+) short=(\d+) elapsed_us=(\d+)"
)

TEST_VID_PID = "VID_1209&PID_0005"
MIB = 1024 * 1024
TRANSFER_BYTES = 4 * MIB
CHUNK = 4096
MODES = (0, 1, 2, 3, 4, 5)
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


def _measure(dut, com_port: str, mode: int, verify: bool) -> dict:
    dut.write(f"C {TRANSFER_BYTES} {CHUNK} {mode}")
    cfg = dut.expect(CFG, timeout=10).groups()
    assert cfg[0] == b"ok", f"device rejected the request: {cfg}"

    command = [
        "uv.exe", "run", "--script", _win_path(READER),
        "--port", com_port, "--bytes", str(TRANSFER_BYTES),
    ]
    if verify:
        command.append("--verify")
    host = _run_windows(command, timeout=300)
    device = dut.expect(SEND, timeout=120).groups()

    elapsed_us = int(device[9])
    return {
        "mode": int(device[0]),
        "name": device[1].decode(),
        "priority": int(device[2]),
        "core": int(device[3]),
        "ran_on": int(device[4]),
        "device_written": int(device[7]),
        "device_short": int(device[8]),
        "device_elapsed_us": elapsed_us,
        "device_mb_s": TRANSFER_BYTES / (elapsed_us / 1e6) / 1e6 if elapsed_us else 0.0,
        "host_mb_s": host["rate_mb_s"],
        "host_received": host["received"],
        "host_stalled": host["stalled"],
        "host_mismatch_at": host["mismatch_at"],
    }


def test_p4_usb_hs_tx_context(dut):
    dut.expect_exact("READY E066", timeout=45)
    dut.write("?")
    dut.expect(BANNER, timeout=5)
    env_fields = dut.expect(ENV, timeout=5).groups()
    env = {
        "chip": env_fields[0].decode(),
        "pattern_ready": int(env_fields[2]),
        "modes": int(env_fields[3]),
        "max_priorities": int(env_fields[4]),
        "arduino_core": int(env_fields[5]),
        "cdc_tx_bufsize": int(env_fields[6]),
        "mounted": int(env_fields[7]),
        "speed": int(env_fields[8]),
    }
    assert env["pattern_ready"] == 1, f"PSRAM pattern buffer not allocated: {env}"
    assert env["speed"] == 2, f"the port under test is not high speed: {env}"
    assert env["modes"] == len(MODES), f"firmware has {env['modes']} modes, harness expects {len(MODES)}"

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
        for repeat in range(REPEATS):
            row = _measure(dut, com_port, mode, verify=(repeat == 0))
            observations.append(row)
            assert not row["host_stalled"], f"host read stalled: {row}"
            assert row["host_received"] == TRANSFER_BYTES, f"short read: {row}"
            assert row["host_mismatch_at"] is None, f"pattern mismatch: {row}"
            assert row["device_short"] == 0, f"short writes: {row}"

    medians = {
        row["name"]: statistics.median(
            r["host_mb_s"] for r in observations if r["mode"] == row["mode"]
        )
        for row in observations
    }
    baseline = medians[observations[0]["name"]]

    print(f"\nE066 env={env}")
    print(f"E066 com_port={com_port}")
    print(f"E066 median_host_mb_s={medians}")
    ratios = {name: round(value / baseline, 3) for name, value in medians.items()}
    print(f"E066 ratio_vs_loop={ratios}")
    for name, value in medians.items():
        print(f"E066 mode {name}: median {value:.2f} MB/s, ratio {value / baseline:.3f}")
    for row in observations:
        print(f"E066 row={row}")
