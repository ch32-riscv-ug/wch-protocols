// E142: non-destructive RVSWD probe of an X035 on ESP32-P4 GPIO2/GPIO54.
// Only DMCONTROL.dmactive is written; target flash and CPU state are untouched.

#include <Arduino.h>
#include <driver/gpio.h>

static int gDio = 2;
static int gClk = 54;
static unsigned gHalfUs = 3;

// ESP32-P4 GPIOs minus USB PHY (24-27), straps (34-38; also UART0 on
// 37/38). The flash pins are dedicated, not members of the GPIO number
// space. Keep this list explicit so a future core change cannot widen it.
static constexpr uint8_t kCandidates[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
  12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
  28, 29, 30, 31, 32, 33,
  39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
  49, 50, 51, 52, 53, 54,
};

struct Reply {
  uint32_t data;
  uint8_t status;
  bool parity_ok;
  uint8_t tail;
};

static inline void halfDelay() {
  if (gHalfUs) delayMicroseconds(gHalfUs);
}

static void releaseBus() {
  pinMode(gDio, INPUT);
  pinMode(gClk, INPUT);
}

static void releaseAll() {
  for (uint8_t pin : kCandidates) pinMode(pin, INPUT);
}

static void dataOutput() {
  pinMode(gDio, OUTPUT_OPEN_DRAIN | PULLUP);
  gpio_set_drive_capability(gpio_num_t(gDio), GPIO_DRIVE_CAP_0);
}

static void clockOutput() {
  pinMode(gClk, OUTPUT);
  gpio_set_drive_capability(gpio_num_t(gClk), GPIO_DRIVE_CAP_0);
}

static void clockBit(bool value) {
  digitalWrite(gClk, LOW);
  digitalWrite(gDio, value ? HIGH : LOW);
  halfDelay();
  digitalWrite(gClk, HIGH);
  halfDelay();
}

static bool readBit() {
  digitalWrite(gClk, LOW);
  pinMode(gDio, INPUT);
  halfDelay();
  bool value = digitalRead(gDio);
  digitalWrite(gClk, HIGH);
  halfDelay();
  return value;
}

static void startFrame() {
  clockOutput();
  dataOutput();
  digitalWrite(gClk, HIGH);
  digitalWrite(gDio, HIGH);
  halfDelay();
  digitalWrite(gDio, LOW);  // START: DIO falling while CLK is high.
  halfDelay();
}

static void stopFrame() {
  // Termination clock carrying zero, followed by STOP (DIO rises while CLK high).
  dataOutput();
  clockBit(false);
  digitalWrite(gClk, HIGH);
  digitalWrite(gDio, HIGH);
  halfDelay();
}

static void initializeBus() {
  clockOutput();
  dataOutput();
  digitalWrite(gClk, HIGH);
  digitalWrite(gDio, HIGH);
  delayMicroseconds(20);
  for (int i = 0; i < 100; ++i) clockBit(true);
  // Explicit STOP after the 100-high synchronization clocks.
  digitalWrite(gClk, LOW);
  digitalWrite(gDio, LOW);
  halfDelay();
  digitalWrite(gClk, HIGH);
  halfDelay();
  digitalWrite(gDio, HIGH);
  delayMicroseconds(20);
}

static void sendHeader(uint8_t address, bool write) {
  bool parity = write;
  for (int bit = 6; bit >= 0; --bit) {
    bool value = (address >> bit) & 1;
    parity ^= value;
    clockBit(value);
  }
  clockBit(write);
  clockBit(parity);  // even parity over address + R/W.
}

static Reply readDmi(uint8_t address) {
  startFrame();
  sendHeader(address, false);

  // Fixed auxiliary pattern used by independent implementations verified on
  // X035/V203/V307. These bits are host-driven even for a read transaction.
  dataOutput();
  clockBit(true);
  clockBit(false);
  clockBit(true);
  clockBit(false);
  clockBit(true);

  Reply reply = {};
  bool parity = false;
  for (int bit = 31; bit >= 0; --bit) {
    bool value = readBit();
    reply.data |= uint32_t(value) << bit;
    parity ^= value;
  }
  reply.parity_ok = readBit() == parity;
  dataOutput();
  clockBit(true);
  clockBit(false);
  clockBit(true);
  clockBit(true);
  clockBit(true);
  reply.status = 0;
  reply.tail = 0x17;
  stopFrame();
  delayMicroseconds(20);
  return reply;
}

static Reply writeDmi(uint8_t address, uint32_t data) {
  startFrame();
  sendHeader(address, true);
  dataOutput();
  clockBit(true);
  clockBit(false);
  clockBit(true);
  clockBit(false);
  clockBit(true);

  bool parity = false;
  for (int bit = 31; bit >= 0; --bit) {
    bool value = (data >> bit) & 1;
    parity ^= value;
    clockBit(value);
  }
  clockBit(parity);
  Reply reply = {};
  clockBit(true);
  clockBit(false);
  clockBit(true);
  clockBit(true);
  clockBit(true);
  reply.status = 0;
  reply.tail = 0x17;
  reply.parity_ok = true;
  stopFrame();
  delayMicroseconds(20);
  return reply;
}

static void printReply(const char *name, const Reply &r) {
  Serial.printf("%s data=0x%08lx status=%u parity=%s tail=0x%02x\n",
                name, (unsigned long)r.data, r.status,
                r.parity_ok ? "ok" : "bad", r.tail);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.printf("# EXP E142 v3 chip=ESP32-P4 fixed-pair timing diagnostic\n");
  releaseAll();
  int found = 0;
  static constexpr uint8_t pairs[][2] = {{2, 54}, {54, 2}};
  for (auto &pair : pairs) {
    for (unsigned halfUs : {0U, 1U, 2U, 3U, 5U}) {
      gDio = pair[0];
      gClk = pair[1];
      gHalfUs = halfUs;
      for (int attempt = 0; attempt < 3; ++attempt) {
        releaseAll();
        initializeBus();
        Reply wr = writeDmi(0x10, 0x00000001);
        Reply wr2 = writeDmi(0x10, 0x00000001);
        Reply status = readDmi(0x11);
        Reply control = readDmi(0x10);
      bool valid = control.parity_ok && status.parity_ok &&
                   control.data == 1 &&
                   (((status.data >> 8) & 0xf) == 3 ||
                    ((status.data >> 8) & 0xf) == 12);
        if (valid) ++found;
        Serial.printf("TRY dio=%u clk=%u half_us=%u attempt=%d valid=%d write_status=%u/%u ",
                      gDio, gClk, gHalfUs, attempt + 1, valid, wr.status, wr2.status);
        printReply("DMCONTROL", control);
        printReply("DMSTATUS", status);
      }
    }
  }
  releaseAll();
  Serial.printf("DONE trials=30 found=%d lines=Hi-Z\n", found);
}

void loop() { delay(1000); }
