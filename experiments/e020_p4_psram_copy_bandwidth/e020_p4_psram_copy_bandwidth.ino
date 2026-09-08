// E020: sustained CPU memcpy bandwidth between internal RAM and PSRAM.
// Plan and report: README.ja.md

#include <Arduino.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_private/esp_cache_private.h>
#include <esp_timer.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif

namespace {

constexpr size_t kTransferSize = 8 * 1024 * 1024;
constexpr size_t kMaxChunkSize = 64 * 1024;
constexpr size_t kChunkCount = 3;
constexpr size_t kChunks[kChunkCount] = {4 * 1024, 16 * 1024, 64 * 1024};
constexpr size_t kRuns = 3;

uint8_t *internal_buffer = nullptr;
uint8_t *psram_buffer = nullptr;

void fill_pattern(size_t size) {
  for (size_t i = 0; i < size; ++i) {
    internal_buffer[i] = static_cast<uint8_t>((i * 29U + (i >> 8U) * 17U + 0x53U) & 0xFFU);
  }
}

void run_case(size_t chunk_size, size_t run) {
  fill_pattern(chunk_size);

  const int64_t write_begin = esp_timer_get_time();
  for (size_t offset = 0; offset < kTransferSize; offset += chunk_size) {
    memcpy(psram_buffer + offset, internal_buffer, chunk_size);
  }
  const int64_t write_copy_us = esp_timer_get_time() - write_begin;

  const int64_t flush_begin = esp_timer_get_time();
  const esp_err_t flush_result = esp_cache_msync(
      psram_buffer, kTransferSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  const int64_t flush_us = esp_timer_get_time() - flush_begin;

  size_t write_mismatch_chunks = 0;
  if (flush_result == ESP_OK) {
    const esp_err_t invalidate_result = esp_cache_msync(
        psram_buffer, kTransferSize, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    if (invalidate_result == ESP_OK) {
      for (size_t offset = 0; offset < kTransferSize; offset += chunk_size) {
        if (memcmp(psram_buffer + offset, internal_buffer, chunk_size) != 0) {
          ++write_mismatch_chunks;
        }
      }
    } else {
      write_mismatch_chunks = kTransferSize / chunk_size;
    }
  } else {
    write_mismatch_chunks = kTransferSize / chunk_size;
  }

  const esp_err_t invalidate_result = esp_cache_msync(
      psram_buffer, kTransferSize, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  size_t read_mismatch_chunks = 0;
  volatile uint32_t read_sink = 0;
  const int64_t read_begin = esp_timer_get_time();
  if (invalidate_result == ESP_OK) {
    for (size_t offset = 0; offset < kTransferSize; offset += chunk_size) {
      memcpy(internal_buffer, psram_buffer + offset, chunk_size);
      read_sink += internal_buffer[(offset / chunk_size) % chunk_size];
    }
  }
  const int64_t read_us = esp_timer_get_time() - read_begin;

  if (invalidate_result == ESP_OK) {
    fill_pattern(chunk_size);
    for (size_t offset = 0; offset < kTransferSize; offset += chunk_size) {
      if (memcmp(psram_buffer + offset, internal_buffer, chunk_size) != 0) {
        ++read_mismatch_chunks;
      }
    }
  } else {
    read_mismatch_chunks = kTransferSize / chunk_size;
  }

  const uint64_t write_copy_mbps_milli =
      kTransferSize * 1000ULL / write_copy_us;
  const uint64_t write_total_mbps_milli =
      kTransferSize * 1000ULL / (write_copy_us + flush_us);
  const uint64_t read_mbps_milli = kTransferSize * 1000ULL / read_us;

  Serial.print("CASE chunk=");
  Serial.print(chunk_size);
  Serial.print(" run=");
  Serial.print(run);
  Serial.print(" flush=");
  Serial.print(esp_err_to_name(flush_result));
  Serial.print(" invalidate=");
  Serial.print(esp_err_to_name(invalidate_result));
  Serial.print(" write_copy_us=");
  Serial.print(write_copy_us);
  Serial.print(" flush_us=");
  Serial.print(flush_us);
  Serial.print(" write_copy_mbps_milli=");
  Serial.print(write_copy_mbps_milli);
  Serial.print(" write_total_mbps_milli=");
  Serial.print(write_total_mbps_milli);
  Serial.print(" read_us=");
  Serial.print(read_us);
  Serial.print(" read_mbps_milli=");
  Serial.print(read_mbps_milli);
  Serial.print(" write_mismatch_chunks=");
  Serial.print(write_mismatch_chunks);
  Serial.print(" read_mismatch_chunks=");
  Serial.print(read_mismatch_chunks);
  Serial.print(" sink=");
  Serial.println(read_sink);
}

void run_experiment() {
  Serial.print("# EXP E020 v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4 target=memory build=");
  Serial.println(__DATE__ " " __TIME__);

  size_t external_alignment = 0;
  const esp_err_t alignment_result =
      esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &external_alignment);
  Serial.print("ENV psram_found=");
  Serial.print(psramFound());
  Serial.print(" psram_size=");
  Serial.print(ESP.getPsramSize());
  Serial.print(" psram_free=");
  Serial.print(ESP.getFreePsram());
  Serial.print(" alignment_result=");
  Serial.print(esp_err_to_name(alignment_result));
  Serial.print(" ext_align=");
  Serial.println(external_alignment);
  if (!psramFound() || alignment_result != ESP_OK) {
    Serial.println("DONE status=environment-failed");
    return;
  }

  internal_buffer = static_cast<uint8_t *>(heap_caps_malloc(
      kMaxChunkSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  psram_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      external_alignment, kTransferSize,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  Serial.print("BUFFERS internal=");
  Serial.print(reinterpret_cast<uintptr_t>(internal_buffer), HEX);
  Serial.print(" psram=");
  Serial.print(reinterpret_cast<uintptr_t>(psram_buffer), HEX);
  Serial.print(" psram_aligned=");
  Serial.print(psram_buffer != nullptr &&
               reinterpret_cast<uintptr_t>(psram_buffer) % external_alignment == 0);
  Serial.print(" psram_external=");
  Serial.println(psram_buffer != nullptr && esp_ptr_external_ram(psram_buffer));
  if (internal_buffer == nullptr || psram_buffer == nullptr) {
    free(internal_buffer);
    free(psram_buffer);
    Serial.println("DONE status=alloc-failed");
    return;
  }

  for (size_t chunk_index = 0; chunk_index < kChunkCount; ++chunk_index) {
    for (size_t run = 0; run < kRuns; ++run) {
      run_case(kChunks[chunk_index], run);
    }
  }
  free(internal_buffer);
  free(psram_buffer);
  internal_buffer = nullptr;
  psram_buffer = nullptr;
  Serial.println("DONE status=ok");
}

}  // namespace

void setup() {
  Serial.begin(115200);
}

void loop() {
  if (Serial.available() > 0 && Serial.read() == '?') {
    run_experiment();
  }
  delay(1);
}
