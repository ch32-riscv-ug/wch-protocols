"""The RVSWD frames of a capture, delimited as link-to-target#frame-delimiting says: START is SWDIO falling while SWCLK
is high outside a frame; STOP is SWDIO rising while SWCLK is high with SWCLK then staying high 2 us or more (a target
changes SWDIO while SWCLK is high inside a read). A frame of 84 clocks or more is read as the long one (host addr7
data32 op2 1 | target addr7 data32 status2 1 | 1); shorter ones are printed as bits.
usage: long_frames.py <capture.wireskein> <dio channel> <clk channel> [count] [rate MS/s]"""
import sys
import numpy as np
from wireskein._engine import fileio


def frames(path, dio, clk, rate):
    cap = fileio.load(path); ch = {c.name: c for c in cap.channels}
    D, C = ch[dio], ch[clk]
    cedges = np.asarray(C.edges)
    ev = sorted([(int(t), 'd') for t in np.asarray(D.edges)] + [(int(t), 'c') for t in cedges])
    gap = int(2 * rate)   # 2 us in ticks
    d, c, cur = D.initial, C.initial, None
    for t, k in ev:
        if k == 'd':
            d ^= 1
            if c == 1:
                if d == 0 and cur is None:
                    cur = {"t": t, "bits": ""}
                elif d == 1 and cur is not None:
                    nxt = cedges[np.searchsorted(cedges, t, side="right")] if np.searchsorted(cedges, t, side="right") < len(cedges) else t + gap + 1
                    if nxt - t >= gap:
                        yield cur
                        cur = None
        else:
            c ^= 1
            if c == 1 and cur is not None:
                cur["bits"] += str(d)


OPS = {0: "nop", 1: "read", 2: "write", 3: "-"}
path, dio, clk = sys.argv[1:4]
count = int(sys.argv[4]) if len(sys.argv) > 4 else 40
rate = float(sys.argv[5]) if len(sys.argv) > 5 else 20.0
for n, f in enumerate(frames(path, dio, clk, rate)):
    b = f["bits"]
    if len(b) < 84:
        print(f"{f['t'] / rate:10.1f} us  {len(b):3d} clocks  {b}")
    else:
        h_addr, h_data, h_op, h_1 = int(b[0:7], 2), int(b[7:39], 2), int(b[39:41], 2), b[41]
        t_addr, t_data, t_st, t_1 = int(b[42:49], 2), int(b[49:81], 2), int(b[81:83], 2), b[83]
        print(f"{f['t'] / rate:10.1f} us  {len(b):3d} clocks  host {OPS[h_op]:5s} addr {h_addr:#04x} data {h_data:#010x} [{h_1}]"
              f" | target addr {t_addr:#04x} data {t_data:#010x} status {t_st} [{t_1}]")
    if n + 1 >= count:
        break
