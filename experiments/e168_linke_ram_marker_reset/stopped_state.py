"""E168 (b): the same RAM-marker judgement, but with the X035 first put into the E164 stopped state.

usage: stopped_state.py <LinkE serial> <control|stopped> <out prefix>
  control: write marker -> flash the default clockwatch (ends with a reset) -> read marker   (does flashing keep it?)
  stopped: write marker -> flash the HPRE 1001 clockwatch -> `target info` (stops it) -> special erase ->
           AttachChip + DMSTATUS + marker word in the same USB session -> read the whole marker
"""
import json, os, random, subprocess, sys, time
import usb.core, usb.util

CH = "/home/mt/dev_wch/ch32rv/target/release/ch32rv"
HERE = os.path.dirname(os.path.abspath(__file__))
IMG_OK = os.path.join(HERE, "../e162_linke_attach_clock_uart/clockwatch/clockwatch-CH32X035.bin")
IMG_STOP = os.path.join(HERE, "../e164_x035_attach_hclk_div2/clockwatch-CH32X035-hpre9-div4.bin")
sn, mode, out = sys.argv[1:4]
MARK = 0x20003000
res = {"mode": mode}


def ch(args):
    p = subprocess.run([CH, "--probe", f"serial:{sn}", "--non-interactive", *args], capture_output=True, text=True)
    return p


def read_bytes(addr, n):
    f = f"{out}.tmp.bin"
    p = ch(["read", "--range", f"{addr:#x}+{n}", "-o", f, "--format", "bin"])
    if p.returncode:
        return None
    b = open(f, "rb").read(); os.remove(f); return b


random.seed(0xE168B)
marker = bytes(random.randrange(256) for _ in range(256))
open(f"{out}.marker.bin", "wb").write(marker)
res["write_rc"] = ch(["write", "--at", f"{MARK:#x}", f"{out}.marker.bin"]).returncode
res["marker_before_ok"] = read_bytes(MARK, 256) == marker

if mode == "control":
    res["flash_rc"] = ch(["flash", IMG_OK, "--reset", "run"]).returncode
    time.sleep(0.5)
    after = read_bytes(MARK, 256)
    res["marker_after_equal_bytes"] = sum(a == b for a, b in zip(after, marker)) if after else None
else:
    res["flash_rc"] = ch(["flash", IMG_STOP, "--reset", "run"]).returncode
    time.sleep(0.5)
    ti = ch(["target", "info"])
    res["stop_target_info"] = [l for l in (ti.stdout + ti.stderr).splitlines() if "uid" in l or "error" in l]
    d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=sn)
    d.get_active_configuration(); usb.util.claim_interface(d, 0)
    log = []; t0 = time.monotonic()

    def x(h, to=6000):
        t = time.monotonic(); d.write(0x01, bytes.fromhex(h), 1000)
        try:
            r = d.read(0x81, 64, to).tobytes().hex()
        except usb.core.USBTimeoutError:
            r = "TIMEOUT"
        log.append({"t_ms": round((t - t0) * 1e3, 1), "out": h, "in": r, "ms": round((time.monotonic() - t) * 1e3, 1)})
        return r

    def dmi(addr, data=0, op=1):
        r = x(f"810806{addr:02x}{data:08x}{op:02x}")
        return int(r[8:16], 16) if r.startswith("820806") else None

    try:
        x("810d0101"); x("810c020d03")
        tries = []
        for i in range(4):                     # repeat until the erase answers 0f (E164)
            r = x("810d020f0d", 10000); tries.append(r)
            if r.startswith("820d010f"):
                break
        res["special_erase_tries"] = tries
        res["attach"] = x("810d0102")
        st = dmi(0x11); res["dmstatus_after"] = f"{st:08x}" if st is not None else None
        res["havereset"] = bool(st is not None and st >> 18 & 3)
        dmi(0x05, MARK, 2); dmi(0x17, 0x02200000, 2); cs = dmi(0x16); m0 = dmi(0x04)
        res["abstractcs"] = f"{cs:08x}" if cs is not None else None
        res["marker_word0_in_session"] = f"{m0:08x}" if m0 is not None else None
        res["marker_word0_expected"] = marker[:4][::-1].hex()
        x("810d0103"); x("810d01ff")
    finally:
        usb.util.release_interface(d, 0); usb.util.dispose_resources(d)
    res["usb"] = log
    after = read_bytes(MARK, 256)
    res["marker_after_equal_bytes"] = sum(a == b for a, b in zip(after, marker)) if after else None
json.dump(res, open(f"{out}.json", "w"), indent=1)
print(json.dumps({k: v for k, v in res.items() if k != "usb"}))
