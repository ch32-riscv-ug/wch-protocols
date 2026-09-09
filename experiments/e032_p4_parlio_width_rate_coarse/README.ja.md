# E032 ESP32-P4 PARLIO width別rate粗探索

状態: **完了 — 1/2/4chは160 MHz成立、8chは80〜120 MHz、16chは40〜80 MHzに境界**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E031](../e031_p4_parlio_channel_width/README.ja.md)

## 問い

**PARLIO RXの1 / 2 / 4 / 8 / 16 channelについて、triggerなしbatch captureが成立するsample rate境界は20 / 40 / 80 / 120 / 160 MHzのどの区間にあるか。**

## 仮説

raw byte rateは`sample rate × channel / 8`なので、狭いwidthほど同じsample rateでPSRAM負荷が小さい。1 / 2 / 4 channelは160 MHzへ近づき、8 channelはE022で成立済みの80 MHz以上、16 channelは80 MHz以下で帯域境界が現れる。

## 反証条件

- channel幅とraw byte rateに対応しない順序で境界が現れる
- 20 MHzでも成立しないwidthがある
- 160 MHzまで全幅が余裕を持って成立し、この掃引で上限区間が得られない

## 方法

E031と同じhardware packing、GPIO 2〜9のPWM、16 channel時のlane複製を使う。各widthで20 / 40 / 80 / 120 / 160 MHzを順に設定し、各条件1,048,576 sampleをPSRAMへ退避する。API、実効sample/byte rate、queue深さ、overflow、全lane dataを記録する。

設定rateの95%以上へ追従し、queue最大15未満、overflow 0、data正常を成立条件とする。失敗後も残り条件を実行し、rateごとの失敗形を残す。

## 対象外

- 境界区間内の細かい二分探索
- trigger、圧縮、deep capture、外部pad
- 24 channel以上のCPU snapshot

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

width、設定rate、実効sample/byte rate、callback/dequeue、queue最大、overflow、data誤差、edge数、複製lane不一致。

## 完了条件

25条件をraw logへ残し、各widthについて最大成立点と最初の不成立点、または160 MHz以上という下限を得る。

## 影響

width別raw rateのfine sweep範囲と、後続trigger/圧縮試験の入力rateを決める。

## 結果

実施日: 2026-09-09

採用run: `_runs/E032_20260909T012950Z_default/test_parlio_width_rate_coarse/dut.log`

| channel | 最大成立設定 | 実効sample rate | 実効byte rate | 最初の不成立設定 | 不成立時の主因 |
|---:|---:|---:|---:|---:|---|
| 1 | 160 MHz | 155.091 MHz | 19.386 MB/s | 未到達 | — |
| 2 | 160 MHz | 157.184 MHz | 39.296 MB/s | 未到達 | — |
| 4 | 160 MHz | 158.156 MHz | 79.078 MB/s | 未到達 | — |
| 8 | 80 MHz | 79.588 MHz | 79.588 MB/s | 120 MHz | 実効97.496 MHz、queue 62で追従条件外 |
| 16 | 40 MHz | 39.883 MHz | 79.766 MB/s | 80 MHz | queue 64、overflow 422、実効42.689 MHz |

全widthの20 MHz baselineは成立した。1 / 2 / 4 channelは160 MHz設定までqueue最大1、overflow 0、data正常だった。160 MHzでの実効値は設定の96.9〜98.8%だったため成立条件内であるが、上限には到達していない。

8 channel / 120 MHzはAPIとdata検証自体は成功しoverflow 0だったが、callback 334に対してdequeue 273、queue最大62、実効97.496 MHzで、設定rateへ追従していない。160 MHzではoverflowした。16 channel / 80 MHz以上はすべてoverflowした。

失敗した最初のpytest runは、過負荷時も余分なcallback byteを64 KiB未満と要求した判定器の誤りで中断した。firmwareは25条件を完走しており、`_runs/E032_20260909T012843Z_default/`に残した。過負荷量を結果として受け入れるよう修正した採用runはpytestを通過した。

## 判定

**triggerなしspool経路はraw byte rate約80 MB/sまでは全widthで安定し、80〜100 MB/s付近でtask退避の実用境界が現れる。** channel数そのものではなく、packing後のbyte rateが第一の律速である。

1 / 2 / 4 channelは160 MHzでもraw byte rateが20 / 40 / 80 MB/sなので成立した。8 / 16 channelのfine sweepはそれぞれ80〜120 MHz、40〜80 MHzを対象にする。狭幅は160 MHzより上をdriverが生成できるか別に測る。

## 事実・候補・未決

**事実**: 25条件完走。1/2/4chは160 MHz成立、8chは80 MHz成立・120 MHz不成立、16chは40 MHz成立・80 MHz不成立。

**候補**: raw byte rate 80 MB/sを全width共通の安全tierとし、幅ごとのsample rateへ換算してcapabilityを返す。

**未決**: 1/2/4chのclock上限 / 8/16chのfine boundary / 長時間・deep capture時の最高rate / trigger追加時のwidth別境界。

## 追記 — E036による再解析(2026-09-09)

本レポートは書き換えない。[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)がsample単位の検証器とring未読byteによるdrop判定で同じ条件を測り直した結果、次の2点を訂正する。

1. **律速はsampling側ではない。** 「実効rateが設定へ追従しない」と書いた条件では、PARLIOは設定どおりsamplingしていた(120 MHz設定で119.560 MB/s = 設定の99.6%)。約98 MB/sの飽和はinternal ring → PSRAMのtask copy側の限界である。本レポートの「実効sample rate」はcapture開始からPSRAMへの退避完了までを分母にしており、samplingとspoolを1つの値へ潰していた。
2. **dutyとedge数による検証はsample単位の欠落を検出できない。** 信号源が定常・周期的な100 kHz PWMなので、ringがcopy前に上書きされても同じ波形が見え、検証を通過する。E036では112 / 120 MHzで連番違反が18 / 27件出たが、`result`は`ESP_OK`、`overflows`は0だった。

数値そのものは有効である。訂正は「その数値が何の限界か」の帰属と、data検証の有効範囲についてである。
