#include "EspUsbDevice.h"

#include <esp_timer.h>

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

static constexpr size_t BLOCK_SAMPLES = 64;
static constexpr size_t WIRE_BLOCK_BYTES = 25;
static constexpr size_t STREAM_BLOCKS = 512;
static constexpr size_t STREAM_BYTES = STREAM_BLOCKS * WIRE_BLOCK_BYTES;
static constexpr size_t TX_ARM_BYTES = CFG_TUD_VENDOR_TX_EPSIZE;
static constexpr size_t BENCH_BLOCKS = 262144;

static uint8_t wirePattern[STREAM_BYTES] __attribute__((aligned(64)));
static uint16_t rawBlock[BLOCK_SAMPLES] __attribute__((aligned(64)));
static uint8_t encodedBlock[WIRE_BLOCK_BYTES] __attribute__((aligned(64)));
static char statusBuffer[256] __attribute__((aligned(64)));

static volatile uint64_t targetBytes = 0;
static volatile uint64_t submittedBytes = 0;
static volatile uint64_t completedBytes = 0;
static volatile int64_t firstAt = 0;
static volatile int64_t lastAt = 0;
static volatile bool streaming = false;
static volatile bool reportPending = false;
static volatile bool statusRequested = false;
static volatile uint32_t benchChecksum = 0;

static uint64_t readLe64(const uint8_t *p)
{
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
  {
    value |= static_cast<uint64_t>(p[i]) << (8 * i);
  }
  return value;
}

static inline __attribute__((always_inline)) void packFast8(
    const uint16_t *__restrict input, uint8_t *__restrict output)
{
  uint32_t pair0;
  uint32_t pair1;
  uint32_t pair2;
  uint32_t pair3;
  memcpy(&pair0, input + 0, sizeof(pair0));
  memcpy(&pair1, input + 2, sizeof(pair1));
  memcpy(&pair2, input + 4, sizeof(pair2));
  memcpy(&pair3, input + 6, sizeof(pair3));
  const uint32_t packed = ((pair0 & 7U) << 0) |
                          (((pair0 >> 16) & 7U) << 3) |
                          ((pair1 & 7U) << 6) |
                          (((pair1 >> 16) & 7U) << 9) |
                          ((pair2 & 7U) << 12) |
                          (((pair2 >> 16) & 7U) << 15) |
                          ((pair3 & 7U) << 18) |
                          (((pair3 >> 16) & 7U) << 21);
  output[0] = static_cast<uint8_t>(packed);
  output[1] = static_cast<uint8_t>(packed >> 8);
  output[2] = static_cast<uint8_t>(packed >> 16);
}

// Fast lanes stay sample-major in 24-bit groups; the eight slow lane values
// share the final byte.  There is no per-channel alignment or block padding.
static void encodeBlock3Fast8Slow(const uint16_t *input, uint8_t *output)
{
  for (unsigned group = 0; group < BLOCK_SAMPLES / 8; ++group)
  {
    packFast8(input + group * 8, output + group * 3);
  }
  output[24] = static_cast<uint8_t>(input[0] >> 3);
}

static void makeRawBlock(uint16_t *output, unsigned block)
{
  const unsigned slow = block & 0xFFU;
  for (unsigned sample = 0; sample < BLOCK_SAMPLES; ++sample)
  {
    const unsigned fast = ((sample & 1U) << 0) |
                          ((((sample >> 1) ^ block) & 1U) << 1) |
                          ((((sample >> 0) ^ (sample >> 1) ^ (sample >> 2) ^
                             (sample >> 3) ^ (sample >> 4) ^ (sample >> 5) ^ block) & 1U) << 2);
    output[sample] = static_cast<uint16_t>(fast | (slow << 3));
  }
}

static void buildWirePattern()
{
  for (unsigned block = 0; block < STREAM_BLOCKS; ++block)
  {
    makeRawBlock(rawBlock, block);
    encodeBlock3Fast8Slow(rawBlock, wirePattern + block * WIRE_BLOCK_BYTES);
  }
}

static void runCodecBenchmark()
{
  makeRawBlock(rawBlock, 105);
  const int64_t started = esp_timer_get_time();
  uint32_t checksum = 0;
  for (size_t block = 0; block < BENCH_BLOCKS; ++block)
  {
    rawBlock[0] = static_cast<uint16_t>((rawBlock[0] & 7U) | ((block & 0xFFU) << 3));
    encodeBlock3Fast8Slow(rawBlock, encodedBlock);
    checksum += encodedBlock[block % WIRE_BLOCK_BYTES];
  }
  const int64_t elapsed = esp_timer_get_time() - started;
  benchChecksum = checksum;
  const double logicalInputBytes = static_cast<double>(BENCH_BLOCKS) * BLOCK_SAMPLES * 2.0;
  const double outputBytes = static_cast<double>(BENCH_BLOCKS) * WIRE_BLOCK_BYTES;
  Serial.printf("E105_CODEC blocks=%u elapsed_us=%lld input_mb_s=%.3f wire_mb_s=%.3f checksum=%lu\n",
                static_cast<unsigned>(BENCH_BLOCKS),
                static_cast<long long>(elapsed),
                logicalInputBytes / static_cast<double>(elapsed),
                outputBytes / static_cast<double>(elapsed),
                static_cast<unsigned long>(checksum));
}

static void submitNext()
{
  if (!streaming || submittedBytes >= targetBytes)
  {
    return;
  }
  const uint64_t remaining = targetBytes - submittedBytes;
  const size_t phase = static_cast<size_t>(submittedBytes % STREAM_BYTES);
  const size_t beforeWrap = STREAM_BYTES - phase;
  size_t want = remaining < TX_ARM_BYTES ? static_cast<size_t>(remaining) : TX_ARM_BYTES;
  if (want > beforeWrap)
  {
    want = beforeWrap;
  }
  if (submittedBytes == 0)
  {
    firstAt = esp_timer_get_time();
  }
  submittedBytes += Vendor.write(wirePattern + phase, want);
}

extern "C" void esp_usb_device_direct_vendor_rx(const uint8_t *buffer, uint32_t length)
{
  if (length == 10 && buffer[0] == 'E' && buffer[1] == '5')
  {
    const uint64_t bytes = readLe64(buffer + 2);
    if (bytes == 0 || bytes % STREAM_BYTES != 0)
    {
      return;
    }
    targetBytes = bytes;
    submittedBytes = 0;
    completedBytes = 0;
    firstAt = 0;
    lastAt = 0;
    reportPending = false;
    streaming = true;
    submitNext();
    return;
  }
  if (length == 2 && buffer[0] == 'E' && buffer[1] == 'Q' && !streaming)
  {
    statusRequested = true;
  }
}

extern "C" void esp_usb_device_direct_vendor_tx_complete(uint32_t sentBytes)
{
  if (!streaming)
  {
    return;
  }
  completedBytes += sentBytes;
  lastAt = esp_timer_get_time();
  if (completedBytes >= targetBytes)
  {
    streaming = false;
    reportPending = true;
    return;
  }
  submitNext();
}

static void sendStatus()
{
  const int64_t elapsed = firstAt && lastAt >= firstAt ? lastAt - firstAt : 0;
  const int length = snprintf(statusBuffer, sizeof(statusBuffer),
                              "E105_STATUS bytes=%llu target=%llu elapsed_us=%lld checksum=%lu\n",
                              static_cast<unsigned long long>(completedBytes),
                              static_cast<unsigned long long>(targetBytes),
                              static_cast<long long>(elapsed),
                              static_cast<unsigned long>(benchChecksum));
  Vendor.write(reinterpret_cast<const uint8_t *>(statusBuffer), static_cast<size_t>(length));
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  runCodecBenchmark();
  buildWirePattern();

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4021;
  config.manufacturer = "wch-protocols";
  config.product = "E105 P4 mixed-rate codec";
  // Keep the already usbipd-bound Windows USB instance while changing firmware.
  config.serialNumber = "e104-p4-windows-v1";
  config.controller = EspUsbController::HighSpeed;
  config.webusbEnabled = true;
  Serial.printf("E105_DEVICE_BEGIN ok=%u stream_bytes=%u tx_arm=%u block_bytes=%u\n",
                device.begin(config) ? 1 : 0,
                static_cast<unsigned>(STREAM_BYTES),
                static_cast<unsigned>(TX_ARM_BYTES),
                static_cast<unsigned>(WIRE_BLOCK_BYTES));
}

void loop()
{
  if (reportPending)
  {
    reportPending = false;
    Serial.printf("E105_DONE bytes=%llu elapsed_us=%lld\n",
                  static_cast<unsigned long long>(completedBytes),
                  static_cast<long long>(lastAt - firstAt));
  }
  if (statusRequested)
  {
    statusRequested = false;
    sendStatus();
  }
  delay(0);
}
