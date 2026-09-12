"""E065: does a second CDC on the OTG HS port raise the combined rate.

Plan and report: README.ja.md

Both readers are launched before either can start, because the device waits for
'G' on every active port; that is what makes the measured span cover the
overlap rather than two staggered transfers.
"""

import json
import re
import statistics
import subprocess
import time
from pathlib import Path


BANNER = re.compile(rb"# EXP E065 v1 git=\S+ probe=esp32p4_usb target=none build=[^\r\n]+\r?\n")
ENV = re.compile(
    rb"ENV chip=(\S+) psram_found=(\d+) psram_size=(\d+) pattern_bytes=(\d+) pattern_ready=(\d+) "
    rb"cdc_ports=(\d+) cdc_tx_bufsize=(\d+) mounted=(\d+) speed=(-?\d+)"
)
CFG = re.compile(rb"CFG status=(\S+) bytes=(\d+) chunk=(\d+) ports=(\d+)")
SEND = re.compile(rb"SEND ports=(\d+) bytes=(\d+) chunk=(\d+) span_us=(\d+)([^\r\n]*)")
PORT_FIELD = re.compile(r"p(\d+)_us=(\d+) p\d+_written=(\d+) p\d+_short=(\d+)")

TEST_VID_PID = "VID_1209&PID_0004"
MIB = 1024 * 1024
BYTES_PER_PORT = 4 * MIB
CHUNK = 4096
REPEATS = 3

HERE = Path(__file__).parent
# Reuse the collector and the reader rather than keeping copies (README.ja.md §1.2).
COLLECT_PS1 = HERE.parent / "e063_p4_usb_hs_enumerate" / "collect_windows.ps1"
READER = HERE.parent / "e064_p4_usb_hs_cdc_rate" / "read_windows.py"


def _win_path(path: Path) -> str:
    return subprocess.run(
        ["wslpath", "-w", str(path)], capture_output=True, text=True, check=True
    ).stdout.strip()


def _find_com_ports() -> list[str]:
    completed = subprocess.run(
        [
            "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", _win_path(COLLECT_PS1), "-Action", "enumerate", "-Match", TEST_VID_PID,
        ],
        capture_output=True, timeout=120,
    )
    stdout = completed.stdout.decode("utf-8", errors="replace")
    if completed.returncode != 0:
        raise RuntimeError(f"collect_windows.ps1 failed: {stdout}")
    result = json.loads(stdout.strip().splitlines()[-1])
    # Interface order decides which COM belongs to which device port, and the
    # instance id carries it as &MI_00 / &MI_02.
    ports = [
        (device["InstanceId"], device["ComPort"])
        for device in result["Devices"]
        if device["ComPort"]
    ]
    ports.sort()
    return [com for _, com in ports]


def _start_reader(com_port: str, total_bytes: int, verify: bool) -> subprocess.Popen:
    command = [
        "uv.exe", "run", "--script", _win_path(READER),
        "--port", com_port, "--bytes", str(total_bytes),
    ]
    if verify:
        command.append("--verify")
    return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def _collect_reader(process: subprocess.Popen, timeout: float) -> dict:
    stdout, stderr = process.communicate(timeout=timeout)
    text = stdout.decode("utf-8", errors="replace")
    if process.returncode != 0:
        raise RuntimeError(f"reader failed: {text}\n{stderr.decode('utf-8', errors='replace')}")
    return json.loads(text.strip().splitlines()[-1])


def _measure(dut, com_ports: list[str], ports: int, verify: bool) -> dict:
    dut.write(f"C {BYTES_PER_PORT} {CHUNK} {ports}")
    cfg = dut.expect(CFG, timeout=10).groups()
    assert cfg[0] == b"ok", f"device rejected the request: {cfg}"

    # Launch every reader before collecting any, so the device sees all the
    # start bytes close together.
    processes = [_start_reader(com_ports[index], BYTES_PER_PORT, verify) for index in range(ports)]
    hosts = [_collect_reader(process, timeout=300) for process in processes]

    device = dut.expect(SEND, timeout=120).groups()
    span_us = int(device[3])
    per_port = {
        int(index): {"elapsed_us": int(elapsed), "written": int(written), "short": int(short)}
        for index, elapsed, written, short in PORT_FIELD.findall(device[4].decode())
    }

    total_bytes = BYTES_PER_PORT * ports
    return {
        "ports": ports,
        "bytes_per_port": BYTES_PER_PORT,
        "total_bytes": total_bytes,
        "span_us": span_us,
        "combined_mb_s": total_bytes / (span_us / 1e6) / 1e6 if span_us else 0.0,
        "device_ports": per_port,
        "host_ports": hosts,
        "host_sum_mb_s": sum(host["rate_mb_s"] for host in hosts),
    }


def test_p4_usb_hs_dual_cdc_rate(dut):
    # usbip can stall the console stream for around 20 s (E063).
    dut.expect_exact("READY E065", timeout=45)
    dut.write("?")
    dut.expect(BANNER, timeout=5)
    env_fields = dut.expect(ENV, timeout=5).groups()
    env = {
        "chip": env_fields[0].decode(),
        "psram_found": int(env_fields[1]),
        "pattern_ready": int(env_fields[4]),
        "cdc_ports": int(env_fields[5]),
        "cdc_tx_bufsize": int(env_fields[6]),
        "mounted": int(env_fields[7]),
        "speed": int(env_fields[8]),
    }
    assert env["pattern_ready"] == 1, f"PSRAM pattern buffer not allocated: {env}"
    assert env["speed"] == 2, f"the port under test is not high speed: {env}"
    assert env["cdc_ports"] == 2, f"the build does not have two CDC ports: {env}"

    deadline = time.time() + 45
    com_ports: list[str] = []
    while time.time() < deadline:
        com_ports = _find_com_ports()
        if len(com_ports) >= 2:
            break
        time.sleep(2)
    assert len(com_ports) >= 2, f"Windows gave only {com_ports} for {TEST_VID_PID}"

    observations = []
    for ports in (1, 2):
        for repeat in range(REPEATS):
            row = _measure(dut, com_ports, ports, verify=(repeat == 0))
            observations.append(row)
            for index, host in enumerate(row["host_ports"]):
                assert not host["stalled"], f"port {index} stalled: {row}"
                assert host["received"] == BYTES_PER_PORT, f"port {index} short read: {row}"
                assert host["mismatch_at"] is None, f"port {index} pattern mismatch: {row}"
            assert all(p["short"] == 0 for p in row["device_ports"].values()), f"short writes: {row}"

    medians = {
        ports: statistics.median(r["combined_mb_s"] for r in observations if r["ports"] == ports)
        for ports in (1, 2)
    }
    gain = medians[2] / medians[1] if medians[1] else 0.0

    print(f"\nE065 env={env}")
    print(f"E065 com_ports={com_ports}")
    print(f"E065 median_combined_mb_s={medians}")
    print(f"E065 gain_2_over_1={gain:.3f}")
    for row in observations:
        print(f"E065 row={row}")
