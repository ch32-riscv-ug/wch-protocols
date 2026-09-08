# E022 ESP32-P4 PARLIO PSRAM退避 80 MHz

状態: **完了 — 80 MHz / 1 MiBを3/3取得**

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

## 結果

実施日: 2026-09-09

採用run: `_runs/E022_20260908T152201Z_default/test_parlio_psram_spool_80mhz/dut.log`

| run | capture | 実効rate | callback / dequeue | stop時超過 | overflow | queue最大 | duty最大誤差 | edge範囲 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 13,174 us | 79.594 MB/s | 273 / 273 | 1,984 byte | 0 | 1 | 118 ppm | 2,620〜2,621 |
| 1 | 13,174 us | 79.594 MB/s | 273 / 273 | 1,984 byte | 0 | 1 | 121 ppm | 2,621〜2,622 |
| 2 | 13,171 us | 79.612 MB/s | 273 / 273 | 1,984 byte | 0 | 1 | 93 ppm | 2,621〜2,622 |

全runでconfig / enable / receive / start / stop / disable / PSRAM syncが`ESP_OK`。PSRAM syncは566〜572 usだった。最初の2 runはhost側に8 MHz用のcapture時間・edge数assertが残っていたため測定成功後にhost assertだけが失敗した。採用runではrate依存の窓へ修正し、同じdevice結果を3回取得した。

E021のfirmware本体は`EXPERIMENT_ID`と`SAMPLE_RATE_HZ`だけをcompile-time設定可能にし、E022は同じ実装をincludeした。E021のdefault値と動作は変えていない。

## 判定

仮説は成立した。**64 KiB internal DMA ring→task copy→PSRAM経路は、80 MHz / 8-bit / 1 MiBでもdropなしで安定した。** callbackは約50 usごとだがqueueは最大1に留まり、80 MB/s入力とPSRAM copyを同時に処理できた。

これは内部GPIO matrixで100 kHz PWMを観測した結果であり、外部padで80 MHz信号を正しく取り込めることまでは示さない。またtrigger検索やUSB処理に使えるCPU余裕もまだ測っていない。

## 事実・候補・未決

**事実**: 80 MHz / 8-bit / 1 MiBを3/3回、overflow 0、実効79.594〜79.612 MB/s、全lane正常でPSRAMへ退避した。

**候補**: 80 MHzをESP32-P4版の上位sample rate候補とし、このrateでSUMP基本trigger検索の負荷を測る。

**未決**: pattern / mask・edge trigger検索を同時実行したときのdrop / pre/post trigger / 120〜160 MHz / 外部pad signal integrity / 32 MiB級capture / host download。
