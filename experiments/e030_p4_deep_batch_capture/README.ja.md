# E030 ESP32-P4 deep batch capture

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E021](../e021_p4_parlio_psram_spool/README.ja.md)、[E029](../e029_p4_circular_ring_soak/README.ja.md)

## 問い

**20 MHz / 8-bit PARLIO RXを64 KiB internal DMA ringから16 MiB PSRAMへ連続退避し、dropやdata化けなしで保持できるか。**

## 仮説

E020ではinternal RAM→PSRAMのflush込みwriteが最低138.590 MB/s、E025とE029ではtrigger処理を含む20 MHz経路が安定した。したがって、32 MiB PSRAMの半分をcapture bufferに使う16 MiB連続取得も、約0.84秒にわたり20 MB/sへ追従できる。

## 反証条件

- 16 MiBのPSRAM bufferを確保できない
- API error、timeout、queue overflow、入力rateへの非追従が起きる
- 16 MiB全域のPWM dutyまたはedge検証が外れる
- capture完了後のcache syncを含めて異常終了する

## 方法

E021のinternal DMA ring→PSRAM spool経路を再利用し、sample rateを20 MHz、保存量を16 MiB、実行回数を1回にする。GPIO 2〜9で生成する8 laneの100 kHz PWMをGPIO matrix経由でPARLIO RXへ入力する。pytest harnessからbuild・upload・reset・monitorを行い、16 MiB全域について各laneのdutyとedge数を検証する。

## 対象外

- circular pre-trigger、multi-stage trigger
- USBまたはnetworkへのdownload
- captureとhost転送の同時実行
- 外部pad入力とsignal integrity
- 16 MiBを超える容量の掃引

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

PSRAM容量、allocation成否、callback/dequeue数、copy量、queue最大深さ、overflow数、capture時間、実効MB/s、cache sync時間、全laneの最大duty誤差とedge数。

## 完了条件

- 16 MiBを1回取得し、overflow 0、入力追従、全域data正常のいずれかをraw logへ残す
- 失敗した場合も、allocation、帯域、queue、data、cache syncのどこが境界かを特定する

## 影響

ESP32-P4実装例で提供できるbatch captureの実用的な深さと、host download試験で使う基準データ量を判断する。
