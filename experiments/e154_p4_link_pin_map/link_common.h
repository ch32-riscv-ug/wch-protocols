// E154 shared body for both boards. ROLE_NAME is defined by each .ino.
// Only the pins listed in LINK_PINS_STR are ever driven.
#pragma once
#include <Arduino.h>

static int gPins[16];
static int gPinCount = 0;

static void parsePins() {
  gPinCount = 0;
  const char *s = LINK_PINS_STR;
  while (*s && gPinCount < 16) {
    while (*s == ',' || *s == ' ') ++s;
    if (!*s) break;
    gPins[gPinCount++] = atoi(s);
    while (*s && *s != ',') ++s;
  }
}

static bool isLinkPin(int pin) {
  for (int i = 0; i < gPinCount; ++i) if (gPins[i] == pin) return true;
  return false;
}

static void releaseAll() {
  for (int i = 0; i < gPinCount; ++i) pinMode(gPins[i], INPUT_PULLDOWN);
}

static void linkSetup() {
  Serial.begin(115200);
  parsePins();
  releaseAll();
}

static void linkLoop() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line == "?") {
    Serial.printf("# EXP E154 v1 git=%s role=%s pins=%s build=%s\n", BANNER_GIT, ROLE_NAME,
                  LINK_PINS_STR, __DATE__ " " __TIME__);
    return;
  }
  if (line.length() == 0) return;  // host write() appends a newline; ignore blanks
  if (line == "Z") { releaseAll(); Serial.println("RELEASED"); return; }
  if (line.length() < 2) { Serial.println("ERR"); return; }
  const char cmd = line[0];
  const int pin = line.substring(1).toInt();
  if (!isLinkPin(pin)) { Serial.printf("ERR pin %d not in link\n", pin); return; }
  if (cmd == 'D') { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); Serial.printf("DRIVE %d=1\n", pin); }
  else if (cmd == 'L') { digitalWrite(pin, LOW); pinMode(pin, INPUT_PULLDOWN); Serial.printf("DRIVE %d=0\n", pin); }
  else if (cmd == 'R') { Serial.printf("READ %d=%d\n", pin, digitalRead(pin)); }
  else Serial.println("ERR");
}
