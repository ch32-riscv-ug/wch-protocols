# E024 ESP32-P4 SUMP基本trigger 32-bit検索 80 MHz

状態: **完了 — 32-bit検索も80 MHzに追従不能**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E023](../e023_p4_sump_basic_trigger_80mhz/README.ja.md)

## 問い

**80 MHz / 8-bitのinternal ring→PSRAM退避と同時に、32-bit word内の4 sampleを並列評価するSUMP基本trigger相当のpattern/mask・edge検索は、dropなしで成立するか。**

## 仮説

E023は1 sampleごとの分岐と状態更新により15.078〜24.555 MB/sに留まった。4 sampleを1 wordとしてmask/value比較とedge候補抽出を行い、候補があるwordだけsample位置を確定すれば、同じcopy taskでも処理量を減らせる。ただし80 MB/s入力に対する余裕は未確認であり、最適化だけでは不足する可能性がある。

## 反証条件

- queue overflow、data不良、またはcapture実効rate低下が起きる
- 既知PWM上のedgeまたはpatternを検出できない
- 存在しないpatternを誤検出する
- trigger走査が80 MB/sを下回る

## 方法

E023と同じ80 MHz / 8-bit / 1 MiB captureと3条件を使う。run 0は存在しない`mask=0xFF, value=0x55`、run 1はlane 0 rising edge、run 2は`mask=0x0F, value=0x00`とする。各descriptorを4-byte word単位で検索し、match候補を含むwordだけbyte単位で最初の位置を確定する。端数はbyte単位で評価し、descriptor境界をまたぐedge状態も保持する。match後も全sampleを走査して負荷を一定にする。

E023と同様、build・upload・monitorはpytest harness経由だけで行う。

## 対象外

- trigger処理の別core化
- pre/post trigger buffer制御
- 発生回数、pulse width、multi-stage trigger
- hardware trigger
- 80 MHz以外のrate境界

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

E023と同じcapture/queue/data/trigger指標、およびscan throughputを各runで記録する。

## 完了条件

- 3条件すべてのcaptureとtrigger指標をraw logへ残す
- 32-bit検索だけで80 MHzが成立するか判定する
- 不成立なら、専用core化とtrigger mode時のrate制限のどちらを次に測るべきか決められる

## 影響

software基本triggerを80 MHz capabilityに含められるか、trigger付きcaptureを別のrate tierとして扱うべきかを決めるgate。

## 結果

実施日: 2026-09-09

採用run: `_runs/E024_20260908T153928Z_default/test_sump_swar_trigger_80mhz/dut.log`

| mode | match | 最初のindex | scan | scan throughput | capture実効rate | queue overflow |
|---|---:|---:|---:|---:|---:|---:|
| no-match `FF/55` | なし | — | 40,126 us | 26.132 MB/s | 21.793 MB/s | 667 |
| lane 0 rising | あり | 474 | 40,951 us | 25.605 MB/s | 21.447 MB/s | 683 |
| pattern `0F/00` | あり | 0 | 33,905 us | 30.926 MB/s | 25.083 MB/s | 537 |

各modeで1,048,576 sampleを走査した。trigger条件は期待どおり、存在しないpatternはmatchせず、risingとpatternはmatchした。しかし全条件でqueueが深さ64まで飽和し、537〜683件overflowした。APIとPSRAM syncは成功したが、capture data検証はoverflowにより対象外となった。

最初の実装では4-byte loadとzero-byte判定が関数呼び出しになっていた。強制inline化前は19.890〜20.679 MB/sだったため採用せず、生成コードからhot loop内の呼び出しが除去された上記runを採用した。

## 判定

**32-bit SWAR検索だけでは80 MHzに追従できない。** E023の素朴な1-byte検索に対して改善する条件はあったが、最良でも30.926 MB/sで、80 MB/s入力の半分に届かなかった。

この結果から、同種の整数software検索を別coreへ移すだけでは、検索単体のthroughput不足は解消しない。次はtrigger付きcaptureの成立rateを測ってcapability境界を定めるか、ESP32-P4固有のSIMD/hardware支援を別方式として調べる必要がある。

## 事実・候補・未決

**事実**: 32-bit検索は25.605〜30.926 MB/sで、80 MHz captureでは537〜683件overflowした。

**候補**: trigger付きcaptureのrate tier、ESP32-P4固有SIMD、edgeだけのhardware支援。

**未決**: software triggerの成立rate上限 / pre/post ring / multi-stage / hardware-assisted external trigger。
