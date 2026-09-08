# E022 ESP32-P4 PARLIO PSRAM退避 80 MHz

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E021](../e021_p4_parlio_psram_spool/README.ja.md)

## 問い

**E021の64 KiB internal DMA ring→task copy→1 MiB PSRAM経路は、80 MHz / 8-bitでもdropなしで3回連続captureできるか。**

## 仮説

E020の16 KiB copy単体はflush込み約182.7 MB/sで80 MB/sの2.28倍あるため、64 KiB ringの約819 usの再利用猶予内にdescriptorを退避できる。ただし4,032 byteごとに約50 usでISRとqueue処理が必要なため、CPU copy帯域だけでは成立を保証できない。

## 反証条件

- queue overflow、timeout、WDT、API失敗が起きる
- callback byteとPSRAM copied byteは揃ってもPWM duty / edge数が期待範囲を外れる
- 実効取得rateが要求80 MB/sから大きく外れる

## 方法

E021と同じfirmware経路、buffer、PWM、検証、receiver再生成を使い、PARLIO RXの要求sample rateだけ80 MHzに変更する。1 MiBを3回取得し、API、queue、descriptor、取得時間、実効rate、duty、edge数を記録する。build・upload・monitorはpytest harness経由だけで行う。

## 対象外

- 80 MHz未満・超の境界掃引
- trigger検索
- 1 MiBを超えるcapture
- USBやnetworkとの同時転送

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

E021と同じ。実効rateの合格窓は75〜85 MB/s、PWM duty最大誤差は30,000 ppm以内とする。

## 完了条件

- 1 MiB × 3回の全指標をraw logへ残す
- 80 MHzでSUMP基本trigger試験へ進めるか、低いrateへ境界を下げるか決められる

## 影響

ESP32-P4版logic analyzerの実用sample rate候補を決めるgate。内部生成PWMでの成立であり、外部padのsignal integrity上限ではない。
