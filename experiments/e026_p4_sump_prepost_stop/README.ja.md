# E026 ESP32-P4 SUMP pre/post trigger停止

状態: **完了 — 3比率すべてsub-chunk誤差で停止**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E025](../e025_p4_sump_trigger_rate_boundary/README.ja.md)

## 問い

**20 MHz / 8-bit captureでtrigger後の指定sample数まで取得して停止し、512 Ki sampleの25/75、50/50、75/25 pre/post windowを構成できるか。停止位置の誤差はどれだけか。**

## 仮説

PARLIO partial receiveはdescriptor chunk単位でtaskへ届くため、trigger位置から要求post sample数を超えた最初のchunkで停止できる。論理windowは要求位置で切り出せるが、物理停止は最大1 chunk未満overshootする。

## 反証条件

- triggerを検出できない、または要求post位置より前に停止する
- 停止overshootが1 callback chunk以上になる
- queue overflowまたはcapture data不良が起きる
- 要求した512 Ki sampleのpre/post windowを取得済み範囲から切り出せない

## 方法

E025で全基本条件が成立した20 MHzを使う。総window長は512 Ki sampleとし、run 0はpre/post 25/75、run 1は50/50、run 2は75/25とする。各runは要求pre長以上を取得してから最初のlane 0 rising edgeをtriggerとし、要求post sample数を含むchunkをcopyした時点でPARLIOを停止する。

trigger index、要求/実取得post sample、overshoot、総取得sample、queue/data指標を記録する。論理window開始位置が0以上、終了位置が総取得sample以下であることをpytestで確認する。build・upload・monitorはpytest harness経由だけで行う。

## 対象外

- trigger待機が1 MiBを超えるPSRAM circular ring
- descriptor途中でのDMA停止
- pattern条件、発生回数、multi-stage trigger
- hostへのwindow転送

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

pre/post要求、trigger index、総取得sample、物理post sample、overshoot、chunk範囲、queue/overflow、capture rate、duty誤差、edge数。

## 完了条件

- 3比率すべてでtrigger後停止と論理window範囲を確認する
- 停止overshootの上限を実測する
- circular ringへ進む前提となる停止制御の可否を判定する

## 影響

SUMPのcapture delayに相当するpre/post配置を、PARLIOのchunk受信上で実装できるかを決める。

## 結果

実施日: 2026-09-09

採用run: `_runs/E026_20260908T155117Z_default/test_sump_prepost_stop/dut.log`

| pre/post | trigger index | 総取得sample | 要求stop | overshoot |
|---:|---:|---:|---:|---:|
| 25/75 (131,072 / 393,216) | 131,119 | 527,296 | 524,335 | 2,961 |
| 50/50 (262,144 / 262,144) | 262,318 | 527,296 | 524,462 | 2,834 |
| 75/25 (393,216 / 131,072) | 393,390 | 527,296 | 524,462 | 2,834 |

3 runともcallback/dequeue 137、queue最大1、overflow 0、実効19.862〜19.865 MB/sだった。duty誤差は2,510〜2,579 ppm、edge数は5,272〜5,273で、取得範囲のdataは正常だった。rising triggerは要求pre長から47 / 174 / 174 sample後に検出された。

要求stopを含むdescriptor全体をcopyしてから停止したため、物理取得長には2,834〜2,961 sampleのovershootが生じた。これは観測した最大chunk 4,032 sample未満で、計画した上限内である。論理windowは各runとも`trigger_index - pre`から`trigger_index + post`までの厳密な524,288 sampleとして取得済み範囲内に収まった。

## 判定

**SUMP capture delay相当のpre/post配置とtrigger後停止は、20 MHzで実装可能。** DMAをsample位置ぴったりで止める必要はなく、最大1 chunk未満を余分に取得してhostへ返す論理windowをtrimすれば、要求sample数と比率を正確に提供できる。

今回は1 MiB内でtriggerが来る有限captureであり、長時間trigger待機に必要なPSRAM circular ringは未検証である。次はring wrap後もpre historyを保ち、同じpost停止と論理window再構成ができるかを分けて確認する。

## 事実・候補・未決

**事実**: 25/75・50/50・75/25の全比率でoverflow 0、data正常、停止overshoot 2,834〜2,961 sample。

**候補**: descriptor単位で停止し、論理windowをsample単位でtrimする方式。

**未決**: PSRAM circular ringのwrap / multi-stage trigger / host転送時のwindow表現。
