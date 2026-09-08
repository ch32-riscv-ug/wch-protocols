# E023 ESP32-P4 SUMP基本trigger検索 80 MHz

状態: **計画**

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
