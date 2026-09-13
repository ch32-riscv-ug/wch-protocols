#include "EspUsbDevice.h"

#include <esp_timer.h>

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

#ifndef E104_VALIDATE_OUT
#define E104_VALIDATE_OUT 1
#endif

static constexpr size_t TX_CHUNK = CFG_TUD_VENDOR_TX_EPSIZE;

enum class Mode : uint8_t { Idle, Out, In };

static uint8_t txPattern[TX_CHUNK] __attribute__((aligned(64)));
static char statusBuffer[256] __attribute__((aligned(64)));

static volatile Mode mode = Mode::Idle;
static volatile uint64_t targetBytes = 0;
static volatile uint64_t submittedBytes = 0;
static volatile uint64_t completedBytes = 0;
static volatile uint64_t receivedBytes = 0;
static volatile uint32_t badTransfers = 0;
static volatile int64_t firstAt = 0;
static volatile int64_t lastAt = 0;
static volatile bool reportPending = false;
static volatile bool statusRequested = false;

static uint64_t readLe64(const uint8_t *p)
{
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
  {
    value |= static_cast<uint64_t>(p[i]) << (8 * i);
  }
  return value;
}

static void resetCounters(uint64_t bytes)
{
  targetBytes = bytes;
  submittedBytes = 0;
  completedBytes = 0;
  receivedBytes = 0;
  badTransfers = 0;
  firstAt = 0;
  lastAt = 0;
  reportPending = false;
}

static void submitNext()
{
  if (mode != Mode::In || submittedBytes >= targetBytes)
  {
    return;
  }
  const uint64_t remaining = targetBytes - submittedBytes;
  const size_t want = remaining < TX_CHUNK ? static_cast<size_t>(remaining) : TX_CHUNK;
  if (submittedBytes == 0)
  {
    firstAt = esp_timer_get_time();
  }
  const size_t written = Vendor.write(txPattern, want);
  submittedBytes += written;
}

extern "C" void esp_usb_device_direct_vendor_rx(const uint8_t *buffer,
                                                  uint32_t length)
{
  if (length == 10 && buffer[0] == 'E' && buffer[1] == '1')
  {
    const uint64_t bytes = readLe64(buffer + 2);
    if (bytes == 0)
    {
      return;
    }
    resetCounters(bytes);
    if (buffer[1] == '1')
    {
      mode = Mode::In;
      submitNext();
    }
    return;
  }
  if (length == 10 && buffer[0] == 'E' && buffer[1] == '0')
  {
    const uint64_t bytes = readLe64(buffer + 2);
    if (bytes == 0)
    {
      return;
    }
    resetCounters(bytes);
    mode = Mode::Out;
    return;
  }
  if (length == 2 && buffer[0] == 'E' && buffer[1] == 'Q' && mode == Mode::Idle)
  {
    statusRequested = true;
    return;
  }
  if (mode != Mode::Out || length == 0)
  {
    return;
  }

  const int64_t now = esp_timer_get_time();
  if (receivedBytes == 0)
  {
    firstAt = now;
  }
  const size_t offset = static_cast<size_t>(receivedBytes % TX_CHUNK);
#if E104_VALIDATE_OUT
  size_t checked = 0;
  while (checked < length)
  {
    const size_t available = TX_CHUNK - ((offset + checked) % TX_CHUNK);
    const size_t part = (length - checked) < available ? (length - checked) : available;
    if (memcmp(buffer + checked, txPattern + ((offset + checked) % TX_CHUNK), part) != 0)
    {
      ++badTransfers;
    }
    checked += part;
  }
#else
  (void)buffer;
  (void)offset;
#endif
  receivedBytes += length;
  lastAt = now;
  if (receivedBytes >= targetBytes)
  {
    mode = Mode::Idle;
    reportPending = true;
  }
}

extern "C" void esp_usb_device_direct_vendor_tx_complete(uint32_t sentBytes)
{
  if (mode != Mode::In)
  {
    return;
  }
  completedBytes += sentBytes;
  lastAt = esp_timer_get_time();
  if (completedBytes >= targetBytes)
  {
    mode = Mode::Idle;
    reportPending = true;
    return;
  }
  submitNext();
}

static void printReport()
{
  const uint64_t bytes = completedBytes ? completedBytes : receivedBytes;
  const int64_t elapsed = firstAt && lastAt >= firstAt ? lastAt - firstAt : 0;
  Serial.printf("E104_DONE bytes=%llu target=%llu bad=%lu elapsed_us=%lld\n",
                static_cast<unsigned long long>(bytes),
                static_cast<unsigned long long>(targetBytes),
                static_cast<unsigned long>(badTransfers),
                static_cast<long long>(elapsed));
}

static void sendStatus()
{
  const uint64_t bytes = completedBytes ? completedBytes : receivedBytes;
  const int64_t elapsed = firstAt && lastAt >= firstAt ? lastAt - firstAt : 0;
  const int length = snprintf(statusBuffer, sizeof(statusBuffer),
                              "E104_STATUS bytes=%llu target=%llu bad=%lu elapsed_us=%lld\n",
                              static_cast<unsigned long long>(bytes),
                              static_cast<unsigned long long>(targetBytes),
                              static_cast<unsigned long>(badTransfers),
                              static_cast<long long>(elapsed));
  Vendor.write(reinterpret_cast<const uint8_t *>(statusBuffer), static_cast<size_t>(length));
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  for (size_t i = 0; i < TX_CHUNK; ++i)
  {
    txPattern[i] = static_cast<uint8_t>(i);
  }
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4021;
  config.manufacturer = "wch-protocols";
  config.product = "E104 P4 Windows continuous bulk";
  config.serialNumber = "e104-p4-windows-v1";
  config.controller = EspUsbController::HighSpeed;
  config.webusbEnabled = true;
  Serial.printf("E104_DEVICE_BEGIN ok=%u rx_xfer=%u tx_xfer=%u buffered=%u\n",
                device.begin(config) ? 1 : 0,
                static_cast<unsigned>(CFG_TUD_VENDOR_RX_EPSIZE),
                static_cast<unsigned>(CFG_TUD_VENDOR_TX_EPSIZE),
                static_cast<unsigned>(CFG_TUD_VENDOR_TXRX_BUFFERED));
}

void loop()
{
  if (reportPending)
  {
    reportPending = false;
    printReport();
  }
  if (statusRequested)
  {
    statusRequested = false;
    sendStatus();
  }
  delay(0);
}
