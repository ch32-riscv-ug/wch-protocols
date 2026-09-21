#include <driver/i2c_master.h>
#include <esp_timer.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_1;
constexpr size_t kMaxPayload = 128;

void setup() { Serial.begin(115200); }

void loop() {
  if (!Serial.available()) return;
  String command = Serial.readStringUntil('\n');
  char mode[8] = {};
  unsigned long input_hz = 0, input_length = 0, input_extra = 0;
  if (sscanf(command.c_str(), "%7s %lu %lu %lu", mode, &input_hz, &input_length, &input_extra) < 3 ||
      (strcmp(mode, "FRAME") && strcmp(mode, "BURST")) || !input_hz || !input_length ||
      input_length > kMaxPayload) {
    Serial.println("usage: FRAME <Hz> <1..128 length> [gap_ms] | BURST <Hz> <1..128 length> <count>");
    return;
  }
  const uint32_t hz = input_hz;
  const size_t length = input_length;
  const bool burst = !strcmp(mode, "BURST");
  const uint32_t gap_ms = burst ? 0 : input_extra;
  const uint32_t count = burst ? input_extra : 1;
  if (!count) {
    Serial.println("BURST count must be non-zero");
    return;
  }

  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = kPort;
  bus_cfg.sda_io_num = (gpio_num_t)kSda;
  bus_cfg.scl_io_num = (gpio_num_t)kScl;
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  esp_err_t header_result = i2c_new_master_bus(&bus_cfg, &bus);
  i2c_master_dev_handle_t dev = nullptr;
  if (header_result == ESP_OK) {
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = kAddress;
    dev_cfg.scl_speed_hz = hz;
    header_result = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
  }
  uint8_t payload[kMaxPayload];
  for (size_t i = 0; i < length; ++i) payload[i] = static_cast<uint8_t>(0x11 + i);
  esp_err_t payload_result = header_result;
  uint32_t completed = 0;
  int64_t started_us = esp_timer_get_time();
  for (; completed < count && payload_result == ESP_OK; ++completed) {
    const uint8_t header = (uint8_t)length;
    header_result = i2c_master_transmit(dev, &header, 1, 50);
    if (header_result != ESP_OK) {
      payload_result = header_result;
      break;
    }
    if (gap_ms) delay(gap_ms);
    payload_result = i2c_master_transmit(dev, payload, length, 50);
  }
  int64_t elapsed_us = esp_timer_get_time() - started_us;
  Serial.printf("%s hz=%lu length=%u count=%lu completed=%lu elapsed_us=%lld header=0x%x payload=0x%x\n",
                mode, (unsigned long)hz, (unsigned)length, (unsigned long)count,
                (unsigned long)completed, (long long)elapsed_us, (unsigned)header_result,
                (unsigned)payload_result);
  if (dev) i2c_master_bus_rm_device(dev);
  if (bus) i2c_del_master_bus(bus);
}
