# E091 P4 peer: hardware I2C slave baseline

P4 pair `30eda0e31108` / `30eda0e34a0e` のGPIO32=SDA、GPIO33=SCL直結を使う。
既存flashは保存せず上書きし、slave `0x42` への4-byte write を確認する。

## 手順

- `master` を `30eda0e31108`、`slave` を `30eda0e34a0e` に書き込む。
- master のUSB CDCへ `RUN 1000`、`RUN 10000`、`RUN 100000`、`RUN 400000` を送る。
- slave の `bytes=4 length=4` とmasterの成功結果を組にして記録する。

識別済みの `/run/board-identify/by-id/esp32-series-*` だけをポート指定に使う。
リンクが無い時に `/dev/tty*` へ代替しない。

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
