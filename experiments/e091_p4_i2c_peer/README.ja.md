# E091 P4 peer: hardware I2C slave baseline

P4 pair `30eda0e31108` / `30eda0e34a0e` のGPIO32=SDA、GPIO33=SCL直結を使う。
既存flashは保存せず上書きし、slave `0x42` への固定長write を確認する。

## 手順

- `master` を `30eda0e31108`、`slave` を `30eda0e34a0e` に書き込む。
- 両sketchの `sketch.yaml` はArduino-ESP32 `3.3.12` とP4のCDC設定を固定する。
  `arduino-cli compile --clean --profile esp32p4 <sketch-dir>` でビルドする。
- master のUSB CDCへ `RUN <Hz> <length>`（例: `RUN 100000 4`）を送る。
- slaveの `length`、payload checksum、transaction countとmasterの成功結果を組にして記録する。

識別済みの `/run/board-identify/by-id/esp32-series-*` だけをポート指定に使う。
リンクが無い時に `/dev/tty*` へ代替しない。

### ビルド再現性

platform versionはCLIに現在インストールされている版へ暗黙に追従させない。必ず
`sketch.yaml` のprofileを選択する。platform versionまたはFQBN/profileを変更した時は、
古いcore由来の中間成果物を使わないよう `--clean` を必須とする。

受信長は `slave/build_opt.h` の `E091_WRITE_LENGTH` で選ぶ。`build.extra_flags` と
`compiler.cpp.extra_flags` は使わない。`build_opt.h` を変更した場合も、core/library objectへ
確実に反映させるため `--clean` を必須とする。

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

最初のtarget診断は、4 byte writeに対して `i2c_slave_receive()` のbuffer sizeを128 byteにしていたため
Load access faultになった。v1のFIFO完了ISRは要求された受信サイズを使ってFIFOからコピーする。
そのため、v1では受信ジョブのsizeを当該トランザクションの期待長に合わせる必要がある。

現行slaveは公式v1手順どおり、staticな固定長bufferをcallback完了まで保持し、callbackでは完了通知だけを
行い、`loop()` 側で次の `i2c_slave_receive()` をarmする。1/10/100/400 kHzで全て4 byte
`11 22 33 44` を受信した。100 kHzでの100連続トランザクションもmaster 100/100成功、slaveは
クラッシュせず全件を受信した。`build_opt.h`で指定して再ビルドした1/16/32/64/128 byteも100 kHzで
master成功・payload checksum一致・再arm成功を確認した。

Espressif IDF v5.5.5の `i2c_slave_network_sensor` 例はslave **v2** API
（`.receive_buf_depth`、`.on_receive`、`i2c_slave_write()`）であり、Arduino配布SDKのv1構成とは
同じAPIにはならない。P4 OEP targetとしては、固定長のv1受信は実証済みだが、可変長フレームは
v1 FIFO挙動を別途設計・試験してから提供する。
