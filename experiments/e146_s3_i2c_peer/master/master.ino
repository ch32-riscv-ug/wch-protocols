#include <Wire.h>

constexpr int kScl = 19;
constexpr int kSda = 20;
constexpr uint8_t kAddress = 0x42;

void setup() { Serial.begin(115200); }
void loop() {
  if (!Serial.available()) return;
  const String command = Serial.readStringUntil('\n');
  if (!command.startsWith("RUN")) { Serial.println("ERR"); return; }
  const uint32_t hz = command.length() > 4 ? command.substring(4).toInt() : 10000;
  Wire.end();
  Wire.begin(kSda, kScl, hz);
  Wire.beginTransmission(kAddress);
  Wire.write((const uint8_t *)"\x11\x22\x33\x44", 4);
  const uint8_t status = Wire.endTransmission();
  Serial.printf("MASTER hz=%lu status=%u\n", (unsigned long)hz, status);
  Wire.end();
}
