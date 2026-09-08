// E027 captures through repeated PSRAM ring wraps before a rising-edge trigger.
#include <Arduino.h>

void copy_to_capture_ring(uint8_t *destination, const uint8_t *data,
                          size_t length, size_t offset);
void report_capture_ring(size_t run, size_t copied);

#define EXPERIMENT_ID "E027"
#define SAMPLE_RATE_HZ 20000000
#define TRIGGER_MODE_FOR_RUN(run) 1
#define TRIGGER_MIN_INDEX_FOR_RUN(run) \
  (((1U << (run)) * 1024U + 256U) * 1024U)
#define POST_SAMPLES_FOR_RUN(run) (256U * 1024U)
#define CAPTURE_COMPLETE(copied)                                      \
  (trigger_test::metrics.found &&                                    \
   (copied) >= trigger_test::metrics.first_index +                   \
                   POST_SAMPLES_FOR_RUN(trigger_test::metrics.run))
#define COPY_SIZE_FOR_CHUNK(chunk_length, copied) (chunk_length)
#define COPY_CAPTURE_CHUNK(destination, data, length, offset) \
  copy_to_capture_ring((destination), (data), (length), (offset))
#define AFTER_CAPTURE_SAMPLES(run, copied) report_capture_ring((run), (copied))
#include "../e024_p4_sump_swar_trigger_80mhz/e024_p4_sump_swar_trigger_80mhz.ino"

void copy_to_capture_ring(uint8_t *buffer, const uint8_t *data, size_t length,
                          size_t offset) {
  const size_t physical = offset % kDestinationSize;
  const size_t first = min(length, kDestinationSize - physical);
  memcpy(buffer + physical, data, first);
  if (first < length) memcpy(buffer, data + first, length - first);
}

void report_capture_ring(size_t run, size_t copied) {
  constexpr size_t kPreSamples = 256 * 1024;
  constexpr size_t kPostSamples = 256 * 1024;
  constexpr size_t kWindowSize = kPreSamples + kPostSamples;
  const size_t trigger_index = trigger_test::metrics.first_index;
  const size_t requested_stop = trigger_index + kPostSamples;
  const size_t window_start = trigger_index - kPreSamples;
  uint32_t max_error_ppm = 0;
  size_t min_edges = SIZE_MAX;
  size_t max_edges = 0;
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    const uint8_t mask = 1U << lane;
    size_t high = 0;
    size_t edges = 0;
    uint8_t previous = 0;
    for (size_t sample = 0; sample < kWindowSize; ++sample) {
      const uint8_t value = destination[(window_start + sample) % kDestinationSize];
      high += (value & mask) != 0;
      if (sample != 0 && ((previous ^ value) & mask) != 0) ++edges;
      previous = value;
    }
    const uint32_t ratio_ppm = high * 1000000ULL / kWindowSize;
    const uint32_t expected_ppm = kDuties[lane] * 1000000ULL / 256;
    const uint32_t error_ppm = ratio_ppm > expected_ppm
                                   ? ratio_ppm - expected_ppm
                                   : expected_ppm - ratio_ppm;
    max_error_ppm = max(max_error_ppm, error_ppm);
    min_edges = min(min_edges, edges);
    max_edges = max(max_edges, edges);
  }

  Serial.print("RING run=");
  Serial.print(run);
  Serial.print(" wraps=");
  Serial.print(copied / kDestinationSize);
  Serial.print(" total=");
  Serial.print(copied);
  Serial.print(" trigger=");
  Serial.print(trigger_index);
  Serial.print(" requested_stop=");
  Serial.print(requested_stop);
  Serial.print(" overshoot=");
  Serial.print(copied - requested_stop);
  Serial.print(" retained_start=");
  Serial.print(copied - kDestinationSize);
  Serial.print(" window_start=");
  Serial.print(window_start);
  Serial.print(" physical_start=");
  Serial.print(window_start % kDestinationSize);
  Serial.print(" max_error_ppm=");
  Serial.print(max_error_ppm);
  Serial.print(" min_edges=");
  Serial.print(min_edges);
  Serial.print(" max_edges=");
  Serial.println(max_edges);
}
