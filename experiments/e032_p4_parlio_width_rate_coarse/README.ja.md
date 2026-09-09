# E032 ESP32-P4 PARLIO width別rate粗探索

状態: **計画**

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
