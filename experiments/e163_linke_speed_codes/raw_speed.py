#!/usr/bin/env -S uv run --no-project --with pyusb==1.3.1 python
"""Stand-in for ch32rv under linke_cap.py (CH32RV=<this file>): send raw WCH-Link commands for one speed experiment.

    raw_speed.py --probe serial:<sn> <pre_code|-> <post_family:code|-> [--capture <ignored>]

pre_code : SetSpeed `81 0c 02 01 <code>` before AttachChip ('-' = none)
post     : SetSpeed `81 0c 02 <family> <code>` after AttachChip ('-' = none)
Then: 20 x DmiOp read DMSTATUS, 4 x DmiOp write DATA0, 4 x abstract memory read of 0x08000000.. (data1/command/data0),
DetachChip. Every command and answer is printed as JSON lines on stdout."""
import json, sys, time, usb.core, usb.util

a = sys.argv[1:]
sn = a[a.index("--probe") + 1].split(":", 1)[1]
rest = [x for i, x in enumerate(a) if not x.startswith("-") and (i == 0 or a[i - 1] not in ("--probe", "--capture"))] \
    + [x for x in a if x == "-"]
pos = [x for x in a[a.index("--probe") + 2:] if x != "--capture"]
pre, post = pos[0], pos[1]

d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=sn)
d.get_active_configuration(); usb.util.claim_interface(d, 0)
t0 = time.monotonic()


def x(h, to=5000):
    t = time.monotonic(); d.write(0x01, bytes.fromhex(h), 1000)
    try:
        r = d.read(0x81, 64, to).tobytes().hex()
    except usb.core.USBTimeoutError:
        r = "TIMEOUT"
    print(json.dumps({"t_ms": round((t - t0) * 1e3, 3), "out": h, "in": r, "ms": round((time.monotonic() - t) * 1e3, 3)}))
    return r


def dmi(addr, data=0, op=1):
    return x(f"810806{addr:02x}{data:08x}{op:02x}")


try:
    x("810d01ff"); x("810d0101")
    if pre != "-":
        x(f"810c0201{int(pre, 16):02x}")
    x("810d0102")
    if post != "-":
        fam, code = post.split(":")
        x(f"810c02{int(fam, 16):02x}{int(code, 16):02x}")
    time.sleep(0.002)
    for _ in range(20):
        dmi(0x11)
    for i in range(4):
        dmi(0x04, 0x11111111 * (i + 1), 2)
    for i in range(4):
        dmi(0x05, 0x08000000 + 4 * i, 2); dmi(0x17, 0x02200000, 2); dmi(0x04)
finally:
    try:
        x("810d01ff")
    finally:
        usb.util.release_interface(d, 0); usb.util.dispose_resources(d)
