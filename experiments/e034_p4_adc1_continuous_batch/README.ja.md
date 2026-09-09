# E034 ESP32-P4 ADC1 continuous batch基礎

状態: **完了 — ADC1 1/2/4/8 channel、83,333 conversion/s成立**

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

## 結果

実施日: 2026-09-09

採用run: `_runs/E034_20260909T020135Z_default/test_adc1_continuous_batch/dut.log`

| channel数 | 最大設定aggregate rate | 実効aggregate rate | 1 channelあたり | sample数/channel | overflow |
|---:|---:|---:|---:|---:|---:|
| 1 | 83,333 conversion/s | 83,251 conversion/s | 83,251 Sa/s | 262,144 | 0 |
| 2 | 83,333 conversion/s | 83,251 conversion/s | 約41,626 Sa/s | 131,072 | 0 |
| 4 | 83,333 conversion/s | 83,251 conversion/s | 約20,813 Sa/s | 65,536 | 0 |
| 8 | 83,333 conversion/s | 83,251 conversion/s | 約10,406 Sa/s | 32,768 | 0 |

全12条件で1 MiB rawをPSRAMへ退避し、readは各1,024回、timeout / pool overflow / unit ID不正 / channel ID不正はいずれも0だった。10,000、40,000、83,333 conversion/sの実効aggregate rateはchannel数によらずそれぞれ9,990、40,282、83,251 conversion/sだった。全条件でchannel別sample数は完全に均等であり、resultのchannel IDを使えばtraceへ分離できる。

ただし40,000 conversion/sまでは設定順の厳密な循環列だったのに対し、83,333 conversion/sでは2 / 4 / 8 channelの単純な循環順序からそれぞれ131,072 / 196,608 / 229,376 resultが外れた。個数は均等なのでdata欠落とは判定しないが、配列位置だけからchannelやchannel間skewを推定してはならない。高速時の実際の並び方は後続実験で短いchannel ID列を直接記録する。

GPIOは無配線で、raw値はboard接続状態やfloating入力の影響を受ける。記録したmin/maxは信号品質・入力範囲・精度の根拠にはしない。

開始同期の初期版は、upload直後にUSB RXへ残った文字を開始命令と誤認して長い測定へ入った。起動1秒後にRXをdrainして`READY E034`を出し、その後のhost triggerだけを受け付けるhandshakeへ修正した採用runはpytestを完走した。

## 判定

**ADC1 continuous DMAからPSRAMへ保存するanalog batch基礎経路は、1〜8 channelと最大83,333 conversion/sで成立する。** 8 channel時は各約10.4 kSa/sであり、高速logic captureとは別の補助analog traceとして実用候補になる。

## 事実・候補・未決

**事実**: ADC1 1/2/4/8 channel、最大aggregate 83,333 conversion/s、各1 MiBで欠落・overflowなし。channel IDによる分離は成立する。

**候補**: analog sampleは4-byte hardware resultをそのまま保持せず、channel metadataと12-bit値を分離して保存容量を削減する。

**未決**: 最大rate時のchannel ID配列規則 / ADC2と両unit / 長時間取得 / 実信号の精度・noise / digital traceとの同期。
