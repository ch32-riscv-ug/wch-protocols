// E006 tool: pulse width generation and capture on one wire, using RMT.
//
// Role is chosen by the host at run time, so both boards run this same code.
// The line idles high and is pulled low for a pulse, matching SWIO's shape
// (see protocols/link-to-target.ja.md section 3).
//
// Commands (line based):
//   ?                identify
//   I<tick_hz>,<dir> init RMT on the line; dir 0 = receive, 1 = transmit
//   P<lo>,<hi>,<n>   send n pulses: low for <lo> ticks, high for <hi> ticks
//   C<n>             capture n symbols, reporting each duration in ticks
//
// Every step is host-orchestrated (rules section 7-10).
//
// Plan and report: ../README.ja.md

#define MAX_SYMBOLS 32

static int gLine = -1;
static uint32_t gTick = 0;
static bool gReady = false;
static rmt_data_t gTx[MAX_SYMBOLS];
static rmt_data_t gRx[MAX_SYMBOLS];

void setup() {
  Serial.begin(115200);
  gLine = atoi(WIRE_LINE);
  pinMode(gLine, INPUT_PULLUP);  // the line idles high, like SWIO
}

static void initRmt(uint32_t tickHz, int dir) {
  if (gReady) {
    rmtDeinit(gLine);
    gReady = false;
  }
  bool ok = rmtInit(gLine, dir ? RMT_TX_MODE : RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_2, tickHz);
  if (ok && !dir) {
    rmtSetRxMinThreshold(gLine, 0);       // no glitch filter: keep the short pulses
    rmtSetRxMaxThreshold(gLine, 20000);   // idle gap that ends a capture
  }
  if (ok && dir) {
    rmtSetEOT(gLine, HIGH);               // leave the line high between bursts
  }
  gReady = ok;
  gTick = ok ? tickHz : 0;
  Serial.print(ok ? "INIT ok tick=" : "INIT fail tick=");
  Serial.print(tickHz);
  Serial.print(" dir=");
  Serial.println(dir);
}

static void sendPulses(uint32_t lo, uint32_t hi, int count) {
  if (!gReady) { Serial.println("ERR not ready"); return; }
  if (count > MAX_SYMBOLS) count = MAX_SYMBOLS;
  for (int i = 0; i < count; i++) {
    gTx[i].level0 = 0;
    gTx[i].duration0 = lo;
    gTx[i].level1 = 1;
    gTx[i].duration1 = hi;
  }
  bool ok = rmtWrite(gLine, gTx, count, 1000);
  Serial.print(ok ? "SENT n=" : "SENDFAIL n=");
  Serial.println(count);
}

static void capture(int count) {
  if (!gReady) { Serial.println("ERR not ready"); return; }
  if (count > MAX_SYMBOLS) count = MAX_SYMBOLS;
  size_t num = count;
  memset(gRx, 0, sizeof(gRx));
  Serial.println("ARMED");
  bool ok = rmtRead(gLine, gRx, &num, 2000);
  if (!ok) { Serial.println("CAPFAIL"); return; }
  Serial.print("SYM n=");
  Serial.print(num);
  Serial.print(" d=");
  for (size_t i = 0; i < num; i++) {
    if (i) Serial.print(';');
    Serial.print(gRx[i].level0);
    Serial.print(':');
    Serial.print(gRx[i].duration0);
    Serial.print(',');
    Serial.print(gRx[i].level1);
    Serial.print(':');
    Serial.print(gRx[i].duration1);
  }
  Serial.println();
}

void loop() {
  static char line[32];
  static size_t len = 0;
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c != '\n' && c != '\r') {
    if (len < sizeof(line) - 1) line[len++] = c;
    return;
  }
  if (len == 0) return;
  line[len] = '\0';
  len = 0;

  if (line[0] == '?') {
    Serial.print("# EXP E006 v1 role=primary line=");
    Serial.print(gLine);
    Serial.println(" build=" __DATE__ " " __TIME__);
  } else if (line[0] == 'I') {
    char *c1 = strchr(line + 1, ',');
    if (!c1) { Serial.println("ERR args"); return; }
    *c1 = '\0';
    initRmt(strtoul(line + 1, NULL, 10), atoi(c1 + 1));
  } else if (line[0] == 'P') {
    char *c1 = strchr(line + 1, ',');
    char *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
    if (!c2) { Serial.println("ERR args"); return; }
    *c1 = '\0'; *c2 = '\0';
    sendPulses(strtoul(line + 1, NULL, 10), strtoul(c1 + 1, NULL, 10), atoi(c2 + 1));
  } else if (line[0] == 'C') {
    capture(atoi(line + 1));
  } else {
    Serial.println("ERR cmd");
  }
}
