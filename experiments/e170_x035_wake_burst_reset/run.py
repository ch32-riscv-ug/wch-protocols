"""E170 host driver for wake_probe on the X035 jig (P4 esp32-series-30eda0e31108).

usage: run.py healthy <repeats>      treatments on a running X035, havereset after each
       run.py stopped [t1,t2,...]      make the E165 stopped state from the jig (ACTLR = ffffffff, CFGR0 = d0), then
                                      apply the treatments in turn until havereset shows
"""
import json, sys, time
import serial

PORT = "/run/board-identify/by-id/esp32-series-30eda0e31108"
TREATMENTS = ["idle 100", "resync", "wake", "hi 100", "hi 200", "lo 100", "lo 236"]
if len(sys.argv) > 3:
    TREATMENTS = sys.argv[3].split(",")
ser = serial.Serial(PORT, 115200, timeout=3); time.sleep(0.3); ser.reset_input_buffer()
log = []


def cmd(c):
    ser.write((c + "\n").encode()); r = ser.readline().decode(errors="replace").strip()
    log.append({"t": round(time.time(), 3), "cmd": c, "reply": r}); return r


def havereset():
    r = cmd("st")
    return ("anyhavereset=1" in r), r


mode = sys.argv[1]
res = {"mode": mode, "rows": []}
print(cmd("a"))
if mode == "healthy":
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    for t in TREATMENTS:
        for i in range(n):
            cmd("ack"); pre = havereset()
            cmd(t); time.sleep(0.05)
            relinked = False
            if "err" in cmd("r 11") or cmd("r 11").endswith("ffffffff"):
                cmd("a"); relinked = True        # the raw runs drop the link; attach again (wake + dmactive)
            post = havereset()
            row = {"treatment": t, "i": i, "before": pre[1], "after": post[1], "relinked": relinked,
                   "reset": post[0] and not pre[0] and "err" not in post[1]}
            res["rows"].append(row); print(row)
else:
    seq = sys.argv[2].split(",") if len(sys.argv) > 2 else ["idle 2000", "resync", "wake", "hi 100", "lo 236", "wake", "wake", "wake"]
    cmd("ack"); print("before stop", havereset()[1])
    cmd("w 10 80000001"); time.sleep(0.01); print("halt", cmd("st"))
    print("cfgr0/actlr before", cmd("m 40021004"), cmd("m 40022000"))
    print("write actlr", cmd("s 40022000 ffffffff"), "write cfgr0", cmd("s 40021004 000000d0"))
    cmd("w 10 40000001"); time.sleep(0.3)
    print("after stop:", cmd("r 11"), cmd("st"))
    for t in seq:
        cmd(t); time.sleep(0.05)
        r1 = cmd("r 11"); hr = havereset()
        row = {"treatment": t, "dmstatus_read": r1, "status": hr[1]}
        if hr[0] and "err" not in hr[1]:
            cmd("w 10 80000001"); time.sleep(0.01)
            row["halted_regs"] = [cmd("st"), cmd("m 40021004"), cmd("m 40022000")]
            cmd("w 10 40000001")
        res["rows"].append(row); print(row)
        if hr[0] and "err" not in hr[1]:
            print("-> havereset after", t); break
res["log"] = log
json.dump(res, open(f"out/{mode}_{int(time.time())}.json", "w"), indent=1)
