// E001 harness smoke test: no hardware involved.
// Prints one banner line, then three known lines, then stops.
// Plan and report: README.ja.md

void setup() {
  Serial.begin(115200);
  Serial.println("# EXP E001 v1 core=lang-ship:host probe=host target=none build="
                 __DATE__ " " __TIME__);
  Serial.println("SMOKE A");
  Serial.println("SMOKE B");
  Serial.println("SMOKE C");
  Serial.println("SMOKE done");
}

void loop() {
}
