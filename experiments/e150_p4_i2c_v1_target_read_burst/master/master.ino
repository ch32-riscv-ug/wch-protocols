#include <driver/i2c_master.h>
#include <esp_timer.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_1;
constexpr size_t kFrameLength = 128;

void setup() { Serial.begin(115200); }

void loop() {
  if (!Serial.available()) return;
  String command = Serial.readStringUntil('\n');
  unsigned long input_hz = 0, input_count = 0;
  if (sscanf(command.c_str(), "BURSTREAD %lu %lu", &input_hz, &input_count) != 2 ||
      !input_hz || !input_count || input_count > 100) {
    Serial.println("usage: BURSTREAD <Hz> <1..100 count>");
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
  uint8_t frame[kFrameLength] = {};
  uint32_t completed = 0, pattern_errors = 0, bad_frames = 0;
  int first_bad_frame = -1, first_bad_offset = -1;
  uint8_t first_bad_actual = 0;
  int64_t started_us = esp_timer_get_time();
  for (; completed < input_count && result == ESP_OK; ++completed) {
    result = i2c_master_receive(dev, frame, sizeof(frame), 50);
    bool frame_ok = true;
    for (size_t i = 0; i < sizeof(frame); ++i) {
      if (frame[i] != static_cast<uint8_t>(0x80 + i)) {
        ++pattern_errors;
        frame_ok = false;
        if (first_bad_frame < 0) {
          first_bad_frame = completed;
          first_bad_offset = i;
          first_bad_actual = frame[i];
        }
      }
    }
    if (!frame_ok) ++bad_frames;
  }
  int64_t elapsed_us = esp_timer_get_time() - started_us;
  Serial.printf("BURSTREAD hz=%lu count=%lu completed=%lu bytes=%lu elapsed_us=%lld result=0x%x pattern_errors=%lu bad_frames=%lu first_bad=%d:%d:%02x\n",
                input_hz, input_count, (unsigned long)completed,
                (unsigned long)(completed * kFrameLength), (long long)elapsed_us,
                (unsigned)result, (unsigned long)pattern_errors, (unsigned long)bad_frames,
                first_bad_frame, first_bad_offset, first_bad_actual);
  if (dev) i2c_master_bus_rm_device(dev);
  if (bus) i2c_del_master_bus(bus);
}
