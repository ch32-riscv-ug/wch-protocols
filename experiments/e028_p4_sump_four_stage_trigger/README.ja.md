# E028 ESP32-P4 SUMP 4-stage trigger

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E027](../e027_p4_sump_circular_pretrigger/README.ja.md)

## 問い

**16 MHz / 8-bit captureで、pattern → edge → occurrence count → patternの4-stage software triggerを順序どおり評価し、最終stageから指定post sample数で停止できるか。**

## 仮説

基本pattern/edge、pre/post停止、circular履歴は個別に成立した。stageごとに次の検索条件と状態を切り替えれば、SUMP型の段階triggerも低いrate tierで構成できる。状態遷移は信号event時だけなので、16 MHzならbyte単位の基準実装でも追従できる。

## 反証条件

- 4 stageが順序どおり成立しない、または発生回数が誤る
- final triggerより前に停止する、停止overshootが1 chunk以上になる
- queue overflow、入力rateへの非追従、data不良が起きる

## 方法

16 MHz / 8-bitで、256 Ki sampleを過ぎてから次の4 stageを評価する。

1. lane 7 high (`mask=0x80, value=0x80`)
2. lane 7 falling edge
3. lane 0 rising edgeを4回
4. low nibble zero (`mask=0x0F, value=0x00`)

stage 4成立位置をfinal triggerとし、さらに256 Ki sampleを含むdescriptorで停止する。各stageの成立index、stage 3のcount、総scan、停止overshoot、queue/data指標を記録する。build・upload・monitorはpytest harness経由だけで行う。

## 対象外

- SUMPの全stage flag/serial triggerとのwire互換
- stageごとに任意のdelay/countを組み合わせる汎用command parser
- 4-stage triggerの最大sample rate
- PSRAM circular ringとの同時利用

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

各stage index、edge occurrence count、final trigger/stop/overshoot、scan throughput、queue/overflow、capture rate、duty誤差、edge数。

## 完了条件

- 4 stageが単調増加するindexで成立し、stage 3 countが4になる
- final trigger後256 Ki sampleを保持して停止する
- overflow 0、入力追従、data正常を確認する

## 影響

SUMPのmulti-stage trigger conceptを共通protocolの拡張能力として表現できる実装根拠になる。
