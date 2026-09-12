"""E064: how fast PSRAM reaches the host over the USB 2.0 OTG HS CDC.

Plan and report: README.ja.md

The console is the harness DUT. The measured port lives on Windows, so the
reader runs there through uv.exe and the device is armed over the console
before the reader writes its start byte.
"""

import json
import re
import statistics
import subprocess
import time
from pathlib import Path


BANNER = re.compile(rb"# EXP E064 v1 git=\S+ probe=esp32p4_usb target=none build=[^\r\n]+\r?\n")
ENV = re.compile(
    rb"ENV chip=(\S+) rev=(\d+) flash_size=(\d+) psram_found=(\d+) psram_size=(\d+) "
    rb"pattern_bytes=(\d+) pattern_ready=(\d+) cdc_tx_bufsize=(\d+) mounted=(\d+) speed=(-?\d+)"
)
CFG = re.compile(rb"CFG status=(\S+) bytes=(\d+) chunk=(\d+)")
SEND = re.compile(rb"SEND bytes=(\d+) chunk=(\d+) written=(\d+) short=(\d+) elapsed_us=(\d+)")

TEST_VID_PID = "VID_1209&PID_0003"
MIB = 1024 * 1024
CHUNK_SWEEP = (64, 512, 4096, 16384, 65536)
CHUNK_SWEEP_BYTES = 4 * MIB
SIZE_SWEEP = (1 * MIB, 4 * MIB, 16 * MIB)
REPEATS = 3

HERE = Path(__file__).parent
# E063 already collects Windows devnodes; reuse it rather than keeping a copy.
COLLECT_PS1 = HERE.parent / "e063_p4_usb_hs_enumerate" / "collect_windows.ps1"
READER = HERE / "read_windows.py"


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


def _read_on_windows(com_port: str, total_bytes: int, verify: bool, timeout: float) -> dict:
    command = [
        "uv.exe", "run", "--script", _win_path(READER),
        "--port", com_port, "--bytes", str(total_bytes),
    ]
    if verify:
        command.append("--verify")
    return _run_windows(command, timeout=timeout)


def _measure(dut, com_port: str, total_bytes: int, chunk: int, verify: bool) -> dict:
    dut.write(f"C {total_bytes} {chunk}")
    cfg = dut.expect(CFG, timeout=10).groups()
    assert cfg[0] == b"ok", f"device rejected the request: {cfg}"

    host = _read_on_windows(com_port, total_bytes, verify=verify, timeout=300)
    device = dut.expect(SEND, timeout=120).groups()

    elapsed_us = int(device[4])
    return {
        "bytes": total_bytes,
        "chunk": chunk,
        "device_written": int(device[2]),
        "device_short": int(device[3]),
        "device_elapsed_us": elapsed_us,
        "device_mb_s": (total_bytes / (elapsed_us / 1e6) / 1e6) if elapsed_us else 0.0,
        "host_received": host["received"],
        "host_elapsed_s": host["elapsed_s"],
        "host_mb_s": host["rate_mb_s"],
        "host_stalled": host["stalled"],
        "host_verified": host["verified"],
        "host_mismatch_at": host["mismatch_at"],
        "host_error": host["error"],
    }


def test_p4_usb_hs_cdc_rate(dut):
    # usbip can stall the console stream for around 20 s (E063).
    dut.expect_exact("READY E064", timeout=45)
    dut.write("?")
    dut.expect(BANNER, timeout=5)
    env_fields = dut.expect(ENV, timeout=5).groups()
    env = {
        "chip": env_fields[0].decode(),
        "psram_found": int(env_fields[3]),
        "psram_size": int(env_fields[4]),
        "pattern_bytes": int(env_fields[5]),
        "pattern_ready": int(env_fields[6]),
        "cdc_tx_bufsize": int(env_fields[7]),
        "mounted": int(env_fields[8]),
        "speed": int(env_fields[9]),
    }
    assert env["pattern_ready"] == 1, f"PSRAM pattern buffer not allocated: {env}"
    assert env["speed"] == 2, f"the port under test is not high speed: {env}"

    # Windows needs a moment to finish installing a PID it has not seen before.
    deadline = time.time() + 30
    while True:
        try:
            com_port = _find_com_port()
            break
        except AssertionError:
            if time.time() > deadline:
                raise
            time.sleep(2)

    observations = []

    # Sweep A: chunk size at a fixed transfer size.
    for chunk in CHUNK_SWEEP:
        for repeat in range(REPEATS):
            row = _measure(dut, com_port, CHUNK_SWEEP_BYTES, chunk, verify=(repeat == 0))
            row["sweep"] = "chunk"
            observations.append(row)
            assert not row["host_stalled"], f"host read stalled: {row}"
            assert row["host_received"] == CHUNK_SWEEP_BYTES, f"short read: {row}"
            assert row["host_mismatch_at"] is None, f"pattern mismatch: {row}"

    by_chunk = {
        chunk: statistics.median(
            r["host_mb_s"] for r in observations if r["sweep"] == "chunk" and r["chunk"] == chunk
        )
        for chunk in CHUNK_SWEEP
    }
    best_chunk = max(by_chunk, key=by_chunk.__getitem__)

    # Sweep B: transfer size at the best chunk, to see whether the rate holds.
    for total_bytes in SIZE_SWEEP:
        for repeat in range(REPEATS):
            row = _measure(dut, com_port, total_bytes, best_chunk, verify=(repeat == 0))
            row["sweep"] = "size"
            observations.append(row)
            assert not row["host_stalled"], f"host read stalled: {row}"
            assert row["host_received"] == total_bytes, f"short read: {row}"
            assert row["host_mismatch_at"] is None, f"pattern mismatch: {row}"

    print(f"\nE064 env={env}")
    print(f"E064 com_port={com_port}")
    print(f"E064 median_host_mb_s_by_chunk={by_chunk}")
    print(f"E064 best_chunk={best_chunk}")
    for row in observations:
        print(f"E064 row={row}")
