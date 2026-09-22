"""E158: is a PC sample after debug reset evidence that user code runs, and what differs in the
cycles whose UART banner never arrives.

Plan and report: README.ja.md
Run:  uv run --env-file .env pytest e158_p4_x035_reset_run_evidence/e158_p4_x035_reset_run_evidence.py -s --clean
Env:  E158_CYCLES (default 100 per phase), TEST_OEP_CLIENT_SRC, TEST_ARDUINOCORE_CH32_DIR
"""

import json
import os
import pathlib
import statistics
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, os.environ.get("TEST_OEP_CLIENT_SRC", str(HERE.parents[3] / "dev_oep" / "oep-client-python" / "src")))
CORE = pathlib.Path(os.environ.get("TEST_ARDUINOCORE_CH32_DIR", str(HERE.parents[2] / "ArduinoCore-CH32")))
sys.path.insert(0, str(CORE / "tests" / "manual" / "oep_smoke"))

from oep_client.v0 import Client, FrameTransport, codec  # noqa: E402
from oep_client.v0.flash_image import Target, program_image  # noqa: E402
from oep_client.v0.services import FixtureUart  # noqa: E402
import oep_smoke  # noqa: E402  (build(), toolchain_bin())

SKETCH = "core_api"
FQBN = "ch32-riscv-ug:ch32v:CH32X035:pnum=ANY"
UART_RX, UART_TX, BAUD = 12, 6, 115200
BANNER_WAIT_S = 0.6
CSRS = {"dpc": 0x7B1, "mcause": 0x342, "mepc": 0x341, "mstatus": 0x300, "mtvec": 0x305}
# CH32X035: APB1 0x40000000, APB2 0x40010000, AHB 0x40020000 (ArduinoCore-CH32 ch32_registers.h)
REGS = {"RCC_CTLR": 0x40021000, "RCC_CFGR0": 0x40021004, "RCC_APB2PCENR": 0x40021018, "RCC_APB1PCENR": 0x4002101C,
        "RCC_RSTSCKR": 0x40021024, "USART4_STATR": 0x40004C00, "USART4_BRR": 0x40004C08, "USART4_CTLR1": 0x40004C0C,
        "GPIOB_CFGLR": 0x40010C00, "FLASH_ACTLR": 0x40022000}


def _symbol(elf: pathlib.Path, name: str) -> int:
    out = subprocess.run([f"{oep_smoke.toolchain_bin()}/riscv-none-elf-nm", str(elf)], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise RuntimeError(f"{name} not in {elf}")


def _wait_banner(uart, banner: bytes, seconds: float):
    buf, t0 = bytearray(), time.perf_counter()
    while time.perf_counter() - t0 < seconds:
        chunk = uart.read(512)
        if chunk:
            buf += chunk
            if banner in buf:
                return round((time.perf_counter() - t0) * 1000, 1), bytes(buf)
    return None, bytes(buf)


def _dump(target, millis_addr: int) -> dict:
    target.control.halt()
    d = {name: target.control.read_register(regno) for name, regno in CSRS.items()}
    d["millis"] = target.memory.read_word(millis_addr)
    for name, addr in REGS.items():
        d[name] = target.memory.read_word(addr)
    target.control.resume()
    return d


def _cycle(target, uart, banner: bytes, millis_addr: int, confirm: bool, phase: str, i: int) -> dict:
    uart.read(4096)
    t0 = time.perf_counter()
    ok, report = target.control.reset_report(confirm=confirm)
    reset_ms = round((time.perf_counter() - t0) * 1000, 1)
    banner_ms, raw = _wait_banner(uart, banner, BANNER_WAIT_S)
    rec = {"phase": phase, "i": i, "ok": ok, "flags": report.flags, "attempts": report.attempts, "pc": report.pc,
           "reset_ms": reset_ms, "banner_ms": banner_ms, "rx_bytes": len(raw)}
    rec["dump"] = _dump(target, millis_addr)
    if banner_ms is None:
        after, raw2 = _wait_banner(uart, banner, BANNER_WAIT_S)
        rec["banner_after_halt_resume_ms"] = after
        rec["rx_bytes_after"] = len(raw2)
        if after is None:
            ok2, report2 = target.control.reset_report(confirm=True)
            rec["recovery"] = {"ok": ok2, "flags": report2.flags, "attempts": report2.attempts, "pc": report2.pc,
                               "banner_ms": _wait_banner(uart, banner, BANNER_WAIT_S)[0]}
    print(f"CYCLE phase={phase} i={i} ok={int(ok)} flags=0x{report.flags:02x} attempts={report.attempts} pc=0x{report.pc:08x} "
          f"reset_ms={reset_ms} banner_ms={banner_ms} dpc=0x{rec['dump']['dpc']:08x} millis={rec['dump']['millis']} "
          f"rstsckr=0x{rec['dump']['RCC_RSTSCKR']:08x} cfgr0=0x{rec['dump']['RCC_CFGR0']:08x} brr=0x{rec['dump']['USART4_BRR']:04x}"
          + (f" AFTER={rec.get('banner_after_halt_resume_ms')} RECOVERY={rec.get('recovery')}" if banner_ms is None else ""))
    return rec


def test_p4_x035_reset_run_evidence(dut, test_case_tempdir):
    cycles = int(os.environ.get("E158_CYCLES", "100"))
    dut.serial._redirect_thread.stop_reading()
    time.sleep(0.3)
    transport = FrameTransport(dut.serial.proc)
    transport.discard_input()
    client = Client(transport, timeout=3.0)
    client.confirm()
    client.list_functions()
    target = Target(client)
    uart = FixtureUart(client, client.find(*codec.DEF_FIXTURE_UART[:2]).function)

    with tempfile.TemporaryDirectory() as tmp:
        binary = oep_smoke.build(SKETCH, FQBN, 4, pathlib.Path(tmp), lambda s: print("BUILD" + s))
        elf = binary.with_suffix(".elf")
        millis_addr = _symbol(elf, "ch32_millis_counter")
        image = binary.read_bytes()
    print(f"IMAGE sketch={SKETCH} bytes={len(image)} ch32_millis_counter=0x{millis_addr:08x}")
    outcome = program_image(target, image)
    assert outcome.verified, outcome.as_dict()
    print(f"PROGRAM pages={outcome.pages_changed} verified=1 reset_flags=0x{int(outcome.timings['reset_flags']):02x} "
          f"clock_hz={target.control.max_clock_hz()}")

    banner = f"{SKETCH} READY".encode()
    lease, _ = client.plan_apply(uart.assignments(rx=UART_RX, tx=UART_TX))
    records = []
    try:
        uart.configure(BAUD)
        for phase, confirm in (("A_dm_only", False), ("B_pc_sample", True)):
            recs = [_cycle(target, uart, banner, millis_addr, confirm, phase, i) for i in range(cycles)]
            records += recs
            got = sum(1 for r in recs if r["banner_ms"] is not None)
            confirmed = sum(1 for r in recs if r["flags"] & 2)
            recovered = sum(1 for r in recs if r["flags"] & 4)
            halt_failed = sum(1 for r in recs if r["flags"] & 8)
            print(f"SUMMARY phase={phase} banner={got}/{cycles} ok={sum(1 for r in recs if r['ok'])} confirmed={confirmed} "
                  f"recovered={recovered} halt_failed={halt_failed} reset_ms_median={statistics.median(r['reset_ms'] for r in recs)} "
                  f"banner_ms_median={statistics.median(r['banner_ms'] for r in recs if r['banner_ms'] is not None) if got else None}")
    finally:
        pathlib.Path(test_case_tempdir, "cycles.json").write_text(json.dumps(records, indent=1))
        client.plan_release(lease)
        ok, report = target.control.reset_report(confirm=True)
        print(f"FINAL reset ok={int(ok)} flags=0x{report.flags:02x}")
        dut.serial._redirect_thread.start_reading()
    missing = [r for r in records if r["banner_ms"] is None]
    if missing:
        good = next(r for r in records if r["banner_ms"] is not None)["dump"]
        for r in missing:
            diff = {k: (f"0x{good[k]:08x}", f"0x{v:08x}") for k, v in r["dump"].items() if v != good[k]}
            print(f"DIFF phase={r['phase']} i={r['i']} vs first good: {diff}")
    assert all(r["ok"] for r in records if r["phase"] == "B_pc_sample"), "confirmed reset reported failure"
