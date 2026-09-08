# E018 ESP32-P4 PARLIO PSRAM cache log抑制

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E017](../e017_p4_parlio_psram_cache_sync/README.ja.md)

## 問い

**Arduino-ESP32 3.3.11のstock PARLIO driverで、`cache` tagのruntime logを抑制すれば、1 MiBのPSRAM direct captureをlog floodなしで完了し、payload全体sync後に正しいdataを読めるか。**

## 仮説

`esp_log_level_set("cache", ESP_LOG_NONE)`でdescriptor callback内のalignment error出力は抑制できる。内部の不整列`esp_cache_msync()`自体は失敗するが、有限長受信完了後に128-byte整列済みpayload全体をM2C syncすれば、1 MiBでも正しいdataを読める。

E017では8,192 byteでtransactionごとに2件のcache errorが出た一方、完了後の全体syncとdataは成功した。runtime log抑制が有効なら、descriptor数に比例するserial出力を性能経路から除外して大容量試験へ進める。

## 反証条件

- runtime log levelを変更しても`cache: esp_cache_msync`が出力される
- 1 MiB captureのAPI、完了後の全体sync、またはdata検証が失敗する
- 8 MHzで期待時間から大きく外れ、timeoutまたは再現性のない停止が起きる

## 方法

1. E017と同じGPIO 2〜9、LEDC 100 kHz、PARLIO RX 8 MHz、8-bit、PSRAM direct payloadを使う
2. log抑制前に8,192 byteを1回captureし、E017と同じcache errorが観測できることをcontrolにする
3. `cache` tagのruntime log levelを`ESP_LOG_NONE`へ変更する
4. 128-byte整列した1 MiBのPSRAM payloadへ3回captureする
5. 各capture前にC2M sync、完了後にpayload全体M2C syncを行う
6. capture wait時間、全体sync時間、全laneのduty誤差とedge数、hostが観測したcache error数を記録する
7. build・upload・monitorはpytest harness経由だけで行う

## 対象外

- 1 MiBを超える容量上限
- sample rate上限
- driver sourceの修正
- partial / continuous receive
- USBやnetworkへのdownload速度

## 必要な環境

- E016で32 MiB PSRAMを確認したESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

| 項目 | 単位・回数 |
|---|---|
| controlのcache error | 件、1回 |
| 抑制後のcache error | 件/run、1 MiB × 3回 |
| receive / start / wait / stop / sync | result、3回 |
| capture wait時間 | us、3回 |
| payload全体sync時間 | us、3回 |
| maximum duty error | ppm/run |
| edge range | edge/run |

## 完了条件

- controlで既知のcache errorが再現する
- log抑制後の1 MiB × 3回について、cache error数、API結果、時間、data検証結果がraw logにある
- stock driverのまま容量・rate試験へ進めるか、driver修正を先にするかを判定できる

## 影響

実用的なbatch logic captureをstock Arduino環境で評価し続けられるかのgate。成功しても容量・sample rate・host downloadは別実験で測る。
