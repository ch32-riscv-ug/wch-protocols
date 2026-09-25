"""E164: flash the HPRE /2 clockwatch on the X035C8T6, then watch the UART before and after each attach.

usage: sequence.py <out prefix> <speed of the attaches> [n attaches] [image]
"""
import json, subprocess, sys, time
import serial

CH = "/home/mt/dev_wch/ch32rv/target/release/ch32rv"
SN = "FC928F068181"
TTY = "/dev/serial/by-id/usb-wch.cn_WCH-Link_FC928F068181-if01"
IMG = "/home/mt/dev_wch/wch-protocols/experiments/e162_linke_attach_clock_uart/clockwatch/clockwatch-CH32X035-ahb-div2.bin"
out, speed = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 2
if len(sys.argv) > 4:
    IMG = sys.argv[4]
log = []


def ch(args, tag):
    t = time.time()
    p = subprocess.run([CH, "--probe", f"serial:{SN}", "--non-interactive", *args], capture_output=True, text=True)
    log.append({"tag": tag, "args": args, "rc": p.returncode, "t0": t, "t1": time.time(),
                "stdout": p.stdout[-1500:], "stderr": p.stderr[-800:]})
    print(f"[{tag}] rc={p.returncode} " + " | ".join(l for l in (p.stdout + p.stderr).splitlines()
                                                     if any(k in l for k in ("uid", "flash:", "error", "warning", "verify"))))


def uart(secs, tag):
    s = serial.Serial(TTY, 115200, timeout=0.05); s.reset_input_buffer()
    t = time.time(); b = b""
    while time.time() - t < secs:
        b += s.read(4096)
    s.close()
    lines = [l for l in b.decode("latin-1").splitlines() if l.startswith("CW ")]
    log.append({"tag": tag, "bytes": len(b), "last": b[-200:].decode("latin-1")})
    print(f"[{tag}] {len(b)} bytes, {len(lines)} CW lines; last: {lines[-1] if lines else repr(b[-60:])}")


ch(["--speed", "low", "flash", IMG, "--reset", "run"], "flash")
uart(2.0, "uart after flash, before any attach")
for i in range(n):
    ch(["--speed", speed, "--capture", f"{out}_attach{i}.ndjson", "target", "info"], f"attach {i} ({speed})")
    uart(2.0, f"uart after attach {i}")
json.dump(log, open(f"{out}.json", "w"), indent=1)
