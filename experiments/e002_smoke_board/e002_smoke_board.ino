// E002 smoke test on real hardware (standing bench v1).
// Prints one banner line, three known lines, and one real-clock measurement,
// then a heartbeat so a missed startup burst can be told apart from a dead board.
// Plan and report: README.ja.md

void setup() {
  Serial.begin(115200);
  Serial.println("# EXP E002 v1 core=esp32:esp32 probe=s3_peer_host target=none build="
                 __DATE__ " " __TIME__);
  Serial.println("SMOKE A");
  Serial.println("SMOKE B");
  Serial.println("SMOKE C");

  uint32_t t0 = micros();
  delayMicroseconds(1000);
  uint32_t t1 = micros();
  Serial.print("CLOCK delta=");
  Serial.println(t1 - t0);

  Serial.println("SMOKE done");
}

void loop() {
  static uint32_t n = 0;
  Serial.print("HEARTBEAT ");
  Serial.println(n++);
  delay(1000);
}
