"""E169: which WCH-LinkE command resets a stopped X035 (E164 state)?  Current wiring: 3V3/SWDIO/SWCLK/TX/RX/GND, no RST.

usage: trigger.py <LinkE serial> <treatment> <out prefix> [wait_s]
  treatment: none | attach5 | redetect5 | erase1 | waiterase  (waiterase: wait wait_s, then one special erase)
Each run: flash the HPRE 1001 clockwatch -> `target info` (stops it) -> treatment (one USB session) ->
judge (new USB session: AttachChip, DMSTATUS, RCC_CFGR0 / FLASH_ACTLR by abstract memory read) ->
recover (special erase until 82 0d 01 0f, one USB session each) -> check `target info`.
"""
import json, os, subprocess, sys, time
import usb.core, usb.util

CH = "/home/mt/dev_wch/ch32rv/target/release/ch32rv"
HERE = os.path.dirname(os.path.abspath(__file__))
IMG_STOP = os.path.join(HERE, "../e164_x035_attach_hclk_div2/clockwatch-CH32X035-hpre9-div4.bin")
sn, treat, out = sys.argv[1:4]
wait_s = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
res = {"treatment": treat, "wait_s": wait_s, "sessions": []}


def ch(args):
    p = subprocess.run([CH, "--probe", f"serial:{sn}", "--non-interactive", *args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


class Session:
    def __init__(self, tag):
        self.tag, self.log, self.t0 = tag, [], time.monotonic()
        self.d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=sn)
        self.d.get_active_configuration(); usb.util.claim_interface(self.d, 0)

    def x(self, h, to=6000):
        t = time.monotonic(); self.d.write(0x01, bytes.fromhex(h), 1000)
        try:
            r = self.d.read(0x81, 64, to).tobytes().hex()
        except usb.core.USBTimeoutError:
            r = "TIMEOUT"
        self.log.append({"t_ms": round((t - self.t0) * 1e3, 1), "out": h, "in": r, "ms": round((time.monotonic() - t) * 1e3, 1)})
        return r

    def dmi(self, addr, data=0, op=1):
        r = self.x(f"810806{addr:02x}{data:08x}{op:02x}")
        return (int(r[8:16], 16), int(r[16:18], 16)) if r.startswith("820806") else (None, None)

    def rd32(self, a):
        self.dmi(0x05, a, 2); self.dmi(0x17, 0x02200000, 2)
        cs, _ = self.dmi(0x16); v, st = self.dmi(0x04)
        if cs is not None and (cs >> 8) & 7:
            self.dmi(0x16, 0x700, 2)
        return v, cs

    def close(self):
        usb.util.release_interface(self.d, 0); usb.util.dispose_resources(self.d)
        res["sessions"].append({"tag": self.tag, "usb": self.log})


rc, txt = ch(["flash", IMG_STOP, "--reset", "run"]); res["flash_rc"] = rc
time.sleep(0.5)
rc, txt = ch(["target", "info"]); res["stop_uid"] = next((l.split()[-1] for l in txt.splitlines() if l.startswith("uid:")), None)
t_stop = time.monotonic()

s = Session("treatment")
try:
    s.x("810d0101"); s.x("810c020d03")
    if treat == "attach5":
        res["treat_answers"] = [s.x("810d0102") for _ in range(5)]
    elif treat == "redetect5":
        res["treat_answers"] = [(s.x("810d0103"), s.x("810d01ff")) for _ in range(5)]
    elif treat == "erase1":
        res["treat_answers"] = [s.x("810d020f0d", 10000)]
    elif treat == "waiterase":
        time.sleep(max(0.0, wait_s - (time.monotonic() - t_stop)))
        res["treat_answers"] = [s.x("810d020f0d", 10000)]
    else:
        time.sleep(3.0)
finally:
    s.close()

s = Session("judge")
try:
    s.x("810d0101"); s.x("810c020d03")
    res["judge_attach"] = s.x("810d0102")
    st, _ = s.dmi(0x11); res["judge_dmstatus"] = f"{st:08x}" if st is not None else None
    res["judge_havereset"] = bool(st is not None and st >> 18 & 3)
    v, cs = s.rd32(0x40021004); res["judge_cfgr0"] = f"{v:08x}" if v is not None else None
    res["judge_cfgr0_abstractcs"] = f"{cs:08x}" if cs is not None else None
    v, cs = s.rd32(0x40022000); res["judge_actlr"] = f"{v:08x}" if v is not None else None
    s.x("810d0103"); s.x("810d01ff")
finally:
    s.close()

tries = []
for i in range(12):
    s = Session(f"recover{i}")
    try:
        s.x("810d0101"); s.x("810c020d03")
        r = s.x("810d020f0d", 10000); tries.append(r[:8])
        s.x("810d0103"); s.x("810d01ff")
    finally:
        s.close()
    if r.startswith("820d010f"):
        break
res["recover_tries"] = tries
rc, txt = ch(["target", "info"]); res["after_uid"] = next((l.split()[-1] for l in txt.splitlines() if l.startswith("uid:")), None)
json.dump(res, open(f"{out}.json", "w"), indent=1)
print(json.dumps({k: v for k, v in res.items() if k != "sessions"}))
