"""E167: record the LinkE 3V3 output (P4 ADC on GPIO17/21) while sending power / special-erase commands.

usage: measure.py <name> <record ms> <linke hex or sleep:s> ...
P4 port: /run/board-identify/by-id/esp32-series-30eda0e343c6 (p4_rail_logger); LinkE: 497E8F06CE2E (no target).
"""
import json, sys, threading, time
import serial, usb.core, usb.util

name, rec_ms, cmds = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
P4 = "/run/board-identify/by-id/esp32-series-30eda0e343c6"
SN = "497E8F06CE2E"
p4 = serial.Serial(P4, 921600, timeout=0.5); time.sleep(0.2); p4.reset_input_buffer()
usb_log = []


def linke():
    d = usb.core.find(idVendor=0x1a86, idProduct=0x8010, serial_number=SN)
    d.get_active_configuration(); usb.util.claim_interface(d, 0)
    try:
        for h in cmds:
            if h.startswith("sleep:"):
                time.sleep(float(h[6:])); continue
            t = time.monotonic(); d.write(0x01, bytes.fromhex(h), 1000)
            try:
                r = d.read(0x81, 64, 5000).tobytes().hex()
            except usb.core.USBTimeoutError:
                r = "TIMEOUT"
            usb_log.append({"t_s": round(t - t_start, 4), "out": h, "in": r, "ms": round((time.monotonic() - t) * 1e3, 2)})
    finally:
        usb.util.release_interface(d, 0); usb.util.dispose_resources(d)


p4.write(f"r{rec_ms}\n".encode()); t_start = time.monotonic()
time.sleep(0.3)
th = threading.Thread(target=linke); th.start(); th.join()
lines = []
while True:
    l = p4.readline().decode(errors="replace").strip()
    if not l:
        if time.monotonic() - t_start > rec_ms / 1000 + 60: break
        continue
    lines.append(l)
    if l == "# end": break
p4.close()
open(f"out/{name}.csv", "w").write("\n".join(lines) + "\n")
json.dump(usb_log, open(f"out/{name}.usb.json", "w"), indent=1)
rows = [list(map(int, l.split(","))) for l in lines if l and l[0].isdigit()]
print(lines[0] if lines else "no data", "rows", len(rows))
for u in usb_log: print("  usb", u)
# summarize 3V3 (mean of the two ADC pins) in 20 ms bins where it changes
prev = None
for r in rows:
    v = (r[1] + r[2]) // 2
    if prev is None or abs(v - prev) > 300:
        print(f"  t={r[0]/1000:8.1f} ms  3V3 ~ {v} mV  (17:{r[1]} 21:{r[2]})  rst={r[3]} dio={r[4]} clk={r[5]} rx={r[6]} tx={r[7]}")
        prev = v
