"""Rewrite the X035 USER option byte (RST_MODE back to 11b) through raw WCH-Link DMI, inside the window that opens
after the LinkE special erase (`81 0d 02 0f 0d`). Procedure: protocols/pc-to-link.ja.md §6b.

usage: x035_fix_user.py <LinkE serial> [--write] [--fixed]   (without --write it only reads, twice, and prints the plan;
       --fixed programs the values recorded before an interrupted run instead of the ones read now)
"""
import sys, time, usb.core, usb.util

sn = sys.argv[1]; WRITE = "--write" in sys.argv
OB = 0x1FFFF800
KEYR, OBKEYR, STATR, CTLR, OBR = 0x40022004, 0x40022008, 0x4002200C, 0x40022010, 0x4002201C
K1, K2 = 0x45670123, 0xCDEF89AB
OPTPG, OPTER, STRT, OPTWRE = 1 << 4, 1 << 5, 1 << 6, 1 << 9

d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=sn)
d.get_active_configuration(); usb.util.claim_interface(d, 0)


def x(h, to=5000):
    d.write(0x01, bytes.fromhex(h), 1000)
    return d.read(0x81, 64, to).tobytes().hex()


def dmi(addr, data=0, op=1):
    r = x(f"810806{addr:02x}{data:08x}{op:02x}")
    assert r.startswith("820806"), r
    return int(r[8:16], 16), int(r[16:18], 16)


def cs_check(tag):
    for _ in range(400):                       # busy (bit 12) while the flash stalls the bus: wait, not an error
        cs, _ = dmi(0x16)
        if not (cs >> 12) & 1:
            break
        time.sleep(0.002)
    err = (cs >> 8) & 7
    if err or (cs >> 12) & 1:
        dmi(0x16, 0x700, 2)
        raise RuntimeError(f"{tag}: abstractcs {cs:08x} (cmderr {err})")


def rd32(a):
    dmi(0x05, a, 2); dmi(0x17, 0x02200000, 2); cs_check(f"read {a:08x}")
    return dmi(0x04)[0]


def wr32(a, v):
    dmi(0x05, a, 2); dmi(0x04, v, 2); dmi(0x17, 0x02210000, 2); cs_check(f"write {a:08x}")


def wr16(a, v):
    dmi(0x05, a, 2); dmi(0x04, v & 0xFFFF, 2); dmi(0x17, 0x02110000, 2); cs_check(f"write16 {a:08x}")


def busy_wait(tag):
    for _ in range(200):
        s = rd32(STATR)
        if not s & 1:
            if s & 0x10:
                raise RuntimeError(f"{tag}: WRPRTERR (STATR {s:08x})")
            return s
        time.sleep(0.005)
    raise RuntimeError(f"{tag}: still busy")


try:
    print("probe", x("810d0101"), "speed", x("810c020d03"))
    print("special erase", x("810d020f0d", 10000))
    r = x("810d0102"); print("attach", r)
    assert r.startswith("820d05"), "attach failed"
    st, _ = dmi(0x11); print(f"dmstatus {st:08x}")
    if not (st >> 9) & 1:            # allhalted
        dmi(0x10, 0x80000001, 2); time.sleep(0.01); st, _ = dmi(0x11); print(f"after haltreq dmstatus {st:08x}")
    a = [rd32(OB + 4 * i) for i in range(4)]
    b = [rd32(OB + 4 * i) for i in range(4)]
    print("option words #1", " ".join(f"{w:08x}" for w in a)); print("option words #2", " ".join(f"{w:08x}" for w in b))
    assert a == b, "option reads differ; link not stable"
    raw = b"".join(w.to_bytes(4, "little") for w in a)
    pairs = [(raw[2 * i], raw[2 * i + 1]) for i in range(8)]
    names = ["RDPR", "USER", "DATA0", "DATA1", "WRPR0", "WRPR1", "WRPR2", "WRPR3"]
    if "--fixed" in sys.argv:
        # values read on 2026-09-25 before the interrupted run, with RST_MODE = 11b
        new = [0xA5, 0x1F, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF]; user = 0x07
    else:
        assert all(v ^ n == 0xFF for v, n in pairs), f"complement mismatch {pairs}"
        new = [v for v, _ in pairs]
        user = new[1]
        new[1] = (user & ~0x18) | 0x18   # RST_MODE [4:3] = 11b (RM reset value; PA21 back to GPIO)
    print("current", {n: f"{v:02x}" for n, (v, _) in zip(names, pairs)})
    print(f"plan: USER {user:02x} -> {new[1]:02x} (RST_MODE {(user >> 3) & 3:02b} -> 11), others unchanged")
    print(f"OBR {rd32(OBR):08x}  CTLR {rd32(CTLR):08x}  STATR {rd32(STATR):08x}")
    if WRITE:
        wr32(KEYR, K1); wr32(KEYR, K2); wr32(OBKEYR, K1); wr32(OBKEYR, K2)
        c = rd32(CTLR); print(f"CTLR after unlock {c:08x}")
        if not c & OPTWRE:
            raise RuntimeError("OPTWRE not set; abort before erase")
        cur = [v | (n << 8) for v, n in pairs]
        if "--no-erase" in sys.argv:
            # only halfwords that are still erased (0xffff) and differ from the target get programmed
            todo = [i for i, v in enumerate(new) if cur[i] == 0xFFFF and (v | ((v ^ 0xFF) << 8)) != 0xFFFF]
            bad = [names[i] for i, v in enumerate(new) if cur[i] not in (0xFFFF, v | ((v ^ 0xFF) << 8))]
            if bad:
                raise RuntimeError(f"halfwords neither erased nor at target: {bad}; would need an erase")
        else:
            wr32(CTLR, OPTER | OPTWRE); wr32(CTLR, OPTER | OPTWRE | STRT); print("erase", f"{busy_wait('option erase'):08x}")
            todo = list(range(8))
        print("programming", [names[i] for i in todo])
        for i in todo:
            v = new[i]
            wr32(CTLR, OPTPG | OPTWRE); wr32(CTLR, OPTPG | OPTWRE | STRT)
            wr16(OB + 2 * i, v | ((v ^ 0xFF) << 8)); busy_wait(f"program {names[i]}")
        wr32(CTLR, 0)
        after = [rd32(OB + 4 * i) for i in range(4)]
        print("option words after", " ".join(f"{w:08x}" for w in after))
        print("system reset (PFIC SYSRST)"); dmi(0x05, 0xE000E048, 2); dmi(0x04, 0xBEEF0080, 2); dmi(0x17, 0x02210000, 2)
finally:
    try:
        print("redetect", x("810d0103"), "detach", x("810d01ff"))
    finally:
        usb.util.release_interface(d, 0); usb.util.dispose_resources(d)
