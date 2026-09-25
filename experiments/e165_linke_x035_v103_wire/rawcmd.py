#!/usr/bin/env -S uv run --no-project --with pyusb==1.3.1 python
"""Stand-in for ch32rv under linke_cap.py (CH32RV=<this file>): send raw WCH-Link commands in one USB session.

    rawcmd.py --probe serial:<sn> <hex> ... [sleep:<s>] [--capture <ignored>]

Prints one JSON line per command (time, out, in, duration)."""
import json, sys, time, usb.core, usb.util

a = sys.argv[1:]
sn = a[a.index("--probe") + 1].split(":", 1)[1]
cmds, skip = [], False
for i, x in enumerate(a):
    if skip:
        skip = False; continue
    if x in ("--probe", "--capture"):
        skip = True; continue
    cmds.append(x)
d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=sn)
d.get_active_configuration(); usb.util.claim_interface(d, 0)
t0 = time.monotonic()
try:
    for h in cmds:
        if h.startswith("sleep:"):
            time.sleep(float(h[6:])); continue
        t = time.monotonic(); d.write(0x01, bytes.fromhex(h), 1000)
        try:
            r = d.read(0x81, 64, 5000).tobytes().hex()
        except usb.core.USBTimeoutError:
            r = "TIMEOUT"
        print(json.dumps({"t_ms": round((t - t0) * 1e3, 3), "out": h, "in": r, "ms": round((time.monotonic() - t) * 1e3, 3)}))
finally:
    usb.util.release_interface(d, 0); usb.util.dispose_resources(d)
