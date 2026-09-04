// E007 wire: emit an RVSWD host-phase frame and check it on the wire.
//
// Role is chosen by the host at run time, so both boards run this same code.
// GPIO WIRE_CLK acts as SWCLK and WIRE_DIO as SWDIO, matching the RVSWD rule:
// data changes while the clock is low and is sampled while the clock is high,
// MSB first (see protocols/link-to-target.ja.md section 3).
//
// Commands (line based):
//   ?              identify
//   T<half>,<bits> transmit <bits> pattern bits, <half> us per clock half
//   A<bits>        arm the receiver, then report BITS=<hex> or TIMEOUT=<n>
//   Z              release both lines
//
// Every step is host-orchestrated (rules section 7-10).
//
// Plan and report: ../README.ja.md

static int gClk = -1;
static int gDio = -1;

// Known pattern, also computed host side: A5 5A C3 3C repeating, MSB first.
static const uint8_t kPattern[] = {0xA5, 0x5A, 0xC3, 0x3C};

static bool patternBit(int i) {
  uint8_t byte = kPattern[(i >> 3) % sizeof(kPattern)];
  return (byte >> (7 - (i & 7))) & 1;
}

static void release() {
  pinMode(gClk, INPUT_PULLDOWN);
  pinMode(gDio, INPUT_PULLDOWN);
}

void setup() {
  Serial.begin(115200);
  gClk = atoi(WIRE_CLK);
  gDio = atoi(WIRE_DIO);
  release();
}

static void transmit(int halfUs, int bits) {
  pinMode(gClk, OUTPUT);
  pinMode(gDio, OUTPUT);
  digitalWrite(gClk, LOW);
  digitalWrite(gDio, LOW);
  if (halfUs > 0) delayMicroseconds(halfUs);

  for (int i = 0; i < bits; i++) {
    digitalWrite(gDio, patternBit(i) ? HIGH : LOW);  // data changes while clock low
    if (halfUs > 0) delayMicroseconds(halfUs);
    digitalWrite(gClk, HIGH);                        // sampled while clock high
    if (halfUs > 0) delayMicroseconds(halfUs);
    digitalWrite(gClk, LOW);
  }
  digitalWrite(gDio, LOW);
  Serial.print("SENT bits=");
  Serial.print(bits);
  Serial.print(" half=");
  Serial.println(halfUs);
}


// One RVSWD host-phase transaction: addr7 + data32 + op2 + parity1, MSB first
// (protocols/link-to-target.ja.md section 3). The parity convention is read as
// "the parity bit makes the number of ones odd"; that reading is what this
// experiment records, and only a real target can settle it.
static void transmitFrame(uint32_t addr, uint32_t data, uint32_t op, int halfUs) {
  bool bits[42];
  int n = 0;
  int ones = 0;
  for (int i = 6; i >= 0; i--) { bits[n] = (addr >> i) & 1; ones += bits[n++]; }
  for (int i = 31; i >= 0; i--) { bits[n] = (data >> i) & 1; ones += bits[n++]; }
  for (int i = 1; i >= 0; i--) { bits[n] = (op >> i) & 1; ones += bits[n++]; }
  bits[n++] = (ones % 2) == 0;  // make the total number of ones odd

  pinMode(gClk, OUTPUT);
  pinMode(gDio, OUTPUT);
  digitalWrite(gClk, LOW);
  digitalWrite(gDio, LOW);
  if (halfUs > 0) delayMicroseconds(halfUs);
  for (int i = 0; i < n; i++) {
    digitalWrite(gDio, bits[i] ? HIGH : LOW);
    if (halfUs > 0) delayMicroseconds(halfUs);
    digitalWrite(gClk, HIGH);
    if (halfUs > 0) delayMicroseconds(halfUs);
    digitalWrite(gClk, LOW);
  }
  digitalWrite(gDio, LOW);
  Serial.print("FRAME bits=");
  Serial.println(n);
}

static void receive(int bits) {
  if (bits > 256) bits = 256;
  static uint8_t buf[32];
  memset(buf, 0, sizeof(buf));

  pinMode(gClk, INPUT_PULLDOWN);
  pinMode(gDio, INPUT_PULLDOWN);
  Serial.println("ARMED");

  const uint32_t kTimeoutMs = 3000;
  uint32_t start = millis();
  int got = 0;
  while (got < bits) {
    while (digitalRead(gClk) == LOW) {           // wait for the rising edge
      if (millis() - start > kTimeoutMs) goto done;
    }
    if (digitalRead(gDio)) buf[got >> 3] |= 0x80 >> (got & 7);
    got++;
    while (digitalRead(gClk) == HIGH) {          // wait for the falling edge
      if (millis() - start > kTimeoutMs) goto done;
    }
  }
done:
  if (got < bits) {
    Serial.print("TIMEOUT=");
    Serial.println(got);
    return;
  }
  Serial.print("BITS=");
  for (int i = 0; i < (bits + 7) / 8; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
  }
  Serial.println();
}

void loop() {
  static char line[24];
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
    Serial.print("# EXP E007 v1 role=peer clk=");
    Serial.print(gClk);
    Serial.print(" dio=");
    Serial.print(gDio);
    Serial.println(" build=" __DATE__ " " __TIME__);
  } else if (line[0] == 'T') {
    char *comma = strchr(line + 1, ',');
    if (!comma) { Serial.println("ERR args"); return; }
    *comma = '\0';
    transmit(atoi(line + 1), atoi(comma + 1));
  } else if (line[0] == 'F') {
    char *c1 = strchr(line + 1, ',');
    char *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
    char *c3 = c2 ? strchr(c2 + 1, ',') : NULL;
    if (!c3) { Serial.println("ERR args"); return; }
    *c1 = '\0'; *c2 = '\0'; *c3 = '\0';
    transmitFrame(strtoul(line + 1, NULL, 0), strtoul(c1 + 1, NULL, 0),
                  strtoul(c2 + 1, NULL, 0), atoi(c3 + 1));
  } else if (line[0] == 'A') {
    receive(atoi(line + 1));
  } else if (line[0] == 'Z') {
    release();
    Serial.println("RELEASED");
  } else {
    Serial.println("ERR cmd");
  }
}
