"""Send raw WCH-Link commands in one USB session: raw_seq.py <serial> <hex>... ; 'sleep:<s>' pauses."""
import sys, time, usb.core, usb.util
sn = sys.argv[1]
d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=sn)
d.get_active_configuration(); usb.util.claim_interface(d, 0)
try:
    for h in sys.argv[2:]:
        if h.startswith("sleep:"): time.sleep(float(h[6:])); continue
        t = time.time(); d.write(0x01, bytes.fromhex(h), 1000)
        try: r = d.read(0x81, 64, 5000).tobytes().hex()
        except usb.core.USBTimeoutError: r = "TIMEOUT"
        print(f"{h:24} -> {r}   ({(time.time()-t)*1e3:.0f} ms)")
finally:
    usb.util.release_interface(d, 0); usb.util.dispose_resources(d)
