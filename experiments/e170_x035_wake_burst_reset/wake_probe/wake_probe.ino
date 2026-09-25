// wake_probe: send individual RVSWD sequences to a CH32X035 and see which ones reset it (E170).
// X035 jig: SWDIO = P4 GPIO2 -> X035 PC18, SWCLK = P4 GPIO54 -> X035 PC19, no NRST.
//
// Serial (USB-Serial/JTAG, 115200), one command per line, one reply line each (starting "ok" or "err"):
//   a               attach (bus bring-up with wake, dmactive, speed pick); prints half ns and DMSTATUS
//   h <ns>          force the half period (ns)
//   r <addr>        DMI read (hex)             w <addr> <val>   DMI write
//   m <addr>        memory read (abstract cmd) s <addr> <val>   memory write
//   st              DMSTATUS decoded (allhalted / allhavereset / anyhavereset)
//   ack             DMCONTROL = ackhavereset | dmactive
//   wake            the library's wake burst: 100 clocks with SWDIO high, one low bit, STOP; then DM config
//   resync          bus bring-up without the wake burst
//   hi <n>          raw: SWDIO driven high, n SWCLK clocks, then STOP         (bit-banged here)
//   lo <n>          raw: SWDIO driven low,  n SWCLK clocks, then SWDIO high    (bit-banged here)
//   idle <ms>       release the bus and wait
#include <Arduino.h>
#include <OepRvswdPhy.h>

static const int DIO = 2, CLK = 54;
static oep::RvswdPhy phy;

static uint32_t hx(const String &s) { return strtoul(s.c_str(), nullptr, 16); }

static bool memRead(uint32_t addr, uint32_t &v) {
  phy.write(0x05, addr);
  phy.write(0x17, 0x02200000);
  uint32_t cs = 0;
  if (!phy.read(0x16, cs)) return false;
  if ((cs >> 8) & 7) { phy.write(0x16, 0x700); return false; }
  return phy.read(0x04, v);
}

static bool memWrite(uint32_t addr, uint32_t v) {
  phy.write(0x05, addr);
  phy.write(0x04, v);
  phy.write(0x17, 0x02210000);
  uint32_t cs = 0;
  if (!phy.read(0x16, cs)) return false;
  if ((cs >> 8) & 7) { phy.write(0x16, 0x700); return false; }
  return true;
}

// Raw clock runs with plain GPIO (about 1 us per half period). The library releases the pins first.
static void rawRun(bool dioLevel, int n, bool stopAfter) {
  phy.release();
  pinMode(DIO, OUTPUT); pinMode(CLK, OUTPUT);
  digitalWrite(CLK, HIGH); digitalWrite(DIO, HIGH); delayMicroseconds(5);
  if (!dioLevel) { digitalWrite(CLK, LOW); delayMicroseconds(2); digitalWrite(DIO, LOW); delayMicroseconds(2); }
  for (int i = 0; i < n; ++i) {
    digitalWrite(CLK, LOW); delayMicroseconds(2);
    digitalWrite(CLK, HIGH); delayMicroseconds(2);
  }
  if (stopAfter || !dioLevel) {   // leave both lines high; for a high run, end with a STOP (DIO rises while CLK high)
    digitalWrite(CLK, LOW); delayMicroseconds(2); digitalWrite(DIO, LOW); delayMicroseconds(2);
    digitalWrite(CLK, HIGH); delayMicroseconds(2); digitalWrite(DIO, HIGH); delayMicroseconds(5);
  }
  pinMode(DIO, INPUT); pinMode(CLK, INPUT);
}

static void printStatus() {
  uint32_t st = 0;
  bool ok = phy.read(0x11, st);
  Serial.printf("%s dmstatus=%08x allhalted=%u allhavereset=%u anyhavereset=%u allrunning=%u\n", ok ? "ok" : "err",
                (unsigned)st, (unsigned)((st >> 9) & 1), (unsigned)((st >> 19) & 1), (unsigned)((st >> 18) & 1),
                (unsigned)((st >> 11) & 1));
}

void setup() {
  Serial.begin(115200);
  phy.begin(DIO, CLK);
}

void loop() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n'); line.trim();
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String a1 = sp < 0 ? "" : line.substring(sp + 1);
  int sp2 = a1.indexOf(' ');
  String a2 = sp2 < 0 ? "" : a1.substring(sp2 + 1);
  if (sp2 >= 0) a1 = a1.substring(0, sp2);
  if (cmd == "a") {
    bool ok = phy.attach();
    uint32_t st = 0; phy.read(0x11, st);
    Serial.printf("%s half_ns=%u dmstatus=%08x\n", ok ? "ok" : "err", (unsigned)phy.halfNs(), (unsigned)st);
  } else if (cmd == "h") {
    phy.useHalf(strtoul(a1.c_str(), nullptr, 10)); Serial.printf("ok half_ns=%u\n", (unsigned)phy.halfNs());
  } else if (cmd == "r") {
    uint32_t v = 0; bool ok = phy.read(hx(a1), v); Serial.printf("%s %08x\n", ok ? "ok" : "err", (unsigned)v);
  } else if (cmd == "w") {
    phy.write(hx(a1), hx(a2)); Serial.println("ok");
  } else if (cmd == "m") {
    uint32_t v = 0; bool ok = memRead(hx(a1), v); Serial.printf("%s %08x\n", ok ? "ok" : "err", (unsigned)v);
  } else if (cmd == "s") {
    bool ok = memWrite(hx(a1), hx(a2)); Serial.println(ok ? "ok" : "err");
  } else if (cmd == "st") {
    printStatus();
  } else if (cmd == "ack") {
    phy.write(0x10, 0x10000001); Serial.println("ok");
  } else if (cmd == "wake") {
    phy.wakeBus(); Serial.println("ok");
  } else if (cmd == "resync") {
    phy.reinit(); Serial.println("ok");
  } else if (cmd == "hi" || cmd == "lo") {
    rawRun(cmd == "hi", a1.toInt(), true); phy.reinit(); Serial.println("ok");
  } else if (cmd == "idle") {
    phy.release(); delay(a1.toInt()); Serial.println("ok");
  } else {
    Serial.println("err unknown");
  }
}
