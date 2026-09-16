// E114: E113 plus a descriptor-driven generic codec (profile D). The host sends
// base rate, channel count and per-channel {gpio, mode, log2 D, phase|polarity};
// the device sorts raw channels onto the low PARLIO lanes, computes the wire
// layout, checks internal limits and the USB budget, and replies ACCEPT/REJECT.
// Also E113: E112 plus profile 2 = PARLIO 2-bit width passed through unchanged
// (2 bit/sample, 256 samples -> 64 bytes), for the "2 channels at 160 Msps"
// claim. Also E112: E111 (dual-core zero-copy stream) plus two 16-channel allocation
// profiles: F = 4 full + 12 D32 (128 samples -> 70 bytes), V = 5 full + 11 D32
// (128 samples -> 86 bytes).
//
// PARLIO chunks (3,840 bytes = a whole number of codec blocks) carry a sequence
// number from the ISR. Two codec workers (core 0 and core 1) take chunks from
// one queue and encode each chunk straight into its final place: the wire
// offset is seq * chunkWire, which selects the stage buffer and the offset in
// it, so no ordering step is needed on the codec side. A stage is published by
// whichever worker completes its byte count; the usb task sends stages in
// index order and releases a buffer when its transfer completes. Everything
// else (zero-copy arm ring, PSRAM spill, probe, stats) is E108 / E110.
//
// Requires EspUsbDevice 2.3.0 with e108's espusbdevice-e108.patch and e110's
// dcd-dwc2-in-fifo-packets.patch (/tmp/EspUsbDevice-e110).

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
#include <freertos/task.h>

#ifndef E108_CODEC_O2
#define E108_CODEC_O2 0
#endif
#ifndef E108_CODEC_PREFETCH
#define E108_CODEC_PREFETCH 0
#endif

namespace {

constexpr uint32_t kSampleRateHz = 32000000;
constexpr size_t kSourceLanes = 8;
constexpr size_t kLegacyBlockSamples = 64;
constexpr size_t kLegacyWireBlockBytes = 25;
constexpr size_t kWideBlockSamples = 128;
constexpr size_t kWideWireBlockBytes = 53;
constexpr size_t kMaxBlockSamples = kWideBlockSamples;
constexpr size_t kMaxWireBlockBytes = kWideWireBlockBytes;
// The PARLIO driver mounts the ring as DMA nodes of up to 4,032 bytes (the last
// two split evenly), so chunk lengths are not a whole number of blocks for the
// 16-bit profiles. Placement therefore uses the raw stream offset carried by
// each chunk: the worker that receives a chunk completes the block that
// straddles its head from the previous chunk's tail (still in the ring), then
// encodes its whole blocks, and leaves its own partial tail to the next chunk.
constexpr size_t kRingBytes = 65280;
constexpr size_t kDelimiterBytes = 65280;
constexpr size_t kWorkers = 2;
constexpr uint32_t kStageWaitTimeoutUs = 500000;
constexpr size_t kQueueDepth = 128;
constexpr size_t kSourceBytes = 8192;
constexpr uint32_t kSourceDivider = 4;
// One stage is one USB transfer. 27,136 = lcm(53, 512): the wide profile fills
// 512 blocks, the 25-byte profiles fill 1,024 blocks (25,600 B). Both are
// multiples of 512 (whole packets) and of 64 (cache lines).
constexpr size_t kStageBytes = 27136;
constexpr size_t kStageFillWide = 512 * kWideWireBlockBytes;
constexpr size_t kStageFillLegacy = 1024 * kLegacyWireBlockBytes;
// 16-channel allocation profiles (E112): 4 full + 12 D32 and 5 full + 11 D32.
constexpr size_t kFourWireBlockBytes = 70;   // 4 x 128 bit + 12 lanes x 4 snapshots
constexpr size_t kFiveWireBlockBytes = 86;   // 5 x 128 bit (80 B) + 11 lanes x 4 snapshots (44 bit -> 6 B)
constexpr size_t kStageFillFour = 256 * kFourWireBlockBytes;   // 17,920 = 35 x 512
constexpr size_t kStageFillFive = 256 * kFiveWireBlockBytes;   // 22,016 = 43 x 512
// E113: 2-bit PARLIO width, no codec. 256 samples = 64 bytes in and out.
constexpr size_t kTwoBlockSamples = 256;
constexpr size_t kTwoWireBlockBytes = 64;
constexpr size_t kStageFillTwo = kStageBytes;   // 27,136 = 424 x 64 = 53 x 512
constexpr size_t kStageCount = 6;  // E114: 4 -> 6 so a host hiccup of a few stages does not reach the PSRAM spill path (8 left no room for the ring)
constexpr size_t kBounceCount = 2;
constexpr size_t kSpillBytes = 16 * 1024 * 1024;  // E114: 8 -> 16 MiB; a 209 ms host stall at 33 MB/s wire used 6.9 MiB
constexpr size_t kArmDepth = 8;
// At most this many stages may sit in the arm ring / in flight; the rest stay
// with the codec so it never starves while USB is the slower side.
constexpr size_t kDirectDepth = 2;  // stages armed directly before spilling; the other 4 stages are the codec's slack while USB stalls
constexpr size_t kStatusBytes = 1536;
constexpr size_t kCommandBytes = 16;
constexpr size_t kMaxChannels = 16;
constexpr size_t kDescHeaderBytes = 18;   // 16-byte command + u16 budget_mbps
constexpr size_t kDescEntryBytes = 4;
constexpr size_t kMailboxBytes = kDescHeaderBytes + kMaxChannels * kDescEntryBytes;
constexpr size_t kDynBlockSamples = 128;
constexpr uint32_t kStatusWaitMs = 2000;
constexpr uint16_t kVid = 0x303a;
constexpr uint16_t kPid = 0x4021;
constexpr int kPins[kSourceLanes] = {2, 3, 4, 5, 6, 7, 8, 9};
constexpr int kUsbCore = 0;  // E107: the DWC2 ISR and usbd task follow Device.begin()

constexpr uint8_t kFlagNoCheck = 0x01;    // skip the device-side Gray check
constexpr uint8_t kFlagNoDirect = 0x02;   // every stage through the PSRAM spill path
constexpr uint8_t kFlagLoadProbe = 0x04;  // priority-1 spin task on core 0 measures ISR-inclusive free time
constexpr uint8_t kFlagSingleCore = 0x10; // one codec worker on core 1 (E108 layout) for comparison
constexpr uint8_t kFlagNoCodecLimit = 0x20; // calibration: accept even above the bench-derived codec limit
constexpr uint8_t kFlagNoPulldown = 0x40;   // leave undriven lane GPIOs floating (to reproduce the USB stall)
constexpr uint8_t kFlagBenchCold = 0x80;    // E118: bench reads each block from a fresh place in the 65 KB ring (L1-cold, like streaming)
// Bench-derived codec limit: single-core bench Msps x (workers x per-core scale), then 90% margin.
constexpr unsigned kDualScalePercent = 135;   // E118: with chunk coalescing and no per-run msync the 2-worker stream reaches ~1.3x the single-core bench (W16 bench 47 -> 60 ok, 65 edge); 1.35 x 0.9 = 1.215x leaves ~10% idle
constexpr unsigned kSingleScalePercent = 90;
constexpr unsigned kCodecMarginPercent = 90;
// DWC2 HS controller registers (dwc2_esp32.h: DWC2_HS_REG_BASE). GAHBCFG.DMAEn and
// GHWCFG2.arch tell whether the DCD really runs the controller in DMA mode.
constexpr uintptr_t kDwc2HsBase = 0x50000000UL;

enum SegmentKind : uint8_t { kSegStage, kSegBounce, kSegProbe, kSegStatus };

struct Segment {
  const uint8_t *data;
  uint32_t length;
  uint8_t kind;
  uint8_t slot;
};

struct Chunk {
  const uint8_t *data;
  uint32_t length;
  uint32_t seq;
  uint64_t rawOffset;  // stream position of data[0]; blocks are placed from this, not from seq
};

struct WireChunk {
  uint8_t *data;
  size_t length;
  uint32_t index;  // stage index; the usb task sends in this order
};

struct StatSnapshot {
  uint32_t idle0;
  uint32_t idle1;
  uint32_t usbd;
  int64_t wall;
};

EspUsbDevice Device;
EspUsbDeviceVendor Vendor(Device, 512);
EspUsbDeviceConfig UsbConfig;
QueueHandle_t ChunkQueue;
QueueHandle_t ReadyStageQueue;
// Stage bookkeeping shared by the workers and the usb task. Buffer b hosts
// stage indices; a slot is assigned from the free list on first use (SlotTable below).
volatile uint32_t StageFilled[kStageCount];
// E114: stage index -> buffer slot is assigned from a free list when the index is
// first written (SlotTable), not index % kStageCount. While USB stalls, the
// directly armed slots stay busy but every other stage goes to the PSRAM spill
// and its slot is recycled at once, so the codec keeps running for as long as
// the spill has room instead of blocking after kStageCount - kDirectDepth stages.
constexpr size_t kSlotTable = 16;
volatile uint32_t SlotTableIndex[kSlotTable];  // stage index the entry is assigned to (UINT32_MAX = none)
volatile uint8_t SlotTableSlot[kSlotTable];
volatile uint32_t FreeSlots;                   // bit s = slot s is free
volatile uint32_t SlotAcquireWaits;
volatile uint32_t AbortFreeSlots, AbortArmQueued, AbortSpillUsed, AbortReadyWaiting, AbortInflightKind;
portMUX_TYPE StageMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t StageWaits;
volatile uint32_t StageWaitTimeouts;
volatile uint32_t ShortChunks;
volatile uint32_t ChunkSeq;
volatile uint32_t ChunkMin;
volatile uint32_t ChunkMax;
volatile bool RunAborted;
volatile uint64_t ProcessedRaw;
volatile uint32_t WorkerChunks[kWorkers];
volatile uint32_t WorkerUs[kWorkers];
volatile uint32_t WorkerCount;
size_t StageFill;
size_t WireBlockBytes;
uint64_t TotalWireBytes;
SemaphoreHandle_t HarvestDone;
SemaphoreHandle_t UsbDone;
SemaphoreHandle_t UsbWake;
SemaphoreHandle_t StatusDone;
SemaphoreHandle_t UsbBeginDone;
uint8_t *Ring;
uint8_t *Source;
uint8_t *Spill;
uint16_t Pending[kMaxBlockSamples] __attribute__((aligned(64)));
uint8_t BenchWire[kMaxWireBlockBytes] __attribute__((aligned(64)));
uint8_t *Stage[kStageCount];  // internal DMA-capable heap, 64-aligned (8 x 27 KiB no longer fits the static data region)
uint8_t Bounce[kBounceCount][kStageBytes] __attribute__((aligned(64)));
uint8_t StatusBuffer[kStatusBytes] __attribute__((aligned(64)));

// Spill FIFO (PSRAM): head written by the usb task, tail read by the usb task.
volatile size_t SpillHead;
volatile size_t SpillTail;
volatile size_t SpillHighWater;
volatile uint32_t SpillOverflow;
volatile uint8_t BounceBusy[kBounceCount];

// Arm ring: pushed by the usb task, popped by whoever arms next (usb task or
// the TX-complete callback on the usbd task). One segment is in flight at a time.
Segment ArmRing[kArmDepth];
volatile uint32_t ArmHead;
volatile uint32_t ArmTail;
volatile bool Inflight;
Segment InflightSegment;
portMUX_TYPE ArmMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool Active;
volatile bool CodecFinished;
volatile uint64_t TargetBlocks;
volatile uint64_t EncodedBlocks;
volatile uint64_t SentBytes;
volatile uint64_t Completions;
volatile uint64_t DirectSegments;
volatile uint64_t BounceSegments;
volatile uint64_t SpillBytes;
volatile uint32_t ArmFailures;
volatile uint8_t LastDirectError;
volatile uint64_t CallbackBytes;
volatile uint32_t QueueOverflow;
volatile uint32_t PulledDownLanes;
// Timeline of the spill path, microseconds since the run began (0 = never).
volatile int64_t RunBeganUs;
volatile int64_t FirstSpillUs;
volatile int64_t FirstOverflowUs;
volatile int64_t SpillModeUs;      // total time with spilled bytes outstanding
volatile int64_t SpillModeBeganUs;
volatile uint32_t SpillPushUsMax;
volatile uint32_t BouncePopUsMax;
volatile int64_t LastCompletionUs;
volatile uint32_t CompletionGapUsMax;  // longest gap between consecutive USB TX completions
volatile int64_t CompletionGapAtUs;    // when it ended, relative to the run start
// Log of completion gaps > 5 ms: end time (run-relative), length, and whether a
// segment was armed when the gap began (1 = host/hardware did not move it,
// 0 = nothing was armed, i.e. the codec produced no stage).
constexpr size_t kGapLog = 8;
volatile uint32_t GapLogEndMs[kGapLog];
volatile uint32_t GapLogUs[kGapLog];
volatile uint32_t GapLogCount;
volatile int64_t LastArmUs;            // when a segment was last handed to usbd_edpt_xfer
volatile uint32_t GapLogArmLatUs[kGapLog];  // arm time minus previous completion, per logged gap
volatile int64_t CaptureUs;
volatile int64_t UsbUs;
volatile uint64_t ProcessUs;
volatile uint32_t HarvestChunks;
volatile uint32_t RawSequenceBad;
volatile uint32_t DuplicateBad;
volatile size_t MaxInflight;
volatile uint64_t SinkBytes;
volatile uint32_t SinkChecksum;
volatile uint32_t UsbTaskUs;
volatile int RxIsrCore = -1;
uint32_t Idle0Us;
uint32_t Idle1Us;
uint32_t UsbdUs;
int64_t StatWallUs;
int UsbInitCore = -1;
TaskHandle_t UsbdTask;
UBaseType_t UsbdPriority;
uint64_t BenchUs;
uint32_t BenchChecksum;
// Query-path bench of the dynamic encoder (single core, synthetic input).
uint8_t DynBenchWire[512] __attribute__((aligned(64)));
uint64_t DynBenchUs;
uint32_t DynBenchBlocks;
uint32_t DynBenchChecksum;
uint8_t BenchFlags;
// E118: cycles spent inside encode() per worker during streaming, to compare with the bench.
volatile uint64_t EncodeCycles[kWorkers];
volatile uint32_t EncodeBlocks[kWorkers];
volatile uint64_t WritebackCycles[kWorkers];
volatile uint32_t WritebackRuns[kWorkers];
volatile uint64_t QueueCycles[kWorkers];
bool UsbReady;
volatile bool RunPending;
volatile bool CommandPending;
uint8_t CommandMailbox[kMailboxBytes];
volatile uint32_t CommandLength;
char CommandMode;
uint32_t RequestedRateHz = kSampleRateHz;
uint8_t RequestedWidth = 16;
uint8_t Flags;
bool SinkOnly;
bool WideProfile;
char ProfileCode = '6';  // '6' legacy 16-bit, '8' eight, 'W' wide, 'F' four+12 D32, 'V' five+11 D32
volatile uint64_t ProbeRemaining;
volatile bool SpinRun;
volatile uint32_t SpinCount;
SemaphoreHandle_t SpinDone;
uint32_t SpinCalibPerSec;
int64_t SpinUs;
uint32_t SpinResult;

static uint32_t dwc2Register(uint32_t offset) {
  return *reinterpret_cast<volatile uint32_t *>(kDwc2HsBase + offset);
}
static unsigned dwc2DmaActive() { return (dwc2Register(0x08) >> 5) & 1U; }
// GINTMSK.RXFLVL (bit 4) is set only by the slave-mode branch of dwc2_core_init().
static unsigned dwc2RxflvlMasked() { return (dwc2Register(0x18) >> 4) & 1U; }
static unsigned dwc2Arch() { return (dwc2Register(0x48) >> 3) & 3U; }

// Idle replacement: everything that runs on core 0 above priority 1 - tasks and
// interrupts alike - steals iterations from this loop, so its count against a
// calibrated rate is the core's ISR-inclusive free fraction.
static void spinTask(void *) {
  uint32_t n = 0;
  while (SpinRun) {
    ++n;
    if ((n & 0x3ff) == 0) SpinCount = n;
  }
  SpinCount = n;
  xSemaphoreGive(SpinDone);
  vTaskDelete(nullptr);
}

static void spinStart() {
  SpinRun = true;
  SpinCount = 0;
  xTaskCreatePinnedToCore(spinTask, "e111_spin", 2048, nullptr, 1, nullptr, 0);
}

static uint32_t spinStop() {
  SpinRun = false;
  xSemaphoreTake(SpinDone, pdMS_TO_TICKS(200));
  return SpinCount;
}

enum ChannelMode : uint8_t { kModeRaw = 0, kModeHold = 1, kModeAnyActive = 2, kModeEdgeLatch = 3 };

struct DynLane {
  uint8_t lane;
  uint8_t mode;
  uint8_t d;       // decimation 1..128
  uint8_t log2d;
  uint8_t phase;
  uint8_t active;  // active level for any_active / edge_latch
  uint8_t bits;    // 1, or 2 for edge_latch
};

struct DynamicProfile {
  bool valid;
  char reason[40];
  uint8_t width;        // PARLIO data width: 1 / 2 / 4 / 8 / 16
  bool passthrough;     // width < 8 and every channel raw: the raw bytes are the wire
  uint8_t fast;         // raw channels on lanes 0..fast-1
  uint8_t channels;
  uint8_t laneGpio[kMaxChannels];
  DynLane dec[kMaxChannels];    // E115: sorted by (mode, log2 D, phase, active) so equal channels sit on contiguous lanes
  uint8_t decCount;
  uint8_t order[kMaxChannels];  // descriptor index of the channel on each lane (raw lanes first, then the sorted decimated ones)
  struct Group { uint8_t lane0, count, mode, log2d, d, phase, active; } groups[kMaxChannels];
  uint8_t groupCount;
  uint8_t needOrAnd[8]; // per log2 D: any_active present
  uint8_t needEdge[8];  // per log2 D: edge_latch present
  uint8_t reduceMaxLog2d; // highest log2 D that needs bucket reductions (0 = none)
  uint8_t needOr, needAnd, needRise, needFall;  // quantities used by any_active / edge_latch channels with D >= 2
  uint8_t needLevel1;     // some such channel has D = 2
  uint32_t payloadBits;
  uint32_t paddingBits;
  uint32_t wireBytes;
  size_t stageFill;
  uint32_t rawBytesPerSample;
  uint32_t codecLimitMsps;  // bench-derived (see applyCodecLimit)
  uint32_t modelLimitMsps;  // the E111/E112 linear model, reported for comparison
  uint32_t benchMspsX100;
  uint16_t budgetMbps;
  bool grayCheck;
  uint8_t grayLane[8];  // lane carrying Gray bit b (GPIO b + 2)
};
DynamicProfile Dyn;
// Per-worker bucket reductions for the decimated planes: [worker][log2 D][bucket].
uint16_t BucketOr[kWorkers][8][64];
uint16_t BucketAnd[kWorkers][8][64];
uint16_t BucketRise[kWorkers][8][64];
uint16_t BucketFall[kWorkers][8][64];


static size_t activeWireBlockBytes() {
  switch (ProfileCode) {
    case 'W': return kWideWireBlockBytes;
    case 'F': return kFourWireBlockBytes;
    case 'V': return kFiveWireBlockBytes;
    case '2': return kTwoWireBlockBytes;
    case 'D': return Dyn.wireBytes;
    default: return kLegacyWireBlockBytes;
  }
}

static size_t activeStageFill() {
  switch (ProfileCode) {
    case 'W': return kStageFillWide;
    case 'F': return kStageFillFour;
    case 'V': return kStageFillFive;
    case '2': return kStageFillTwo;
    case 'D': return Dyn.stageFill;
    default: return kStageFillLegacy;
  }
}

static const char *activeProfileName() {
  switch (ProfileCode) {
    case 'W': return "wide";
    case 'F': return "four";
    case 'V': return "five";
    case '2': return "two";
    case 'D': return "dynamic";
    default: return "legacy";
  }
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
#if E108_CODEC_O2
#pragma GCC push_options
#pragma GCC optimize("O2")
#endif

static inline __attribute__((always_inline)) uint32_t gatherLow3x4Bytes(uint32_t w) {
  w &= 0x07070707U;
  w = (w | (w >> 5)) & 0x003f003fU;
  return (w | (w >> 10)) & 0x0fffU;
}

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

static inline __attribute__((always_inline)) void encodeBlock(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 8; ++group) {
    const uint32_t *w = words + group * 4;
    store24(wire + group * 3,
            gatherLow3x4Halves(w[0], w[1]) | (gatherLow3x4Halves(w[2], w[3]) << 12));
  }
  wire[24] = static_cast<uint8_t>(words[0] >> 3);
}

static inline __attribute__((always_inline)) void encodeBlock8(
    const uint8_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 8; ++group) {
    const uint32_t *w = words + group * 2;
    store24(wire + group * 3, gatherLow3x4Bytes(w[0]) | (gatherLow3x4Bytes(w[1]) << 12));
  }
  wire[24] = static_cast<uint8_t>((words[0] >> 3) & 0x1f);
}

static inline __attribute__((always_inline)) void encodeBlockWide(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  uint32_t d8 = 0;
  for (size_t group = 0; group < 16; ++group) {
    const uint32_t *w = words + group * 4;
    store24(wire + group * 3,
            gatherLow3x4Halves(w[0], w[1]) | (gatherLow3x4Halves(w[2], w[3]) << 12));
    d8 |= ((w[0] >> 3) & 1U) << group;
  }
  wire[48] = static_cast<uint8_t>(d8);
  wire[49] = static_cast<uint8_t>(d8 >> 8);
  const uint32_t d64 = spread12((words[0] & 0xffffU) >> 4) |
      (spread12((words[32] & 0xffffU) >> 4) << 1);
  store24(wire + 50, d64);
}

// Four 16-bit samples (two words) -> their low 4 bits, packed into 16 bits.
static inline __attribute__((always_inline)) uint32_t gatherLow4x4Halves(uint32_t w0, uint32_t w1) {
  const uint32_t x = (w0 & 0x000f000fU) | ((w1 & 0x000f000fU) << 8);
  return (x | (x >> 12)) & 0xffffU;
}

// Four 16-bit samples (two words) -> their low 5 bits, packed into 20 bits.
static inline __attribute__((always_inline)) uint32_t gatherLow5x4Halves(uint32_t w0, uint32_t w1) {
  return (w0 & 0x1fU) | ((w0 >> 11) & 0x3e0U) | ((w1 & 0x1fU) << 10) | ((w1 >> 1) & 0xf8000U);
}

static inline __attribute__((always_inline)) void store32(uint8_t *out, uint32_t v) {
  out[0] = static_cast<uint8_t>(v);
  out[1] = static_cast<uint8_t>(v >> 8);
  out[2] = static_cast<uint8_t>(v >> 16);
  out[3] = static_cast<uint8_t>(v >> 24);
}

// E112 F: 4 full + 12 D32, 128 samples -> 70 bytes. Snapshots at samples 0/32/64/96.
static inline __attribute__((always_inline)) void encodeBlockFour(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 16; ++group) {
    const uint32_t *w = words + group * 4;
    store32(wire + group * 4, gatherLow4x4Halves(w[0], w[1]) | (gatherLow4x4Halves(w[2], w[3]) << 16));
  }
  const uint64_t d = static_cast<uint64_t>((words[0] & 0xffffU) >> 4) |
      (static_cast<uint64_t>((words[16] & 0xffffU) >> 4) << 12) |
      (static_cast<uint64_t>((words[32] & 0xffffU) >> 4) << 24) |
      (static_cast<uint64_t>((words[48] & 0xffffU) >> 4) << 36);
  for (size_t i = 0; i < 6; ++i) wire[64 + i] = static_cast<uint8_t>(d >> (8 * i));
}

// E112 V: 5 full + 11 D32, 128 samples -> 86 bytes.
static inline __attribute__((always_inline)) void encodeBlockFive(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 16; ++group) {
    const uint32_t *w = words + group * 4;
    const uint32_t r0 = gatherLow5x4Halves(w[0], w[1]);
    const uint32_t r1 = gatherLow5x4Halves(w[2], w[3]);
    uint8_t *out = wire + group * 5;
    out[0] = static_cast<uint8_t>(r0);
    out[1] = static_cast<uint8_t>(r0 >> 8);
    out[2] = static_cast<uint8_t>((r0 >> 16) | (r1 << 4));
    out[3] = static_cast<uint8_t>(r1 >> 4);
    out[4] = static_cast<uint8_t>(r1 >> 12);
  }
  const uint64_t d = static_cast<uint64_t>((words[0] & 0xffffU) >> 5) |
      (static_cast<uint64_t>((words[16] & 0xffffU) >> 5) << 11) |
      (static_cast<uint64_t>((words[32] & 0xffffU) >> 5) << 22) |
      (static_cast<uint64_t>((words[48] & 0xffffU) >> 5) << 33);
  for (size_t i = 0; i < 6; ++i) wire[80 + i] = static_cast<uint8_t>(d >> (8 * i));
}

enum class Profile { Legacy16, Eight, Wide, FourD32, FiveD32, Two };

static uint64_t stageExpectedFill(uint32_t index) {
  const uint64_t start = static_cast<uint64_t>(index) * StageFill;
  if (start >= TotalWireBytes) return 0;
  const uint64_t remain = TotalWireBytes - start;
  return remain < StageFill ? remain : StageFill;
}

// Return the buffer slot for stage `index`, assigning one from the free list
// the first time the index is seen. Both workers may ask for the same index and
// get the same slot. A bounded wait: on timeout the run is aborted instead of
// starving IDLE0 into the task watchdog. Returns nullptr when aborted.
static uint32_t armQueued();
static size_t spillUsed();

static inline uint8_t *acquireStage(uint32_t index) {
  const size_t e = index % kSlotTable;
  bool waited = false;
  int64_t deadline = 0;
  while (true) {
    uint8_t slot = 0xff;
    portENTER_CRITICAL(&StageMux);
    if (SlotTableIndex[e] == index) {
      slot = SlotTableSlot[e];
    } else if (FreeSlots) {
      slot = static_cast<uint8_t>(__builtin_ctz(FreeSlots));
      FreeSlots &= ~(1u << slot);
      SlotTableIndex[e] = index;
      SlotTableSlot[e] = slot;
      StageFilled[slot] = 0;
    }
    portEXIT_CRITICAL(&StageMux);
    if (slot != 0xff) return Stage[slot];
    if (!waited) {
      waited = true;
      ++SlotAcquireWaits;
      ++StageWaits;
      deadline = esp_timer_get_time() + kStageWaitTimeoutUs;
    }
    if (RunAborted) return nullptr;
    if (esp_timer_get_time() > deadline) {
      ++StageWaitTimeouts;
      AbortFreeSlots = FreeSlots;
      AbortArmQueued = armQueued();
      AbortSpillUsed = spillUsed();
      AbortReadyWaiting = uxQueueMessagesWaiting(ReadyStageQueue);
      AbortInflightKind = Inflight ? InflightSegment.kind + 1 : 0;
      RunAborted = true;
      return nullptr;
    }
    taskYIELD();
  }
}

#ifndef E118_NO_WRITEBACK
#define E118_NO_WRITEBACK 0
#endif
static inline void writebackLines(const uint8_t *begin, size_t bytes) {
#if E118_NO_WRITEBACK
  (void)begin;
  (void)bytes;
  return;  // E118: test whether the per-run C2M writeback is needed at all (host byte-exact check decides)
#endif
  const uintptr_t start = reinterpret_cast<uintptr_t>(begin) & ~uintptr_t(63);
  const uintptr_t end = (reinterpret_cast<uintptr_t>(begin) + bytes + 63) & ~uintptr_t(63);
  esp_cache_msync(reinterpret_cast<void *>(start), end - start, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

// Account bytes written into stage `index`; the worker that completes the
// stage publishes it.
static void commitStage(uint32_t index, size_t bytes) {
  bool complete = false;
  uint32_t filled = 0;
  uint8_t b = 0;
  portENTER_CRITICAL(&StageMux);
  b = SlotTableSlot[index % kSlotTable];  // assigned by acquireStage before any write
  StageFilled[b] += bytes;
  filled = StageFilled[b];
  if (filled >= stageExpectedFill(index)) complete = true;
  portEXIT_CRITICAL(&StageMux);
  if (complete) {
    const WireChunk ready = {Stage[b], filled, index};
    xQueueSend(ReadyStageQueue, &ready, portMAX_DELAY);
  }
}

template <Profile P, bool kCheck>
static void codecWorkerBody(unsigned worker) {
  constexpr bool kWideBlock = P == Profile::Wide || P == Profile::FourD32 || P == Profile::FiveD32;
  constexpr size_t kSamples = P == Profile::Two ? kTwoBlockSamples : kWideBlock ? kWideBlockSamples : kLegacyBlockSamples;
  constexpr size_t kRaw = P == Profile::Two ? kTwoWireBlockBytes : kSamples * (P == Profile::Eight ? 1 : 2);
  constexpr size_t kWire = P == Profile::Wide ? kWideWireBlockBytes
      : P == Profile::FourD32 ? kFourWireBlockBytes
      : P == Profile::FiveD32 ? kFiveWireBlockBytes
      : P == Profile::Two ? kTwoWireBlockBytes : kLegacyWireBlockBytes;
  constexpr uint8_t kExpectedDelta = kSamples / 4;
  constexpr uint8_t kDeltaMargin = kSamples / 64;
  const uint64_t target = TargetBlocks;
  const size_t fill = StageFill;
  uint32_t idle = 0;
  uint32_t chunks = 0;
  uint32_t rawSequenceBad = 0;
  uint32_t duplicateBad = 0;
  size_t maxInflight = 0;
  uint64_t encCycles = 0;
  uint32_t encBlocks = 0;
  uint64_t wbCycles = 0;
  uint32_t wbRuns = 0;
  uint64_t qCycles = 0;
  uint8_t straddle[kRaw] __attribute__((aligned(64)));
  const auto encode = [](const uint8_t *in, uint8_t *out) __attribute__((always_inline)) {
    if constexpr (P == Profile::Two) memcpy(out, in, kTwoWireBlockBytes);
    else if constexpr (P == Profile::Wide) encodeBlockWide(reinterpret_cast<const uint16_t *>(in), out);
    else if constexpr (P == Profile::FourD32) encodeBlockFour(reinterpret_cast<const uint16_t *>(in), out);
    else if constexpr (P == Profile::FiveD32) encodeBlockFive(reinterpret_cast<const uint16_t *>(in), out);
    else if constexpr (P == Profile::Eight) encodeBlock8(in, out);
    else encodeBlock(reinterpret_cast<const uint16_t *>(in), out);
  };
  // Gray check on consecutive blocks handled by this worker inside one chunk.
  bool previousValid = false;
  uint8_t previousBinary = 0;
  const auto check = [&](const uint8_t *bytes) __attribute__((always_inline)) {
    if (!kCheck) return;
    if constexpr (P == Profile::Two) {
      // 4 samples per byte; the source advances every 4 samples, so a 256-sample
      // block is 64 steps and the visible low two Gray bits repeat exactly.
      const uint8_t low2 = bytes[0] & 3;
      if (previousValid && low2 != (previousBinary & 3)) ++rawSequenceBad;
      previousBinary = low2;
      previousValid = true;
      return;
    }
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
  // Encode `count` consecutive blocks starting at block index `first` from `in`
  // into their stage(s). Returns false when the run was aborted.
  const auto place = [&](uint64_t first, const uint8_t *in, size_t count) -> bool {
    const uint64_t wireOffset = first * kWire;
    uint32_t stage = static_cast<uint32_t>(wireOffset / fill);
    size_t offset = static_cast<size_t>(wireOffset % fill);
    while (count) {
      size_t n = (fill - offset) / kWire;
      if (n > count) n = count;
      uint8_t *const stageBase = acquireStage(stage);
      if (!stageBase) return false;
      uint8_t *out = stageBase + offset;
      uint8_t *const outBegin = out;
      for (size_t i = 0; i < n; ++i, in += kRaw, out += kWire) {
#if E108_CODEC_PREFETCH
        if (i + 1 < n) {
          for (size_t line = 0; line < kRaw; line += 64) {
            const uint32_t touched = *reinterpret_cast<const volatile uint32_t *>(in + kRaw + line);
            asm volatile("" : : "r"(touched));
          }
        }
#endif
        check(in);
        encode(in, out);
      }
      writebackLines(outBegin, n * kWire);
      commitStage(stage, n * kWire);
      count -= n;
      ++stage;
      offset = 0;
    }
    return true;
  };
  while (!RunAborted) {
    Chunk chunk = {};
    if (xQueueReceive(ChunkQueue, &chunk, pdMS_TO_TICKS(200)) != pdTRUE) {
      if (++idle >= 10) break;
      continue;
    }
    idle = 0;
    previousValid = false;
    const size_t tail = static_cast<size_t>(chunk.rawOffset % kRaw);  // bytes of a block begun in the previous chunk
    uint64_t block = chunk.rawOffset / kRaw;                             // index of the block containing data[0]
    size_t at = 0;
    if (block >= target) break;
    if (tail != 0) {
      const size_t lead = kRaw - tail;
      if (chunk.length < lead) {
        ++ShortChunks;
        continue;
      }
      // The previous chunk's tail is still in the ring just before this chunk
      // (or at the ring end when this chunk starts the ring).
      const uint8_t *previous = chunk.data == Ring ? Ring + kRingBytes - tail : chunk.data - tail;
      memcpy(straddle, previous, tail);
      memcpy(straddle + tail, chunk.data, lead);
      if (!place(block, straddle, 1)) break;
      ++block;
      at = lead;
    }
    size_t full = (chunk.length - at) / kRaw;
    if (block + full > target) full = static_cast<size_t>(target - block);
    if (full && !place(block, chunk.data + at, full)) break;
    ++chunks;
    __atomic_fetch_add(&EncodedBlocks, static_cast<uint64_t>(full) + (tail ? 1 : 0), __ATOMIC_RELAXED);
    const uint64_t processed = __atomic_add_fetch(&ProcessedRaw, static_cast<uint64_t>(chunk.length), __ATOMIC_RELAXED);
    const uint64_t callback = CallbackBytes;
    const size_t inflight = callback > processed ? static_cast<size_t>(callback - processed) : 0;
    if (inflight > maxInflight) maxInflight = inflight;
  }
  WorkerChunks[worker] = chunks;
  EncodeCycles[worker] = encCycles;
  EncodeBlocks[worker] = encBlocks;
  WritebackCycles[worker] = wbCycles;
  WritebackRuns[worker] = wbRuns;
  QueueCycles[worker] = qCycles;
  WorkerUs[worker] = taskRunTime(nullptr);
  __atomic_fetch_add(&RawSequenceBad, rawSequenceBad, __ATOMIC_RELAXED);
  __atomic_fetch_add(&DuplicateBad, duplicateBad, __ATOMIC_RELAXED);
  portENTER_CRITICAL(&StageMux);
  if (maxInflight > MaxInflight) MaxInflight = maxInflight;
  portEXIT_CRITICAL(&StageMux);
}

template <bool kCheck>
static void dynamicWorkerBody(unsigned worker);

static void codecWorkerTask(void *arg) {
  const unsigned worker = static_cast<unsigned>(reinterpret_cast<uintptr_t>(arg));
  const bool check = (Flags & kFlagNoCheck) == 0;
  switch (ProfileCode) {
    case 'W': check ? codecWorkerBody<Profile::Wide, true>(worker) : codecWorkerBody<Profile::Wide, false>(worker); break;
    case 'F': check ? codecWorkerBody<Profile::FourD32, true>(worker) : codecWorkerBody<Profile::FourD32, false>(worker); break;
    case 'V': check ? codecWorkerBody<Profile::FiveD32, true>(worker) : codecWorkerBody<Profile::FiveD32, false>(worker); break;
    case '8': check ? codecWorkerBody<Profile::Eight, true>(worker) : codecWorkerBody<Profile::Eight, false>(worker); break;
    case '2': check ? codecWorkerBody<Profile::Two, true>(worker) : codecWorkerBody<Profile::Two, false>(worker); break;
    case 'D': check ? dynamicWorkerBody<true>(worker) : dynamicWorkerBody<false>(worker); break;
    default: check ? codecWorkerBody<Profile::Legacy16, true>(worker) : codecWorkerBody<Profile::Legacy16, false>(worker); break;
  }
  xSemaphoreGive(HarvestDone);
  vTaskDelete(nullptr);
}

// ---- E114: descriptor-driven profile ----------------------------------------
static void sendLine(int length);
#pragma GCC push_options
#pragma GCC optimize("O2")
static uint32_t gcd32(uint32_t a, uint32_t b) { while (b) { const uint32_t t = a % b; a = b; b = t; } return a; }

static bool reject(const char *why) {
  strncpy(Dyn.reason, why, sizeof(Dyn.reason) - 1);
  Dyn.reason[sizeof(Dyn.reason) - 1] = 0;
  Dyn.valid = false;
  return false;
}

// Measured anchors (E111 / E112 / E113, two workers): 16-bit F=3 -> 72, F=4 -> 68,
// F=5 -> 64 Msps; 8-bit F=3 -> 108; passthrough 160. Linear in F, discounted for
// the bucket reductions. This is the "internal limit" the device rejects against.
static uint32_t codecLimitModel(const DynamicProfile &p) {
  if (p.passthrough) return 160;
  int32_t limit = p.width == 16 ? 72 - 4 * (static_cast<int32_t>(p.fast) - 3) : 108 - 6 * (static_cast<int32_t>(p.fast) - 3);
  bool anyActive = false, edge = false;
  for (unsigned i = 0; i < p.decCount; ++i) {
    anyActive |= p.dec[i].mode == kModeAnyActive;
    edge |= p.dec[i].mode == kModeEdgeLatch;
  }
  if (anyActive) limit = limit * 9 / 10;
  if (edge) limit = limit * 7 / 10;
  if (limit < 1) limit = 1;
  return static_cast<uint32_t>(limit);
}

static bool buildDynamicProfile(const uint8_t *cmd, uint32_t length, uint32_t rateHz) {
  memset(&Dyn, 0, sizeof(Dyn));
  const unsigned n = cmd[15];
  if (n < 1 || n > kMaxChannels) return reject("channel_count");
  if (length < kDescHeaderBytes + n * kDescEntryBytes) return reject("descriptor_short");
  if (rateHz < 1000000 || rateHz > 160000000) return reject("rate_range");
  Dyn.channels = n;
  Dyn.budgetMbps = static_cast<uint16_t>(cmd[16] | (cmd[17] << 8));
  // Pass 1: validate and place raw channels on the low lanes, decimated after.
  unsigned fast = 0;
  for (unsigned i = 0; i < n; ++i) {
    const uint8_t *e = cmd + kDescHeaderBytes + i * kDescEntryBytes;
    const uint8_t mode = e[1], log2d = e[2];
    if (e[0] > 54) return reject("gpio_range");
    if (mode > kModeEdgeLatch) return reject("mode");
    if (log2d > 7) return reject("decimation_range");
    if (mode == kModeRaw && log2d != 0) return reject("raw_decimation");
    if ((e[3] & 0x7f) >= (1u << log2d)) return reject("phase_range");
    if (mode == kModeRaw) Dyn.laneGpio[fast++] = e[0];
  }
  Dyn.fast = fast;
  // Raw lanes keep descriptor order; remember their descriptor indices.
  {
    unsigned r = 0;
    for (unsigned i = 0; i < n; ++i) {
      if (cmd[kDescHeaderBytes + i * kDescEntryBytes + 1] == kModeRaw) Dyn.order[r++] = static_cast<uint8_t>(i);
    }
  }
  // E115: decimated channels sorted by (mode, log2 D, phase, active) so that
  // channels with identical bucket structure sit on contiguous lanes and can be
  // extracted as one bit-matrix transpose per group (encodeDynamicBlock).
  uint8_t decIndex[kMaxChannels];
  unsigned decN = 0;
  for (unsigned i = 0; i < n; ++i) {
    if (cmd[kDescHeaderBytes + i * kDescEntryBytes + 1] != kModeRaw) decIndex[decN++] = static_cast<uint8_t>(i);
  }
  const auto key = [&](uint8_t idx) -> uint32_t {
    const uint8_t *e = cmd + kDescHeaderBytes + idx * kDescEntryBytes;
    return (static_cast<uint32_t>(e[1]) << 24) | (static_cast<uint32_t>(e[2]) << 16) | (static_cast<uint32_t>(e[3] & 0x7f) << 8) | (e[3] >> 7);
  };
  for (unsigned i = 1; i < decN; ++i) {  // insertion sort, stable
    const uint8_t v = decIndex[i];
    unsigned j = i;
    while (j > 0 && key(decIndex[j - 1]) > key(v)) { decIndex[j] = decIndex[j - 1]; --j; }
    decIndex[j] = v;
  }
  unsigned lane = fast;
  for (unsigned s = 0; s < decN; ++s) {
    const unsigned i = decIndex[s];
    const uint8_t *e = cmd + kDescHeaderBytes + i * kDescEntryBytes;
    DynLane &d = Dyn.dec[Dyn.decCount++];
    d.lane = lane;
    d.mode = e[1];
    d.log2d = e[2];
    d.d = 1u << e[2];
    d.phase = e[3] & 0x7f;
    d.active = e[3] >> 7;
    d.bits = e[1] == kModeEdgeLatch ? 2 : 1;
    if (d.mode == kModeAnyActive) Dyn.needOrAnd[d.log2d] = 1;
    if (d.mode == kModeEdgeLatch) Dyn.needEdge[d.log2d] = 1;
    if (d.log2d >= 1 && d.mode != kModeHold) {
      if (d.log2d > Dyn.reduceMaxLog2d) Dyn.reduceMaxLog2d = d.log2d;
      if (d.log2d == 1) Dyn.needLevel1 = 1;
      if (d.mode == kModeAnyActive) (d.active ? Dyn.needOr : Dyn.needAnd) = 1;
      else (d.active ? Dyn.needRise : Dyn.needFall) = 1;
    }
    Dyn.order[lane] = static_cast<uint8_t>(i);
    Dyn.laneGpio[lane++] = e[0];
    // Group: extend the previous one when the bucket structure is identical.
    DynamicProfile::Group *g = Dyn.groupCount ? &Dyn.groups[Dyn.groupCount - 1] : nullptr;
    if (g && g->mode == d.mode && g->log2d == d.log2d && g->phase == d.phase && g->active == d.active) {
      ++g->count;
    } else {
      DynamicProfile::Group &ng = Dyn.groups[Dyn.groupCount++];
      ng = {static_cast<uint8_t>(lane - 1), 1, d.mode, d.log2d, d.d, d.phase, d.active};
    }
  }
  // Width.
  if (Dyn.decCount == 0 && (n == 1 || n == 2 || n == 4)) {
    Dyn.width = n;
    Dyn.passthrough = true;
  } else {
    Dyn.width = n <= 8 ? 8 : 16;
  }
  for (unsigned i = n; i < Dyn.width && i < kMaxChannels; ++i) Dyn.laneGpio[i] = Dyn.laneGpio[0];  // unused lanes: harmless duplicate
  Dyn.rawBytesPerSample = Dyn.width >= 8 ? Dyn.width / 8 : 0;
  // Wire layout.
  if (Dyn.passthrough) {
    Dyn.payloadBits = kDynBlockSamples * Dyn.width;
  } else {
    Dyn.payloadBits = kDynBlockSamples * Dyn.fast;
    for (unsigned i = 0; i < Dyn.decCount; ++i) Dyn.payloadBits += (kDynBlockSamples / Dyn.dec[i].d) * Dyn.dec[i].bits;
  }
  uint32_t wire = (Dyn.payloadBits + 7) / 8;
  Dyn.paddingBits = wire * 8 - Dyn.payloadBits;
  uint32_t l = wire / gcd32(wire, 512) * 512;
  if (l > kStageBytes) {
    const uint32_t padded = (wire + 7) & ~7u;
    Dyn.paddingBits += (padded - wire) * 8;
    wire = padded;
    l = wire / gcd32(wire, 512) * 512;
  }
  if (l > kStageBytes) return reject("stage_alignment");
  Dyn.wireBytes = wire;
  Dyn.stageFill = (kStageBytes / l) * l;
  // Limits.
  const double rawMbPerSec = static_cast<double>(rateHz) * Dyn.width / 8.0 / 1e6;
  if (rawMbPerSec > 160.0) return reject("raw_bandwidth");
  Dyn.modelLimitMsps = codecLimitModel(Dyn);
  Dyn.codecLimitMsps = 0;  // set by applyCodecLimit after the bench
  const double wireMbps = static_cast<double>(rateHz) / kDynBlockSamples * Dyn.wireBytes * 8.0 / 1e6;
  if (Dyn.budgetMbps && wireMbps > Dyn.budgetMbps) return reject("usb_budget");
  // Device-side Gray check is possible when GPIO 2..9 are all captured.
  Dyn.grayCheck = true;
  for (unsigned b = 0; b < 8; ++b) {
    Dyn.grayLane[b] = 0xff;
    for (unsigned i = 0; i < n; ++i) {
      if (Dyn.laneGpio[i] == b + 2) { Dyn.grayLane[b] = i; break; }
    }
    if (Dyn.grayLane[b] == 0xff) Dyn.grayCheck = false;
  }
  if (Dyn.width < 8) Dyn.grayCheck = false;
  Dyn.valid = true;
  strncpy(Dyn.reason, "ok", sizeof(Dyn.reason));
  return true;
}

static void sendDescriptorReply(uint32_t rateHz) {
  char lanes[kMaxChannels * 4 + 1];
  char order[kMaxChannels * 4 + 1];
  int at = 0;
  for (unsigned i = 0; i < Dyn.channels && at < static_cast<int>(sizeof(lanes)) - 4; ++i) {
    at += snprintf(lanes + at, sizeof(lanes) - at, "%s%u", i ? "," : "", Dyn.laneGpio[i]);
  }
  at = 0;
  for (unsigned i = 0; i < Dyn.channels && at < static_cast<int>(sizeof(order)) - 4; ++i) {
    at += snprintf(order + at, sizeof(order) - at, "%s%u", i ? "," : "", Dyn.order[i]);
  }
  const double rawMbPerSec = static_cast<double>(rateHz) * Dyn.width / 8.0 / 1e6;
  const double wireMbps = static_cast<double>(rateHz) / kDynBlockSamples * Dyn.wireBytes * 8.0 / 1e6;
  const int length = snprintf(
      reinterpret_cast<char *>(StatusBuffer), kStatusBytes,
      "E118_DESCRIPTOR accept=%u reason=%s rate_hz=%lu width=%u passthrough=%u fast=%u channels=%u dec=%u block=%u "
      "payload_bits=%lu padding_bits=%lu wire_block_bytes=%lu stage_fill=%lu raw_mb_s=%.3f wire_mbps=%.3f "
      "budget_mbps=%u codec_limit_msps=%lu model_limit_msps=%lu gray_check=%u bench_blocks=%lu bench_us=%llu bench_msps=%.2f bench_cold=%u groups=%u order=%s lanes=%s\n",
      Dyn.valid ? 1U : 0U, Dyn.reason, static_cast<unsigned long>(rateHz), Dyn.width, Dyn.passthrough ? 1U : 0U,
      Dyn.fast, Dyn.channels, Dyn.decCount, static_cast<unsigned>(kDynBlockSamples),
      static_cast<unsigned long>(Dyn.payloadBits), static_cast<unsigned long>(Dyn.paddingBits),
      static_cast<unsigned long>(Dyn.wireBytes), static_cast<unsigned long>(Dyn.stageFill),
      rawMbPerSec, wireMbps, Dyn.budgetMbps, static_cast<unsigned long>(Dyn.codecLimitMsps),
      static_cast<unsigned long>(Dyn.modelLimitMsps), Dyn.grayCheck ? 1U : 0U, static_cast<unsigned long>(DynBenchBlocks), static_cast<unsigned long long>(DynBenchUs),
      DynBenchUs ? static_cast<double>(DynBenchBlocks) * kDynBlockSamples / static_cast<double>(DynBenchUs) : 0.0,
      (BenchFlags & kFlagBenchCold) ? 1U : 0U, Dyn.groupCount, order, lanes);
  sendLine(length);
}

// Fast lanes: F bits per sample, sample-major, 8 samples -> F bytes. F is a
// template parameter and everything stays in 32-bit arithmetic: a 64-bit
// accumulator with variable shifts became libgcc calls at -Os and made the codec
// ~40x slower than the fixed profiles.
template <unsigned F>
static inline __attribute__((always_inline)) uint32_t dynGather4Halves(uint32_t w0, uint32_t w1) {
  // Two words = four 16-bit samples -> 4F bits: s0 | s1 << F | s2 << 2F | s3 << 3F.
  // E115: fold each word's two halves in two ops (valid for every F <= 8 since 2F <= 16).
  constexpr uint32_t lo = (1u << F) - 1;
  constexpr uint32_t m = lo | (lo << 16);
  constexpr uint32_t lo2 = (1u << (2 * F)) - 1;
  uint32_t a = w0 & m;
  uint32_t b = w1 & m;
  a = (a | (a >> (16 - F))) & lo2;
  b = (b | (b >> (16 - F))) & lo2;
  return a | (b << (2 * F));
}

template <unsigned F>
static inline __attribute__((always_inline)) uint32_t dynGather4Bytes(uint32_t w) {
  // One word = four 8-bit samples -> 4F bits.
  constexpr uint32_t lo = (1u << F) - 1;
  if constexpr (F <= 4) {
    // E115: two folds (E106's fixed-profile form); the fields never overlap for F <= 4.
    constexpr uint32_t m1 = 0x01010101u * lo;
    constexpr uint32_t lo2 = (1u << (2 * F)) - 1;
    constexpr uint32_t m2 = lo2 | (lo2 << 16);
    constexpr uint32_t lo4 = (1u << (4 * F)) - 1;
    uint32_t x = w & m1;
    x = (x | (x >> (8 - F))) & m2;
    x = (x | (x >> (16 - 2 * F))) & lo4;
    return x;
  } else {
    return (w & lo) | (((w >> 8) & lo) << F) | (((w >> 16) & lo) << (2 * F)) | (((w >> 24) & lo) << (3 * F));
  }
}

// Emit 8 samples' worth (8F bits) from two 4F-bit halves using 32-bit ops only.
template <unsigned F>
static inline __attribute__((always_inline)) void dynEmit8(uint8_t *&o, uint32_t r0, uint32_t r1) {
  constexpr unsigned bits = 4 * F;
  constexpr unsigned whole = bits / 8;
  for (unsigned i = 0; i < whole; ++i) *o++ = static_cast<uint8_t>(r0 >> (8 * i));
  if constexpr (bits % 8 == 4) {
    *o++ = static_cast<uint8_t>((r0 >> (bits - 4)) | (r1 << 4));
    r1 >>= 4;
    for (unsigned i = 0; i < (bits - 4) / 8; ++i) *o++ = static_cast<uint8_t>(r1 >> (8 * i));
  } else {
    for (unsigned i = 0; i < whole; ++i) *o++ = static_cast<uint8_t>(r1 >> (8 * i));
  }
}

template <unsigned F, bool kWide16>
static inline __attribute__((always_inline)) void encodeDynamicFast(const uint8_t *in, uint8_t *&o) {
  if constexpr (F == 0) {
    (void)in;
    return;
  } else if constexpr (kWide16) {
    const auto *words = reinterpret_cast<const uint32_t *>(in);
    for (unsigned g = 0; g < 16; ++g) {
      const uint32_t *w = words + g * 4;
      dynEmit8<F>(o, dynGather4Halves<F>(w[0], w[1]), dynGather4Halves<F>(w[2], w[3]));
    }
  } else if constexpr (F == 8) {
    memcpy(o, in, kDynBlockSamples);
    o += kDynBlockSamples;
  } else {
    const auto *words = reinterpret_cast<const uint32_t *>(in);
    for (unsigned g = 0; g < 16; ++g) {
      const uint32_t *w = words + g * 2;
      dynEmit8<F>(o, dynGather4Bytes<F>(w[0]), dynGather4Bytes<F>(w[1]));
    }
  }
}

template <bool kWide16>
static inline uint32_t dynSample(const uint8_t *in, unsigned i) {
  if constexpr (kWide16) return reinterpret_cast<const uint16_t *>(in)[i];
  else return in[i];
}

// Bucket reductions over the whole sample width, one quantity at a time and only
// for the quantities the descriptor uses. A 32-bit word packs 4 (8-bit) or 2
// (16-bit) samples, so the word level (D = 4 or D = 2) is a few shifts per word;
// higher levels combine pairs. Rise / fall follow E105: only transitions strictly
// inside the bucket count, so combining two halves adds the transition across
// their boundary (cross[j] = transition from word j-1's last sample into word j).
enum DynQuantity : unsigned { kQOr = 0, kQAnd = 1, kQRise = 2, kQFall = 3 };

template <bool kWide16, unsigned Q>
static inline __attribute__((always_inline)) uint32_t dynWordFold(uint32_t w) {
  if constexpr (kWide16) {
    if constexpr (Q == kQOr) return (w | (w >> 16)) & 0xffffu;
    else if constexpr (Q == kQAnd) return (w & (w >> 16)) & 0xffffu;
    else if constexpr (Q == kQRise) return (~w & (w >> 16)) & 0xffffu;
    else return (w & ~(w >> 16)) & 0xffffu;
  } else {
    uint32_t x;
    if constexpr (Q == kQOr) {
      x = w | (w >> 16);
      x |= x >> 8;
    } else if constexpr (Q == kQAnd) {
      x = w & (w >> 16);
      x &= x >> 8;
    } else if constexpr (Q == kQRise) {
      x = ~w & (w >> 8);  // bytes 0..2 = t1..t3, byte 3 = 0
      x |= x >> 16;
      x |= x >> 8;
    } else {
      x = (w & ~(w >> 8)) & 0x00ffffffu;  // bytes 0..2 = f1..f3
      x |= x >> 16;
      x |= x >> 8;
    }
    return x & 0xffu;
  }
}

template <bool kWide16, unsigned Q>
static void dynReduceQuantity(const uint8_t *__restrict in, uint16_t (*__restrict levels)[64], unsigned maxLog2d, bool needLevel1) {
  const uint32_t *__restrict words = reinterpret_cast<const uint32_t *>(in);
  constexpr unsigned kWords = kWide16 ? 64 : 32;
  constexpr unsigned kWordLevel = kWide16 ? 1 : 2;
  constexpr bool kEdge = Q >= kQRise;
  uint16_t cross[kWords];
  uint16_t *__restrict base = levels[kWordLevel];
  uint32_t prev = 0;
  for (unsigned j = 0; j < kWords; ++j) {
    const uint32_t w = words[j];
    base[j] = static_cast<uint16_t>(dynWordFold<kWide16, Q>(w));
    if constexpr (kEdge) {
      if constexpr (kWide16) cross[j] = static_cast<uint16_t>((Q == kQRise ? ((~prev) >> 16) & w : (prev >> 16) & ~w) & 0xffffu);
      else cross[j] = static_cast<uint16_t>((Q == kQRise ? ((~prev) >> 24) & w : (prev >> 24) & ~w) & 0xffu);
      prev = w;
    }
  }
  if constexpr (!kWide16) {
    if (needLevel1) {  // D = 2 inside a word: sample pairs (0,1) and (2,3)
      uint16_t *__restrict l1 = levels[1];
      for (unsigned j = 0; j < kWords; ++j) {
        const uint32_t w = words[j];
        uint32_t y;
        if constexpr (Q == kQOr) y = w | (w >> 8);
        else if constexpr (Q == kQAnd) y = w & (w >> 8);
        else if constexpr (Q == kQRise) y = ~w & (w >> 8);
        else y = w & ~(w >> 8);
        l1[2 * j] = static_cast<uint16_t>(y & 0xffu);
        l1[2 * j + 1] = static_cast<uint16_t>((y >> 16) & 0xffu);
      }
    }
  } else {
    (void)needLevel1;
  }
  for (unsigned l = kWordLevel + 1; l <= maxLog2d; ++l) {
    const unsigned buckets = kDynBlockSamples >> l;
    const uint16_t *__restrict lo = levels[l - 1];
    uint16_t *__restrict hi = levels[l];
    const unsigned shift = l - kWordLevel - 1;  // first word of the second half = (2k+1) << shift
    for (unsigned k = 0; k < buckets; ++k) {
      uint32_t v;
      if constexpr (Q == kQAnd) v = lo[2 * k] & lo[2 * k + 1];
      else v = lo[2 * k] | lo[2 * k + 1];
      if constexpr (kEdge) v |= cross[(2 * k + 1) << shift];
      hi[k] = static_cast<uint16_t>(v);
    }
  }
}

static const uint16_t kZeroBuckets[64] = {};

// Append n (<= 16) bits; the accumulator never exceeds 24 bits.
static inline __attribute__((always_inline)) void dynAppend(uint8_t *&o, uint32_t &acc, unsigned &bits, uint32_t word, unsigned n) {
  acc |= word << bits;
  bits += n;
  while (bits >= 8) {
    *o++ = static_cast<uint8_t>(acc);
    acc >>= 8;
    bits -= 8;
  }
}

// Append n (<= 32) bits.
static inline __attribute__((always_inline)) void dynAppend32(uint8_t *&o, uint32_t &acc, unsigned &bits, uint32_t word, unsigned n) {
  if (n > 16) {
    dynAppend(o, acc, bits, word & 0xffffu, 16);
    word >>= 16;
    n -= 16;
  }
  dynAppend(o, acc, bits, word, n);
}

// E115: spread bit c of x to bit c << LOG2B (x has at most 32 >> LOG2B bits).
// Summing dynSpread<LOG2B>(x_k) << k over the B = 1 << LOG2B buckets of a group
// transposes the B x G bucket-by-channel bit matrix into channel-major planes:
// output bit (c * B + k) = bit c of x_k, i.e. the wire layout of G channels.
template <unsigned LOG2B>
static inline __attribute__((always_inline)) uint32_t dynSpread(uint32_t x) {
  if constexpr (LOG2B == 0) {
    return x;
  } else if constexpr (LOG2B == 1) {
    x = (x | (x << 8)) & 0x00ff00ffu;
    x = (x | (x << 4)) & 0x0f0f0f0fu;
    x = (x | (x << 2)) & 0x33333333u;
    x = (x | (x << 1)) & 0x55555555u;
    return x;
  } else if constexpr (LOG2B == 2) {
    x = (x | (x << 12)) & 0x000f000fu;
    x = (x | (x << 6)) & 0x03030303u;
    x = (x | (x << 3)) & 0x11111111u;
    return x;
  } else if constexpr (LOG2B == 3) {
    x = (x | (x << 14)) & 0x00030003u;
    x = (x | (x << 7)) & 0x01010101u;
    return x;
  } else {
    static_assert(LOG2B == 4, "spread stride up to 16");
    x = (x | (x << 15)) & 0x00010001u;
    return x;
  }
}

static inline __attribute__((always_inline)) uint32_t dynLowMask(unsigned g) {
  return g >= 32 ? 0xffffffffu : ((1u << g) - 1);
}

// hold (and any_active with D = 1): bucket k of every channel in the group is
// sample k * D + phase; take G bits from lane0 and transpose.
template <unsigned LOG2B, bool kWide16>
static inline __attribute__((always_inline)) void dynGroupHold(const uint8_t *__restrict in, unsigned lane0, unsigned count, unsigned phase, uint8_t *&o,
                                uint32_t &acc, unsigned &bits) {
  constexpr unsigned B = 1u << LOG2B;
  constexpr unsigned D = kDynBlockSamples / B;
  constexpr unsigned kPer = 32 >> LOG2B;
  constexpr size_t kBytes = kWide16 ? 2 : 1;
  const uint8_t *const s0 = in + kBytes * phase;
  while (count) {
    const unsigned g = count < kPer ? count : kPer;
    const uint32_t mask = dynLowMask(g);
    uint32_t word = 0;
    const uint8_t *s = s0;
    for (unsigned k = 0; k < B; ++k, s += kBytes * D) {
      const uint32_t x = ((kWide16 ? *reinterpret_cast<const uint16_t *>(s) : *s) >> lane0) & mask;
      word |= dynSpread<LOG2B>(x) << k;
    }
    dynAppend32(o, acc, bits, word, g * B);
    lane0 += g;
    count -= g;
  }
}

// any_active with D >= 2: bucket values come from the OR / AND level array.
template <unsigned LOG2B>
static inline __attribute__((always_inline)) void dynGroupVec(const uint16_t *__restrict vec, unsigned lane0, unsigned count, uint8_t *&o, uint32_t &acc,
                               unsigned &bits) {
  constexpr unsigned B = 1u << LOG2B;
  constexpr unsigned kPer = 32 >> LOG2B;
  while (count) {
    const unsigned g = count < kPer ? count : kPer;
    const uint32_t mask = dynLowMask(g);
    uint32_t word = 0;
    for (unsigned k = 0; k < B; ++k) word |= dynSpread<LOG2B>((vec[k] >> lane0) & mask) << k;
    dynAppend32(o, acc, bits, word, g * B);
    lane0 += g;
    count -= g;
  }
}

// edge_latch: per bucket 2 bits (bucket-end level | edge << 1). Level and edge
// planes are transposed with stride 2B and interleaved.
template <unsigned LOG2B, bool kWide16>
static inline __attribute__((always_inline)) void dynGroupEdge(const uint8_t *__restrict in, const uint16_t *__restrict vec, unsigned lane0, unsigned count,
                                uint8_t *&o, uint32_t &acc, unsigned &bits) {
  constexpr unsigned B = 1u << LOG2B;
  constexpr unsigned D = kDynBlockSamples / B;
  constexpr unsigned kPer = 32 >> (LOG2B + 1);
  constexpr size_t kBytes = kWide16 ? 2 : 1;
  const uint8_t *const s0 = in + kBytes * (D - 1);
  while (count) {
    const unsigned g = count < kPer ? count : kPer;
    const uint32_t mask = dynLowMask(g);
    uint32_t word = 0;
    const uint8_t *s = s0;
    for (unsigned k = 0; k < B; ++k, s += kBytes * D) {
      const uint32_t level = ((kWide16 ? *reinterpret_cast<const uint16_t *>(s) : *s) >> lane0) & mask;
      const uint32_t edge = (vec[k] >> lane0) & mask;
      word |= (dynSpread<LOG2B + 1>(level) << (2 * k)) | (dynSpread<LOG2B + 1>(edge) << (2 * k + 1));
    }
    dynAppend32(o, acc, bits, word, g * 2 * B);
    lane0 += g;
    count -= g;
  }
}

// Fallback (D <= 4 for hold / any, D <= 8 for edge): the E114 per-channel path.
template <bool kWide16>
static inline __attribute__((always_inline)) void dynGroupSlow(const uint8_t *__restrict in, unsigned worker, const DynamicProfile::Group &gref, uint8_t *&o, uint32_t &acc,
                         unsigned &bits) {
  const DynamicProfile::Group g = gref;  // by value: byte stores below would otherwise force reloads of every field
  constexpr size_t kBytes = kWide16 ? 2 : 1;
  const unsigned d = g.d;
  const size_t stride = kBytes * d;
  const unsigned mode = g.mode, log2d = g.log2d, phase = g.phase, active = g.active, count = g.count, lane0 = g.lane0;
  uint32_t lacc = acc;
  unsigned lbits = bits;
  uint8_t *lo = o;
  for (unsigned c = 0; c < count; ++c) {
    const unsigned lane = lane0 + c;
    unsigned buckets = kDynBlockSamples / d;
    if (mode == kModeHold || (mode == kModeAnyActive && log2d == 0)) {
      const uint8_t *s = in + kBytes * phase;
      while (buckets) {
        const unsigned n = buckets > 16 ? 16 : buckets;
        uint32_t word = 0;
        for (unsigned j = 0; j < n; ++j, s += stride) {
          word |= ((kWide16 ? *reinterpret_cast<const uint16_t *>(s) : *s) >> lane & 1u) << j;
        }
        dynAppend(lo, lacc, lbits, word, n);
        buckets -= n;
      }
    } else if (mode == kModeAnyActive) {
      const uint16_t *vec = active ? BucketOr[worker][log2d] : BucketAnd[worker][log2d];
      while (buckets) {
        const unsigned n = buckets > 16 ? 16 : buckets;
        uint32_t word = 0;
        for (unsigned j = 0; j < n; ++j) word |= ((*vec++ >> lane) & 1u) << j;
        dynAppend(lo, lacc, lbits, word, n);
        buckets -= n;
      }
    } else {
      const uint16_t *vec = log2d == 0 ? kZeroBuckets : active ? BucketRise[worker][log2d] : BucketFall[worker][log2d];
      const uint8_t *s = in + kBytes * (d - 1);
      while (buckets) {
        const unsigned n = buckets > 8 ? 8 : buckets;
        uint32_t word = 0;
        for (unsigned j = 0; j < n; ++j, s += stride) {
          const uint32_t level = (kWide16 ? *reinterpret_cast<const uint16_t *>(s) : *s) >> lane & 1u;
          const uint32_t edge = (*vec++ >> lane) & 1u;
          word |= (level | (edge << 1)) << (2 * j);
        }
        dynAppend(lo, lacc, lbits, word, 2 * n);
        buckets -= n;
      }
    }
  }
  o = lo;
  acc = lacc;
  bits = lbits;
}

template <unsigned F, bool kWide16>
static void encodeDynamicBlock(const uint8_t *__restrict in, uint8_t *__restrict out, unsigned worker) {
  const DynamicProfile &p = Dyn;
  uint8_t *o = out;
  encodeDynamicFast<F, kWide16>(in, o);
  if (p.needOr) dynReduceQuantity<kWide16, kQOr>(in, BucketOr[worker], p.reduceMaxLog2d, p.needLevel1);
  if (p.needAnd) dynReduceQuantity<kWide16, kQAnd>(in, BucketAnd[worker], p.reduceMaxLog2d, p.needLevel1);
  if (p.needRise) dynReduceQuantity<kWide16, kQRise>(in, BucketRise[worker], p.reduceMaxLog2d, p.needLevel1);
  if (p.needFall) dynReduceQuantity<kWide16, kQFall>(in, BucketFall[worker], p.reduceMaxLog2d, p.needLevel1);
  uint32_t acc = 0;
  unsigned bits = 0;
  const unsigned groupCount = p.groupCount;
  for (unsigned gi = 0; gi < groupCount; ++gi) {
    const DynamicProfile::Group g = p.groups[gi];
    if (g.count == 1) {  // a B x 1 transpose costs more than the per-channel gather
      dynGroupSlow<kWide16>(in, worker, g, o, acc, bits);
      continue;
    }
    if (g.mode == kModeHold || (g.mode == kModeAnyActive && g.log2d == 0)) {
      switch (g.log2d) {
        case 7: dynGroupHold<0, kWide16>(in, g.lane0, g.count, g.phase, o, acc, bits); break;
        case 6: dynGroupHold<1, kWide16>(in, g.lane0, g.count, g.phase, o, acc, bits); break;
        case 5: dynGroupHold<2, kWide16>(in, g.lane0, g.count, g.phase, o, acc, bits); break;
        case 4: dynGroupHold<3, kWide16>(in, g.lane0, g.count, g.phase, o, acc, bits); break;
        case 3: dynGroupHold<4, kWide16>(in, g.lane0, g.count, g.phase, o, acc, bits); break;
        default: dynGroupSlow<kWide16>(in, worker, g, o, acc, bits); break;
      }
    } else if (g.mode == kModeAnyActive) {
      const uint16_t *vec = g.active ? BucketOr[worker][g.log2d] : BucketAnd[worker][g.log2d];
      switch (g.log2d) {
        case 7: dynGroupVec<0>(vec, g.lane0, g.count, o, acc, bits); break;
        case 6: dynGroupVec<1>(vec, g.lane0, g.count, o, acc, bits); break;
        case 5: dynGroupVec<2>(vec, g.lane0, g.count, o, acc, bits); break;
        case 4: dynGroupVec<3>(vec, g.lane0, g.count, o, acc, bits); break;
        case 3: dynGroupVec<4>(vec, g.lane0, g.count, o, acc, bits); break;
        default: dynGroupSlow<kWide16>(in, worker, g, o, acc, bits); break;
      }
    } else {
      const uint16_t *vec = g.log2d == 0 ? kZeroBuckets : g.active ? BucketRise[worker][g.log2d] : BucketFall[worker][g.log2d];
      switch (g.log2d) {
        case 7: dynGroupEdge<0, kWide16>(in, vec, g.lane0, g.count, o, acc, bits); break;
        case 6: dynGroupEdge<1, kWide16>(in, vec, g.lane0, g.count, o, acc, bits); break;
        case 5: dynGroupEdge<2, kWide16>(in, vec, g.lane0, g.count, o, acc, bits); break;
        case 4: dynGroupEdge<3, kWide16>(in, vec, g.lane0, g.count, o, acc, bits); break;
        default: dynGroupSlow<kWide16>(in, worker, g, o, acc, bits); break;
      }
    }
  }
  if (bits) *o++ = static_cast<uint8_t>(acc);
  while (o < out + p.wireBytes) *o++ = 0;
}

// Worker body for the dynamic profile: the same chunk / straddle / stage logic as
// codecWorkerBody, with runtime block sizes.
using DynEncodeFn = void (*)(const uint8_t *, uint8_t *, unsigned);

template <bool kWide16>
static DynEncodeFn pickDynamicEncoder(unsigned F) {
  switch (F) {
    case 0: return &encodeDynamicBlock<0, kWide16>;
    case 1: return &encodeDynamicBlock<1, kWide16>;
    case 2: return &encodeDynamicBlock<2, kWide16>;
    case 3: return &encodeDynamicBlock<3, kWide16>;
    case 4: return &encodeDynamicBlock<4, kWide16>;
    case 5: return &encodeDynamicBlock<5, kWide16>;
    case 6: return &encodeDynamicBlock<6, kWide16>;
    case 7: return &encodeDynamicBlock<7, kWide16>;
    default: return &encodeDynamicBlock<8, kWide16>;
  }
}

template <bool kCheck>
static void dynamicWorkerBody(unsigned worker) {
  const DynamicProfile &p = Dyn;
  const DynEncodeFn encodeFn = p.width == 16 ? pickDynamicEncoder<true>(p.fast) : pickDynamicEncoder<false>(p.fast);
  const size_t kRaw = p.passthrough ? kDynBlockSamples * p.width / 8 : kDynBlockSamples * p.rawBytesPerSample;
  const size_t kWire = p.wireBytes;
  const bool wide16 = p.width == 16;
  const uint64_t target = TargetBlocks;
  const size_t fill = StageFill;
  uint32_t idle = 0;
  uint32_t chunks = 0;
  uint32_t rawSequenceBad = 0;
  size_t maxInflight = 0;
  uint64_t encCycles = 0;
  uint32_t encBlocks = 0;
  uint64_t wbCycles = 0;
  uint32_t wbRuns = 0;
  uint64_t qCycles = 0;
  uint8_t straddle[kDynBlockSamples * 2] __attribute__((aligned(64)));
  bool previousValid = false;
  uint8_t previousBinary = 0;
  uint8_t grayLane[8];
  memcpy(grayLane, p.grayLane, sizeof(grayLane));
  const auto grayOf = [&](const uint8_t *bytes) -> uint8_t {
    const uint32_t sample = wide16 ? reinterpret_cast<const uint16_t *>(bytes)[0] : bytes[0];
    uint8_t g = 0;
    for (unsigned b = 0; b < 8; ++b) g |= ((sample >> grayLane[b]) & 1u) << b;
    return g;
  };
  const auto check = [&](const uint8_t *bytes) {
    if (!kCheck || !p.grayCheck) return;
    uint8_t binary = grayOf(bytes);
    binary ^= binary >> 1;
    binary ^= binary >> 2;
    binary ^= binary >> 4;
    if (previousValid) {
      const uint8_t delta = static_cast<uint8_t>(binary - previousBinary);
      if (delta < 30 || delta > 34) ++rawSequenceBad;
    }
    previousBinary = binary;
    previousValid = true;
  };
  const auto encode = [&](const uint8_t *in, uint8_t *out) {
    if (p.passthrough) memcpy(out, in, kWire);
    else encodeFn(in, out, worker);
  };
  const auto place = [&](uint64_t first, const uint8_t *in, size_t count) -> bool {
    const uint64_t wireOffset = first * kWire;
    uint32_t stage = static_cast<uint32_t>(wireOffset / fill);
    size_t offset = static_cast<size_t>(wireOffset % fill);
    while (count) {
      size_t n = (fill - offset) / kWire;
      if (n > count) n = count;
      uint8_t *const stageBase = acquireStage(stage);
      if (!stageBase) return false;
      uint8_t *out = stageBase + offset;
      uint8_t *const outBegin = out;
      for (size_t i = 0; i < n; ++i, in += kRaw, out += kWire) {
#if E108_CODEC_PREFETCH
        if (i + 1 < n) {
          for (size_t line = 0; line < kRaw; line += 64) {
            const uint32_t touched = *reinterpret_cast<const volatile uint32_t *>(in + kRaw + line);
            asm volatile("" : : "r"(touched));
          }
        }
#endif
        check(in);
        const uint32_t c0 = esp_cpu_get_cycle_count();
        encode(in, out);
        encCycles += esp_cpu_get_cycle_count() - c0;
        ++encBlocks;
      }
      const uint32_t w0 = esp_cpu_get_cycle_count();
      writebackLines(outBegin, n * kWire);
      commitStage(stage, n * kWire);
      wbCycles += esp_cpu_get_cycle_count() - w0;
      ++wbRuns;
      count -= n;
      ++stage;
      offset = 0;
    }
    return true;
  };
  while (!RunAborted) {
    Chunk chunk = {};
    const uint32_t q0 = esp_cpu_get_cycle_count();
    const bool got = xQueueReceive(ChunkQueue, &chunk, 0) == pdTRUE ||  // fast path: a chunk is already waiting
                     xQueueReceive(ChunkQueue, &chunk, pdMS_TO_TICKS(200)) == pdTRUE;
    if (!got) {
      if (++idle >= 10) break;
      continue;
    }
    qCycles += esp_cpu_get_cycle_count() - q0;
    idle = 0;
    previousValid = false;
    const size_t tail = static_cast<size_t>(chunk.rawOffset % kRaw);
    uint64_t block = chunk.rawOffset / kRaw;
    size_t at = 0;
    if (block >= target) break;
    if (tail != 0) {
      const size_t lead = kRaw - tail;
      if (chunk.length < lead) {
        ++ShortChunks;
        continue;
      }
      const uint8_t *previous = chunk.data == Ring ? Ring + kRingBytes - tail : chunk.data - tail;
      memcpy(straddle, previous, tail);
      memcpy(straddle + tail, chunk.data, lead);
      if (!place(block, straddle, 1)) break;
      ++block;
      at = lead;
    }
    size_t full = (chunk.length - at) / kRaw;
    if (block + full > target) full = static_cast<size_t>(target - block);
    if (full && !place(block, chunk.data + at, full)) break;
    ++chunks;
    __atomic_fetch_add(&EncodedBlocks, static_cast<uint64_t>(full) + (tail ? 1 : 0), __ATOMIC_RELAXED);
    const uint64_t processed = __atomic_add_fetch(&ProcessedRaw, static_cast<uint64_t>(chunk.length), __ATOMIC_RELAXED);
    const uint64_t callback = CallbackBytes;
    const size_t inflight = callback > processed ? static_cast<size_t>(callback - processed) : 0;
    if (inflight > maxInflight) maxInflight = inflight;
  }
  WorkerChunks[worker] = chunks;
  EncodeCycles[worker] = encCycles;
  EncodeBlocks[worker] = encBlocks;
  WritebackCycles[worker] = wbCycles;
  WritebackRuns[worker] = wbRuns;
  QueueCycles[worker] = qCycles;
  WorkerUs[worker] = taskRunTime(nullptr);
  __atomic_fetch_add(&RawSequenceBad, rawSequenceBad, __ATOMIC_RELAXED);
  portENTER_CRITICAL(&StageMux);
  if (maxInflight > MaxInflight) MaxInflight = maxInflight;
  portEXIT_CRITICAL(&StageMux);
}

#pragma GCC pop_options

// ---- USB return path -------------------------------------------------------

static uint32_t armQueued() {
  portENTER_CRITICAL(&ArmMux);
  const uint32_t queued = ArmTail - ArmHead + (Inflight ? 1U : 0U);
  portEXIT_CRITICAL(&ArmMux);
  return queued;
}

// Arm the next segment if nothing is in flight. Safe from the usb task and from
// the TX-complete callback (usbd task); the spinlock decides who claims.
static void tryArm() {
  Segment seg = {};
  bool claimed = false;
  portENTER_CRITICAL(&ArmMux);
  if (!Inflight && ArmHead != ArmTail) {
    seg = ArmRing[ArmHead % kArmDepth];
    ++ArmHead;
    Inflight = true;
    InflightSegment = seg;
    claimed = true;
  }
  portEXIT_CRITICAL(&ArmMux);
  if (!claimed) return;
  // Non-buffered + zero-copy: this is usbd_edpt_xfer() on our buffer, no copy.
  LastArmUs = esp_timer_get_time();
  // EspUsbDevice 2.4.0 public API: the segment buffer goes straight to usbd_edpt_xfer().
  if (!Vendor.writeDirect(seg.data, seg.length)) {
    ++ArmFailures;
    LastDirectError = static_cast<uint8_t>(Vendor.lastDirectError());
    portENTER_CRITICAL(&ArmMux);
    Inflight = false;
    portEXIT_CRITICAL(&ArmMux);
  }
}

static bool armPush(const Segment &seg) {
  bool ok = false;
  portENTER_CRITICAL(&ArmMux);
  if (ArmTail - ArmHead < kArmDepth) {
    ArmRing[ArmTail % kArmDepth] = seg;
    ++ArmTail;
    ok = true;
  }
  portEXIT_CRITICAL(&ArmMux);
  if (ok) tryArm();
  return ok;
}

static void armReset() {
  portENTER_CRITICAL(&ArmMux);
  ArmHead = ArmTail = 0;
  Inflight = false;
  portEXIT_CRITICAL(&ArmMux);
}

static size_t spillUsed() { return SpillHead - SpillTail; }

static void spillPush(const uint8_t *data, size_t length) {
  if (kSpillBytes - spillUsed() < length) {
    ++SpillOverflow;  // dropped; the host sees the shortfall
    return;
  }
  const int64_t t0 = esp_timer_get_time();
  if (SpillHead == SpillTail) SpillModeBeganUs = t0;
  if (FirstSpillUs == 0) FirstSpillUs = t0 - RunBeganUs;
  const size_t offset = SpillHead % kSpillBytes;
  const size_t first = min(length, kSpillBytes - offset);
  memcpy(Spill + offset, data, first);
  if (length > first) memcpy(Spill, data + first, length - first);
  const uint32_t took = static_cast<uint32_t>(esp_timer_get_time() - t0);
  if (took > SpillPushUsMax) SpillPushUsMax = took;
  SpillHead += length;
  SpillBytes += length;
  const size_t used = spillUsed();
  if (used > SpillHighWater) SpillHighWater = used;
}

static void spillPop(uint8_t *out, size_t length) {
  const int64_t t0 = esp_timer_get_time();
  const size_t offset = SpillTail % kSpillBytes;
  const size_t first = min(length, kSpillBytes - offset);
  memcpy(out, Spill + offset, first);
  if (length > first) memcpy(out + first, Spill, length - first);
  SpillTail += length;
  const int64_t t1 = esp_timer_get_time();
  const uint32_t took = static_cast<uint32_t>(t1 - t0);
  if (took > BouncePopUsMax) BouncePopUsMax = took;
  if (SpillHead == SpillTail) SpillModeUs += t1 - SpillModeBeganUs;
}

// Move spilled bytes into free bounce buffers and queue them, in order.
static void refillBounces(bool final) {
  for (size_t b = 0; b < kBounceCount; ++b) {
    if (BounceBusy[b]) continue;
    const size_t used = spillUsed();
    if (used == 0 || armQueued() >= kArmDepth) return;
    size_t length = min(kStageBytes, used);
    if (!final) {
      length &= ~size_t(511);
      if (length == 0) return;
    }
    spillPop(Bounce[b], length);
    BounceBusy[b] = 1;
    const Segment seg = {Bounce[b], static_cast<uint32_t>(length), kSegBounce, static_cast<uint8_t>(b)};
    armPush(seg);
    ++BounceSegments;
  }
}

static uint8_t stageSlot(const uint8_t *data) {
  for (size_t i = 0; i < kStageCount; ++i) {
    if (data == Stage[i]) return static_cast<uint8_t>(i);
  }
  return 0xff;
}

static void releaseStage(uint8_t slot) {
  if (slot >= kStageCount) return;
  portENTER_CRITICAL(&StageMux);
  FreeSlots |= 1u << slot;
  portEXIT_CRITICAL(&StageMux);
}

static void dispatchStage(const WireChunk &ready) {
  const bool direct = (Flags & kFlagNoDirect) == 0 && spillUsed() == 0 && armQueued() < kDirectDepth;
  if (direct) {
    const Segment seg = {ready.data, static_cast<uint32_t>(ready.length), kSegStage, stageSlot(ready.data)};
    armPush(seg);
    ++DirectSegments;
  } else {
    spillPush(ready.data, ready.length);
    releaseStage(stageSlot(ready.data));
  }
}

static void usbTask(void *) {
  const int64_t began = esp_timer_get_time();
  WireChunk held[kStageCount] = {};
  bool heldValid[kStageCount] = {};
  uint32_t nextSend = 0;
  while (true) {
    WireChunk ready = {};
    if (xQueueReceive(ReadyStageQueue, &ready, pdMS_TO_TICKS(1)) == pdTRUE) {
      if (ready.index == nextSend) {
        dispatchStage(ready);
        ++nextSend;
        while (heldValid[nextSend % kStageCount] && held[nextSend % kStageCount].index == nextSend) {
          heldValid[nextSend % kStageCount] = false;
          dispatchStage(held[nextSend % kStageCount]);
          ++nextSend;
        }
      } else {
        held[ready.index % kStageCount] = ready;
        heldValid[ready.index % kStageCount] = true;
      }
    }
    const bool drained = CodecFinished && uxQueueMessagesWaiting(ReadyStageQueue) == 0;
    refillBounces(drained);
    if (RunAborted && CodecFinished) break;
    bool anyHeld = false;
    for (size_t i = 0; i < kStageCount; ++i) anyHeld |= heldValid[i];
    if (drained && !anyHeld && spillUsed() == 0 && armQueued() == 0) break;
  }
  UsbUs = esp_timer_get_time() - began;
  UsbTaskUs = taskRunTime(nullptr);
  xSemaphoreGive(UsbDone);
  vTaskDelete(nullptr);
}

// Internal-sink mode: release stages without sending, so the codec alone is measured.
static void sinkTask(void *) {
  while (true) {
    WireChunk ready = {};
    if (xQueueReceive(ReadyStageQueue, &ready, pdMS_TO_TICKS(1)) == pdTRUE) {
      uint32_t checksum = SinkChecksum;
      for (size_t i = 0; i < ready.length; i += 64) checksum += ready.data[i];
      SinkChecksum = checksum;
      SinkBytes += ready.length;
      releaseStage(stageSlot(ready.data));
      continue;
    }
    if (CodecFinished && uxQueueMessagesWaiting(ReadyStageQueue) == 0) break;
  }
  UsbTaskUs = taskRunTime(nullptr);
  xSemaphoreGive(UsbDone);
  vTaskDelete(nullptr);
}

}  // namespace

// TinyUSB completion (usbd task, core kUsbCore): release the segment, chain the next.
static void onDirectTxComplete(size_t sentBytes) {
  Segment seg = {};
  bool had = false;
  portENTER_CRITICAL(&ArmMux);
  if (Inflight) {
    seg = InflightSegment;
    Inflight = false;
    had = true;
  }
  portEXIT_CRITICAL(&ArmMux);
  if (!had) return;
  SentBytes += sentBytes;
  ++Completions;
  {
    const int64_t now = esp_timer_get_time();
    if (LastCompletionUs) {
      const uint32_t gap = static_cast<uint32_t>(now - LastCompletionUs);
      if (gap > CompletionGapUsMax) {
        CompletionGapUsMax = gap;
        CompletionGapAtUs = now - RunBeganUs;
      }
      if (gap > 5000 && GapLogCount < kGapLog) {
        GapLogEndMs[GapLogCount] = static_cast<uint32_t>((now - RunBeganUs) / 1000);
        GapLogUs[GapLogCount] = gap;
        GapLogArmLatUs[GapLogCount] = static_cast<uint32_t>(LastArmUs > LastCompletionUs ? LastArmUs - LastCompletionUs : 0);
        ++GapLogCount;
      }
    }
    LastCompletionUs = now;
  }
  switch (seg.kind) {
    case kSegStage:
      releaseStage(seg.slot);
      break;
    case kSegBounce:
      BounceBusy[seg.slot] = 0;
      break;
    case kSegStatus:
      xSemaphoreGive(StatusDone);
      break;
    default:
      break;
  }
  tryArm();
  if (UsbWake) xSemaphoreGive(UsbWake);
}

// Non-buffered vendor RX (usbd task): commands are exactly 16 bytes.
static void onDirectRxData(const uint8_t *buffer, size_t length) {
  if (length < kCommandBytes || CommandPending) return;
  const uint32_t take = length < kMailboxBytes ? length : kMailboxBytes;
  memcpy(CommandMailbox, buffer, take);
  CommandLength = take;
  CommandPending = true;
}

namespace {

// E118: adjacent DMA nodes (2,368..4,032 bytes) are coalesced into one queue item
// of up to kCoalesceBytes, so the worker pays the FreeRTOS receive (about 6 us,
// ~160 cycles per 128-sample block at 30 Msps) a quarter as often. A pending
// chunk is pushed when the next node is not contiguous (ring wrap), when it would
// exceed the limit, or once the stream has reached the run's target so the last
// blocks never sit in the pending chunk.
constexpr uint32_t kCoalesceBytes = 16 * 1024;
Chunk PendingChunk;
bool PendingValid;
volatile uint64_t TargetRawBytes;
volatile uint32_t CoalescedPushes;

static inline bool IRAM_ATTR pushChunk(const Chunk &chunk, BaseType_t *wake) {
  if (xQueueSendFromISR(ChunkQueue, &chunk, wake) != pdTRUE) {
    if (QueueOverflow++ == 0) FirstOverflowUs = esp_timer_get_time() - RunBeganUs;
    return false;
  }
  ++CoalescedPushes;
  return true;
}

static bool IRAM_ATTR onPartialReceive(
    parlio_rx_unit_handle_t, const parlio_rx_event_data_t *event, void *) {
  const uint8_t *data = static_cast<const uint8_t *>(event->data);
  const uint32_t bytes = static_cast<uint32_t>(event->recv_bytes);
  const uint64_t offset = CallbackBytes;
  CallbackBytes += bytes;
  if (bytes < ChunkMin) ChunkMin = bytes;
  if (bytes > ChunkMax) ChunkMax = bytes;
  RxIsrCore = xPortGetCoreID();
  BaseType_t wake = pdFALSE;
  if (PendingValid) {
    const bool contiguous = PendingChunk.data + PendingChunk.length == data;
    if (contiguous && PendingChunk.length + bytes <= kCoalesceBytes) {
      PendingChunk.length += bytes;
    } else {
      pushChunk(PendingChunk, &wake);
      PendingChunk = {data, bytes, ChunkSeq++, offset};
    }
  } else {
    PendingChunk = {data, bytes, ChunkSeq++, offset};
    PendingValid = true;
  }
  if (CallbackBytes >= TargetRawBytes || PendingChunk.length >= kCoalesceBytes) {
    pushChunk(PendingChunk, &wake);
    PendingValid = false;
  }
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
    if (i >= captureWidth) c.data_gpio_nums[i] = GPIO_NUM_NC;
    else if (ProfileCode == 'D') c.data_gpio_nums[i] = gpio_num_t(Dyn.laneGpio[i]);
    else c.data_gpio_nums[i] = gpio_num_t(kPins[i % kSourceLanes]);
  }
  esp_err_t result = parlio_new_rx_unit(&c, unit);
  if (result != ESP_OK) return result;
  // E114: a lane GPIO that nothing drives (not one of the loopback outputs)
  // must not float. Floating inputs on GPIO 10/12/14-17 of this board stalled
  // the USB IN path for 30-300 ms (completion gaps), see README §結果.
  PulledDownLanes = 0;
  if (ProfileCode == 'D' && (Flags & kFlagNoPulldown) == 0) {
    for (unsigned i = 0; i < captureWidth; ++i) {
      const int gpio = Dyn.laneGpio[i];
      bool driven = false;
      for (unsigned k = 0; k < kSourceLanes; ++k) driven |= kPins[k] == gpio;
      if (!driven && gpio_set_pull_mode(gpio_num_t(gpio), GPIO_PULLDOWN_ONLY) == ESP_OK) ++PulledDownLanes;
    }
  }
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

static void resetRunState() {
  EncodedBlocks = SentBytes = CallbackBytes = 0;
  Completions = DirectSegments = BounceSegments = SpillBytes = 0;
  ArmFailures = QueueOverflow = 0;
  PendingValid = false;
  CoalescedPushes = 0;
  RunBeganUs = esp_timer_get_time();
  FirstSpillUs = FirstOverflowUs = SpillModeUs = SpillModeBeganUs = 0;
  SpillPushUsMax = BouncePopUsMax = 0;
  LastCompletionUs = 0;
  CompletionGapUsMax = 0;
  CompletionGapAtUs = 0;
  GapLogCount = 0;
  LastArmUs = 0;
  CaptureUs = UsbUs = 0;
  ProcessUs = 0;
  HarvestChunks = 0;
  RawSequenceBad = DuplicateBad = 0;
  MaxInflight = 0;
  SinkBytes = SinkChecksum = 0;
  UsbTaskUs = 0;
  for (size_t i = 0; i < kWorkers; ++i) WorkerChunks[i] = WorkerUs[i] = 0;
  for (size_t i = 0; i < kStageCount; ++i) StageFilled[i] = 0;
  for (size_t i = 0; i < kSlotTable; ++i) SlotTableIndex[i] = UINT32_MAX;
  FreeSlots = (1u << kStageCount) - 1;
  SlotAcquireWaits = 0;
  for (size_t w = 0; w < kWorkers; ++w) { EncodeCycles[w] = 0; EncodeBlocks[w] = 0; WritebackCycles[w] = 0; WritebackRuns[w] = 0; QueueCycles[w] = 0; }
  AbortFreeSlots = AbortArmQueued = AbortSpillUsed = AbortReadyWaiting = AbortInflightKind = 0;
  StageWaits = StageWaitTimeouts = ShortChunks = ChunkSeq = 0;
  ChunkMin = 0xffffffffU;
  ChunkMax = 0;
  RunAborted = false;
  ProcessedRaw = 0;
  WireBlockBytes = activeWireBlockBytes();
  StageFill = activeStageFill();
  TotalWireBytes = TargetBlocks * WireBlockBytes;
  Idle0Us = Idle1Us = UsbdUs = 0;
  StatWallUs = 0;
  RxIsrCore = -1;
  SpinUs = 0;
  SpinResult = 0;
  SpillHead = SpillTail = SpillHighWater = 0;
  SpillOverflow = 0;
  for (size_t i = 0; i < kBounceCount; ++i) BounceBusy[i] = 0;
  CodecFinished = false;
  armReset();
  xQueueReset(ChunkQueue);
  xQueueReset(ReadyStageQueue);
}

static esp_err_t runCapture(uint64_t blocks, uint32_t sampleRateHz) {
  TargetBlocks = blocks;
  resetRunState();
  {
    // raw bytes the run needs; the RX callback stops coalescing once the stream has them
    const size_t rawPerBlock = ProfileCode == 'D'
        ? (Dyn.passthrough ? kDynBlockSamples * Dyn.width / 8 : kDynBlockSamples * Dyn.rawBytesPerSample)
        : (ProfileCode == '2' ? kTwoWireBlockBytes
           : (ProfileCode == 'W' || ProfileCode == 'F' || ProfileCode == 'V') ? kWideBlockSamples * 2
           : ProfileCode == '8' ? kLegacyBlockSamples : kLegacyBlockSamples * 2);
    TargetRawBytes = blocks * rawPerBlock;
  }
  memset(Ring, 0xa5, kRingBytes);
  esp_cache_msync(Ring, kRingBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  StatSnapshot began = {};
  StatSnapshot ended = {};

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
  // This task is pinned to core 0, so the PARLIO RX interrupt lives there with
  // the usb task and the USB stack; the codec has core 1 to itself.
  // usb / sink task above the workers on core 0 so a worker there never delays
  // an arm; the usbd task (priority 24) is above both.
  xTaskCreatePinnedToCore(SinkOnly ? sinkTask : usbTask, "e111_usb", 4096, nullptr, 6, nullptr, 0);
  WorkerCount = (Flags & kFlagSingleCore) ? 1 : kWorkers;
  xTaskCreatePinnedToCore(codecWorkerTask, "e111_codec1", 6144, reinterpret_cast<void *>(1), 5, nullptr, 1);
  if (WorkerCount == 2) xTaskCreatePinnedToCore(codecWorkerTask, "e111_codec0", 6144, reinterpret_cast<void *>(0), 5, nullptr, 0);
  {
    if (Flags & kFlagLoadProbe) spinStart();
    began = snapshotStats();
    result = parlio_rx_soft_delimiter_start_stop(rxUnit, delimiter, true);
    if (result == ESP_OK) {
      for (uint32_t w = 0; w < WorkerCount; ++w) xSemaphoreTake(HarvestDone, portMAX_DELAY);
    }
    ended = snapshotStats();
    CaptureUs = ended.wall - began.wall;
    SpinUs = CaptureUs;
    SpinResult = (Flags & kFlagLoadProbe) ? spinStop() : 0;
  }
  Idle0Us = ended.idle0 - began.idle0;
  Idle1Us = ended.idle1 - began.idle1;
  UsbdUs = ended.usbd - began.usbd;
  StatWallUs = ended.wall - began.wall;
  parlio_rx_soft_delimiter_start_stop(rxUnit, delimiter, false);
  parlio_rx_unit_disable(rxUnit);
  CodecFinished = true;
  xSemaphoreTake(UsbDone, portMAX_DELAY);
  Active = false;

cleanup:
  Active = false;
  CodecFinished = true;
  if (delimiter) parlio_del_rx_delimiter(delimiter);
  if (rxUnit) parlio_del_rx_unit(rxUnit);
  if (txUnit) {
    parlio_tx_unit_disable(txUnit);
    parlio_del_tx_unit(txUnit);
  }
  for (int pin : kPins) gpio_reset_pin(gpio_num_t(pin));
  return result;
}

// Queue a status line as its own (short) transfer and wait for its completion.
static void sendLine(int length) {
  Serial.write(StatusBuffer, length);
  Serial.flush();
  if (!Vendor.mounted()) return;
  xSemaphoreTake(StatusDone, 0);
  const Segment seg = {StatusBuffer, static_cast<uint32_t>(length), kSegStatus, 0};
  if (armPush(seg)) xSemaphoreTake(StatusDone, pdMS_TO_TICKS(kStatusWaitMs));
}

static void sendStatus(esp_err_t result) {
  int length = snprintf(
      reinterpret_cast<char *>(StatusBuffer), kStatusBytes,
      "E118_STATUS result=%s rate_hz=%lu width=%u profile=%s sink_only=%u flags=0x%02x usb_core=%d rx_isr_core=%d "
      "stage_bytes=%lu blocks=%llu encoded=%llu sent=%llu completions=%llu direct_segments=%llu bounce_segments=%llu "
      "spill_bytes=%llu spill_high_water=%lu spill_overflow=%lu first_spill_us=%lld first_overflow_us=%lld "
      "spill_mode_us=%lld spill_push_us_max=%lu bounce_pop_us_max=%lu completion_gap_us_max=%lu completion_gap_at_us=%lld arm_failures=%lu direct_supported=%u last_direct_error=%s callbacks=%llu "
      "queue_overflow=%lu fifo_overflow=%lu capture_us=%lld usb_us=%lld chunks=%lu process_us=%llu "
      "raw_sequence_bad=%lu duplicate_bad=%lu max_inflight=%lu ring_bytes=%lu sink_bytes=%llu sink_checksum=%lu "
      "stat_wall_us=%lld idle0_us=%lu idle1_us=%lu usbd_us=%lu usb_task_us=%lu codec0_task_us=%lu codec1_task_us=%lu "
      "workers=%lu chunks0=%lu chunks1=%lu chunk_min=%lu chunk_max=%lu short_chunks=%lu stage_waits=%lu stage_wait_timeouts=%lu aborted=%u "
      "usbd_priority=%u dma=%u dma_active=%u dwc2_arch=%u gahbcfg=0x%08lx gintmsk_rxflvl=%u codec_o2=%u codec_prefetch=%u no_writeback=%u "
      "spin_count=%lu spin_us=%lld spin_calib_per_s=%lu bench_us=%llu bench_checksum=%lu\n",
      esp_err_to_name(result), static_cast<unsigned long>(RequestedRateHz),
      RequestedWidth, activeProfileName(), SinkOnly, Flags, UsbInitCore, RxIsrCore,
      static_cast<unsigned long>(kStageBytes),
      static_cast<unsigned long long>(TargetBlocks),
      static_cast<unsigned long long>(EncodedBlocks), static_cast<unsigned long long>(SentBytes),
      static_cast<unsigned long long>(Completions), static_cast<unsigned long long>(DirectSegments),
      static_cast<unsigned long long>(BounceSegments), static_cast<unsigned long long>(SpillBytes),
      static_cast<unsigned long>(SpillHighWater), static_cast<unsigned long>(SpillOverflow),
      static_cast<long long>(FirstSpillUs), static_cast<long long>(FirstOverflowUs), static_cast<long long>(SpillModeUs),
      static_cast<unsigned long>(SpillPushUsMax), static_cast<unsigned long>(BouncePopUsMax),
      static_cast<unsigned long>(CompletionGapUsMax), static_cast<long long>(CompletionGapAtUs),
      static_cast<unsigned long>(ArmFailures), EspUsbDeviceVendor::directWriteSupported() ? 1U : 0U, Vendor.lastDirectErrorName(), static_cast<unsigned long long>(CallbackBytes),
      static_cast<unsigned long>(QueueOverflow), static_cast<unsigned long>(SpillOverflow),
      static_cast<long long>(CaptureUs), static_cast<long long>(UsbUs),
      static_cast<unsigned long>(HarvestChunks), static_cast<unsigned long long>(ProcessUs),
      static_cast<unsigned long>(RawSequenceBad), static_cast<unsigned long>(DuplicateBad),
      static_cast<unsigned long>(MaxInflight), static_cast<unsigned long>(kRingBytes),
      static_cast<unsigned long long>(SinkBytes), static_cast<unsigned long>(SinkChecksum),
      static_cast<long long>(StatWallUs), static_cast<unsigned long>(Idle0Us),
      static_cast<unsigned long>(Idle1Us), static_cast<unsigned long>(UsbdUs),
      static_cast<unsigned long>(UsbTaskUs), static_cast<unsigned long>(WorkerUs[0]), static_cast<unsigned long>(WorkerUs[1]),
      static_cast<unsigned long>(WorkerCount), static_cast<unsigned long>(WorkerChunks[0]), static_cast<unsigned long>(WorkerChunks[1]),
      static_cast<unsigned long>(ChunkMin == 0xffffffffU ? 0 : ChunkMin), static_cast<unsigned long>(ChunkMax),
      static_cast<unsigned long>(ShortChunks), static_cast<unsigned long>(StageWaits),
      static_cast<unsigned long>(StageWaitTimeouts), RunAborted ? 1U : 0U,
      static_cast<unsigned>(UsbdPriority),
#if CFG_TUD_DWC2_DMA_ENABLE
      1U,
#else
      0U,
#endif
      dwc2DmaActive(), dwc2Arch(), static_cast<unsigned long>(dwc2Register(0x08)), dwc2RxflvlMasked(),
      static_cast<unsigned>(E108_CODEC_O2), static_cast<unsigned>(E108_CODEC_PREFETCH), static_cast<unsigned>(E118_NO_WRITEBACK),
      static_cast<unsigned long>(SpinResult), static_cast<long long>(SpinUs),
      static_cast<unsigned long>(SpinCalibPerSec),
      static_cast<unsigned long long>(BenchUs), static_cast<unsigned long>(BenchChecksum));
  // Completion gaps > 5 ms as end_ms:gap_us:arm_latency_us (arm latency = time
  // from the previous completion to the next usbd_edpt_xfer; ~0 means a transfer
  // was armed the whole gap and the host or DWC2 did not move it).
  if (length > 0 && length < static_cast<int>(kStatusBytes) - 1) {
    int at = length - 1;  // overwrite the trailing newline
    at += snprintf(reinterpret_cast<char *>(StatusBuffer) + at, kStatusBytes - at, " gaps=");
    for (uint32_t i = 0; i < GapLogCount && at < static_cast<int>(kStatusBytes) - 40; ++i) {
      at += snprintf(reinterpret_cast<char *>(StatusBuffer) + at, kStatusBytes - at, "%s%lu:%lu:%lu", i ? "," : "",
                     static_cast<unsigned long>(GapLogEndMs[i]), static_cast<unsigned long>(GapLogUs[i]),
                     static_cast<unsigned long>(GapLogArmLatUs[i]));
    }
    at += snprintf(reinterpret_cast<char *>(StatusBuffer) + at, kStatusBytes - at, " coalesced_pushes=%lu enc_cyc0=%lu enc_cyc1=%lu wb_cyc_per_block0=%lu wb_cyc_per_block1=%lu q_cyc_per_block0=%lu q_cyc_per_block1=%lu wb_runs1=%lu pulled_down_lanes=%lu slot_waits=%lu abort_free_slots=0x%02lx abort_arm_queued=%lu abort_spill_used=%lu abort_ready_waiting=%lu abort_inflight_kind=%lu\n",
                   static_cast<unsigned long>(CoalescedPushes),
                   static_cast<unsigned long>(EncodeBlocks[0] ? EncodeCycles[0] / EncodeBlocks[0] : 0), static_cast<unsigned long>(EncodeBlocks[1] ? EncodeCycles[1] / EncodeBlocks[1] : 0),
                   static_cast<unsigned long>(EncodeBlocks[0] ? WritebackCycles[0] / EncodeBlocks[0] : 0), static_cast<unsigned long>(EncodeBlocks[1] ? WritebackCycles[1] / EncodeBlocks[1] : 0),
                   static_cast<unsigned long>(EncodeBlocks[0] ? QueueCycles[0] / EncodeBlocks[0] : 0), static_cast<unsigned long>(EncodeBlocks[1] ? QueueCycles[1] / EncodeBlocks[1] : 0), static_cast<unsigned long>(WritebackRuns[1]),
                   static_cast<unsigned long>(PulledDownLanes), static_cast<unsigned long>(SlotAcquireWaits), static_cast<unsigned long>(AbortFreeSlots), static_cast<unsigned long>(AbortArmQueued), static_cast<unsigned long>(AbortSpillUsed), static_cast<unsigned long>(AbortReadyWaiting), static_cast<unsigned long>(AbortInflightKind));
    length = at;
  }
  sendLine(length);
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

// USB-only probe: the 256-periodic pattern in Bounce[0] is armed over and over,
// zero-copy, with the arm ring kept full from this task.
static void runUsbProbe(uint64_t target) {
  resetRunState();
  for (size_t i = 0; i < kStageBytes; ++i) Bounce[0][i] = static_cast<uint8_t>(i);
  esp_cache_msync(Bounce[0], kStageBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  ProbeRemaining = target;
  if (Flags & kFlagLoadProbe) spinStart();
  const StatSnapshot began = snapshotStats();
  uint32_t idleTicks = 0;
  while (ProbeRemaining > 0 || armQueued() > 0) {
    while (ProbeRemaining > 0 && armQueued() < kArmDepth) {
      const uint64_t remaining = ProbeRemaining;
      const uint32_t length = static_cast<uint32_t>(remaining < kStageBytes ? remaining : kStageBytes);
      const Segment seg = {Bounce[0], length, kSegProbe, 0};
      if (!armPush(seg)) break;
      ProbeRemaining -= length;
    }
    if (xSemaphoreTake(UsbWake, pdMS_TO_TICKS(100)) == pdTRUE) {
      idleTicks = 0;
    } else if (++idleTicks >= 20 || !Vendor.mounted()) {
      break;  // host stopped reading
    }
  }
  const StatSnapshot ended = snapshotStats();
  SpinUs = ended.wall - began.wall;
  SpinResult = (Flags & kFlagLoadProbe) ? spinStop() : 0;
  int length = snprintf(
      reinterpret_cast<char *>(StatusBuffer), kStatusBytes,
      "E114_PROBE bytes=%llu target=%llu elapsed_us=%lld completions=%llu arm_failures=%lu usb_core=%d "
      "idle0_us=%lu idle1_us=%lu usbd_us=%lu dma=%u dma_active=%u dwc2_arch=%u "
      "spin_count=%lu spin_us=%lld spin_calib_per_s=%lu flags=0x%02x\n",
      static_cast<unsigned long long>(SentBytes), static_cast<unsigned long long>(target),
      static_cast<long long>(ended.wall - began.wall), static_cast<unsigned long long>(Completions),
      static_cast<unsigned long>(ArmFailures), UsbInitCore,
      static_cast<unsigned long>(ended.idle0 - began.idle0),
      static_cast<unsigned long>(ended.idle1 - began.idle1),
      static_cast<unsigned long>(ended.usbd - began.usbd),
#if CFG_TUD_DWC2_DMA_ENABLE
      1U,
#else
      0U,
#endif
      dwc2DmaActive(), dwc2Arch(), static_cast<unsigned long>(SpinResult),
      static_cast<long long>(SpinUs), static_cast<unsigned long>(SpinCalibPerSec), Flags);
  sendLine(length);
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

// Time the dynamic encoder for the accepted descriptor on this core only: the
// answer to "how fast is the generic codec for this layout" without capture,
// USB or queue effects. Input is a fixed synthetic pattern with the first byte
// varied per block so nothing is hoisted.
static void benchmarkDynamic() {
  const DynamicProfile &p = Dyn;
  DynBenchUs = 0;
  DynBenchBlocks = 0;
  if (!p.valid || p.wireBytes > sizeof(DynBenchWire)) return;
  const size_t raw = p.passthrough ? kDynBlockSamples * p.width / 8 : kDynBlockSamples * p.rawBytesPerSample;
  uint8_t *in = reinterpret_cast<uint8_t *>(Pending);
  for (size_t i = 0; i < raw; ++i) in[i] = static_cast<uint8_t>(i * 37U + (i >> 3));
  // E118: optionally walk the DMA ring so every block's input is L1-cold (the
  // streaming case); the hot variant measures the codec alone.
  const bool cold = (BenchFlags & kFlagBenchCold) != 0;
  if (cold) {
    for (size_t i = 0; i < kRingBytes; ++i) Ring[i] = static_cast<uint8_t>(i * 37U + (i >> 3));
    esp_cache_msync(Ring, kRingBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
  const size_t coldBlocks = kRingBytes / raw;
  const DynEncodeFn fn = p.width == 16 ? pickDynamicEncoder<true>(p.fast) : pickDynamicEncoder<false>(p.fast);
  constexpr uint32_t kBlocks = 8192;  // 1 M samples: 20-200 ms, well under the idle watchdog
  uint32_t checksum = 0;
  const int64_t began = esp_timer_get_time();
  for (uint32_t b = 0; b < kBlocks; ++b) {
    uint8_t *src = in;
    if (cold) {
      src = Ring + (b % coldBlocks) * raw;
      if ((b % coldBlocks) == 0) esp_cache_msync(Ring, kRingBytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    } else {
      in[0] = static_cast<uint8_t>(b);
    }
    if (p.passthrough) memcpy(DynBenchWire, src, p.wireBytes);
    else fn(src, DynBenchWire, 0);
    checksum += DynBenchWire[b % p.wireBytes];
  }
  DynBenchUs = esp_timer_get_time() - began;
  DynBenchBlocks = kBlocks;
  DynBenchChecksum = checksum;
  Dyn.benchMspsX100 = DynBenchUs ? static_cast<uint32_t>(static_cast<uint64_t>(kBlocks) * kDynBlockSamples * 100 / DynBenchUs) : 0;
}

// Decide codec_limit from the bench instead of a fixed table: the same
// descriptor, encoder and core that will run the capture, so a new layout
// or a slower build is judged by measurement. Passthrough is bounded by PARLIO.
static bool applyCodecLimit(uint32_t rateHz, uint8_t flags) {
  const unsigned scale = (flags & kFlagSingleCore) ? kSingleScalePercent : kDualScalePercent;
  const uint64_t limitX100 = static_cast<uint64_t>(Dyn.benchMspsX100) * scale / 100;
  uint32_t limit = static_cast<uint32_t>(limitX100 * kCodecMarginPercent / 100 / 100);
  if (limit > 160) limit = 160;  // PARLIO input clock ceiling is a hard limit, not an estimate: no margin on it
  Dyn.codecLimitMsps = limit;
  if ((flags & kFlagNoCodecLimit) == 0 && rateHz > static_cast<uint64_t>(Dyn.codecLimitMsps) * 1000000ULL) {
    Dyn.valid = false;
    strlcpy(Dyn.reason, "codec_limit", sizeof(Dyn.reason));
    return false;
  }
  return true;
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
  // 2.4.0 direct API instead of the E097/E101 experimental hooks; both run on the
  // usbd task and only do bookkeeping, the next arm and semaphore gives.
  Vendor.onTxComplete(onDirectTxComplete);
  Vendor.onRxData(onDirectRxData);
  xSemaphoreGive(UsbBeginDone);
  vTaskDelete(nullptr);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Ring = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      64, kRingBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  Source = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      64, kSourceBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  for (size_t i = 0; i < kStageCount; ++i) {
    Stage[i] = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, kStageBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  }
  if (!Ring || !Source || !Stage[kStageCount - 1]) {
    Serial.printf("E118_FATAL internal allocation failed ring=%p source=%p stage_last=%p free_internal=%u\n", Ring, Source,
                  Stage[kStageCount - 1], static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    abort();
  }
  Spill = static_cast<uint8_t *>(heap_caps_malloc(
      kSpillBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ChunkQueue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  ReadyStageQueue = xQueueCreate(kStageCount, sizeof(WireChunk));
  HarvestDone = xSemaphoreCreateCounting(kWorkers, 0);
  UsbDone = xSemaphoreCreateBinary();
  UsbWake = xSemaphoreCreateBinary();
  StatusDone = xSemaphoreCreateBinary();
  UsbBeginDone = xSemaphoreCreateBinary();
  SpinDone = xSemaphoreCreateBinary();
  for (size_t i = 0; i < kSourceBytes; ++i) {
    const uint8_t value = static_cast<uint8_t>(i);
    Source[i] = value ^ (value >> 1);
  }
  esp_cache_msync(Source, kSourceBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  benchmarkCodec();

  UsbConfig.vid = kVid;
  UsbConfig.pid = kPid;
  UsbConfig.manufacturer = "wch-protocols";
  UsbConfig.product = "E118 P4 generic fast path";
  // Same serial as E104..E107 so the Windows devnode / WinUSB binding persists.
  UsbConfig.serialNumber = "e104-p4-windows-v1";
  UsbConfig.controller = EspUsbController::HighSpeed;
  UsbConfig.webusbEnabled = true;
  xTaskCreatePinnedToCore(usbBeginTask, "e111_usb_begin", 8192, nullptr, 6, nullptr, kUsbCore);
  xSemaphoreTake(UsbBeginDone, portMAX_DELAY);
  UsbdTask = xTaskGetHandle("espusb-device");
  UsbdPriority = UsbdTask ? uxTaskPriorityGet(UsbdTask) : 0;
  // Spin calibration: 300 ms with only the USB stack and the tick running on core 0.
  spinStart();
  delay(300);
  SpinCalibPerSec = static_cast<uint32_t>(static_cast<uint64_t>(spinStop()) * 1000U / 300U);
  Serial.printf("E118_READY usb=%u usb_core=%d usbd_priority=%u dma=%u dma_active=%u dwc2_arch=%u gahbcfg=0x%08lx gintmsk_rxflvl=%u codec_o2=%u "
                "spin_calib_per_s=%lu stage_bytes=%u stages=%u "
                "free_internal=%u free_spiram=%u buffers=%u bench_us=%llu checksum=%lu\n",
                UsbReady, UsbInitCore, static_cast<unsigned>(UsbdPriority),
#if CFG_TUD_DWC2_DMA_ENABLE
                1U,
#else
                0U,
#endif
                dwc2DmaActive(), dwc2Arch(), static_cast<unsigned long>(dwc2Register(0x08)), dwc2RxflvlMasked(),
                static_cast<unsigned>(E108_CODEC_O2),
                static_cast<unsigned long>(SpinCalibPerSec),
                static_cast<unsigned>(kStageBytes), static_cast<unsigned>(kStageCount),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                Ring && Source && Spill && ChunkQueue,
                static_cast<unsigned long long>(BenchUs), static_cast<unsigned long>(BenchChecksum));
}

void loop() {
  if (RunPending || !CommandPending) {
    delay(1);
    return;
  }
  uint8_t command[kMailboxBytes];
  const uint32_t commandLength = CommandLength;
  memcpy(command, CommandMailbox, kMailboxBytes);
  CommandPending = false;
  if (command[1] == 'D' && (command[0] == 'E' || command[0] == 'I' || command[0] == 'Q')) {
    const uint32_t rateHz = readLe32(command + 2);
    const uint64_t blocks = readLe64(command + 6);
    bool ok = buildDynamicProfile(command, commandLength, rateHz);
    if (ok) {
      BenchFlags = command[14];
      benchmarkDynamic();
      ok = applyCodecLimit(rateHz, command[14]);
    } else {
      DynBenchUs = DynBenchBlocks = 0;
    }
    sendDescriptorReply(rateHz);
    if (!ok || command[0] == 'Q' || blocks == 0) return;
    TargetBlocks = blocks;
    RequestedRateHz = rateHz;
    RequestedWidth = Dyn.width;
    WideProfile = false;
    ProfileCode = 'D';
    SinkOnly = command[0] == 'I';
    Flags = command[14];
    CommandMode = static_cast<char>(command[0]);
    RunPending = true;
    xTaskCreatePinnedToCore(captureControlTask, "e114_ctl", 4096, nullptr, 6, nullptr, 0);
    return;
  }
  if ((command[0] != 'E' && command[0] != 'I') ||
      (command[1] != '6' && command[1] != '8' && command[1] != 'W' && command[1] != 'F' && command[1] != 'V' &&
       command[1] != '2' && command[1] != 'P')) return;
  const uint32_t rateHz = readLe32(command + 2);
  const uint64_t blocks = readLe64(command + 6);
  if (blocks == 0) return;
  TargetBlocks = blocks;
  RequestedRateHz = command[1] == 'P' ? kSampleRateHz : rateHz;
  RequestedWidth = command[1] == '8' ? 8 : command[1] == '2' ? 2 : 16;
  WideProfile = command[1] == 'W';
  ProfileCode = command[1] == 'P' ? ProfileCode : static_cast<char>(command[1]);
  SinkOnly = command[0] == 'I';
  Flags = command[14];
  if (command[1] != 'P' && (RequestedRateHz < 1000000 || RequestedRateHz > 160000000)) return;
  CommandMode = command[1] == 'P' ? 'P' : static_cast<char>(command[0]);
  RunPending = true;
  xTaskCreatePinnedToCore(captureControlTask, "e111_ctl", 4096, nullptr, 6, nullptr, 0);
}
