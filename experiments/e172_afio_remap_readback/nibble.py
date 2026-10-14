import sys, fcntl
sys.argv, A = ["x"], sys.argv[1:]
exec(open("regprobe.py").read().split("def main():")[0])
t = T(A[0], A[1])
with open(LOCK, "w") as lk:
    fcntl.flock(lk, fcntl.LOCK_EX)
    t.wr(0x40021018, t.rd(0x40021018) | 0x1D)
    for pad in A[2:]:
        base = 0x40010800 + 0x400 * (ord(pad[1]) - 65); n = int(pad[2:])
        cfg = base + (4 if n >= 8 else 0); sh = (n % 8) * 4; o = t.rd(cfg); res = []
        for v in range(16):
            t.wr(cfg, (o & ~(0xF << sh)) | (v << sh)); res.append(f"{v:x}->{(t.rd(cfg) >> sh) & 0xF:x}")
        t.wr(cfg, o); print(pad, " ".join(res), flush=True)
