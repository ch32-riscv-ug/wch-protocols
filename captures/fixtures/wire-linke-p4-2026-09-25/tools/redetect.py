"""Send one WCH-Link command straight to the LinkE (as board-identify's wch_link.py does), in place of ch32rv under
linke_cap.py: redetect.py --probe serial:<sn> [hex ...] (default 810d0103 = RedetectChip). Prints each answer."""
import sys, usb.core, usb.util
args = sys.argv[1:]
sn = args[args.index("--probe") + 1].split(":", 1)[1]
rest = args[args.index("--probe") + 2:]
if "--capture" in rest:   # linke_cap.py appends ch32rv's --capture <file>; not ours
    i = rest.index("--capture"); del rest[i:i + 2]
cmds = [a for a in rest if not a.startswith("-")] or ["810d0103"]
d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=sn)
d.get_active_configuration()
usb.util.claim_interface(d, 0)
try:
    for h in cmds:
        d.write(0x01, bytes.fromhex(h), 1000)
        print(h, "->", d.read(0x81, 64, 3000).tobytes().hex())
finally:
    usb.util.release_interface(d, 0)
    usb.util.dispose_resources(d)
