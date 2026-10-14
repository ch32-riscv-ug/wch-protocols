import sys, fcntl
sys.argv = ["x"]; exec(open("regprobe.py").read().split("def main():")[0])
t = T("serial:497F8F06CE2F", "CH32V006K8U6")
RCC_APB2, AFIO04, AFIO0C = 0x40021018, 0x40010004, 0x4001000C
U1, GPIOC, GPIOD = 0x40013800, 0x40011000, 0x40011400
with open(LOCK, "w") as lk:
    fcntl.flock(lk, fcntl.LOCK_EX)
    save = {a: t.rd(a) for a in (RCC_APB2, AFIO04, AFIO0C, U1 + 0x0C, U1 + 0x08, GPIOC, GPIOD)}
    t.wr(RCC_APB2, save[RCC_APB2] | 0x1 | 0x10 | 0x20 | (1 << 14))
    t.wr(U1 + 0x08, 0x1A1)
    t.wr(U1 + 0x0C, 0x2008)                               # UE | TE: TX idles high
    t.wr(GPIOC, (save[GPIOC] & ~0xF) | 0xB)               # PC0 AF push-pull
    t.wr(GPIOD, (save[GPIOD] & ~(0xF << 20)) | (0xB << 20))  # PD5 AF push-pull
    def level():
        return {"PC0": t.rd(GPIOC + 8) & 1, "PD5": (t.rd(GPIOD + 8) >> 5) & 1}
    for name, a04, a0c in (("neither (route 0)", 0, 0), ("0x04 = route 3", 0xC0, 0), ("0x0C = route 3", 0, 0xC0)):
        t.wr(AFIO04, (save[AFIO04] & ~0x3C0) | a04)
        t.wr(AFIO0C, (save[AFIO0C] & ~0x3C0) | a0c)
        print(f"{name:20s}", level(), f"(0x04={t.rd(AFIO04):#x}, 0x0C={t.rd(AFIO0C):#x})")
    for a in (U1 + 0x0C, AFIO04, AFIO0C, GPIOC, GPIOD, U1 + 0x08, RCC_APB2):
        t.wr(a, save[a])
