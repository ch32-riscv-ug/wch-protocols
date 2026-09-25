"""E166: can the host reproduce the LinkE special-erase window by itself?

(A) power:  watch the target UART (clockwatch) across `81 0d 01 0a` (3V3 off) / `81 0d 01 09` (3V3 on).
(B) dmiop:  after 3V3 off/on, send DmiOp haltreq / DMSTATUS reads without a successful AttachChip, and see whether the
            hart halts before the app runs (then read RCC_CFGR0 with an abstract memory read).

usage: power_window.py power <LinkE serial> [off_s]
       power_window.py dmiop <LinkE serial> [off_s] [--attach-first]
"""
import json, sys, threading, time
import serial, usb.core, usb.util

mode, sn = sys.argv[1], sys.argv[2]
off_s = float(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith("-") else 0.15
tty = f"/dev/serial/by-id/usb-wch.cn_WCH-Link_{sn}-if01"
d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=sn)
d.get_active_configuration(); usb.util.claim_interface(d, 0)
t0 = time.monotonic(); log = []


def x(h, to=3000):
    t = time.monotonic(); d.write(0x01, bytes.fromhex(h), 1000)
    try:
        r = d.read(0x81, 64, to).tobytes().hex()
    except usb.core.USBTimeoutError:
        r = "TIMEOUT"
    log.append({"t_ms": round((t - t0) * 1e3, 2), "out": h, "in": r})
    return r


def dmi(addr, data=0, op=1):
    r = x(f"810806{addr:02x}{data:08x}{op:02x}")
    return (int(r[8:16], 16), int(r[16:18], 16)) if r.startswith("820806") else (None, r)


try:
    if mode == "power":
        ser = serial.Serial(tty, 115200, timeout=0.02); ser.reset_input_buffer()
        chunks = []; stop = False

        def rd():
            while not stop:
                b = ser.read(4096)
                if b:
                    chunks.append((time.monotonic() - t0, b))
        th = threading.Thread(target=rd); th.start()
        time.sleep(1.0); x("810d010a"); t_off = time.monotonic() - t0
        time.sleep(off_s + 1.0); x("810d0109"); t_on = time.monotonic() - t0
        time.sleep(2.0); stop = True; th.join(); ser.close()
        def seqs(a, b):
            txt = b"".join(c for t, c in chunks if a <= t < b).decode("latin-1")
            return [int(l.split()[1]) for l in txt.splitlines() if l.startswith("CW ") and l.split()[1].isdigit()]
        before, during, after = seqs(0, t_off), seqs(t_off + 0.1, t_on), seqs(t_on, 99)
        print(json.dumps({"t_off": round(t_off, 3), "t_on": round(t_on, 3),
                          "CW lines before/during/after": [len(before), len(during), len(after)],
                          "last seq before": before[-1:], "seq during": during[:3], "first seq after": after[:3]}))
    else:
        x("810d0101"); x("810c020d03")
        if "--attach-first" in sys.argv:
            x("810d0102")
        x("810d010a"); time.sleep(off_s); x("810d0109")
        halted_at = None
        for i in range(80):
            dmi(0x10, 0x80000001, 2)
            st, s2 = dmi(0x11)
            if st is not None and (st >> 9) & 1 and halted_at is None:
                halted_at = i
                break
        res = {"halted_after_iterations": halted_at, "last_dmstatus": log[-1]["in"]}
        if halted_at is not None:
            dmi(0x05, 0x40021004, 2); dmi(0x17, 0x02200000, 2); cs, _ = dmi(0x16); v, _ = dmi(0x04)
            res["abstractcs"] = f"{cs:08x}" if cs is not None else None
            res["CFGR0"] = f"{v:08x}" if v is not None else None
            dmi(0x05, 0x40022000, 2); dmi(0x17, 0x02200000, 2); v2, _ = dmi(0x04)
            res["ACTLR"] = f"{v2:08x}" if v2 is not None else None
        print(json.dumps(res))
finally:
    try:
        x("810d0109"); x("810d01ff")
    finally:
        usb.util.release_interface(d, 0); usb.util.dispose_resources(d)
        json.dump(log, open(f"out/{mode}_{int(time.time())}.json", "w"), indent=0)
