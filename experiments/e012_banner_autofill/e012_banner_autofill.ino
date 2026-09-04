// E012 banner: version facts injected at build time.
//
// BANNER_GIT and BANNER_STAMP arrive as compile-time defines that conftest.py
// publishes as environment variables; build_config.toml maps them.
//
// Plan and report: README.ja.md

void setup() {
  Serial.begin(115200);
  Serial.print("# EXP E012 v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" stamp=");
  Serial.print(BANNER_STAMP);
  Serial.print(" probe=host target=none build=");
  Serial.println(__DATE__ " " __TIME__);
  Serial.println("SMOKE done");
}

void loop() {
}
