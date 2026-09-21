#include <driver/i2c_master.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_1;
constexpr size_t kMaxLength = 128;

void setup() { Serial.begin(115200); }

void loop() {
  if (!Serial.available()) return;
  String command = Serial.readStringUntil('\n');
  unsigned long input_hz = 0, input_length = 0;
  if (sscanf(command.c_str(), "READ %lu %lu", &input_hz, &input_length) != 2 ||
      !input_hz || !input_length || input_length > kMaxLength) {
    Serial.println("usage: READ <Hz> <1..128 length>");
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
  esp_err_t result = i2c_new_master_bus(&bus_cfg, &bus);
  i2c_master_dev_handle_t dev = nullptr;
  if (result == ESP_OK) {
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = kAddress;
    dev_cfg.scl_speed_hz = input_hz;
    result = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
  }
  uint8_t data[kMaxLength] = {};
  if (result == ESP_OK) result = i2c_master_receive(dev, data, input_length, 50);
  bool pattern_ok = result == ESP_OK;
  for (size_t i = 0; i < input_length; ++i) {
    if (data[i] != static_cast<uint8_t>(0x80 + i)) pattern_ok = false;
  }
  Serial.printf("READ hz=%lu length=%lu result=0x%x pattern_ok=%d first=%02x last=%02x\n",
                input_hz, input_length, (unsigned)result, pattern_ok, data[0], data[input_length - 1]);
  if (dev) i2c_master_bus_rm_device(dev);
  if (bus) i2c_del_master_bus(bus);
}
