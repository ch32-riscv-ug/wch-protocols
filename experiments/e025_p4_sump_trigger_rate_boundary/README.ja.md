# E025 ESP32-P4 SUMP基本trigger rate境界

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E024](../e024_p4_sump_swar_trigger_80mhz/README.ja.md)

## 問い

**32-bit software検索によるSUMP基本trigger相当のpattern/mask・edgeを使いながら、8-bit captureをdropなしで継続できるsample rate境界はどこか。**

## 仮説

E024の検索単体throughputは25.605〜30.926 MB/sだったため、16 MHzと20 MHzは全条件で成立し、28 MHz以上では少なくとも重い条件がoverflowする。24 MHz付近が共通capabilityの境界候補になる。

## 反証条件

- 16 MHzでもいずれかの条件がoverflowまたはdata不良になる
- 32 MHzでも全条件が余裕をもって成立し、掃引範囲内に境界がない
- trigger条件が期待と異なるmatch結果になる

## 方法

E024の32-bit検索を使い、16 / 20 / 24 / 28 / 32 MHzの各rateで1 MiB captureを行う。各rateにつき、存在しない`mask=0xFF, value=0x55`、lane 0 rising edge、`mask=0x0F, value=0x00`の3条件を評価する。match後も全sampleを検索する。rateごとのcapture/queue/data/trigger指標を記録し、全条件がoverflow 0かつdata正常となる最高rateを求める。

build・upload・monitorはpytest harness経由だけで行う。

## 対象外

- 4 MHz未満の刻みによる厳密な最大値探索
- trigger処理の別core化またはESP32-P4固有SIMD化
- pre/post trigger、発生回数、multi-stage trigger
- 外部pad入力

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

sample rate、trigger mode、match/index、scan throughput、callback/dequeue/overflow/queue深さ、capture実効rate、dataのduty誤差とedge数。

## 完了条件

- 5 rate × 3条件をraw logへ残す
- 全基本条件が成立する最高rateと、最初に不成立となるrateを確定する
- pre/post trigger実験に使う安全なsample rateを選べる

## 影響

ESP32-P4実装例がadvertiseする「software basic trigger付き8-bit capture」のrate tierを定める。
