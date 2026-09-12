// E072 host side: measure a device-to-host bulk IN stream with no PC in the
// path. Plan and report: README.ja.md
//
// Board 2 runs this as USB host on its OTG HS port, cabled straight to board 1's
// OTG HS port. Every earlier number went through usbipd and a Windows host, so
// this is the first measurement of the device stack on its own.

#include <Arduino.h>
#include <HWCDC.h>

#include "EspUsbHost.h"

#include <esp_timer.h>

// The proven console pattern on these boards: own the USB-Serial-JTAG
// explicitly rather than relying on the CDCOnBoot menu option.
static HWCDC Console;
static EspUsbHost usb;
static uint8_t deviceAddress = 0;

static volatile uint64_t first_us = 0;
static volatile uint64_t last_us = 0;
static volatile size_t total_bytes = 0;
static volatile uint32_t chunks = 0;
static volatile size_t max_chunk = 0;

static uint64_t idle_since = 0;
static size_t reported_bytes = 0;

void setup() {
  Console.begin();
  delay(1500);
  Console.println("READY E072");
  Console.flush();

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
    deviceAddress = device.address;
    Console.printf("CONNECT addr=%u vid=%04x pid=%04x speed=%u\n", device.address, device.vid, device.pid,
                  (unsigned)device.speed);
    Console.printf("VENDOROPEN %s\n", usb.vendorOpen(deviceAddress) ? "ok" : "failed");
  });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &) { Console.println("DISCONNECT"); });

  usb.onVendorData([](const EspUsbHostVendorData &data) {
    const uint64_t now = esp_timer_get_time();
    if (total_bytes == 0) {
      first_us = now;
    }
    last_us = now;
    total_bytes += data.length;
    ++chunks;
    if (data.length > max_chunk) {
      max_chunk = data.length;
    }
  });

  if (!usb.begin()) {
    Console.printf("BEGIN failed: %s\n", usb.lastErrorName());
  }
}

void loop() {
  const size_t seen = total_bytes;
  const uint64_t now = esp_timer_get_time();

  if (seen != reported_bytes) {
    reported_bytes = seen;
    idle_since = now;
  } else if (seen > 0 && idle_since && (now - idle_since) > 300000) {
    // The stream has been quiet for 300 ms: report the burst and reset.
    const uint64_t span = last_us - first_us;
    const double mb_s = span ? (double)seen / ((double)span / 1e6) / 1e6 : 0.0;
    Console.printf("BURST bytes=%u chunks=%lu max_chunk=%u span_us=%llu mb_s=%.3f\n", (unsigned)seen,
                  (unsigned long)chunks, (unsigned)max_chunk, (unsigned long long)span, mb_s);
    total_bytes = 0;
    chunks = 0;
    max_chunk = 0;
    first_us = 0;
    idle_since = 0;
    reported_bytes = 0;
  }
  delay(5);
}
