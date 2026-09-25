import sys, time, usb.core, usb.util
sn = sys.argv[1]; n = int(sys.argv[2])
d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=sn)
d.get_active_configuration(); usb.util.claim_interface(d, 0)
def x(h, to=3000):
    d.write(0x01, bytes.fromhex(h), 1000); return d.read(0x81, 64, to).tobytes().hex()
try:
    x("810d0101"); x("810c020103")
    for i in range(n):
        t = time.time(); r = x("810d0102"); dt = time.time() - t
        extra = ""
        if r.startswith("820d05"):
            extra = " dmstatus=" + x("81080611000000000" + "1")[6:14]   # DmiOp read 0x11
        print(f"{i:2d} {dt*1e3:6.1f} ms {r}{extra}")
        x("810d0103"); x("810d01ff"); time.sleep(0.2)
finally:
    usb.util.release_interface(d, 0); usb.util.dispose_resources(d)
