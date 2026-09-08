# E018 ESP32-P4 PARLIO PSRAM cache log抑制

状態: **完了 — 1 MiB soft delimiterはAPI上限で拒否**

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

## 結果

実施日: 2026-09-08

採用run: `_runs/E018_20260908T144655Z_default/test_psram_cache_log_suppression/dut.log`

1 MiB payloadの確保とPARLIO RX unitの作成は成功した。しかし、`eof_data_len = 1,048,576`のsoft delimiter作成は次のerrorで`ESP_ERR_INVALID_ARG`となった。

```text
parlio_new_rx_soft_delimiter: EOF data length is 0 or exceed the max value 65535
```

同じunitに対する8,192 byteのcontrol delimiterは`ESP_OK`だった。pytestはこの拒否を期待結果として確認し、1件成功した。最初のrun (`E018_20260908T144344Z_default`) は1 MiB delimiterを受理すると仮定したassertで失敗し、採用runでは実測した上限拒否をassertした。

公開されているESP-IDF 5.5の[PARLIO RX source](https://github.com/espressif/esp-idf/blob/release/v5.5/components/esp_driver_parlio/src/parlio_rx.c)も、soft delimiterの`eof_data_len`を`PARLIO_LL_RX_MAX_BYTES_PER_FRAME`以下に制限している。ESP32-P4 headerでこの値は`0xFFFF`である。

## 判定

仮説の前提を反証した。stock driverのsoft delimiterを使う**単一の有限長transaction**では1 MiB captureを表現できないため、この経路でのcache log抑制と1 MiB data検証は実行できなかった。

これはPSRAM容量やGDMAの上限ではなく、frame終端長のAPI上限である。深い連続captureには、`partial_rx_en`によるring transactionと停止処理、level/pulse delimiterによる外部終端、または複数の有限長transactionのいずれかが必要になる。隙間のないlogic captureという目的から、次は`partial_rx_en`を優先して切り分ける。

## 事実・候補・未決

**事実**: 1 MiB PSRAM payloadは確保でき、PARLIO unitも`max_recv_size = 1 MiB`で作成できた。soft delimiterのEOF長だけが65,535 byte上限で拒否された。

**候補**: 1 MiB PSRAMをdirect DMA ringとしてmountし、partial callbackで一周を検出して停止する。

**未決**: partial direct mountがPSRAMで動くか / 公開APIだけで正確に停止できるか / cache log抑制が効くか / 一周後のpayload全体syncでdataが正しいか。
