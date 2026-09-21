# E149 P4 I2C slave v1: target read response

P4 targetがmaster readへ応答できるかを検証する。slave CDCへ `PREP <length>` を送り、
`i2c_slave_transmit()` で`0x80 + offset`の固定patternをpreloadする。次にmaster CDCへ
`READ <Hz> <length>`を送り、全byteがpatternどおりかを検査する。

両sketchは`sketch.yaml`のArduino-ESP32 3.3.12 profileを使い、`--clean`でビルドする。
masterは`30eda0e31108`、slaveは`30eda0e34a0e`へ書き込む。

## 2026-09-22 実測

以下はすべてslaveで`PREP <length>`を完了してからmasterを実行し、master側が全byteを
`0x80 + offset`と比較した結果である。

| clock | length | result | full-pattern |
|---:|---:|---:|---:|
| 1 kHz | 4 | `ESP_OK` | PASS |
| 100 kHz | 16 | `ESP_OK` | PASS |
| 400 kHz | 128 | `ESP_OK` | PASS |
| 1 MHz | 128 | `ESP_OK` | PASS |

v1にはv2の`on_request` callbackがない。そのため本実験で確認した能力は、**read開始前に
`i2c_slave_transmit()`で応答をpreloadする固定応答**までである。read要求を受けてから内容を生成する
動的応答、連続readの補充、clock stretchの利用は別実験で確認する。
