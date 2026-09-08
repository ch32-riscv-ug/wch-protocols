// E028 composes pattern, edge, count, and pattern into four trigger stages.
#include <Arduino.h>
#include <esp_timer.h>

namespace staged_trigger {

constexpr size_t kArmAfter = 256 * 1024;
constexpr size_t kPostSamples = 256 * 1024;

struct Metrics {
  size_t stage;
  size_t stage_index[4];
  size_t rising_count;
  bool found;
  size_t trigger_index;
  size_t scanned;
  int64_t scan_us;
  uint8_t previous;
  bool has_previous;
};

Metrics metrics = {};

void before(size_t) {
  metrics = {};
  for (size_t &index : metrics.stage_index) index = SIZE_MAX;
}

void scan(const uint8_t *data, size_t length, size_t offset) {
  const int64_t begin = esp_timer_get_time();
  if (!metrics.found) {
    for (size_t i = 0; i < length; ++i) {
      const uint8_t sample = data[i];
      const size_t index = offset + i;
      if (index >= kArmAfter) {
        if (metrics.stage == 0 && (sample & 0x80U) == 0x80U) {
          metrics.stage_index[0] = index;
          metrics.stage = 1;
        } else if (metrics.stage == 1 && metrics.has_previous &&
                   (metrics.previous & 0x80U) && !(sample & 0x80U)) {
          metrics.stage_index[1] = index;
          metrics.stage = 2;
        } else if (metrics.stage == 2 && metrics.has_previous &&
                   !(metrics.previous & 0x01U) && (sample & 0x01U)) {
          ++metrics.rising_count;
          if (metrics.rising_count == 4) {
            metrics.stage_index[2] = index;
            metrics.stage = 3;
          }
        } else if (metrics.stage == 3 && (sample & 0x0FU) == 0x00U) {
          metrics.stage_index[3] = index;
          metrics.stage = 4;
          metrics.found = true;
          metrics.trigger_index = index;
        }
      }
      metrics.previous = sample;
      metrics.has_previous = true;
    }
  }
  metrics.scanned += length;
  metrics.scan_us += esp_timer_get_time() - begin;
}

void after(size_t run) {
  const uint64_t scan_mbps_milli =
      metrics.scan_us > 0 ? metrics.scanned * 1000ULL / metrics.scan_us : 0;
  Serial.print("STAGED run=");
  Serial.print(run);
  Serial.print(" found=");
  Serial.print(metrics.found);
  Serial.print(" stage=");
  Serial.print(metrics.stage);
  Serial.print(" index0=");
  Serial.print(metrics.stage_index[0]);
  Serial.print(" index1=");
  Serial.print(metrics.stage_index[1]);
  Serial.print(" index2=");
  Serial.print(metrics.stage_index[2]);
  Serial.print(" index3=");
  Serial.print(metrics.stage_index[3]);
  Serial.print(" rising_count=");
  Serial.print(metrics.rising_count);
  Serial.print(" scanned=");
  Serial.print(metrics.scanned);
  Serial.print(" scan_us=");
  Serial.print(metrics.scan_us);
  Serial.print(" scan_mbps_milli=");
  Serial.println(scan_mbps_milli);
}

}  // namespace staged_trigger

#define EXPERIMENT_ID "E028"
#define SAMPLE_RATE_HZ 16000000
#define BEFORE_CAPTURE(run) staged_trigger::before(run)
#define PROCESS_CHUNK(data, length, offset) \
  staged_trigger::scan((data), (length), (offset))
#define AFTER_CAPTURE(run) staged_trigger::after(run)
#define CAPTURE_COMPLETE(copied)                                       \
  (staged_trigger::metrics.found &&                                   \
   (copied) >= staged_trigger::metrics.trigger_index +                \
                   staged_trigger::kPostSamples)
#include "../e021_p4_parlio_psram_spool/e021_p4_parlio_psram_spool.ino"
