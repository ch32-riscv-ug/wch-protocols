// E025 sweeps the E024 software trigger across five sample rates.
#include <Arduino.h>

#define EXPERIMENT_ID "E025"
#define EXPERIMENT_RUNS 15
#define TRIGGER_MODE_FOR_RUN(run) ((run) % 3)
#define SAMPLE_RATE_FOR_RUN(run) (16000000U + ((run) / 3) * 4000000U)
#define BEFORE_CAPTURE(run)          \
  do {                               \
    trigger_test::before(run);       \
    Serial.print("RATE run=");       \
    Serial.print(run);               \
    Serial.print(" sample_rate_hz="); \
    Serial.println(SAMPLE_RATE_FOR_RUN(run)); \
  } while (0)
#include "../e024_p4_sump_swar_trigger_80mhz/e024_p4_sump_swar_trigger_80mhz.ino"
