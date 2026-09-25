# /// script
# dependencies = ["pyserial>=3.5", "pyusb", "numpy"]
# ///
"""Capture the LinkE -> CH32L103 RVSWD pair in repeat mode while a ch32rv command runs (tapped at the LinkE end:
a tap at the target end broke the link). P4 GPIO14 = SWCLK, GPIO15 = SWDIO.
Saves <out>.sr (sigrok, D0 = SWCLK, D1 = SWDIO, as the 2026-09-11 LA2016 fixture) and <out>.json.

    linke_cap.py <p4 hs serial> <rate_hz> <out> <segments> <linke serial> <pins csv> <names csv> [target] -- <ch32rv args...>"""
import json, os, struct, subprocess, sys, threading, time, zipfile
import numpy as np
sys.path.insert(0, "/home/mt/dev_oep/oep-client-python/src")
from oep_client.v1 import core, link
from oep_client.v1.capture import LogicCapture, Segment, REPEAT, EVENT_SEGMENT, EVENT_STOPPED

CH32RV = os.environ.get("CH32RV", "/home/mt/dev_wch/ch32rv/target/release/ch32rv")
A = sys.argv[:sys.argv.index("--")]
LINKE = "serial:" + A[5]
PINS = tuple(int(x) for x in A[6].split(","))    # role k = PINS[k] = NAMES[k]
NAMES = tuple(A[7].split(","))
TARGET = A[8] if len(A) > 8 else ""

serial, rate, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
cmd = [CH32RV, "--probe", LINKE] + sys.argv[sys.argv.index("--") + 1:]
hst = link.open_usb_host(serial=serial); lk = hst.link; hst.open(lease_ms=10000)
chunks, segs, stopped = [], [], []
try:
    cap = LogicCapture(hst)
    core.plan_release(hst)
    core.plan_apply(hst, [(cap.fn, k, p) for k, p in enumerate(PINS)])
    cfg = cap.configure(mode=REPEAT, rate=rate, samples=262144, segments=int(A[4]))
    hst.call(0, 0x30, struct.pack("<H", cap.fn)); lk.events.clear()
    result = {}
    def run():
        t = time.monotonic()
        p = subprocess.run(cmd, capture_output=True, text=True)
        result.update(rc=p.returncode, stdout=p.stdout, stderr=p.stderr, seconds=time.monotonic() - t)
    t0 = time.monotonic(); cap.start(); time.sleep(0.05)
    th = threading.Thread(target=run); th.start()
    stop_at = None
    while True:
        lk.pump(0.02, until_one=True)
        while lk.events:
            if not th.is_alive() and stop_at is None:
                stop_at = time.monotonic() + 0.05
            if stop_at and time.monotonic() > stop_at and not getattr(cap, "_stop_sent", False):
                cap.stop(); cap._stop_sent = True
            e = lk.events.popleft(); kind, payload = e[5], e[6:]
            if kind == EVENT_STOPPED: stopped.append(payload[0]); continue
            if kind != EVENT_SEGMENT: continue
            seg = Segment.unpack(payload)
            chunks.append(cap.read(seg.position, (seg.samples * cfg.width + 7) // 8)); segs.append(seg)
            cap.release(seg.serial)
        if not th.is_alive() and stop_at is None:
            stop_at = time.monotonic() + 0.05
        if stop_at and time.monotonic() > stop_at and 1 not in stopped and not getattr(cap, "_stop_sent", False):
            cap.stop(); cap._stop_sent = True
        if 1 in stopped and not lk.events:
            break
    hst.call(0, 0x32, struct.pack("<H", cap.fn))
finally:
    core.plan_release(hst); hst.end(); hst.link.stream.close()

data = np.frombuffer(b"".join(chunks), dtype=np.uint8)
w = cfg.width   # 2 lines -> w = 2
samples = np.stack([(data >> k) & ((1 << w) - 1) for k in range(0, 8, w)], axis=1).reshape(-1)
with zipfile.ZipFile(out + ".sr", "w", compression=zipfile.ZIP_DEFLATED) as z:
    z.writestr("version", "2")
    z.writestr("metadata", "\n".join(["[global]", "sigrok version=0.5.2", "", "[device 1]", "capturefile=logic-1",
                                       f"total probes={len(NAMES)}", f"samplerate={rate} Hz", "total analog=0"]
                                      + [f"probe{k + 1}={n}" for k, n in enumerate(NAMES)] + ["unitsize=1", ""]))
    z.writestr("logic-1-1", samples.astype(np.uint8).tobytes())
meta = {"command": cmd, "rate_hz": rate, "actual_rate": str(cfg.rate),
        "channels": {f"D{k}": f"{n} (P4 GPIO{p})" for k, (n, p) in enumerate(zip(NAMES, PINS))},
        "probe": "OEP P4 esp32-series-30eda0e343c6, oep.fixture.capture repeat mode over HS vendor bulk",
        "linke": LINKE, "target": TARGET,
        "segments": len(segs), "gap_marked": sum(1 for s in segs if s.flags & 1),
        "segment_start_us": [s.start_us for s in segs], "segment_flags": [s.flags for s in segs],
        "stopped_events": stopped, **result}
json.dump(meta, open(out + ".json", "w"), indent=1)
# a first look: activity and the shortest runs on each line
for k, pin in enumerate(PINS):
    ch = (samples >> k) & 1
    print(f"{NAMES[k]}:", end=" ")
    edges = np.flatnonzero(np.diff(ch.astype(np.int8)))
    runs = np.diff(edges)
    print(f"GPIO{pin}: edges {len(edges)}, shortest runs {np.sort(runs)[:5].tolist() if len(runs) else []}, "
          f"median run {int(np.median(runs)) if len(runs) else 0} samples")
print(f"{out}: {len(samples)} samples at {rate/1e6:g} MHz, segments {len(segs)} (gap-marked {meta['gap_marked']}), "
      f"ch32rv rc {result.get('rc')} in {result.get('seconds', 0):.2f} s")
