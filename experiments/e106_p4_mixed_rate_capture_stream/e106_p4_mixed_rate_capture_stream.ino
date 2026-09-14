// E106: continuous 16-bit/48 Msps PARLIO capture, mixed-rate encode, then USB HS.
// The internal 8-bit PARLIO source is mirrored into RX lanes 8..15.  This
// validates the 16-bit capture/codec/USB path, not eleven independent pads.

#include <Arduino.h>
#include "EspUsbDevice.h"

#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <driver/parlio_tx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace {

constexpr uint32_t kSampleRateHz = 40000000;
constexpr size_t kSourceLanes = 8;
constexpr size_t kCaptureWidth = 16;
constexpr size_t kBlockSamples = 64;
constexpr size_t kRawBlockBytes = kBlockSamples * 2;
constexpr size_t kWireBlockBytes = 25;
constexpr size_t kRingBytes = 64 * 1024;
constexpr size_t kDelimiterBytes = 65408;
constexpr size_t kQueueDepth = 128;
constexpr size_t kSourceBytes = 8192;
constexpr uint32_t kSourceDivider = 4;
constexpr size_t kFifoBytes = 8 * 1024 * 1024;
constexpr size_t kStageBytes = 12800;  // 512 codec blocks and one USB period
constexpr size_t kStageCount = 4;
constexpr size_t kUsbPrebufferBytes = 1024 * 1024;
constexpr uint32_t kWaitMs = 1000;
constexpr uint32_t kMaxTimeouts = 10;
constexpr uint16_t kVid = 0x303a;
constexpr uint16_t kPid = 0x4021;
constexpr int kPins[kSourceLanes] = {2, 3, 4, 5, 6, 7, 8, 9};

struct Chunk {
  const uint8_t *data;
  size_t length;
};

struct WireChunk {
  uint8_t *data;
  size_t length;
};

struct Fifo {
  uint8_t *data;
  volatile size_t head;
  volatile size_t tail;
  volatile size_t highWater;
  volatile uint32_t overflow;
};

EspUsbDevice Device;
EspUsbDeviceVendor Vendor(Device, 512);
Fifo FifoState = {};
QueueHandle_t ChunkQueue;
SemaphoreHandle_t HarvestDone;
SemaphoreHandle_t SpoolDone;
SemaphoreHandle_t UsbDone;
QueueHandle_t FreeStageQueue;
QueueHandle_t ReadyStageQueue;
uint8_t *Ring;
uint8_t *Source;
uint16_t Pending[kBlockSamples] __attribute__((aligned(64)));
uint8_t BenchWire[kWireBlockBytes] __attribute__((aligned(64)));
uint8_t Stage[kStageCount][kStageBytes] __attribute__((aligned(64)));
// PARLIO's DMA ring is accessed uncached. One linear copy makes the many
// scattered codec loads hit cached internal RAM; E042 already established that
// a linear ring copy can sustain 96 MB/s.
uint8_t Scratch[kDelimiterBytes] __attribute__((aligned(64)));

volatile bool Active;
volatile uint64_t TargetBlocks;
volatile uint64_t EncodedBlocks;
volatile uint64_t SentBytes;
volatile uint64_t CallbackBytes;
volatile uint32_t QueueOverflow;
volatile uint32_t UsbWaits;
volatile uint32_t UsbTimeouts;
volatile int64_t CaptureUs;
volatile int64_t UsbUs;
volatile uint64_t CopyUs;
volatile uint64_t ProcessUs;
volatile uint64_t PushUs;
volatile uint32_t HarvestChunks;
volatile uint32_t RawSequenceBad;
volatile uint32_t DuplicateBad;
uint64_t BenchUs;
uint32_t BenchChecksum;
bool UsbReady;
volatile bool RunPending;
char CommandMode;

static inline __attribute__((always_inline)) void packFast8(
    const uint16_t *__restrict input, uint8_t *__restrict output) {
  // Ring, chunk, block and pending-buffer boundaries are all 64-byte aligned.
  // An explicit aligned load is essential here: memcpy(4) only folded to lw
  // for E105's fixed global input, but remained 32 real function calls/block
  // for a runtime DMA-ring pointer.
  const auto *pairs = reinterpret_cast<const uint32_t *>(input);
  const uint32_t p0 = pairs[0];
  const uint32_t p1 = pairs[1];
  const uint32_t p2 = pairs[2];
  const uint32_t p3 = pairs[3];
  const uint32_t packed = ((p0 & 7U) << 0) |
      (((p0 >> 16) & 7U) << 3) | ((p1 & 7U) << 6) |
      (((p1 >> 16) & 7U) << 9) | ((p2 & 7U) << 12) |
      (((p2 >> 16) & 7U) << 15) | ((p3 & 7U) << 18) |
      (((p3 >> 16) & 7U) << 21);
  output[0] = packed;
  output[1] = packed >> 8;
  output[2] = packed >> 16;
}

static inline __attribute__((always_inline)) void encodeBlock(
    const uint16_t *samples, uint8_t *wire) {
  for (size_t group = 0; group < 8; ++group) {
    packFast8(samples + group * 8, wire + group * 3);
  }
  // D=64 hold: lanes 3..10 at the first base sample share one byte.
  wire[24] = static_cast<uint8_t>(samples[0] >> 3);
}

static size_t fifoUsed() { return FifoState.head - FifoState.tail; }

static bool fifoPush(const uint8_t *data, size_t length) {
  const int64_t began = esp_timer_get_time();
  if (kFifoBytes - fifoUsed() < length) {
    ++FifoState.overflow;
    PushUs += esp_timer_get_time() - began;
    return false;
  }
  const size_t offset = FifoState.head % kFifoBytes;
  const size_t first = min(length, kFifoBytes - offset);
  memcpy(FifoState.data + offset, data, first);
  if (length > first) memcpy(FifoState.data, data + first, length - first);
  FifoState.head += length;
  const size_t used = fifoUsed();
  if (used > FifoState.highWater) FifoState.highWater = used;
  PushUs += esp_timer_get_time() - began;
  return true;
}

static bool IRAM_ATTR onPartialReceive(
    parlio_rx_unit_handle_t, const parlio_rx_event_data_t *event, void *) {
  const Chunk chunk = {static_cast<const uint8_t *>(event->data), event->recv_bytes};
  CallbackBytes += event->recv_bytes;
  BaseType_t wake = pdFALSE;
  if (xQueueSendFromISR(ChunkQueue, &chunk, &wake) != pdTRUE) ++QueueOverflow;
  return wake == pdTRUE;
}

static esp_err_t createSource(parlio_tx_unit_handle_t *unit) {
  parlio_tx_unit_config_t c = {};
  c.clk_src = PARLIO_CLK_SRC_DEFAULT;
  c.clk_in_gpio_num = GPIO_NUM_NC;
  c.output_clk_freq_hz = kSampleRateHz / kSourceDivider;
  c.data_width = kSourceLanes;
  for (size_t i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; ++i) {
    c.data_gpio_nums[i] = i < kSourceLanes ? gpio_num_t(kPins[i]) : GPIO_NUM_NC;
  }
  c.clk_out_gpio_num = GPIO_NUM_NC;
  c.valid_gpio_num = GPIO_NUM_NC;
  c.trans_queue_depth = 1;
  c.max_transfer_size = kSourceBytes;
  c.sample_edge = PARLIO_SAMPLE_EDGE_NEG;
  c.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  c.flags.clk_gate_en = false;
  return parlio_new_tx_unit(&c, unit);
}

static esp_err_t createReceiver(parlio_rx_unit_handle_t *unit,
                                parlio_rx_delimiter_handle_t *delimiter) {
  parlio_rx_unit_config_t c = {};
  c.trans_queue_depth = 1;
  c.max_recv_size = kRingBytes;
  c.data_width = kCaptureWidth;
  c.clk_src = PARLIO_CLK_SRC_DEFAULT;
  c.exp_clk_freq_hz = kSampleRateHz;
  c.clk_in_gpio_num = GPIO_NUM_NC;
  c.clk_out_gpio_num = GPIO_NUM_NC;
  c.valid_gpio_num = GPIO_NUM_NC;
  for (size_t i = 0; i < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++i) {
    c.data_gpio_nums[i] = i < kCaptureWidth ? gpio_num_t(kPins[i % kSourceLanes]) : GPIO_NUM_NC;
  }
  esp_err_t result = parlio_new_rx_unit(&c, unit);
  if (result != ESP_OK) return result;
  parlio_rx_event_callbacks_t callbacks = {};
  callbacks.on_partial_receive = onPartialReceive;
  result = parlio_rx_unit_register_event_callbacks(*unit, &callbacks, nullptr);
  if (result != ESP_OK) return result;
  parlio_rx_soft_delimiter_config_t d = {};
  d.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  d.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  d.eof_data_len = kDelimiterBytes;
  return parlio_new_rx_soft_delimiter(&d, delimiter);
}

static void harvestTask(void *) {
  uint8_t *stage = nullptr;
  xQueueReceive(FreeStageQueue, &stage, portMAX_DELAY);
  size_t staged = 0;
  size_t pending = 0;
  uint32_t idle = 0;
  uint64_t encoded = 0;
  const uint64_t target = TargetBlocks;
  const int64_t taskBegan = esp_timer_get_time();
  bool previousValid = false;
  uint8_t previousBinary = 0;
  uint32_t rawSequenceBad = 0;
  uint32_t duplicateBad = 0;
  const auto recordRawBlock = [&](const uint16_t *samples) {
    const uint16_t sample = samples[0];
    const uint8_t low = static_cast<uint8_t>(sample);
    if (static_cast<uint8_t>(sample >> 8) != low) ++duplicateBad;
    uint8_t binary = low;
    binary ^= binary >> 1;
    binary ^= binary >> 2;
    binary ^= binary >> 4;
    if (previousValid) {
      const uint8_t delta = static_cast<uint8_t>(binary - previousBinary);
      if (delta < 15 || delta > 17) ++rawSequenceBad;
    }
    previousBinary = binary;
    previousValid = true;
  };
  while (encoded < target) {
    Chunk chunk = {};
    if (xQueueReceive(ChunkQueue, &chunk, pdMS_TO_TICKS(200)) != pdTRUE) {
      if (++idle >= 10) break;
      continue;
    }
    idle = 0;
    size_t at = 0;
    if (pending != 0) {
      const size_t take = min(kRawBlockBytes - pending, chunk.length);
      memcpy(reinterpret_cast<uint8_t *>(Pending) + pending, chunk.data, take);
      pending += take;
      at += take;
      if (pending == kRawBlockBytes) {
        recordRawBlock(Pending);
        encodeBlock(Pending, stage + staged);
        staged += kWireBlockBytes;
        pending = 0;
        ++encoded;
        if (staged == kStageBytes) {
          const WireChunk ready = {stage, staged};
          xQueueSend(ReadyStageQueue, &ready, portMAX_DELAY);
          xQueueReceive(FreeStageQueue, &stage, portMAX_DELAY);
          staged = 0;
        }
      }
    }
    while (at + kRawBlockBytes <= chunk.length && encoded < target) {
      const auto *samples = reinterpret_cast<const uint16_t *>(chunk.data + at);
      recordRawBlock(samples);
      encodeBlock(samples, stage + staged);
      staged += kWireBlockBytes;
      at += kRawBlockBytes;
      ++encoded;
      if (staged == kStageBytes) {
        const WireChunk ready = {stage, staged};
        xQueueSend(ReadyStageQueue, &ready, portMAX_DELAY);
        xQueueReceive(FreeStageQueue, &stage, portMAX_DELAY);
        staged = 0;
      }
    }
    while (at < chunk.length && encoded < target) {
      const size_t take = min(kRawBlockBytes - pending, chunk.length - at);
      memcpy(reinterpret_cast<uint8_t *>(Pending) + pending, chunk.data + at, take);
      pending += take;
      at += take;
      if (pending != kRawBlockBytes) continue;
      recordRawBlock(Pending);
      encodeBlock(Pending, stage + staged);
      staged += kWireBlockBytes;
      pending = 0;
      ++encoded;
      if (staged == kStageBytes) {
        const WireChunk ready = {stage, staged};
        xQueueSend(ReadyStageQueue, &ready, portMAX_DELAY);
        xQueueReceive(FreeStageQueue, &stage, portMAX_DELAY);
        staged = 0;
      }
    }
    ++HarvestChunks;
  }
  if (staged != 0) {
    const WireChunk ready = {stage, staged};
    xQueueSend(ReadyStageQueue, &ready, portMAX_DELAY);
  } else {
    xQueueSend(FreeStageQueue, &stage, portMAX_DELAY);
  }
done:
  ProcessUs = esp_timer_get_time() - taskBegan;
  EncodedBlocks = encoded;
  RawSequenceBad = rawSequenceBad;
  DuplicateBad = duplicateBad;
  xSemaphoreGive(HarvestDone);
  vTaskDelete(nullptr);
}

static void spoolTask(void *) {
  const uint64_t target = TargetBlocks * kWireBlockBytes;
  uint64_t moved = 0;
  while (moved < target) {
    WireChunk chunk = {};
    if (xQueueReceive(ReadyStageQueue, &chunk, pdMS_TO_TICKS(1000)) != pdTRUE) break;
    if (!fifoPush(chunk.data, chunk.length)) break;
    moved += chunk.length;
    xQueueSend(FreeStageQueue, &chunk.data, portMAX_DELAY);
  }
  xSemaphoreGive(SpoolDone);
  vTaskDelete(nullptr);
}

static void usbTask(void *) {
  const uint64_t target = TargetBlocks * kWireBlockBytes;
  uint64_t sent = 0;
  uint32_t consecutiveTimeouts = 0;
  const int64_t began = esp_timer_get_time();
  const size_t tx = EspUsbDeviceVendor::writeCapacity();
  while (fifoUsed() < min(kUsbPrebufferBytes, static_cast<size_t>(target)) && Active) {
    vTaskDelay(1);
  }
  while (sent < target) {
    const size_t used = fifoUsed();
    if (used == 0) {
      if (!Active) break;
      vTaskDelay(1);
      continue;
    }
    size_t span = min(used, tx);
    span = min(span, static_cast<size_t>(target - sent));
    const size_t offset = FifoState.tail % kFifoBytes;
    span = min(span, kFifoBytes - offset);
    if (Vendor.writeAvailable() < span) {
      ++UsbWaits;
      if (!Vendor.waitWritable(span, kWaitMs)) {
        ++UsbTimeouts;
        if (!Vendor.mounted() || ++consecutiveTimeouts > kMaxTimeouts) break;
        continue;
      }
    }
    consecutiveTimeouts = 0;
    const size_t wrote = Vendor.write(FifoState.data + offset, span);
    if (wrote == 0) {
      vTaskDelay(1);
      continue;
    }
    FifoState.tail += wrote;
    sent += wrote;
  }
  Vendor.flush();
  SentBytes = sent;
  UsbUs = esp_timer_get_time() - began;
  xSemaphoreGive(UsbDone);
  vTaskDelete(nullptr);
}

static esp_err_t runCapture(uint64_t blocks) {
  TargetBlocks = blocks;
  EncodedBlocks = SentBytes = CallbackBytes = 0;
  QueueOverflow = UsbWaits = UsbTimeouts = 0;
  CaptureUs = UsbUs = 0;
  CopyUs = ProcessUs = PushUs = 0;
  HarvestChunks = 0;
  RawSequenceBad = DuplicateBad = 0;
  FifoState.head = FifoState.tail = FifoState.highWater = FifoState.overflow = 0;
  xQueueReset(ChunkQueue);
  xQueueReset(FreeStageQueue);
  xQueueReset(ReadyStageQueue);
  for (size_t i = 0; i < kStageCount; ++i) {
    uint8_t *slot = Stage[i];
    xQueueSend(FreeStageQueue, &slot, 0);
  }
  memset(Ring, 0xa5, kRingBytes);
  esp_cache_msync(Ring, kRingBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  parlio_tx_unit_handle_t txUnit = nullptr;
  parlio_rx_unit_handle_t rxUnit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  esp_err_t result = createSource(&txUnit);
  if (result == ESP_OK) result = parlio_tx_unit_enable(txUnit);
  if (result == ESP_OK) {
    parlio_transmit_config_t t = {};
    t.flags.loop_transmission = true;
    result = parlio_tx_unit_transmit(txUnit, Source, kSourceBytes * 8, &t);
  }
  if (result == ESP_OK) result = createReceiver(&rxUnit, &delimiter);
  if (result == ESP_OK) result = parlio_rx_unit_enable(rxUnit, true);
  if (result == ESP_OK) {
    parlio_receive_config_t r = {};
    r.delimiter = delimiter;
    r.flags.partial_rx_en = true;
    result = parlio_rx_unit_receive(rxUnit, Ring, kRingBytes, &r);
  }
  if (result != ESP_OK) goto cleanup;

  Active = true;
  // runCapture itself is pinned to core 0, so the RX interrupt allocated by
  // createReceiver lives there. The codec is isolated on core 1.
  xTaskCreatePinnedToCore(usbTask, "e106_usb", 4096, nullptr, 5, nullptr, 0);
  xTaskCreatePinnedToCore(spoolTask, "e106_spool", 4096, nullptr, 5, nullptr, 0);
  xTaskCreatePinnedToCore(harvestTask, "e106_codec", 4096, nullptr, 5, nullptr, 1);
  {
    const int64_t began = esp_timer_get_time();
    result = parlio_rx_soft_delimiter_start_stop(rxUnit, delimiter, true);
    if (result == ESP_OK) xSemaphoreTake(HarvestDone, portMAX_DELAY);
    CaptureUs = esp_timer_get_time() - began;
  }
  parlio_rx_soft_delimiter_start_stop(rxUnit, delimiter, false);
  parlio_rx_unit_disable(rxUnit);
  xSemaphoreTake(SpoolDone, portMAX_DELAY);
  Active = false;
  xSemaphoreTake(UsbDone, portMAX_DELAY);

cleanup:
  Active = false;
  if (delimiter) parlio_del_rx_delimiter(delimiter);
  if (rxUnit) parlio_del_rx_unit(rxUnit);
  if (txUnit) {
    parlio_tx_unit_disable(txUnit);
    parlio_del_tx_unit(txUnit);
  }
  for (int pin : kPins) gpio_reset_pin(gpio_num_t(pin));
  return result;
}

static void sendStatus(esp_err_t result) {
  char status[512];
  const int length = snprintf(
      status, sizeof(status),
      "E106_STATUS result=%s blocks=%llu encoded=%llu sent=%llu callbacks=%llu "
      "queue_overflow=%lu fifo_overflow=%lu high_water=%lu waits=%lu timeouts=%lu "
      "capture_us=%lld usb_us=%lld chunks=%lu copy_us=%llu process_us=%llu push_us=%llu "
      "raw_sequence_bad=%lu duplicate_bad=%lu bench_us=%llu bench_checksum=%lu\n",
      esp_err_to_name(result), static_cast<unsigned long long>(TargetBlocks),
      static_cast<unsigned long long>(EncodedBlocks), static_cast<unsigned long long>(SentBytes),
      static_cast<unsigned long long>(CallbackBytes), static_cast<unsigned long>(QueueOverflow),
      static_cast<unsigned long>(FifoState.overflow), static_cast<unsigned long>(FifoState.highWater),
      static_cast<unsigned long>(UsbWaits), static_cast<unsigned long>(UsbTimeouts),
      static_cast<long long>(CaptureUs), static_cast<long long>(UsbUs),
      static_cast<unsigned long>(HarvestChunks), static_cast<unsigned long long>(CopyUs),
      static_cast<unsigned long long>(ProcessUs), static_cast<unsigned long long>(PushUs),
      static_cast<unsigned long>(RawSequenceBad), static_cast<unsigned long>(DuplicateBad),
      static_cast<unsigned long long>(BenchUs), static_cast<unsigned long>(BenchChecksum));
  Serial.write(reinterpret_cast<const uint8_t *>(status), length);
  Serial.flush();
  Vendor.waitWritable(length, kWaitMs);
  Vendor.write(reinterpret_cast<const uint8_t *>(status), length);
}

static uint64_t readLe64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= uint64_t(p[i]) << (8 * i);
  return value;
}

static void runUsbProbe(uint64_t target) {
  for (size_t i = 0; i < kStageBytes; ++i) Stage[0][i] = static_cast<uint8_t>(i);
  uint64_t sent = 0;
  const size_t tx = EspUsbDeviceVendor::writeCapacity();
  const int64_t began = esp_timer_get_time();
  while (sent < target) {
    const size_t phase = static_cast<size_t>(sent % kStageBytes);
    size_t span = min(tx, kStageBytes - phase);
    span = min(span, static_cast<size_t>(target - sent));
    if (!Vendor.waitWritable(span, kWaitMs)) break;
    sent += Vendor.write(Stage[0] + phase, span);
  }
  Vendor.flush();
  const int64_t elapsed = esp_timer_get_time() - began;
  char status[192];
  const int length = snprintf(
      status, sizeof(status), "E106_PROBE bytes=%llu target=%llu elapsed_us=%lld\n",
      static_cast<unsigned long long>(sent), static_cast<unsigned long long>(target),
      static_cast<long long>(elapsed));
  Serial.write(reinterpret_cast<const uint8_t *>(status), length);
  Vendor.waitWritable(length, kWaitMs);
  Vendor.write(reinterpret_cast<const uint8_t *>(status), length);
}

static void benchmarkCodec() {
  for (size_t i = 0; i < kBlockSamples; ++i) Pending[i] = static_cast<uint16_t>(i * 257U);
  uint32_t checksum = 0;
  const int64_t began = esp_timer_get_time();
  for (size_t block = 0; block < 65536; ++block) {
    Pending[0] = static_cast<uint16_t>(block);
    encodeBlock(Pending, BenchWire);
    checksum += BenchWire[block % kWireBlockBytes];
  }
  BenchUs = esp_timer_get_time() - began;
  BenchChecksum = checksum;
}

static void captureControlTask(void *) {
  if (CommandMode == 'P') {
    runUsbProbe(TargetBlocks);
  } else {
    const esp_err_t result = runCapture(TargetBlocks);
    sendStatus(result);
  }
  RunPending = false;
  vTaskDelete(nullptr);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Ring = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      64, kRingBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  Source = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      64, kSourceBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  FifoState.data = static_cast<uint8_t *>(heap_caps_malloc(
      kFifoBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ChunkQueue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  FreeStageQueue = xQueueCreate(kStageCount, sizeof(uint8_t *));
  ReadyStageQueue = xQueueCreate(kStageCount, sizeof(WireChunk));
  HarvestDone = xSemaphoreCreateBinary();
  SpoolDone = xSemaphoreCreateBinary();
  UsbDone = xSemaphoreCreateBinary();
  for (size_t i = 0; i < kSourceBytes; ++i) {
    const uint8_t value = static_cast<uint8_t>(i);
    Source[i] = value ^ (value >> 1);
  }
  esp_cache_msync(Source, kSourceBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  benchmarkCodec();

  EspUsbDeviceConfig config;
  config.vid = kVid;
  config.pid = kPid;
  config.manufacturer = "wch-protocols";
  config.product = "E106 P4 capture mixed-rate stream";
  config.serialNumber = "e104-p4-windows-v1";
  config.controller = EspUsbController::HighSpeed;
  config.webusbEnabled = true;
  UsbReady = Device.begin(config);
  Serial.printf("E106_READY usb=%u buffers=%u rate=%lu raw_MB_s=80 wire_MB_s=15.625 bench_us=%llu checksum=%lu\n",
                UsbReady, Ring && Source && FifoState.data && ChunkQueue,
                static_cast<unsigned long>(kSampleRateHz),
                static_cast<unsigned long long>(BenchUs), static_cast<unsigned long>(BenchChecksum));
}

void loop() {
  if (RunPending || Vendor.available() < 10) {
    delay(1);
    return;
  }
  uint8_t command[10];
  if (Vendor.read(command, sizeof(command)) != sizeof(command) ||
      command[0] != 'E' || (command[1] != '6' && command[1] != 'P')) return;
  const uint64_t blocks = readLe64(command + 2);
  if (blocks == 0) return;
  TargetBlocks = blocks;
  CommandMode = static_cast<char>(command[1]);
  RunPending = true;
  xTaskCreatePinnedToCore(captureControlTask, "e106_ctl", 4096, nullptr, 6, nullptr, 0);
}
