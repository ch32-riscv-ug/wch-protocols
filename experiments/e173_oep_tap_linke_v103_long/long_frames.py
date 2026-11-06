"""The RVSWD frames of a capture. START is SWDIO falling while SWCLK is high outside a frame; STOP is SWDIO rising while
SWCLK is high, in a high of SWCLK longer than 1.5 times the median of the 16 highs before it (a target changes SWDIO
while SWCLK is high inside a read, in a high of up to 1.2 times its neighbours; a WCH-LinkE's STOP high is 3.3 - 3.75
times, an OEP probe's about 2 times; a fixed 2 us missed fast LinkE STOPs of 1.84 - 2.0 us, wireskein ee37142).
A frame of 84 clocks or more is read as the long one (host addr7 data32 op2 1 | target addr7 data32 status2 1 | 1);
shorter ones are printed as bits. The sample rate is the capture's.
usage: long_frames.py <capture.wireskein> <dio channel> <clk channel> [count]"""
import sys
import numpy as np
from wireskein._engine import fileio


def frames(path, dio, clk):
    cap = fileio.load(path); ch = {c.name: c for c in cap.channels}
    D, C = ch[dio], ch[clk]
    cedges = np.asarray(C.edges)
    # SWCLK's highs: (rise, fall) pairs
    first_rise = 0 if C.initial == 0 else 1
    rises = cedges[first_rise::2]
    falls = cedges[first_rise + 1::2]
    n = min(len(rises), len(falls))
    rises, falls = rises[:n], falls[:n]
    highs = falls - rises

    def long_high(t):   # the high of SWCLK that holds t, against the 16 before it
        i = int(np.searchsorted(rises, t, side="right")) - 1
        if i < 0:
            return False
        if i >= n or falls[i] < t:
            return True   # high to the end of the capture
        before = highs[max(0, i - 16):i]
        return len(before) > 0 and highs[i] > 1.5 * float(np.median(before))

    ev = sorted([(int(t), 'd') for t in np.asarray(D.edges)] + [(int(t), 'c') for t in cedges])
    d, c, cur = D.initial, C.initial, None
    for t, k in ev:
        if k == 'd':
            d ^= 1
            if c == 1:
                if d == 0 and cur is None:
                    cur = {"t": t, "bits": ""}
                elif d == 1 and cur is not None and long_high(t):
                    yield cur
                    cur = None
        else:
            c ^= 1
            if c == 1 and cur is not None:
                cur["bits"] += str(d)
    return cap


OPS = {0: "nop", 1: "read", 2: "write", 3: "-"}
path, dio, clk = sys.argv[1:4]
count = int(sys.argv[4]) if len(sys.argv) > 4 else 40
rate = fileio.load(path).rate / 1e6   # ticks per us
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
