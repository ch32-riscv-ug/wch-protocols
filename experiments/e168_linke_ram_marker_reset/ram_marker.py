"""E168: did a LinkE operation cut the target's power or reset it? Judge by RAM contents, the app's loop counter and
DMSTATUS havereset (RAM survives a reset, not a power loss).

usage: ram_marker.py <LinkE serial> <op> <out prefix>
  op: none | power3v3 | power5v | special_erase
Target: clockwatch (E162) running at the core default clock; cw_seq at 0x2000000c. Marker: 256 B at 0x20003000.
"""
import json, os, random, subprocess, sys, time
import usb.core, usb.util

CH = "/home/mt/dev_wch/ch32rv/target/release/ch32rv"
sn, op, out = sys.argv[1:4]
MARK, SEQ = 0x20003000, 0x2000000C
res = {"op": op}


def ch(args):
    p = subprocess.run([CH, "--probe", f"serial:{sn}", "--non-interactive", *args], capture_output=True, text=True)
    return p


def read_bytes(addr, n):
    f = f"{out}.tmp.bin"
    p = ch(["read", "--range", f"{addr:#x}+{n}", "-o", f, "--format", "bin"])
    if p.returncode:
        return None
    b = open(f, "rb").read(); os.remove(f); return b


random.seed(0xE168)
marker = bytes(random.randrange(256) for _ in range(256))
open(f"{out}.marker.bin", "wb").write(marker)
w = ch(["write", "--at", f"{MARK:#x}", f"{out}.marker.bin"])
res["write_rc"] = w.returncode
chk = read_bytes(MARK, 256); res["marker_before_ok"] = chk == marker
s0 = read_bytes(SEQ, 4); res["seq_before"] = int.from_bytes(s0, "little") if s0 else None

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


def dmi(addr, data=0, op_=1):
    r = x(f"810806{addr:02x}{data:08x}{op_:02x}")
    return int(r[8:16], 16) if r.startswith("820806") else None


def rd32(a):
    dmi(0x05, a, 2); dmi(0x17, 0x02200000, 2); return dmi(0x04)


try:
    x("810d0101"); x("810c020d03")
    if op == "power3v3":
        x("810d010a"); time.sleep(1.0); x("810d0109")
    elif op == "power5v":
        x("810d010c"); time.sleep(1.0); x("810d010b")
    elif op == "special_erase":
        res["special_erase"] = x("810d020f0d", 10000)
    else:
        time.sleep(1.0)
    time.sleep(0.3)
    res["attach"] = x("810d0102")
    st = dmi(0x11); res["dmstatus_after"] = f"{st:08x}" if st is not None else None
    res["havereset"] = bool(st is not None and st >> 18 & 3)
    v = rd32(SEQ); res["seq_after_in_session"] = v
    m0 = rd32(MARK); res["marker_word0_in_session"] = f"{m0:08x}" if m0 is not None else None
    x("810d0103"); x("810d01ff")
finally:
    usb.util.release_interface(d, 0); usb.util.dispose_resources(d)

after = read_bytes(MARK, 256)
res["marker_after_ok"] = after == marker if after else None
res["marker_after_bytes_equal"] = sum(a == b for a, b in zip(after, marker)) if after else None
s1 = read_bytes(SEQ, 4); res["seq_after"] = int.from_bytes(s1, "little") if s1 else None
res["usb"] = log
json.dump(res, open(f"{out}.json", "w"), indent=1)
print(json.dumps({k: v for k, v in res.items() if k != "usb"}))
