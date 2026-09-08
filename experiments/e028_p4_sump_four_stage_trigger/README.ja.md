# E028 ESP32-P4 SUMP 4-stage trigger

状態: **完了 — 4-stage triggerを3/3回確認**

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

## 結果

実施日: 2026-09-09

採用run: `_runs/E028_20260908T204111Z_default/test_sump_four_stage_trigger/dut.log`

| run | stage 0 | stage 1 | stage 2 | stage 3/final | count | overshoot |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 262,144 | 262,250 | 262,740 | 262,810 | 4 | 2,342 |
| 1 | 262,144 | 262,156 | 262,646 | 262,716 | 4 | 2,436 |
| 2 | 262,144 | 262,294 | 262,784 | 262,854 | 4 | 2,298 |

3 runとも4 stageが単調増加するindexで成立し、lane 0 risingの発生回数は正確に4だった。final trigger後256 Ki sampleを含むdescriptorで停止し、総取得は527,296 sample、停止overshootは2,298〜2,436 sampleで最大chunk 4,032未満だった。

callback/dequeueは137/137、queue最大0、overflow 0、実効15.971〜15.973 MB/s。dataのduty誤差は最大61 ppm、edge数は6,591〜6,592だった。stage検索を含む計測上のscan throughputは76.176〜76.375 MB/sだが、final成立後は条件評価を省略しているため、これは未成立状態を永続検索する上限ではない。

## 判定

**pattern、edge、occurrence count、stage遷移を組み合わせた4-stage triggerからpost取得・停止まで16 MHzで実装可能。** 固定条件の実装例でありSUMPの全flagやserial trigger互換を証明するものではないが、共通protocolがmulti-stage triggerを将来拡張として表現することを妨げる実装上の制約は見つからなかった。

汎用化するとstage定義の解釈や条件ごとの持続検索負荷が増える。実装例ではbasic triggerの24 MHz tierと同一性能を仮定せず、multi-stageを別capabilityまたは別rate tierとしてadvertiseするのが安全である。

## 事実・候補・未決

**事実**: 固定4-stage条件は16 MHzで3/3回、queue 0、overflow 0、data正常、count 4で成立。

**候補**: multi-stage triggerを独立capabilityとし、対応stage数・条件種・最大rateを個別に示す。

**未決**: 汎用stage command表現 / 全条件の持続検索rate / SUMP wire互換範囲 / circular ringとの統合。
