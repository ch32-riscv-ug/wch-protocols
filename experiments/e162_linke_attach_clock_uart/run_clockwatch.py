"""Flash clockwatch, watch the target's own UART report across a LinkE attach, then read its RAM log.

usage: run_clockwatch.py <name> <LinkE serial> <tty> <image.bin> <outdir>
"""
import json, struct, subprocess, sys, threading, time
from pathlib import Path
import serial

CH = "/home/mt/dev_wch/ch32rv/target/release/ch32rv"
name, sn, tty, image, outdir = sys.argv[1:6]
out = Path(outdir); out.mkdir(parents=True, exist_ok=True)
log = {"name": name, "probe": sn, "tty": tty, "image": image, "steps": []}


def run(args, tag):
    t = time.time()
    p = subprocess.run([CH, "--probe", f"serial:{sn}", "--non-interactive", *args], capture_output=True, text=True)
    log["steps"].append({"tag": tag, "args": args, "rc": p.returncode, "t0": t, "t1": time.time(),
                         "stdout": p.stdout[-2000:], "stderr": p.stderr[-2000:]})
    return p


r = run(["flash", image, "--reset", "run"], "flash")
if r.returncode:
    print("flash failed", r.stderr); json.dump(log, open(out / f"{name}.json", "w"), indent=1); sys.exit(1)
time.sleep(0.5)

ser = serial.Serial(tty, 115200, timeout=0.05)
ser.reset_input_buffer()
chunks = []
stop = False


def reader():
    while not stop:
        b = ser.read(4096)
        if b:
            chunks.append((time.time(), b))


th = threading.Thread(target=reader); th.start()
time.sleep(1.5)
run(["target", "info"], "attach(target info)")
time.sleep(2.0)
stop = True; th.join(); ser.close()

raw = b"".join(b for _, b in chunks)
(out / f"{name}.uart.bin").write_bytes(raw)
attach = next(s for s in log["steps"] if s["tag"].startswith("attach"))
before = b"".join(b for t, b in chunks if t < attach["t0"])
after = b"".join(b for t, b in chunks if t > attach["t1"] + 0.2)
log["uart_before_tail"] = before[-400:].decode("latin-1")
log["uart_after_tail"] = after[-400:].decode("latin-1")

# RAM log: cw_seq@0x2000000c, cw_count@0x10, cw_magic@0x14, cw_log@0x30 (32 x 5 words)
r = run(["read", "--range", "0x2000000c+0x2a4", "-o", str(out / f"{name}.ram.bin"), "--format", "bin"], "read ram log")
d = (out / f"{name}.ram.bin").read_bytes() if r.returncode == 0 else b""
if len(d) >= 0x2a4:
    seq, count, magic = struct.unpack_from("<3I", d, 0)
    ent = [struct.unpack_from("<5I", d, 0x24 + 20 * i) for i in range(32)]
    order = [ent[i % 32] for i in range(max(0, count - 32), count)]
    log["ram"] = {"seq": seq, "count": count, "magic": hex(magic),
                  "entries": [dict(seq=e[0], ctlr=hex(e[1]), cfgr0=hex(e[2]), actlr=hex(e[3]), cfgr2=hex(e[4])) for e in order]}
json.dump(log, open(out / f"{name}.json", "w"), indent=1)
print(json.dumps({k: log[k] for k in ("uart_before_tail", "uart_after_tail", "ram") if k in log}, indent=1)[:4000])
