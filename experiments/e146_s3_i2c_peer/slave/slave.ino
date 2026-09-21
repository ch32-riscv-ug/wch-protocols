#include <Wire.h>

constexpr int kScl = 19;
constexpr int kSda = 20;
constexpr uint8_t kAddress = 0x42;
volatile uint16_t received = 0;
volatile uint8_t last_length = 0;

void onReceive(int length) {
  last_length = length;
  while (Wire.available()) { Wire.read(); ++received; }
}
void setup() {
  Serial.begin(115200);
  Wire.onReceive(onReceive);
  Wire.begin(kAddress, kSda, kScl, 100000);
  Serial.println("SLAVE READY");
}
void loop() {
  static uint32_t next;
  if (millis() - next < 250) return;
  next = millis();
  Serial.printf("SLAVE bytes=%u length=%u\n", received, last_length);
}
