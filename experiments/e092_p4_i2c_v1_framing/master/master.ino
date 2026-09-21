#include <driver/i2c_master.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_1;
constexpr size_t kMaxPayload = 128;

void setup() { Serial.begin(115200); }

void loop() {
  if (!Serial.available()) return;
  String command = Serial.readStringUntil('\n');
  uint32_t hz = 0, length = 0, gap_ms = 0;
  if (sscanf(command.c_str(), "FRAME %lu %lu %lu", &hz, &length, &gap_ms) < 2 ||
      !hz || !length || length > kMaxPayload) {
    Serial.println("usage: FRAME <Hz> <1..128 length> [gap_ms]");
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
  const uint8_t header = (uint8_t)length;
  if (header_result == ESP_OK) header_result = i2c_master_transmit(dev, &header, 1, 50);
  if (gap_ms) delay(gap_ms);
  uint8_t payload[kMaxPayload];
  for (size_t i = 0; i < length; ++i) payload[i] = static_cast<uint8_t>(0x11 + i);
  esp_err_t payload_result = header_result;
  if (payload_result == ESP_OK) payload_result = i2c_master_transmit(dev, payload, length, 50);
  Serial.printf("FRAME hz=%lu length=%lu gap_ms=%lu header=0x%x payload=0x%x\n",
                (unsigned long)hz, (unsigned long)length, (unsigned long)gap_ms,
                (unsigned)header_result, (unsigned)payload_result);
  if (dev) i2c_master_bus_rm_device(dev);
  if (bus) i2c_del_master_bus(bus);
}
