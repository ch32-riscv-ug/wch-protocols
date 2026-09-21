#include <Wire.h>
constexpr int kScl=33,kSda=32; constexpr uint8_t kAddress=0x42; volatile uint16_t bytes; volatile uint8_t length;
void received(int n){length=n;while(Wire.available()){Wire.read();++bytes;}}
void setup(){
  Serial.begin(115200);
  pinMode(kSda, INPUT_PULLUP);
  pinMode(kScl, INPUT_PULLUP);
  delay(2);
  Serial.printf("SLAVE LINES before sda=%d scl=%d\n", digitalRead(kSda), digitalRead(kScl));
  Wire.onReceive(received);
  bool begun = Wire.begin(kAddress,kSda,kScl,100000);
  Serial.printf("SLAVE READY begin=%d\n", begun);
}
void loop() {
  static uint32_t deadline;
  if (millis()-deadline<250) return;
  deadline=millis();
  Serial.printf("SLAVE bytes=%u length=%u\n",bytes,length);
}
