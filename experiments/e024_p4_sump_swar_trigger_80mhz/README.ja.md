# E024 ESP32-P4 SUMP基本trigger 32-bit検索 80 MHz

状態: **計画**

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
