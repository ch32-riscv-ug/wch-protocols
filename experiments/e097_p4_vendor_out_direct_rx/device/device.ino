#include "EspUsbDevice.h"

#include <esp_timer.h>

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

static volatile bool receiving = false;
static volatile bool ackPending = false;
static volatile uint32_t targetBytes = 0;
static volatile uint32_t receivedBytes = 0;
static volatile int64_t firstByteAt = 0;
static volatile int64_t lastByteAt = 0;

static uint32_t readLe32(const uint8_t *p)
{
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

extern "C" void esp_usb_device_direct_vendor_rx(const uint8_t *buffer, uint32_t length)
{
  if (length == 0)
  {
    return;
  }
  if (!receiving && length == 5 && buffer[0] == 'B')
  {
    targetBytes = readLe32(buffer + 1);
    receivedBytes = 0;
    firstByteAt = 0;
    lastByteAt = 0;
    ackPending = false;
    receiving = targetBytes != 0;
    return;
  }
  if (!receiving && length == 1 && buffer[0] == 'E')
  {
    ackPending = true;
    return;
  }
  if (receiving)
  {
    const int64_t now = esp_timer_get_time();
    if (receivedBytes == 0)
    {
      firstByteAt = now;
    }
    lastByteAt = now;
    receivedBytes += length;
    if (receivedBytes >= targetBytes)
    {
      receiving = false;
    }
  }
}

static void sendAck()
{
  char line[160];
  const uint32_t received = receivedBytes;
  const uint32_t target = targetBytes;
  const int64_t first = firstByteAt;
  const int64_t last = lastByteAt;
  const long long elapsed = first && last >= first ? static_cast<long long>(last - first) : 0;
  const int length = snprintf(line, sizeof(line),
                              "E091_ACK received=%lu target=%lu bad_words=0 elapsed_us=%lld\n",
                              static_cast<unsigned long>(received),
                              static_cast<unsigned long>(target), elapsed);
  Vendor.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(length));
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4020;
  config.manufacturer = "wch-protocols";
  config.product = "E097 direct vendor RX peer";
  config.serialNumber = "e097-p4-direct-rx";
  Serial.printf("E097_DEVICE_BEGIN ok=%u rx_xfer=%u buffered=%u\n",
                device.begin(config) ? 1 : 0,
                static_cast<unsigned>(CFG_TUD_VENDOR_RX_EPSIZE),
                static_cast<unsigned>(CFG_TUD_VENDOR_TXRX_BUFFERED));
}

void loop()
{
  if (ackPending)
  {
    ackPending = false;
    sendAck();
  }
  delay(0);
}
