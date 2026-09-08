# E019 ESP32-P4 PARLIO PSRAM partial ring

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E018](../e018_p4_parlio_psram_log_suppression/README.ja.md)

## 問い

**stock PARLIO driverの`partial_rx_en`で1 MiB PSRAMをdirect DMA ringとして使い、descriptor callbackで一周を検出して公開APIだけで停止した後、正しい8-bit連続captureを得られるか。**

## 仮説

`partial_rx_en=true`、`indirect_mount=false`ではuser payloadがDMA descriptorへ直接mountされ、末尾から先頭へring接続される。partial callbackが通知した直後にtask側からsoft delimiterを停止してRX unitをdisableすれば、一周分を大きく超えて先頭を上書きする前に止められる。

E018でsoft delimiterのEOF長は65,535 byte以下に制限された。しかしpartial transactionではEOFは区間通知であり、DMA ring全体の大きさは`max_recv_size`とpayload sizeで1 MiBにできると予想する。

## 反証条件

- PSRAM payloadを使うpartial direct transactionがAPIに拒否される
- soft delimiterの最初のEOF後にcaptureが継続しない
- 一周通知後、公開APIで停止できずtimeout・crash・大幅な上書きが起きる
- payload全体M2C sync後のPWM dutyまたはedge数が期待範囲を外れる

## 方法

1. GPIO 2〜9で8 laneの100 kHz PWMを生成し、PARLIO RXを8 MHz、8-bitにする
2. 128-byte整列した1 MiB PSRAM payloadを確保する
3. soft delimiterのEOF長を128-byte整列した65,408 byteにする
4. `partial_rx_en=true`、`indirect_mount=false`でpayloadをdirect ring mountする
5. `on_partial_receive`でdescriptor完了byteを加算し、1 MiB以上でcapture taskへISR-safe通知する
6. taskは通知を受けてsoft delimiterを停止し、RX unitをdisableする。private APIは使わない
7. capture前にC2M sync、停止後にpayload全体M2C syncを行い、全laneのdutyとedge数を検証する
8. `cache` tagはruntimeで`ESP_LOG_NONE`にし、host側でもcache errorが出ないことを確認する
9. receiverを作り直して3回実行する
10. build・upload・monitorはpytest harness経由だけで行う

## 対象外

- 1 MiBを超える容量
- sample rate上限
- 停止遅延をゼロにするdriver修正またはprivate API
- trigger前後比率
- USBやnetworkへのdownload

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

| 項目 | 単位・回数 |
|---|---|
| API結果 | receive / start / notify / stop / disable / sync、3回 |
| descriptor callback | 件/run |
| callback累計byteと一周超過 | byte/run |
| capture時間 | us/run |
| payload全体sync時間 | us/run |
| cache error | 件/run |
| maximum duty error | ppm/run |
| edge range | edge/run |

## 完了条件

- 1 MiB partial direct ringを3回開始・停止し、callback累計、停止時超過、API結果、時間、data検証をraw logへ残す
- 失敗した場合も、API拒否・EOF後停止・停止race・coherencyのどこで失敗したかを特定する
- 容量・sample rate掃引へ進めるか、停止方法またはdriverを先に変えるか判定できる

## 影響

深いbatch logic captureの基本経路を決めるgate。成功しても実用性判定には容量、sample rate、trigger、host downloadを別に測る。
