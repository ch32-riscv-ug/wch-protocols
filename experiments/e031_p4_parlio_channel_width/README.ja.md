# E031 ESP32-P4 PARLIO channel width

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)

## 問い

**ESP32-P4 PARLIO RXの1 / 2 / 4 / 8 / 16 data lineで、hardware packingをsample列へ復元し、各幅1,048,576 sampleを8 MHzでdropやdata化けなくPSRAMへbatch取得できるか。**

## 仮説

driverはdata widthを1 / 2 / 4 / 8 / 16から選べる。8未満では複数sampleが1 byteへpackされ、16では1 sampleが2 byteになる。8 MHzなら最大の16 channelでも16 MB/sで、E030の20 MB/sより低いため、spool帯域には収まる。

## 反証条件

- いずれかのwidthをdriverが拒否する
- packed byte数またはsample順を一意に復元できない
- API error、timeout、queue overflow、入力rateへの非追従が起きる
- 復元したlaneのduty、edge数、lane間の対応が期待から外れる

## 方法

GPIO 2〜9の8種類の100 kHz PWMを信号源とする。1 / 2 / 4 / 8 channelでは先頭から必要本数を使い、16 channelでは同じ8 GPIOをPARLIOのlane 8〜15にも入力して、lane 0〜7の複製として検証する。これはGPIO matrix上の内部観測であり、16本の外部pad品質は評価しない。

各widthで1,048,576 sampleを取得する。保存byte数はwidth 1 / 2 / 4 / 8 / 16について128 KiB / 256 KiB / 512 KiB / 1 MiB / 2 MiBとなる。soft delimiterのbit pack orderをLSBに固定し、host側でなくfirmware内で各sampleを復元して、全laneのduty、edge数、複製lane一致を検証する。

build・upload・reset・monitorはpytest harness経由だけで行う。

## 対象外

- widthごとの最高sample rate
- trigger、circular ring、圧縮
- 16本の独立した外部pad入力
- 24 channel以上のCPU snapshot方式

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

width、保存byte数、復元sample数、callback/dequeue、queue最大、overflow、capture時間、sample rate、byte rate、最大duty誤差、edge数、複製lane不一致数。

## 完了条件

5種類すべてについてAPI結果とpackingをraw logへ残し、成立したwidthと失敗したwidthを確定する。失敗してもそのwidthのAPI、packing、帯域、dataのどこで外れたかを特定する。

## 影響

高速parallel tierの最大channel数とsample表現を確定し、次のwidth別rate掃引の条件を決める。
