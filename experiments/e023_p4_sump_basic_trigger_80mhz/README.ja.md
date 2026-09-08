# E023 ESP32-P4 SUMP基本trigger検索 80 MHz

状態: **完了 — 素朴な1-byte検索は80 MHzに追従不能**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E022](../e022_p4_parlio_spool_80mhz/README.ja.md)

## 問い

**80 MHz / 8-bitのinternal ring→PSRAM退避と同時に、SUMP基本trigger相当のlevel/pattern+maskおよびedge条件を全sampleでsoftware検索してもdropしないか。**

## 仮説

単純なmask/value比較はcopy task内で実行できるが、400 MHz CPUに対して80 MS/sでは1 sampleあたり約5 cycleしかなく、edge状態保持とqueue/copyまで含めると限界に近い。64 KiB ringの猶予で短期的な処理揺らぎは吸収できる。

## 反証条件

- queue overflow、data不良、実効rate低下が起きる
- 既知PWM上のedgeまたはpatternを検出できない
- 存在しないpatternを誤検出する

## 方法

E022と同じ80 MHz / 8-bit / 1 MiB captureを使い、各descriptorをPSRAMへcopyする前に全sampleを走査する。run 0は存在しない`mask=0xFF, value=0x55`、run 1はlane 0 rising edge、run 2は`mask=0x0F, value=0x00`を評価する。最初のmatch index、走査sample数、走査時間、capture/queue/data指標を記録する。match後も全sampleを走査し、負荷を一定にする。build・upload・monitorはpytest harness経由だけで行う。

## 対象外

- pre/post trigger buffer制御
- falling edge、発生回数、pulse width
- multi-stage trigger
- hardware trigger
- 80 MHz以外の境界

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

E022の全指標に加え、trigger mode、match有無、最初のmatch index、scan sample、scan時間、scan throughputを各runで記録する。

## 完了条件

- 3条件すべてのcaptureとtrigger指標をraw logへ残す
- 80 MHzでsoftware基本triggerを採れるか、低rate・専用core・hardware支援へ分岐するか決められる

## 影響

SUMP互換操作modelのうち基本triggerをESP32-P4で実装できる範囲を決める最初のgate。

## 結果

実施日: 2026-09-09

採用run: `_runs/E023_20260908T152754Z_default/test_sump_basic_trigger_80mhz/dut.log`

| mode | match | 最初のindex | scan | scan throughput | capture実効rate | queue overflow |
|---|---:|---:|---:|---:|---:|---:|
| no-match `FF/55` | なし | — | 42,703 us | 24.555 MB/s | 20.629 MB/s | 722 |
| lane 0 rising | あり | 695 | 60,525 us | 17.324 MB/s | 15.246 MB/s | 1,096 |
| pattern `0F/00` | あり | 12 | 69,543 us | 15.078 MB/s | 13.469 MB/s | 1,286 |

各modeで1,048,576 sampleを走査した。trigger条件自体は期待どおり、存在しないpatternはmatchせず、risingとpatternはmatchした。しかしqueueは全条件で深さ64まで飽和した。callback byteは4.06〜6.23 MiBまで進む間にtaskが1 MiBしか処理できず、resultはoverflowにより`ESP_FAIL`となった。API自体とPSRAM syncは成功したが、data検証は実行対象外になった。

最初のrun (`E023_20260908T152707Z_default`) もno-matchで727 overflow、scan 24.611 MB/sとなり、同じ傾向を再現した。

## 判定

仮説は「限界に近い」より厳しく、**同じcopy taskで1 byteずつ条件評価する実装は80 MHzへ追従できない**と判定する。最も軽いno-matchでもscan単体24.555 MB/s、copyを含む処理20.629 MB/sで、80 MB/s入力の約1/4だった。

これはSUMP型trigger自体が不可能という結果ではない。mask/valueは32-bit word内の4 sampleを並列比較でき、edgeもbit演算で4 sampleをまとめられる。専用coreや低いsample rateも選択肢になる。まず同じ80 MHz条件でword単位検索を試し、algorithmだけで追従可能かを判定する。

## 事実・候補・未決

**事実**: 素朴なpattern/mask・edge全sample検索は15.078〜24.555 MB/sで、80 MHz captureでは722〜1,286件overflowした。

**候補**: 32-bit SWAR検索、trigger専用core、trigger有効時のsample rate制限。

**未決**: word単位検索の80 MHz成立 / pre/post ring / multi-stage / hardware-assisted external trigger / triggerなし80 MHzとのmode別capability表現。
