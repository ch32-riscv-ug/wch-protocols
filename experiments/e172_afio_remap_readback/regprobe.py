"""Write/readback AFIO remap registers and GPIO over the debug link (ch32rv write/read)."""
import subprocess, struct, sys, json, fcntl
CH = "/home/mt/dev_wch/ch32rv/target/debug/ch32rv"
LOCK = "/home/mt/dev_oep/oep-bench/locks/bench.lock"

class T:
    def __init__(self, probe, chip):
        self.p, self.c = probe, chip
    def run(self, *a):
        r = subprocess.run([CH, *a, "--probe", self.p, "--chip", self.c, "--non-interactive", "--progress", "none"],
                           capture_output=True, timeout=60)
        if r.returncode:
            raise RuntimeError(f"{a}: {r.stderr.decode()[-300:]}")
        return r.stdout
    def rd(self, addr):
        f = "/tmp/claude-1000/-home-mt-dev-wch-ArduinoCore-CH32RV/304fc7e3-f69d-4a99-9b8d-7edb631ee0f8/scratchpad/rd.bin"
        self.run("read", "--range", f"0x{addr:08x}+4", "--format", "bin", "-o", f)
        return struct.unpack("<I", open(f, "rb").read(4))[0]
    def wr(self, addr, val):
        try:
            self.run("write", "--at", f"0x{addr:08x}", f"word:0x{val:08x}")
        except RuntimeError as e:
            if "verify-mismatch" not in str(e):   # the write happened; the readback is the data
                raise
    def probe_bits(self, addr, try_bits):
        orig = self.rd(addr)
        self.wr(addr, orig | try_bits); ones = self.rd(addr)
        self.wr(addr, orig & ~try_bits); zeros = self.rd(addr)
        self.wr(addr, orig); back = self.rd(addr)
        return {"orig": f"0x{orig:08x}", "set_all": f"0x{ones:08x}", "clear_all": f"0x{zeros:08x}",
                "implemented": f"0x{(ones & ~zeros) & try_bits:08x}", "restored": back == orig}
    def field_values(self, addr, mask, shift, values):
        orig = self.rd(addr); out = {}
        for v in values:
            self.wr(addr, (orig & ~mask) | ((v << shift) & mask))
            out[v] = (self.rd(addr) & mask) >> shift
        self.wr(addr, orig)
        return out

def main():
    target, probe, chip = sys.argv[1:4]
    t = T(probe, chip)
    res = {"target": target}
    with open(LOCK, "w") as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        RCC_APB2 = 0x40021018
        t.wr(RCC_APB2, t.rd(RCC_APB2) | 0x1 | 0x1c)          # AFIO + GPIOA..C clocks
        SAFE = 0xFFFFFFFF & ~0x07000000                       # never touch SW_CFG[26:24]
        res["PCFR1@0x04"] = t.probe_bits(0x40010004, SAFE)
        for extra in sys.argv[4:]:
            k, v = extra.split("=", 1)
            if k == "pcfr2": res["PCFR2@0x1c"] = t.probe_bits(0x4001001c, SAFE)
            if k == "pcfr0c": res["PCFR@0x0c"] = t.probe_bits(0x4001000c, SAFE)
            if k == "field":                                  # field=addr:mask:shift:v1,v2,..
                a, m, s, vals = v.split(":")
                res[f"field {a} {m}"] = t.field_values(int(a, 16), int(m, 16), int(s), [int(x, 0) for x in vals.split(",")])
            if k == "gpio":                                   # gpio=port_base:pin  (CFGHR/CFGLR nibble + OUTDR bit)
                b, pin = v.split(":"); b = int(b, 16); pin = int(pin)
                cfg = b + (0x04 if pin >= 8 else 0x00); sh = (pin % 8) * 4
                o = t.rd(cfg); od = t.rd(b + 0x0c)
                got = {}
                for nib in (0x8, 0x3, 0xB):
                    t.wr(cfg, (o & ~(0xF << sh)) | (nib << sh)); got[f"cfg={nib:x}"] = f"{(t.rd(cfg) >> sh) & 0xF:x}"
                t.wr(cfg, (o & ~(0xF << sh)) | (0x8 << sh))   # input with pull
                for up in (1, 0):
                    t.wr(b + 0x0c, (od & ~(1 << pin)) | (up << pin))
                    got[f"pull{'up' if up else 'down'}: OUTDR={ (t.rd(b+0x0c)>>pin)&1 } INDR"] = (t.rd(b + 0x08) >> pin) & 1
                t.wr(cfg, o); t.wr(b + 0x0c, od)
                res[f"gpio {v}"] = got
    print(json.dumps(res, indent=1))
main()
