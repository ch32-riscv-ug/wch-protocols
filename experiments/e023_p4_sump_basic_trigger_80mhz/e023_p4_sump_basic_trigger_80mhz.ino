// E023 adds sample-by-sample SUMP-style basic trigger searches to E022.
#include <Arduino.h>
#include <esp_timer.h>

namespace trigger_test {

struct Metrics {
  size_t mode;
  bool found;
  size_t first_index;
  size_t scanned;
  int64_t scan_us;
  uint8_t previous;
  bool has_previous;
};

Metrics metrics = {};
const char *const mode_names[] = {"nomatch", "rising", "pattern"};

void before(size_t run) {
  metrics = {};
  metrics.mode = run;
}

void scan(const uint8_t *data, size_t length, size_t offset) {
  const int64_t begin = esp_timer_get_time();
  for (size_t i = 0; i < length; ++i) {
    const uint8_t sample = data[i];
    bool matched = false;
    if (metrics.mode == 0) {
      matched = (sample & 0xFFU) == 0x55U;
    } else if (metrics.mode == 1) {
      matched = metrics.has_previous && !(metrics.previous & 0x01U) &&
                (sample & 0x01U);
    } else {
      matched = (sample & 0x0FU) == 0x00U;
    }
    if (matched && !metrics.found) {
      metrics.found = true;
      metrics.first_index = offset + i;
    }
    metrics.previous = sample;
    metrics.has_previous = true;
  }
  metrics.scanned += length;
  metrics.scan_us += esp_timer_get_time() - begin;
}

void after(size_t run) {
  const uint64_t scan_mbps_milli =
      metrics.scan_us > 0 ? metrics.scanned * 1000ULL / metrics.scan_us : 0;
  Serial.print("TRIGGER run=");
  Serial.print(run);
  Serial.print(" mode=");
  Serial.print(mode_names[metrics.mode]);
  Serial.print(" found=");
  Serial.print(metrics.found);
  Serial.print(" index=");
  Serial.print(metrics.first_index);
  Serial.print(" scanned=");
  Serial.print(metrics.scanned);
  Serial.print(" scan_us=");
  Serial.print(metrics.scan_us);
  Serial.print(" scan_mbps_milli=");
  Serial.println(scan_mbps_milli);
}

}  // namespace trigger_test

#define EXPERIMENT_ID "E023"
#define SAMPLE_RATE_HZ 80000000
#define BEFORE_CAPTURE(run) trigger_test::before(run)
#define PROCESS_CHUNK(data, length, offset) \
  trigger_test::scan(data, length, offset)
#define AFTER_CAPTURE(run) trigger_test::after(run)
#include "../e021_p4_parlio_psram_spool/e021_p4_parlio_psram_spool.ino"
