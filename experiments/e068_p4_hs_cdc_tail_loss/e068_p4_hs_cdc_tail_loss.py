"""E068: is the missing tail of a CDC transfer lost or stuck?

Plan and report: README.ja.md
"""

import json
import re
import subprocess
import time
from pathlib import Path


BANNER = re.compile(rb"# EXP E068 v1 git=\S+ probe=esp32p4_usb target=none build=[^\r\n]+\r?\n")
ENV = re.compile(
    rb"ENV chip=(\S+) pattern_ready=(\d+) usb_bytes=(\d+) usb_chunk=(\d+) sender_core=(-?\d+) "
    rb"terminator_bytes=(\d+) cdc_tx_bufsize=(\d+) mounted=(\d+) speed=(-?\d+)"
)
CFG = re.compile(rb"CFG status=(\S+)")
SEND = re.compile(rb"SEND bytes=(\d+) written=(\d+) short=(\d+) elapsed_us=(\d+)")

TEST_VID_PID = "VID_1209&PID_0007"
USB_BYTES = 4 * 1024 * 1024
ATTEMPTS = 30

HERE = Path(__file__).parent
COLLECT_PS1 = HERE.parent / "e063_p4_usb_hs_enumerate" / "collect_windows.ps1"
READER = HERE / "read_tail.py"


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


def test_p4_hs_cdc_tail_loss(dut):
    dut.expect_exact("READY E068", timeout=45)
    dut.write("?")
    dut.expect(BANNER, timeout=5)
    env_fields = dut.expect(ENV, timeout=5).groups()
    env = {
        "chip": env_fields[0].decode(),
        "pattern_ready": int(env_fields[1]),
        "usb_bytes": int(env_fields[2]),
        "usb_chunk": int(env_fields[3]),
        "sender_core": int(env_fields[4]),
        "cdc_tx_bufsize": int(env_fields[6]),
        "mounted": int(env_fields[7]),
        "speed": int(env_fields[8]),
    }
    assert env["pattern_ready"] == 1, f"PSRAM pattern buffer not allocated: {env}"
    assert env["speed"] == 2, f"the port under test is not high speed: {env}"
    assert env["usb_bytes"] == USB_BYTES, f"firmware sends {env['usb_bytes']}, harness expects {USB_BYTES}"

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
    for attempt in range(ATTEMPTS):
        dut.write("C")
        assert dut.expect(CFG, timeout=10).groups()[0] == b"ok"

        host = _run_windows(
            [
                "uv.exe", "run", "--script", _win_path(READER),
                "--port", com_port, "--bytes", str(USB_BYTES),
            ],
            timeout=300,
        )
        device = dut.expect(SEND, timeout=120).groups()

        row = {
            "attempt": attempt,
            "device_written": int(device[1]),
            "device_short": int(device[2]),
            "device_elapsed_us": int(device[3]),
            **{key: host[key] for key in (
                "phase1_bytes", "phase2_bytes", "phase3_bytes", "prodded", "body_bytes",
                "short_by", "short_by_packets", "terminator_seen", "body_mismatch_at",
                "rate_mb_s", "error",
            )},
        }
        observations.append(row)
        assert row["error"] is None, f"reader failed: {row}"
        assert row["device_written"] == USB_BYTES, f"device did not write everything: {row}"
        # A gap in the middle is exactly what this experiment found, so it is
        # counted rather than asserted away (README.ja.md section 7-6).

    gaps = [r for r in observations if r["body_mismatch_at"] is not None]
    short_runs = [r for r in observations if r["short_by"] > 0]
    recovered_by_wait = [r for r in observations if r["phase2_bytes"] > 0]
    recovered_by_prod = [r for r in observations if r["prodded"] and r["short_by"] == 0]
    never_recovered = [r for r in observations if r["prodded"] and r["short_by"] > 0]
    first_short = [r["phase1_bytes"] for r in observations if r["phase1_bytes"] < USB_BYTES]

    print(f"\nE068 env={env}")
    print(f"E068 com_port={com_port} attempts={ATTEMPTS}")
    print(f"E068 phase1_short={len(first_short)}/{ATTEMPTS}")
    print(f"E068 recovered_by_wait={len(recovered_by_wait)} recovered_by_prod={len(recovered_by_prod)} "
          f"never_recovered={len(never_recovered)} still_short_after_all={len(short_runs)}")
    print(f"E068 mid_stream_gaps={len(gaps)}/{ATTEMPTS}")
    for row in gaps:
        offset = row["body_mismatch_at"]
        print(f"E068 gap attempt={row['attempt']} at_offset={offset} (packet {offset / 512:.1f}) "
              f"size={row['short_by']} B ({row['short_by_packets']:.0f} packets) "
              f"phase2={row['phase2_bytes']} phase3={row['phase3_bytes']} terminator={row['terminator_seen']}")
    if first_short:
        deficits = sorted({USB_BYTES - value for value in first_short})
        print(f"E068 phase1_deficit_bytes={deficits} packets={[d / 512 for d in deficits]}")
    for row in observations:
        if row["phase1_bytes"] < USB_BYTES or row["short_by"] > 0:
            print(f"E068 short_row={row}")
    print(f"E068 median_rate_mb_s={sorted(r['rate_mb_s'] for r in observations)[ATTEMPTS // 2]:.2f}")
