// E004 smoke test on real hardware, driven by a host trigger.
//
// setup() prints nothing: E002 showed the monitor attaches more than a second
// after reset, so anything printed at startup is lost. The host asks with '?'
// and the board answers, which is the minimal form of the dmibridge `hello`
// handshake.
//
// Plan and report: README.ja.md

void setup() {
  Serial.begin(115200);
}

void loop() {
  if (!Serial.available()) {
    return;
  }
  if (Serial.read() != '?') {
    return;
  }

  Serial.println("# EXP E004 v1 core=esp32:esp32 probe=s3_peer_host target=none build="
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
