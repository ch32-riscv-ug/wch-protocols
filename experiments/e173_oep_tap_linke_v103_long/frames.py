import sys, numpy as np
from wireskein._engine import fileio
def frames(path, dio, clk, limit=12):
    cap = fileio.load(path); ch = {c.name: c for c in cap.channels}
    D, C = ch[dio], ch[clk]
    ev = sorted([(int(t), 'd') for t in np.asarray(D.edges)] + [(int(t), 'c') for t in np.asarray(C.edges)])
    d, c = D.initial, C.initial
    out, cur, starts = [], None, 0
    t0 = None
    for t, k in ev:
        if k == 'd':
            d ^= 1
            if c == 1:
                if d == 0: cur = {"t": t, "bits": ""}; starts += 1
                elif cur is not None: cur["end"] = t; out.append(cur); cur = None
                if len(out) >= limit: break
        else:
            c ^= 1
            if c == 1 and cur is not None: cur["bits"] += str(d)
    for f in out:
        print(f"{f['t']/20:10.1f} us  n={len(f['bits']):3d}  {f['bits']}")
    print("starts", starts)
if len(sys.argv) > 3: print("== OEP v208"); frames(sys.argv[3], "dio", "clk", 16)
print("== LinkE"); frames(sys.argv[1], "g45", "g44")
print("== OEP"); frames(sys.argv[2], "dio", "clk")
