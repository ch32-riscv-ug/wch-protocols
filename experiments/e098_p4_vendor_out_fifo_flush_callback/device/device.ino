#include "EspUsbDevice.h"
#include "class/vendor/vendor_device.h"

#include <esp_timer.h>

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

#ifndef E098_COPY_FIFO_PAYLOAD
#define E098_COPY_FIFO_PAYLOAD 0
#endif
#if E098_COPY_FIFO_PAYLOAD
static uint8_t payloadBuffer[16384];
#endif

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

extern "C" void esp_usb_device_direct_vendor_rx(const uint8_t *, uint32_t length)
{
  if (receiving && length > 0)
  {
    const int64_t now = esp_timer_get_time();
    if (receivedBytes == 0)
    {
      firstByteAt = now;
    }
    lastByteAt = now;
#if E098_COPY_FIFO_PAYLOAD
    const uint32_t consumed = tud_vendor_n_read(0, payloadBuffer, sizeof(payloadBuffer));
#else
    const uint32_t consumed = length;
    tud_vendor_n_read_flush(0);
#endif
    receivedBytes += consumed;
    if (receivedBytes >= targetBytes)
    {
      receiving = false;
    }
    return;
  }

  uint8_t command[5];
  const uint32_t got = tud_vendor_n_read(0, command, sizeof(command));
  if (got == 5 && command[0] == 'B')
  {
    targetBytes = readLe32(command + 1);
    receivedBytes = 0;
    firstByteAt = 0;
    lastByteAt = 0;
    ackPending = false;
    receiving = targetBytes != 0;
  }
  else if (got == 1 && command[0] == 'E')
  {
    ackPending = true;
  }
  else
  {
    tud_vendor_n_read_flush(0);
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
  Vendor.flush();
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4020;
  config.manufacturer = "wch-protocols";
  config.product = "E098 buffered callback flush peer";
  config.serialNumber = "e098-p4-fifo-flush";
  Serial.printf("E098_DEVICE_BEGIN ok=%u rx_fifo=%u rx_xfer=%u\n",
                device.begin(config) ? 1 : 0,
                static_cast<unsigned>(CFG_TUD_VENDOR_RX_BUFSIZE),
                static_cast<unsigned>(CFG_TUD_VENDOR_RX_EPSIZE));
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
