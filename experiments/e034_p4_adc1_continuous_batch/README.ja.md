# E034 ESP32-P4 ADC1 continuous batch基礎

状態: **計画**

規則: [実測の規則](../README.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)

## 問い

**Arduino環境からESP-IDF ADC continuous driverを直接使い、ADC1の1 / 2 / 4 / 8 channelを最大aggregate rateまで連続取得してPSRAMへ欠落なく退避できるか。**

## 仮説

最大83,333 conversion/sは4 byte/conversionで約333 kB/sにすぎないため、DMA poolからPSRAMへのtask退避は全channel数で成立する。pattern中のchannel IDからround-robin順を復元できる。

## 反証条件

- 1 / 2 / 4 / 8 channelのいずれかをcontinuous driverが受け付けない
- 最大rateでpool overflow、read error、またはconversion欠落が起きる
- result内のunit/channel IDが設定patternと一致せず、各traceへ分離できない
- aggregate rateが設定値の90%へ届かない

## 方法

ADC1のGPIO 16〜23を入力として、1 / 2 / 4 / 8 channelを先頭から選ぶ。10,000 / 40,000 / 83,333 conversion/sで各262,144 conversion（1 MiB raw）をcontinuous DMA poolから読み、32 MiB PSRAMへ順次コピーする。

各resultのunit/channel ID、pattern順、channel別sample数、raw値の最小/最大、実効aggregate rate、read回数、timeout、pool overflowを記録する。入力値そのものはfloatingまたはboard実装依存なので、精度判定には使わない。

## 対象外

- ADC2およびADC1+ADC2の同時利用
- 電圧精度、noise、ENOB、attenuation別入力範囲
- digital captureとの同期
- 外部信号源を使う波形再現性

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・信号源は不要

ベンチ種別: **一時・配線なし**

## 完了条件

12条件をraw logへ残し、ADC1 continuous batchのchannel分離、最大aggregate rate、PSRAM退避の成立可否を確定する。

## 影響

成立すればanalog補助traceの基礎経路とする。結果を受け、ADC2単独・両unit併用、長時間取得、digital同期を別実験として設計する。
