# E150 P4 I2C slave v1: preloaded target-read burst

P4 I2C slave v1のTX ringに128 byte frameを最大100本（12,800 byte）preloadし、同じI2C busを維持した
master read burstで全byteを照合する。slaveへ`PREP <count>`、masterへ`BURSTREAD <Hz> <count>`を送る。

Arduino-ESP32 3.3.12は各sketchの`sketch.yaml`で固定し、変更時を含め常に`--clean`でビルドする。

## 2026-09-22 実測

単純に128 byte frameを100本連結したTX ringでは、master transactionは100/100 `ESP_OK`でも
12,671 byteが不一致だった。2本目の先頭が`0x81`であり、v1 slave TX ringはmaster readのNACK境界で
次slot先頭の1 byteを余分に消費することを観測した。

各128 byte payloadの直後へ1 byte fillerを置く129 byte slotにすると、この消費を隔離できた。

| clock | frames | payload | elapsed | full-pattern |
|---:|---:|---:|---:|---:|
| 400 kHz | 100 | 12,800 byte | 331,278 us | PASS |
| 1 MHz | 100 | 12,800 byte | 199,838 us | PASS |

同じ二台（`30:ed:a0:e3:11:08` controller、`30:ed:a0:e3:4a:0e` target）の固定aliasを
2026-09-22に再接続後、firmwareを上書きせず再実行した結果も、`199,844 us`、
`completed=100`、`pattern_errors=0`、`bad_frames=0`だった。従ってこの値は一回限りの
初回成功ではなく、現在の配線・Arduino-ESP32 3.3.12における再現可能な基準値である。

続けて、毎回`PREP 100`からやり直す1 MHz burstを10回反復した。10/10回が
`completed=100`、`pattern_errors=0`、`bad_frames=0`で、elapsedは199,716–199,849 us
（平均199,828 us）だった。これは短時間の反復安定性を示すが、温度・給電変動を含む長時間
耐久、clock stretch、masterが途中でNACKする異常系は別試験とする。

従ってP4 I2C slave v1で複数の固定128 byte応答をpreloadする場合、**read slotごとに1 byte fillerを
予約する**。これはdriver固有の実測契約であり、OEP capabilityにはtarget側がこのslot管理を提供できる場合だけ
`i2c.target.read.preloaded`を宣言する。
