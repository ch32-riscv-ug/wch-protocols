// E003 peer environment check: discover which pins are wired between the two
// boards, without assuming a mapping.
//
// Role: peer. Both boards run the same logic; the host drives one side and reads
// the other. Commands are line based:
//   D<n>  drive pin n high      L<n>  drive pin n low
//   R<n>  read pin n (pull-down)  Z  release every candidate pin
//   ?     identify
// Every step is host-orchestrated, so nothing depends on attach timing
// (rules section 7-10). Inputs use a pull-down, so an unconnected pin always
// reads 0 and a READ=1 is real evidence of a connection.
//
// Candidate pins arrive as a compile-time define from .env (a quoted list).
//
// Plan and report: ../README.ja.md

static const char kPins[] = PEER_PINS;
static int gPins[8];
static int gPinCount = 0;

static bool known(int pin) {
  for (int i = 0; i < gPinCount; i++) {
    if (gPins[i] == pin) return true;
  }
  return false;
}

static void releaseAll() {
  for (int i = 0; i < gPinCount; i++) {
    pinMode(gPins[i], INPUT_PULLDOWN);
  }
}

void setup() {
  Serial.begin(115200);
  int value = -1;
  for (const char *p = kPins;; p++) {
    if (*p >= '0' && *p <= '9') {
      value = (value < 0 ? 0 : value * 10) + (*p - '0');
    } else {
      if (value >= 0 && gPinCount < 8) gPins[gPinCount++] = value;
      value = -1;
      if (*p == '\0') break;
    }
  }
  releaseAll();
}

void loop() {
  static char line[16];
  static size_t len = 0;

  if (!Serial.available()) return;
  char c = Serial.read();
  if (c != '\n' && c != '\r') {
    if (len < sizeof(line) - 1) line[len++] = c;
    return;
  }
  if (len == 0) return;
  line[len] = '\0';
  char cmd = line[0];
  int pin = atoi(line + 1);
  len = 0;

  switch (cmd) {
    case '?':
      Serial.print("# EXP E003 v2 role=peer pins=");
      for (int i = 0; i < gPinCount; i++) {
        if (i) Serial.print(',');
        Serial.print(gPins[i]);
      }
      Serial.println(" build=" __DATE__ " " __TIME__);
      break;
    case 'D':
    case 'L':
      if (!known(pin)) { Serial.println("ERR pin"); break; }
      pinMode(pin, OUTPUT);
      digitalWrite(pin, cmd == 'D' ? HIGH : LOW);
      Serial.print("DRIVE ");
      Serial.print(pin);
      Serial.println(cmd == 'D' ? "=1" : "=0");
      break;
    case 'R':
      if (!known(pin)) { Serial.println("ERR pin"); break; }
      pinMode(pin, INPUT_PULLDOWN);
      Serial.print("READ ");
      Serial.print(pin);
      Serial.print('=');
      Serial.println(digitalRead(pin));
      break;
    case 'Z':
      releaseAll();
      Serial.println("RELEASED");
      break;
    default:
      Serial.println("ERR cmd");
      break;
  }
}
