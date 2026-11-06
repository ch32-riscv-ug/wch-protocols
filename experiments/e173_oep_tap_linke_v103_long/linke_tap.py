"""Tap the WCH-LinkE <-> CH32V103 RVSWD lines (branched onto an OEP probe's GPIO44 = SWCLK, GPIO45 = SWDIO) while
ch32rv runs one read-only operation on the LinkE; each run to out/<name>.wireskein with out/<name>.json (the command,
its output, the rate). usage: linke_tap.py <out dir> [name,...] (run with the wireskein checkout's Python)"""
import json, subprocess, sys
from pathlib import Path
from oep_client import acquire, core, link
from wireskein import capture as wc
from wireskein.sources import oep as wso

OUT = Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)
PROBE, LINK = "oep://80f1b2d80a34?via=usb", "wchlink://FB868F068059"
CH32RV = "/home/mt/dev_wch/ch32rv/target/release/ch32rv"
RATE = 80_000_000
RUNS = [
    ("11_info_low", ["target", "info", "--speed", "low"]),
    ("12_info_medium", ["target", "info", "--speed", "medium"]),
    ("13_info_high", ["target", "info", "--speed", "high"]),
    ("14_option_get", ["target", "option", "get", "--speed", "high"]),
    ("15_read_option", ["read", "--region", "option", "-o", "/dev/null", "--speed", "high"]),
    ("16_read_ram", ["read", "--region", "ram+0+20480", "-o", "/dev/null", "--speed", "high"]),
    ("17_read_code", ["read", "--region", "code", "-o", "/dev/null", "--speed", "high"]),
    ("18_dbg_halt", ["dbg", "halt", "--speed", "medium"]),
    ("19_dbg_regs", ["dbg", "regs", "--speed", "medium"]),
    ("20_dbg_resume", ["dbg", "resume", "--speed", "medium"]),
    ("21_dbg_resume_low", ["dbg", "resume", "--speed", "low"]),
]
ONLY = sys.argv[2].split(",") if len(sys.argv) > 2 else None
RUNS = [r for r in RUNS if ONLY is None or r[0] in ONLY]
hst = link.open_host(PROBE)
core.take(hst, 60_000, owner="e173 linke tap")
try:
    for name, args in RUNS:
        cmd = [CH32RV, *args, "--probe", LINK, "--non-interactive", "--progress", "none"]
        got = {}

        def steps(wait, cmd=cmd, got=got):
            wait(0.05)
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
            got.update(rc=r.returncode, out=(r.stdout + r.stderr)[-4000:])
            wait(0.05)
        res = acquire.capture(hst, acquire.Request([("swclk", "GPIO44"), ("swdio", "GPIO45")], RATE, 0, mode="stream",
                                                   during=steps, on_loss="continue"))
        wc.write(OUT / f"{name}.wireskein", wso.capture_of(res))
        (OUT / f"{name}.json").write_text(json.dumps({
            "command": " ".join(cmd[1:]), "rc": got["rc"], "output": got["out"], "rate_hz": RATE,
            "channels": {"swclk": "GPIO44", "swdio": "GPIO45"}, "probe": PROBE,
            "firmware": res.meta["probe"].get("firmware"), "complete": res.complete, "lost": res.lost}, indent=1))
        print(name, "rc", got["rc"], "complete", res.complete, "lost", len(res.lost or []))
finally:
    hst.end(); hst.link.close()
