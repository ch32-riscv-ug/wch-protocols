// E107: E106's continuous 8/16-bit PARLIO capture -> mixed-rate codec -> USB HS
// stream, with runtime-selectable core placement, task priorities and FIFO
// location, plus FreeRTOS run-time stats so the coupled ceiling is attributed
// by measurement rather than inferred.
//
// Codec profiles are unchanged from E106:
//   8-bit:          3 raw + 5 hold D=64          (64 samples -> 25 bytes)
//   16-bit legacy:  3 raw + 8 hold D=64          (64 samples -> 25 bytes)
//   16-bit wide:    3 raw + 1 D=8 + 12 D=64      (128 samples -> 53 bytes)

#include <Arduino.h>
#include "EspUsbDevice.h"

#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <driver/parlio_tx.h>
#include <esp_attr.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

constexpr uint32_t kSampleRateHz = 32000000;
constexpr size_t kSourceLanes = 8;
constexpr size_t kLegacyBlockSamples = 64;
constexpr size_t kLegacyWireBlockBytes = 25;
constexpr size_t kWideBlockSamples = 128;
constexpr size_t kWideWireBlockBytes = 53;
constexpr size_t kMaxBlockSamples = kWideBlockSamples;
constexpr size_t kMaxWireBlockBytes = kWideWireBlockBytes;
constexpr size_t kRingBytes = 64 * 1024;
constexpr size_t kDelimiterBytes = 65408;
constexpr size_t kQueueDepth = 128;
constexpr size_t kSourceBytes = 8192;
constexpr uint32_t kSourceDivider = 4;
constexpr size_t kPsramFifoBytes = 8 * 1024 * 1024;
// Internal-RAM alternative to the PSRAM FIFO. Tried largest first at boot.
constexpr size_t kInternalFifoCandidates[] = {256 * 1024, 192 * 1024, 128 * 1024};
// Keep every row in Stage[] 64-byte aligned. Wide 53-byte blocks flush before
// crossing this boundary instead of forcing a non-cache-aligned row stride.
constexpr size_t kStageBytes = 12800;
constexpr size_t kStageCount = 4;
constexpr size_t kUsbPrebufferBytes = 1024 * 1024;
constexpr uint32_t kWaitMs = 1000;
constexpr uint32_t kMaxTimeouts = 10;
constexpr uint16_t kVid = 0x303a;
constexpr uint16_t kPid = 0x4021;
constexpr int kPins[kSourceLanes] = {2, 3, 4, 5, 6, 7, 8, 9};
constexpr size_t kCommandBytes = 16;

// Runtime variants selected per capture by command byte 14.
constexpr uint8_t kFlagSpoolPriority = 0x01;   // spool 6 / usbTask 4 instead of 5 / 5
constexpr uint8_t kFlagInternalFifo = 0x02;    // internal-RAM FIFO instead of PSRAM
constexpr uint8_t kFlagRxCore1 = 0x04;         // control task (and PARLIO RX ISR) on core 1
constexpr uint8_t kFlagUsbdLowPriority = 0x08; // espusb-device priority 4 during capture

// Boot-time variant: which core Device.begin() runs on. The DWC2 interrupt is
// allocated on the calling core (dwc2_esp32.h: esp_intr_alloc), so this decides
// where the USB ISR and, in practice, the unpinned usbd task execute.
constexpr uint32_t kUsbCoreMagic = 0xe1070c0eU;
constexpr int kDefaultUsbCore = 1;  // Arduino setup() core, i.e. E106's placement

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

struct StatSnapshot {
  uint32_t idle0;
  uint32_t idle1;
  uint32_t usbd;
  int64_t wall;
};

RTC_NOINIT_ATTR uint32_t UsbCoreMagic;
RTC_NOINIT_ATTR uint32_t UsbCoreValue;

EspUsbDevice Device;
EspUsbDeviceVendor Vendor(Device, 512);
EspUsbDeviceConfig UsbConfig;
Fifo FifoState = {};
QueueHandle_t ChunkQueue;
SemaphoreHandle_t HarvestDone;
SemaphoreHandle_t SpoolDone;
SemaphoreHandle_t UsbDone;
SemaphoreHandle_t UsbBeginDone;
QueueHandle_t FreeStageQueue;
QueueHandle_t ReadyStageQueue;
uint8_t *Ring;
uint8_t *Source;
uint8_t *PsramFifo;
uint8_t *InternalFifo;
size_t InternalFifoBytes;
size_t FifoBytes = kPsramFifoBytes;
uint16_t Pending[kMaxBlockSamples] __attribute__((aligned(64)));
uint8_t BenchWire[kMaxWireBlockBytes] __attribute__((aligned(64)));
uint8_t Stage[kStageCount][kStageBytes] __attribute__((aligned(64)));

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
volatile uint64_t ProcessUs;
volatile uint64_t PushUs;
volatile uint32_t HarvestChunks;
volatile uint32_t RawSequenceBad;
volatile uint32_t DuplicateBad;
volatile size_t MaxInflight;
volatile uint64_t SinkBytes;
volatile uint32_t SinkChecksum;
volatile uint32_t UsbTaskUs;
volatile uint32_t SpoolTaskUs;
volatile uint32_t CodecTaskUs;
volatile int RxIsrCore = -1;
uint32_t Idle0Us;
uint32_t Idle1Us;
uint32_t UsbdUs;
int64_t StatWallUs;
int CtlCore = -1;
int UsbInitCore = -1;
TaskHandle_t UsbdTask;
UBaseType_t UsbdPriority;
uint64_t BenchUs;
uint32_t BenchChecksum;
bool UsbReady;
volatile bool RunPending;
char CommandMode;
uint32_t RequestedRateHz = kSampleRateHz;
uint8_t RequestedWidth = 16;
uint8_t Flags;
bool SinkOnly;
bool WideProfile;

static size_t activeBlockSamples() {
  return WideProfile ? kWideBlockSamples : kLegacyBlockSamples;
}

static size_t activeWireBlockBytes() {
  return WideProfile ? kWideWireBlockBytes : kLegacyWireBlockBytes;
}

static uint32_t taskRunTime(TaskHandle_t task) {
  TaskStatus_t status = {};
  vTaskGetInfo(task, &status, pdFALSE, eInvalid);
  return status.ulRunTimeCounter;
}

static StatSnapshot snapshotStats() {
  StatSnapshot s;
  s.idle0 = ulTaskGetIdleRunTimeCounterForCore(0);
  s.idle1 = ulTaskGetIdleRunTimeCounterForCore(1);
  s.usbd = UsbdTask ? taskRunTime(UsbdTask) : 0;
  s.wall = esp_timer_get_time();
  return s;
}

// ---- codec ----------------------------------------------------------------
// Bit gathering works on whole 32-bit words loaded from the 64-byte-aligned
// DMA ring / pending buffer. E106 showed that memcpy(4) stayed a real call for
// a runtime pointer, so every load here is an explicit aligned word load.

// Four 8-bit samples in one word -> their low 3 bits, packed into 12 bits.
static inline __attribute__((always_inline)) uint32_t gatherLow3x4Bytes(uint32_t w) {
  w &= 0x07070707U;
  w = (w | (w >> 5)) & 0x003f003fU;
  return (w | (w >> 10)) & 0x0fffU;
}

// Four 16-bit samples in two words -> their low 3 bits, packed into 12 bits.
static inline __attribute__((always_inline)) uint32_t gatherLow3x4Halves(uint32_t w0, uint32_t w1) {
  const uint32_t x = (w0 & 0x00070007U) | ((w1 & 0x00070007U) << 6);
  return (x | (x >> 13)) & 0x0fffU;
}

static inline __attribute__((always_inline)) void store24(uint8_t *out, uint32_t packed) {
  out[0] = static_cast<uint8_t>(packed);
  out[1] = static_cast<uint8_t>(packed >> 8);
  out[2] = static_cast<uint8_t>(packed >> 16);
}

static inline __attribute__((always_inline)) uint32_t spread12(uint32_t value) {
  value &= 0x00000fffU;
  value = (value | (value << 8)) & 0x00ff00ffU;
  value = (value | (value << 4)) & 0x0f0f0f0fU;
  value = (value | (value << 2)) & 0x33333333U;
  value = (value | (value << 1)) & 0x55555555U;
  return value;
}

// 16-bit legacy: 3 raw + 8 hold D=64, 64 samples -> 25 bytes.
static inline __attribute__((always_inline)) void encodeBlock(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 8; ++group) {
    const uint32_t *w = words + group * 4;
    store24(wire + group * 3,
            gatherLow3x4Halves(w[0], w[1]) | (gatherLow3x4Halves(w[2], w[3]) << 12));
  }
  // D=64 hold: lanes 3..10 at the first base sample share one byte.
  wire[24] = static_cast<uint8_t>(words[0] >> 3);
}

// 8-bit: 3 raw + 5 hold D=64, 64 samples -> 25 bytes.
static inline __attribute__((always_inline)) void encodeBlock8(
    const uint8_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 8; ++group) {
    const uint32_t *w = words + group * 2;
    store24(wire + group * 3, gatherLow3x4Bytes(w[0]) | (gatherLow3x4Bytes(w[1]) << 12));
  }
  // Five D=64 channels occupy bits 0..4; bits 5..7 are defined padding.
  wire[24] = static_cast<uint8_t>((words[0] >> 3) & 0x1f);
}

// 16-bit wide: 3 raw + 1 D=8 + 12 D=64, 128 samples -> 53 bytes.
static inline __attribute__((always_inline)) void encodeBlockWide(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  uint32_t d8 = 0;
  for (size_t group = 0; group < 16; ++group) {
    const uint32_t *w = words + group * 4;
    store24(wire + group * 3,
            gatherLow3x4Halves(w[0], w[1]) | (gatherLow3x4Halves(w[2], w[3]) << 12));
    // Lane 3 (D=8) at the first sample of each 8-sample bucket.
    d8 |= ((w[0] >> 3) & 1U) << group;
  }
  wire[48] = static_cast<uint8_t>(d8);
  wire[49] = static_cast<uint8_t>(d8 >> 8);
  // Lanes 4..15 (D=64): two 12-bit snapshots at samples 0 and 64, interleaved.
  const uint32_t d64 = spread12((words[0] & 0xffffU) >> 4) |
      (spread12((words[32] & 0xffffU) >> 4) << 1);
  store24(wire + 50, d64);
}

static size_t fifoUsed() { return FifoState.head - FifoState.tail; }

static bool fifoPush(const uint8_t *data, size_t length) {
  const int64_t began = esp_timer_get_time();
  if (FifoBytes - fifoUsed() < length) {
    ++FifoState.overflow;
    PushUs += esp_timer_get_time() - began;
    return false;
  }
  const size_t offset = FifoState.head % FifoBytes;
  const size_t first = min(length, FifoBytes - offset);
  memcpy(FifoState.data + offset, data, first);
  if (length > first) memcpy(FifoState.data, data + first, length - first);
  FifoState.head += length;
  const size_t used = fifoUsed();
  if (used > FifoState.highWater) FifoState.highWater = used;
  PushUs += esp_timer_get_time() - began;
  return true;
}

static void sinkPush(const uint8_t *data, size_t length) {
  const int64_t began = esp_timer_get_time();
  const size_t offset = static_cast<size_t>(SinkBytes % FifoBytes);
  const size_t first = min(length, FifoBytes - offset);
  memcpy(FifoState.data + offset, data, first);
  if (length > first) memcpy(FifoState.data, data + first, length - first);
  uint32_t checksum = SinkChecksum;
  for (size_t i = 0; i < length; i += 64) checksum += data[i];
  SinkChecksum = checksum;
  SinkBytes += length;
  PushUs += esp_timer_get_time() - began;
}

static bool IRAM_ATTR onPartialReceive(
    parlio_rx_unit_handle_t, const parlio_rx_event_data_t *event, void *) {
  const Chunk chunk = {static_cast<const uint8_t *>(event->data), event->recv_bytes};
  CallbackBytes += event->recv_bytes;
  RxIsrCore = xPortGetCoreID();
  BaseType_t wake = pdFALSE;
  if (xQueueSendFromISR(ChunkQueue, &chunk, &wake) != pdTRUE) ++QueueOverflow;
  return wake == pdTRUE;
}

static esp_err_t createSource(parlio_tx_unit_handle_t *unit, uint32_t sampleRateHz) {
  parlio_tx_unit_config_t c = {};
  c.clk_src = PARLIO_CLK_SRC_DEFAULT;
  c.clk_in_gpio_num = GPIO_NUM_NC;
  c.output_clk_freq_hz = sampleRateHz / kSourceDivider;
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
                                parlio_rx_delimiter_handle_t *delimiter,
                                uint32_t sampleRateHz, uint8_t captureWidth) {
  parlio_rx_unit_config_t c = {};
  c.trans_queue_depth = 1;
  c.max_recv_size = kRingBytes;
  c.data_width = captureWidth;
  c.clk_src = PARLIO_CLK_SRC_DEFAULT;
  c.exp_clk_freq_hz = sampleRateHz;
  c.clk_in_gpio_num = GPIO_NUM_NC;
  c.clk_out_gpio_num = GPIO_NUM_NC;
  c.valid_gpio_num = GPIO_NUM_NC;
  for (size_t i = 0; i < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++i) {
    c.data_gpio_nums[i] = i < captureWidth ? gpio_num_t(kPins[i % kSourceLanes]) : GPIO_NUM_NC;
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

enum class Profile { Legacy16, Eight, Wide };

// One capture's codec loop, specialised per profile so block sizes are
// constants and there is no per-block profile branch. Blocks are encoded in
// runs bounded by the chunk, the stage and the target, which keeps the
// per-block work to: Gray check, encode, pointer bumps.
template <Profile P>
static void harvestBody() {
  constexpr size_t kSamples = P == Profile::Wide ? kWideBlockSamples : kLegacyBlockSamples;
  constexpr size_t kRaw = kSamples * (P == Profile::Eight ? 1 : 2);
  constexpr size_t kWire = P == Profile::Wide ? kWideWireBlockBytes : kLegacyWireBlockBytes;
  constexpr uint8_t kExpectedDelta = kSamples / 4;
  constexpr uint8_t kDeltaMargin = kSamples / 64;
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
  size_t maxInflight = 0;
  uint8_t *const pendingBytes = reinterpret_cast<uint8_t *>(Pending);
  // Device-side check of the raw Gray sequence at each block start (E106).
  const auto check = [&](const uint8_t *bytes) __attribute__((always_inline)) {
    const uint8_t low = bytes[0];
    if (P != Profile::Eight && bytes[1] != low) ++duplicateBad;
    uint8_t binary = low;
    binary ^= binary >> 1;
    binary ^= binary >> 2;
    binary ^= binary >> 4;
    if (previousValid) {
      const uint8_t delta = static_cast<uint8_t>(binary - previousBinary);
      if (delta < kExpectedDelta - kDeltaMargin || delta > kExpectedDelta + kDeltaMargin) ++rawSequenceBad;
    }
    previousBinary = binary;
    previousValid = true;
  };
  const auto encode = [](const uint8_t *in, uint8_t *out) __attribute__((always_inline)) {
    if constexpr (P == Profile::Wide) encodeBlockWide(reinterpret_cast<const uint16_t *>(in), out);
    else if constexpr (P == Profile::Eight) encodeBlock8(in, out);
    else encodeBlock(reinterpret_cast<const uint16_t *>(in), out);
  };
  const auto rotate = [&]() {
    const WireChunk ready = {stage, staged};
    xQueueSend(ReadyStageQueue, &ready, portMAX_DELAY);
    xQueueReceive(FreeStageQueue, &stage, portMAX_DELAY);
    staged = 0;
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
      // Finish the block split across the previous chunk boundary.
      const size_t take = min(kRaw - pending, chunk.length);
      memcpy(pendingBytes + pending, chunk.data, take);
      pending += take;
      at += take;
      if (pending == kRaw) {
        if (staged + kWire > kStageBytes) rotate();
        check(pendingBytes);
        encode(pendingBytes, stage + staged);
        staged += kWire;
        ++encoded;
        pending = 0;
      }
    }
    while (at + kRaw <= chunk.length && encoded < target) {
      if (staged + kWire > kStageBytes) rotate();
      size_t n = (chunk.length - at) / kRaw;
      n = min(n, (kStageBytes - staged) / kWire);
      const uint64_t remaining = target - encoded;
      if (remaining < n) n = static_cast<size_t>(remaining);
      const uint8_t *in = chunk.data + at;
      uint8_t *out = stage + staged;
      for (size_t i = 0; i < n; ++i, in += kRaw, out += kWire) {
        check(in);
        encode(in, out);
      }
      at += n * kRaw;
      staged += n * kWire;
      encoded += n;
    }
    if (at < chunk.length && encoded < target) {
      // Stash the tail fragment (< kRaw) for the next chunk.
      const size_t take = chunk.length - at;
      memcpy(pendingBytes, chunk.data + at, take);
      pending = take;
    }
    ++HarvestChunks;
    const size_t processedRaw = static_cast<size_t>(encoded * kRaw + pending);
    const size_t callback = static_cast<size_t>(CallbackBytes);
    const size_t inflight = callback > processedRaw ? callback - processedRaw : 0;
    if (inflight > maxInflight) maxInflight = inflight;
  }
  if (staged != 0) {
    const WireChunk ready = {stage, staged};
    xQueueSend(ReadyStageQueue, &ready, portMAX_DELAY);
  } else {
    xQueueSend(FreeStageQueue, &stage, portMAX_DELAY);
  }
  ProcessUs = esp_timer_get_time() - taskBegan;
  EncodedBlocks = encoded;
  RawSequenceBad = rawSequenceBad;
  DuplicateBad = duplicateBad;
  MaxInflight = maxInflight;
  CodecTaskUs = taskRunTime(nullptr);
}

static void harvestTask(void *) {
  if (WideProfile) harvestBody<Profile::Wide>();
  else if (RequestedWidth == 8) harvestBody<Profile::Eight>();
  else harvestBody<Profile::Legacy16>();
  xSemaphoreGive(HarvestDone);
  vTaskDelete(nullptr);
}

static void spoolTask(void *) {
  const uint64_t target = TargetBlocks * activeWireBlockBytes();
  uint64_t moved = 0;
  while (moved < target) {
    WireChunk chunk = {};
    if (xQueueReceive(ReadyStageQueue, &chunk, pdMS_TO_TICKS(1000)) != pdTRUE) break;
    // On FIFO overflow the stage is dropped and counted; the spool must keep
    // recycling stages or the codec blocks on FreeStageQueue and the capture
    // never finishes (E106 left this as a latent hang).
    if (SinkOnly) sinkPush(chunk.data, chunk.length);
    else fifoPush(chunk.data, chunk.length);
    moved += chunk.length;
    xQueueSend(FreeStageQueue, &chunk.data, portMAX_DELAY);
  }
  SpoolTaskUs = taskRunTime(nullptr);
  xSemaphoreGive(SpoolDone);
  vTaskDelete(nullptr);
}

static void usbTask(void *) {
  const uint64_t target = TargetBlocks * activeWireBlockBytes();
  uint64_t sent = 0;
  uint32_t consecutiveTimeouts = 0;
  const int64_t began = esp_timer_get_time();
  const size_t tx = EspUsbDeviceVendor::writeCapacity();
  const size_t prebuffer = min(kUsbPrebufferBytes, FifoBytes / 4);
  while (fifoUsed() < min(prebuffer, static_cast<size_t>(target)) && Active) {
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
    const size_t offset = FifoState.tail % FifoBytes;
    span = min(span, FifoBytes - offset);
    // Codec stages need not end on a USB packet boundary (the wide profile is
    // 53 bytes/block). Avoid terminating one USB transfer per stage: hold the
    // tail fragment until another stage arrives, except for the final bytes.
    if (sent + span < target) {
      span &= ~size_t(511);
      if (span == 0) {
        vTaskDelay(1);
        continue;
      }
    }
    if (Vendor.writeAvailable() < span) {
      // Everything this task writes is a multiple of 512, so a flush here never
      // creates a short packet in steady state. It does push out anything left
      // unflushed by an earlier status line; without it a request for the full
      // FIFO can never be satisfied and the task gives up after kMaxTimeouts
      // (E106 inherited that deadlock).
      Vendor.flush();
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
  UsbTaskUs = taskRunTime(nullptr);
  xSemaphoreGive(UsbDone);
  vTaskDelete(nullptr);
}

static esp_err_t runCapture(uint64_t blocks, uint32_t sampleRateHz) {
  TargetBlocks = blocks;
  EncodedBlocks = SentBytes = CallbackBytes = 0;
  QueueOverflow = UsbWaits = UsbTimeouts = 0;
  CaptureUs = UsbUs = 0;
  ProcessUs = PushUs = 0;
  HarvestChunks = 0;
  RawSequenceBad = DuplicateBad = 0;
  MaxInflight = 0;
  SinkBytes = SinkChecksum = 0;
  UsbTaskUs = SpoolTaskUs = CodecTaskUs = 0;
  Idle0Us = Idle1Us = UsbdUs = 0;
  StatWallUs = 0;
  RxIsrCore = -1;
  CtlCore = xPortGetCoreID();
  const bool internalFifo = (Flags & kFlagInternalFifo) && InternalFifo;
  FifoState.data = internalFifo ? InternalFifo : PsramFifo;
  FifoBytes = internalFifo ? InternalFifoBytes : kPsramFifoBytes;
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
  StatSnapshot began = {};
  StatSnapshot ended = {};
  const UBaseType_t spoolPriority = (Flags & kFlagSpoolPriority) ? 6 : 5;
  const UBaseType_t usbPriority = (Flags & kFlagSpoolPriority) ? 4 : 5;
  const bool lowerUsbd = UsbdTask && (Flags & kFlagUsbdLowPriority);

  parlio_tx_unit_handle_t txUnit = nullptr;
  parlio_rx_unit_handle_t rxUnit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  esp_err_t result = createSource(&txUnit, sampleRateHz);
  if (result == ESP_OK) result = parlio_tx_unit_enable(txUnit);
  if (result == ESP_OK) {
    parlio_transmit_config_t t = {};
    t.flags.loop_transmission = true;
    result = parlio_tx_unit_transmit(txUnit, Source, kSourceBytes * 8, &t);
  }
  if (result == ESP_OK) result = createReceiver(&rxUnit, &delimiter, sampleRateHz, RequestedWidth);
  if (result == ESP_OK) result = parlio_rx_unit_enable(rxUnit, true);
  if (result == ESP_OK) {
    parlio_receive_config_t r = {};
    r.delimiter = delimiter;
    r.flags.partial_rx_en = true;
    result = parlio_rx_unit_receive(rxUnit, Ring, kRingBytes, &r);
  }
  if (result != ESP_OK) goto cleanup;

  Active = true;
  // The RX interrupt allocated by createReceiver lives on this task's core
  // (core 0 by default, core 1 with kFlagRxCore1). The codec is always core 1;
  // spool and usbTask are always core 0.
  if (lowerUsbd) vTaskPrioritySet(UsbdTask, 4);
  if (!SinkOnly) xTaskCreatePinnedToCore(usbTask, "e107_usb", 4096, nullptr, usbPriority, nullptr, 0);
  xTaskCreatePinnedToCore(spoolTask, "e107_spool", 4096, nullptr, spoolPriority, nullptr, 0);
  xTaskCreatePinnedToCore(harvestTask, "e107_codec", 4096, nullptr, 5, nullptr, 1);
  {
    began = snapshotStats();
    result = parlio_rx_soft_delimiter_start_stop(rxUnit, delimiter, true);
    if (result == ESP_OK) xSemaphoreTake(HarvestDone, portMAX_DELAY);
    ended = snapshotStats();
    CaptureUs = ended.wall - began.wall;
  }
  Idle0Us = ended.idle0 - began.idle0;
  Idle1Us = ended.idle1 - began.idle1;
  UsbdUs = ended.usbd - began.usbd;
  StatWallUs = ended.wall - began.wall;
  parlio_rx_soft_delimiter_start_stop(rxUnit, delimiter, false);
  parlio_rx_unit_disable(rxUnit);
  xSemaphoreTake(SpoolDone, portMAX_DELAY);
  Active = false;
  if (!SinkOnly) xSemaphoreTake(UsbDone, portMAX_DELAY);
  if (lowerUsbd) vTaskPrioritySet(UsbdTask, UsbdPriority);

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
  char status[1024];
  const int length = snprintf(
      status, sizeof(status),
      "E107_STATUS result=%s rate_hz=%lu width=%u profile=%s sink_only=%u flags=0x%02x usb_core=%d ctl_core=%d rx_isr_core=%d "
      "fifo_bytes=%lu blocks=%llu encoded=%llu sent=%llu callbacks=%llu "
      "queue_overflow=%lu fifo_overflow=%lu high_water=%lu waits=%lu timeouts=%lu "
      "capture_us=%lld usb_us=%lld chunks=%lu process_us=%llu push_us=%llu "
      "raw_sequence_bad=%lu duplicate_bad=%lu max_inflight=%lu ring_bytes=%lu sink_bytes=%llu sink_checksum=%lu "
      "stat_wall_us=%lld idle0_us=%lu idle1_us=%lu usbd_us=%lu usb_task_us=%lu spool_task_us=%lu codec_task_us=%lu "
      "usbd_priority=%u bench_us=%llu bench_checksum=%lu\n",
      esp_err_to_name(result), static_cast<unsigned long>(RequestedRateHz),
      RequestedWidth, WideProfile ? "wide" : "legacy", SinkOnly, Flags, UsbInitCore, CtlCore, RxIsrCore,
      static_cast<unsigned long>(FifoBytes),
      static_cast<unsigned long long>(TargetBlocks),
      static_cast<unsigned long long>(EncodedBlocks), static_cast<unsigned long long>(SentBytes),
      static_cast<unsigned long long>(CallbackBytes), static_cast<unsigned long>(QueueOverflow),
      static_cast<unsigned long>(FifoState.overflow), static_cast<unsigned long>(FifoState.highWater),
      static_cast<unsigned long>(UsbWaits), static_cast<unsigned long>(UsbTimeouts),
      static_cast<long long>(CaptureUs), static_cast<long long>(UsbUs),
      static_cast<unsigned long>(HarvestChunks),
      static_cast<unsigned long long>(ProcessUs), static_cast<unsigned long long>(PushUs),
      static_cast<unsigned long>(RawSequenceBad), static_cast<unsigned long>(DuplicateBad),
      static_cast<unsigned long>(MaxInflight), static_cast<unsigned long>(kRingBytes),
      static_cast<unsigned long long>(SinkBytes), static_cast<unsigned long>(SinkChecksum),
      static_cast<long long>(StatWallUs), static_cast<unsigned long>(Idle0Us),
      static_cast<unsigned long>(Idle1Us), static_cast<unsigned long>(UsbdUs),
      static_cast<unsigned long>(UsbTaskUs), static_cast<unsigned long>(SpoolTaskUs),
      static_cast<unsigned long>(CodecTaskUs), static_cast<unsigned>(UsbdPriority),
      static_cast<unsigned long long>(BenchUs), static_cast<unsigned long>(BenchChecksum));
  Serial.write(reinterpret_cast<const uint8_t *>(status), length);
  Serial.flush();
  Vendor.waitWritable(length, kWaitMs);
  Vendor.write(reinterpret_cast<const uint8_t *>(status), length);
  Vendor.flush();
}

static uint64_t readLe64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= uint64_t(p[i]) << (8 * i);
  return value;
}

static uint32_t readLe32(const uint8_t *p) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= uint32_t(p[i]) << (8 * i);
  return value;
}

static void runUsbProbe(uint64_t target) {
  for (size_t i = 0; i < kStageBytes; ++i) Stage[0][i] = static_cast<uint8_t>(i);
  uint64_t sent = 0;
  const size_t tx = EspUsbDeviceVendor::writeCapacity();
  const StatSnapshot began = snapshotStats();
  while (sent < target) {
    const size_t phase = static_cast<size_t>(sent % kStageBytes);
    size_t span = min(tx, kStageBytes - phase);
    span = min(span, static_cast<size_t>(target - sent));
    if (!Vendor.waitWritable(span, kWaitMs)) break;
    sent += Vendor.write(Stage[0] + phase, span);
  }
  Vendor.flush();
  const StatSnapshot ended = snapshotStats();
  char status[320];
  const int length = snprintf(
      status, sizeof(status),
      "E107_PROBE bytes=%llu target=%llu elapsed_us=%lld usb_core=%d ctl_core=%d "
      "idle0_us=%lu idle1_us=%lu usbd_us=%lu\n",
      static_cast<unsigned long long>(sent), static_cast<unsigned long long>(target),
      static_cast<long long>(ended.wall - began.wall), UsbInitCore, xPortGetCoreID(),
      static_cast<unsigned long>(ended.idle0 - began.idle0),
      static_cast<unsigned long>(ended.idle1 - began.idle1),
      static_cast<unsigned long>(ended.usbd - began.usbd));
  Serial.write(reinterpret_cast<const uint8_t *>(status), length);
  Vendor.waitWritable(length, kWaitMs);
  Vendor.write(reinterpret_cast<const uint8_t *>(status), length);
  // The line is shorter than one packet; without a flush it stays in the FIFO
  // and surfaces as stale bytes at the start of the next run.
  Vendor.flush();
}

static void benchmarkCodec() {
  for (size_t i = 0; i < kLegacyBlockSamples; ++i) Pending[i] = static_cast<uint16_t>(i * 257U);
  uint32_t checksum = 0;
  const int64_t began = esp_timer_get_time();
  for (size_t block = 0; block < 65536; ++block) {
    Pending[0] = static_cast<uint16_t>(block);
    encodeBlock(Pending, BenchWire);
    checksum += BenchWire[block % kLegacyWireBlockBytes];
  }
  BenchUs = esp_timer_get_time() - began;
  BenchChecksum = checksum;
}

static void captureControlTask(void *) {
  if (CommandMode == 'P') {
    runUsbProbe(TargetBlocks);
  } else {
    const esp_err_t result = runCapture(TargetBlocks, RequestedRateHz);
    sendStatus(result);
  }
  RunPending = false;
  vTaskDelete(nullptr);
}

static void usbBeginTask(void *) {
  UsbInitCore = xPortGetCoreID();
  UsbReady = Device.begin(UsbConfig);
  xSemaphoreGive(UsbBeginDone);
  vTaskDelete(nullptr);
}

static void restartWithUsbCore(uint8_t core) {
  UsbCoreValue = core;
  UsbCoreMagic = kUsbCoreMagic;
  char line[96];
  const int length = snprintf(line, sizeof(line), "E107_RESTART usb_core=%u\n", core);
  Serial.write(reinterpret_cast<const uint8_t *>(line), length);
  Serial.flush();
  Vendor.waitWritable(length, kWaitMs);
  Vendor.write(reinterpret_cast<const uint8_t *>(line), length);
  Vendor.flush();
  delay(200);
  esp_restart();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Ring = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      64, kRingBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  Source = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      64, kSourceBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  PsramFifo = static_cast<uint8_t *>(heap_caps_malloc(
      kPsramFifoBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  for (size_t candidate : kInternalFifoCandidates) {
    InternalFifo = static_cast<uint8_t *>(heap_caps_aligned_alloc(
        64, candidate, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (InternalFifo) {
      InternalFifoBytes = candidate;
      break;
    }
  }
  FifoState.data = PsramFifo;
  ChunkQueue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  FreeStageQueue = xQueueCreate(kStageCount, sizeof(uint8_t *));
  ReadyStageQueue = xQueueCreate(kStageCount, sizeof(WireChunk));
  HarvestDone = xSemaphoreCreateBinary();
  SpoolDone = xSemaphoreCreateBinary();
  UsbDone = xSemaphoreCreateBinary();
  UsbBeginDone = xSemaphoreCreateBinary();
  for (size_t i = 0; i < kSourceBytes; ++i) {
    const uint8_t value = static_cast<uint8_t>(i);
    Source[i] = value ^ (value >> 1);
  }
  esp_cache_msync(Source, kSourceBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  benchmarkCodec();

  UsbConfig.vid = kVid;
  UsbConfig.pid = kPid;
  UsbConfig.manufacturer = "wch-protocols";
  UsbConfig.product = "E107 P4 stream core placement";
  // Same serial as E104..E106 so the Windows devnode / WinUSB binding and the
  // usbipd share persist across the firmware change.
  UsbConfig.serialNumber = "e104-p4-windows-v1";
  UsbConfig.controller = EspUsbController::HighSpeed;
  UsbConfig.webusbEnabled = true;
  const int usbCore = (UsbCoreMagic == kUsbCoreMagic && UsbCoreValue <= 1)
      ? static_cast<int>(UsbCoreValue) : kDefaultUsbCore;
  xTaskCreatePinnedToCore(usbBeginTask, "e107_usb_begin", 8192, nullptr, 6, nullptr, usbCore);
  xSemaphoreTake(UsbBeginDone, portMAX_DELAY);
  UsbdTask = xTaskGetHandle("espusb-device");
  UsbdPriority = UsbdTask ? uxTaskPriorityGet(UsbdTask) : 0;
  Serial.printf("E107_READY usb=%u usb_core=%d setup_core=%d usbd_task=%u usbd_priority=%u internal_fifo_bytes=%u "
                "buffers=%u rate=%lu bench_us=%llu checksum=%lu\n",
                UsbReady, UsbInitCore, xPortGetCoreID(), UsbdTask != nullptr, static_cast<unsigned>(UsbdPriority),
                static_cast<unsigned>(InternalFifoBytes),
                Ring && Source && PsramFifo && ChunkQueue,
                static_cast<unsigned long>(kSampleRateHz),
                static_cast<unsigned long long>(BenchUs), static_cast<unsigned long>(BenchChecksum));
}

void loop() {
  if (RunPending || Vendor.available() < static_cast<int>(kCommandBytes)) {
    delay(1);
    return;
  }
  uint8_t command[kCommandBytes];
  if (Vendor.read(command, sizeof(command)) != sizeof(command)) return;
  if (command[0] == 'E' && command[1] == 'C') {
    if (command[2] <= 1) restartWithUsbCore(command[2]);
    return;
  }
  if ((command[0] != 'E' && command[0] != 'I') ||
      (command[1] != '6' && command[1] != '8' && command[1] != 'W' && command[1] != 'P')) return;
  const uint32_t rateHz = readLe32(command + 2);
  const uint64_t blocks = readLe64(command + 6);
  if (blocks == 0) return;
  TargetBlocks = blocks;
  RequestedRateHz = command[1] == 'P' ? kSampleRateHz : rateHz;
  RequestedWidth = command[1] == '8' ? 8 : 16;
  WideProfile = command[1] == 'W';
  SinkOnly = command[0] == 'I';
  Flags = command[14];
  if (command[1] != 'P' && (RequestedRateHz < 1000000 || RequestedRateHz > 160000000)) return;
  CommandMode = command[1] == 'P' ? 'P' : static_cast<char>(command[0]);
  RunPending = true;
  xTaskCreatePinnedToCore(captureControlTask, "e107_ctl", 4096, nullptr, 6, nullptr,
                          (Flags & kFlagRxCore1) ? 1 : 0);
}
