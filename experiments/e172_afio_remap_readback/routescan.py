"""Every route of a series, on one chip: does the remap value stick, and does each pin's GPIO config stick?"""
import csv, collections, sys, json, fcntl, re
sys.argv, ARGS = ["x"], sys.argv[1:]
exec(open("/home/mt/dev_wch/ArduinoCore-CH32RV/work/remapexp/regprobe.py").read().split("def main():")[0])
series, chip, probe = ARGS[:3]
DD = "/home/mt/dev_wch/ch32-device-data/index/"
PCFR = {"PCFR1": 0x0C if series in ("CH32V002", "CH32V004", "CH32V005", "CH32V006", "CH32V007", "CH32M007") else 0x04,
        "PCFR2": 0x1C}
DEBUG = {"PD1", "PA13", "PA14", "PC18", "PC19"}
ROLES = {"TX", "RX", "SCK", "MISO", "MOSI", "SCL", "SDA"}
sel = {}
for r in csv.DictReader(open(DD + "routes.csv")):
    if r["series"] == series and r["controller"] == "afio" and re.match(r"(usart|uart|spi|i2c)", r["peripheral"].lower()):
        sel[r["peripheral"].upper()] = [(reg, int(b)) for reg, b in (x.split(":") for x in r["bits"].split(";"))]
pins = collections.defaultdict(set)          # (peripheral, value) -> (pad, role) (union over parts)
for r in csv.DictReader(open(DD + "pinout.csv")):
    if r["series"] != series or r["role"] not in ROLES: continue
    p = r["peripheral"].upper()
    if p not in sel: continue
    m = re.match(r"(default|main)$|remap-(\d+)$", r["route"])
    if not m: continue
    pins[(p, 0 if m.group(1) else int(m.group(2)))].add((r["pad"], r["role"]))
t = T(probe, chip)
AFIO = 0x40010000
def port_base(pad): return 0x40010800 + 0x400 * (ord(pad[1]) - ord("A"))
out = []
with open(LOCK, "w") as lk:
    fcntl.flock(lk, fcntl.LOCK_EX)
    t.wr(0x40021018, t.rd(0x40021018) | 0x7D)              # AFIO + GPIOA..E clocks
    for (p, v) in sorted(pins):
        bits = sel[p]
        regs = {reg for reg, _ in bits}
        save = {reg: t.rd(AFIO + PCFR[reg]) for reg in regs}
        want = {reg: save[reg] for reg in regs}
        for i, (reg, b) in enumerate(bits):
            want[reg] = (want[reg] & ~(1 << b)) | (((v >> i) & 1) << b)
        for reg in regs: t.wr(AFIO + PCFR[reg], want[reg])
        got = {reg: t.rd(AFIO + PCFR[reg]) for reg in regs}
        remap_ok = all((got[reg] ^ want[reg]) & sum(1 << b for rr, b in bits if rr == reg) == 0 for reg in regs)
        for reg in regs: t.wr(AFIO + PCFR[reg], save[reg])
        pin_res = {}
        for pad, role in sorted(pins[(p, v)]):
            want_cfg = 0x8 if role in ("RX", "MISO") else (0xF if role in ("SCL", "SDA") else 0xB)
            if pad in DEBUG or not re.match(r"P[A-E]\d+$", pad):
                pin_res[pad] = "skip"; continue
            n = int(pad[2:]); cfg = port_base(pad) + (0x04 if n >= 8 else 0); sh = (n % 8) * 4
            o = t.rd(cfg); t.wr(cfg, (o & ~(0xF << sh)) | (want_cfg << sh)); g = (t.rd(cfg) >> sh) & 0xF; t.wr(cfg, o)
            ok = (g & 0xC) == (want_cfg & 0xC) and bool(g & 3) == bool(want_cfg & 3)
            pin_res[f"{pad}({role})"] = "ok" if ok else f"NG({g:x})"
        out.append({"periph": p, "value": v, "remap_ok": remap_ok, "pins": pin_res})
        print(f"{p:7s} value={v}  remap={'ok' if remap_ok else 'NG'}  " + " ".join(f"{k}:{x}" for k, x in pin_res.items()), flush=True)
json.dump(out, open(f"/home/mt/dev_wch/ArduinoCore-CH32RV/work/remapexp/scan-{chip}.json", "w"), indent=1)
