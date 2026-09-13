// E087: which GPIOs on this board are free, and does pull-up + pull-down really
// sit at half rail? Plan and report: README.ja.md
//
// The survey never drives a pin. It only enables the internal pull-up, reads,
// then the pull-down, and reads again: a pin that follows both is floating and
// free to wire to, one that refuses to move is held by something on the board.
// A weak pull cannot damage whatever that something is, which is why the test
// is built this way round.
//
// On ESP32-P4 flash, PSRAM and both USB ports sit on dedicated pads -- the SoC
// headers give MSPI_IOMUX_PIN_NUM_* = GPIO_NUM_INVALID and USBPHY_*_NUM = -1 --
// so nothing in the GPIO space is reserved by the chip. What is taken is the
// board's doing, and that is what this measures.
//
// A blind 0..54 sweep killed this board's console on the first attempt, and
// recovering cost a physical replug. So there is no blind sweep any more: the
// caller names the pins, one range at a time, and every line is flushed before
// the next pin is touched -- if a pin takes the board down, the last line
// received names the pin before it.

#include <Arduino.h>
#include <HWCDC.h>
#include <driver/gpio.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif
// Pins the survey will not touch. Empty by default: nothing on a P4 is reserved
// by the chip itself.
#ifndef SKIP_PINS
#define SKIP_PINS ""
#endif

static constexpr int kMaxPin = 54;      // SOC_GPIO_IN_RANGE_MAX
static constexpr int kAdcFirst = 16;    // ADC1 channels 0..7 are GPIO 16..23
static constexpr int kAdcLast = 23;

static HWCDC Console;
static char command[96];
static size_t command_length;
static bool host_armed;

static bool skipped(int pin) {
  const char *cursor = SKIP_PINS;
  while (*cursor) {
    char *end = nullptr;
    const long value = strtol(cursor, &end, 10);
    if (end == cursor) {
      break;
    }
    if (value == pin) {
      return true;
    }
    cursor = (*end == ',') ? end + 1 : end;
  }
  return false;
}

// Returns the level a pin settles to under the given pull, or -1 if the pin
// cannot be configured at all.
static int read_under_pull(int pin, gpio_pull_mode_t pull) {
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  if (gpio_reset_pin(gpio) != ESP_OK) {
    return -1;
  }
  if (gpio_set_direction(gpio, GPIO_MODE_INPUT) != ESP_OK) {
    return -1;
  }
  if (gpio_set_pull_mode(gpio, pull) != ESP_OK) {
    return -1;
  }
  delayMicroseconds(2000);  // let the pad settle through whatever is attached
  return gpio_get_level(gpio);
}

static void survey(int from, int to) {
  Console.printf("SURVEY begin from=%d to=%d\n", from, to);
  Console.flush();
  for (int pin = from; pin <= to && pin <= kMaxPin; ++pin) {
    if (skipped(pin)) {
      Console.printf("PIN %d skip\n", pin);
      Console.flush();
      continue;
    }
    const int up = read_under_pull(pin, GPIO_PULLUP_ONLY);
    const int down = read_under_pull(pin, GPIO_PULLDOWN_ONLY);
    const char *verdict;
    if (up < 0 || down < 0) {
      verdict = "unusable";
    } else if (up == 1 && down == 0) {
      verdict = "free";  // follows its own pull: nothing else is holding it
    } else if (up == 1 && down == 1) {
      verdict = "held_high";
    } else if (up == 0 && down == 0) {
      verdict = "held_low";
    } else {
      verdict = "inverted";
    }
    Console.printf("PIN %d up=%d down=%d %s\n", pin, up, down, verdict);
    Console.flush();  // so a pin that kills the board is identified by what came before
    gpio_reset_pin(static_cast<gpio_num_t>(pin));
  }
  Console.println("SURVEY end");
  Console.flush();
}

// Both pulls at once should divide the rail. Measured with this chip's own ADC
// on the same pad, so it needs no wiring.
static void divider(int pin) {
  if (pin < kAdcFirst || pin > kAdcLast) {
    Console.printf("DIV pin=%d status=not_adc (ADC1 is GPIO %d..%d)\n", pin, kAdcFirst, kAdcLast);
    Console.flush();
    return;
  }
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  struct { const char *name; gpio_pull_mode_t mode; } cases[] = {
    {"floating", GPIO_FLOATING},
    {"pullup", GPIO_PULLUP_ONLY},
    {"pulldown", GPIO_PULLDOWN_ONLY},
    {"both", GPIO_PULLUP_PULLDOWN},
  };
  for (auto &entry : cases) {
    // Order matters: analogRead reconfigures the pad, so the pull goes on after
    // the ADC has claimed it. Setting it the other way round is the thing being
    // checked here.
    analogReadMilliVolts(pin);
    gpio_set_pull_mode(gpio, entry.mode);
    delay(5);
    int sum = 0;
    for (int i = 0; i < 16; ++i) {
      sum += analogReadMilliVolts(pin);
    }
    Console.printf("DIV pin=%d pull=%s mv=%d\n", pin, entry.name, sum / 16);
    Console.flush();
  }
  gpio_reset_pin(gpio);
}

static void handle_command(void) {
  if (command[0] == '?') {
    Console.printf("# EXP E087 v1 git=%s probe=esp32p4_pins target=none build=%s %s\n", BANNER_GIT, __DATE__,
                   __TIME__);
    Console.printf("ENV chip=%s pins=0..%d adc1=%d..%d skip=%s\n", ESP.getChipModel(), kMaxPin, kAdcFirst, kAdcLast,
                   SKIP_PINS[0] ? SKIP_PINS : "(none)");
    Console.flush();
    return;
  }
  // L <from> <to> -- read the level only. Touches nothing: no direction, no
  // pull, no reset. Safe on any pin, and enough to spot a pin the board is
  // driving hard.
  if (command[0] == 'L') {
    int from = 0;
    int to = kMaxPin;
    sscanf(command + 1, "%d %d", &from, &to);
    for (int pin = from; pin <= to && pin <= kMaxPin; ++pin) {
      Console.printf("LVL %d %d\n", pin, gpio_get_level(static_cast<gpio_num_t>(pin)));
      Console.flush();
    }
    Console.println("LVL end");
    Console.flush();
    return;
  }
  // W <from> <to> -- drive high, read back, drive low, read back. This is the
  // only command that drives, so it is restricted to a range the caller names
  // and should only be pointed at pins the board documents as general purpose.
  if (command[0] == 'W') {
    int from = -1;
    int to = -1;
    if (sscanf(command + 1, "%d %d", &from, &to) != 2 || from < 0 || to < from) {
      Console.println("DRIVE status=reject (need: W <from> <to>)");
      Console.flush();
      return;
    }
    for (int pin = from; pin <= to && pin <= kMaxPin; ++pin) {
      if (skipped(pin)) {
        continue;
      }
      const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
      gpio_reset_pin(gpio);
      gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT);
      gpio_set_level(gpio, 1);
      delayMicroseconds(500);
      const int high = gpio_get_level(gpio);
      gpio_set_level(gpio, 0);
      delayMicroseconds(500);
      const int low = gpio_get_level(gpio);
      gpio_reset_pin(gpio);
      Console.printf("DRIVE %d high=%d low=%d %s\n", pin, high, low,
                     (high == 1 && low == 0) ? "ok" : "contended");
      Console.flush();
    }
    Console.println("DRIVE end");
    Console.flush();
    return;
  }
  // S <from> <to> -- the pull-follow test. Changes pull settings, so it is the
  // one that can disturb the board. Deliberately requires a range.
  if (command[0] == 'S') {
    int from = -1;
    int to = -1;
    if (sscanf(command + 1, "%d %d", &from, &to) != 2 || from < 0 || to < from) {
      Console.println("SURVEY status=reject (need: S <from> <to>)");
      Console.flush();
      return;
    }
    survey(from, to);
    return;
  }
  if (command[0] == 'D') {
    int pin = 0;
    if (sscanf(command + 1, "%d", &pin) != 1) {
      Console.println("DIV status=reject");
      Console.flush();
      return;
    }
    divider(pin);
    return;
  }
  Console.println("CFG status=unknown");
  Console.flush();
}

void setup() {
  Console.begin();
}

void loop() {
  if (!host_armed) {
    if (millis() < 1000) {
      delay(10);
      return;
    }
    while (Console.available()) {
      Console.read();
    }
    Console.println("READY E087");
    Console.flush();
    host_armed = true;
    return;
  }
  if (!Console.available()) {
    delay(1);
    return;
  }
  while (Console.available()) {
    const int value = Console.read();
    if (value < 0) {
      break;
    }
    const char received = static_cast<char>(value);
    if (received == '\r') {
      continue;
    }
    if (received != '\n') {
      if (command_length < sizeof(command) - 1) {
        command[command_length++] = received;
      }
      continue;
    }
    command[command_length] = '\0';
    if (command_length > 0) {
      handle_command();
    }
    command_length = 0;
  }
}
