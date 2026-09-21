# E091 P4 peer: hardware I2C slave baseline

P4 pair `30eda0e31108` / `30eda0e34a0e` のGPIO32=SDA、GPIO33=SCL直結を使う。
既存flashは保存せず上書きし、slave `0x42` への4-byte write を確認する。

## 手順

- `master` を `30eda0e31108`、`slave` を `30eda0e34a0e` に書き込む。
- 両sketchの `sketch.yaml` はArduino-ESP32 `3.3.12` とP4のCDC設定を固定する。
  `arduino-cli compile --clean --profile esp32p4 <sketch-dir>` でビルドする。
- master のUSB CDCへ `RUN 1000`、`RUN 10000`、`RUN 100000`、`RUN 400000` を送る。
- slave の `bytes=4 length=4` とmasterの成功結果を組にして記録する。

識別済みの `/run/board-identify/by-id/esp32-series-*` だけをポート指定に使う。
リンクが無い時に `/dev/tty*` へ代替しない。

### ビルド再現性

platform versionはCLIに現在インストールされている版へ暗黙に追従させない。必ず
`sketch.yaml` のprofileを選択する。platform versionまたはFQBN/profileを変更した時は、
古いcore由来の中間成果物を使わないよう `--clean` を必須とする。

## 2026-09-21 の観測

Arduino-ESP32 3.3.11 の `Wire` master/slave では、32/33の双方が内部プルアップでHighであり、
masterの `Wire.begin()` も成功した。一方、1/10/100/400 kHz のいずれも
`endTransmission()` は `status=4`、slave は `bytes=0 length=0` だった。
従ってX035を外しても再現し、X035または配線固有の問題ではない。

P4のIDFは新旧I2C driverの混在を禁止する。旧式の `i2c_master_write_to_device()` を追加した
診断版では `i2c: CONFLICT! driver_ng is not allowed to be used with this old driver` でabortした。
このためmasterは新I2C driver API (`driver/i2c_master.h`) 専用にしてあり、次回はこの版と
slave側の新I2C target APIを対にして再試験する。診断中に `30eda0e31108` の識別リンクが消えたため、
本日時点では新API版の実機結果は未確定である。

その後の再アタッチ後に、新driver masterをI2C0・I2C1の双方で実行した。Arduino-ESP32 3.3.11では
bus作成とdevice追加は成功したが、`i2c_master_transmit()`（stage 3）が全速度で
`ESP_ERR_INVALID_STATE (0x103)` を返した。

## Arduino-ESP32 3.3.12 の結果

3.3.12へ更新後、new driver master（I2C1）とnew driver target（I2C0）の組では、
1/10/100/400 kHzの全てでmasterの `i2c_master_transmit()` が `ESP_OK (0x0)` になった。
従ってP4のGPIO32/33配線、内部プルアップ、ハードウェアtargetのアドレスACK、およびmaster送信は成立する。

しかしtargetが最初のwriteを受けるとCore 1で必ずLoad access faultになった。callbackを登録した場合も、
callbackを完全に登録せず `i2c_slave_receive()` を一回だけarmした場合も同じだった。クラッシュPCは
`memcpy` で、呼出元はIDFの `esp_driver_i2c/i2c_slave.c` の
`s_i2c_handle_complete` → `s_slave_fifo_isr_handler` → `s_slave_isr_handle_default` である。

これは実験アプリのcallback処理ではなく、Arduino-ESP32 3.3.12が同梱するP4 I2C slave **v1** driverの
FIFO受信完了経路にある再現性のある障害として扱う。Espressif IDF v5.5.5の
`i2c_slave_network_sensor` 例はslave **v2** API（`.receive_buf_depth`、`.on_receive`、
`i2c_slave_write()`）を使うため、このArduino配布SDKのv1構成とは同じ使い方にできない。
P4をOEP I2C targetに採用する前に上流へ最小再現として報告し、当面のprobe実装はS3で実証済みのI2C target
またはP4のソフトウェアtargetを使う。
