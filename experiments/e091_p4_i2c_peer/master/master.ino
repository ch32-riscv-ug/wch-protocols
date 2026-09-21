#include <driver/i2c_master.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_1;

void setup() { Serial.begin(115200); }

void loop() {
  if (!Serial.available()) return;
  String c = Serial.readStringUntil('\n');
  uint32_t hz = c.substring(4).toInt();
  if (!hz) hz = 10000;

  pinMode(kSda, INPUT_PULLUP);
  pinMode(kScl, INPUT_PULLUP);
  delay(2);
  Serial.printf("LINES before sda=%d scl=%d\n", digitalRead(kSda), digitalRead(kScl));

  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = kPort;
  bus_cfg.sda_io_num = (gpio_num_t)kSda;
  bus_cfg.scl_io_num = (gpio_num_t)kScl;
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  int stage = 1;
  esp_err_t result = i2c_new_master_bus(&bus_cfg, &bus);

  i2c_master_dev_handle_t dev = nullptr;
  if (result == ESP_OK) {
    stage = 2;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = kAddress;
    dev_cfg.scl_speed_hz = hz;
    result = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
  }
  const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
  if (result == ESP_OK) {
    stage = 3;
    result = i2c_master_transmit(dev, payload, sizeof(payload), 50);
  }
  Serial.printf("MASTER-NG port=%d stage=%d hz=%lu result=0x%x lines sda=%d scl=%d\n",
                kPort, stage, (unsigned long)hz, (unsigned)result, digitalRead(kSda), digitalRead(kScl));
  if (dev) i2c_master_bus_rm_device(dev);
  if (bus) i2c_del_master_bus(bus);
}
