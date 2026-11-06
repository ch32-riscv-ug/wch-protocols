"""The long (84 + 1 clock) frames of a capture: host addr7 data32 op2 1 | target addr7 data32 status2 1 | 1.
usage: long_frames.py <capture.wireskein> <dio channel> <clk channel> [count]"""
import sys
import numpy as np
from wireskein._engine import fileio


def frames(path, dio, clk):
    cap = fileio.load(path); ch = {c.name: c for c in cap.channels}
    D, C = ch[dio], ch[clk]
    ev = sorted([(int(t), 'd') for t in np.asarray(D.edges)] + [(int(t), 'c') for t in np.asarray(C.edges)])
    d, c, cur = D.initial, C.initial, None
    for t, k in ev:
        if k == 'd':
            d ^= 1
            if c == 1:
                if d == 0:
                    cur = {"t": t, "bits": ""}
                elif cur is not None:
                    yield cur
                    cur = None
        else:
            c ^= 1
            if c == 1 and cur is not None:
                cur["bits"] += str(d)


OPS = {0: "nop", 1: "read", 2: "write", 3: "-"}
path, dio, clk = sys.argv[1:4]
count = int(sys.argv[4]) if len(sys.argv) > 4 else 40
rate = 20.0   # ticks per microsecond (20 MS/s)
for n, f in enumerate(frames(path, dio, clk)):
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
