# E092 P4 I2C slave v1: length-prefixed framing

Arduino-ESP32 3.3.12のP4 I2C slave v1は、受信をarmする時点で長さを決める必要がある。
そこで1-byte payload lengthを独立I2C transactionで送り、slaveがpayload長で再armした後に
payload transactionを送る。

`sketch.yaml` の `esp32p4` profileを使い、`--clean` でビルドする。masterは
`30eda0e31108`、slaveは`30eda0e34a0e`へ書き込む。

```sh
arduino-cli compile --clean --profile esp32p4 master
arduino-cli compile --clean --profile esp32p4 slave
```

masterのCDCへ `FRAME <Hz> <length> [gap_ms]` を送る。`gap_ms` はheader受信とpayload armの間に
slaveへ与える時間であり、0/1/5/10 msを比較する。payloadは`0x11, 0x12, ...`で、slaveは
長さと8-bit checksumを報告する。

## 2026-09-22 実測

100 kHzで以下を実行し、header/payloadともmasterの結果は`ESP_OK (0x0)`だった。

- 4 byte、10 ms: `last_len=4`, checksum `0x4a`
- 128 byte、10 ms: `last_len=128`, checksum `0x40`
- 16 byte、5 ms / 1 ms / 0 ms: すべて `last_len=16`, checksum `0x88`
- 128 byte、0 ms、100連続フレーム: master 100/100成功。slaveのframe countは既存5件から105件へ増加し、
  crashなし。

したがってこのP4ペアでは、v1の「長さヘッダとpayloadを別transactionにする」方式は、追加の待ち時間を
プロトコルへ要求せずに128 byteまで成立する。ただしこれは治具配線・100 kHzでの実測値であり、OEP公開APIは
ターゲットや速度ごとに同じ連続フレーム試験を通した後に対応能力として宣言する。
