# E031 ESP32-P4 PARLIO channel width

状態: **完了 — 1 / 2 / 4 / 8 / 16 channelすべて成立**

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

## 結果

実施日: 2026-09-09

採用run: `_runs/E031_20260909T012045Z_default/test_parlio_channel_width/dut.log`

| channel | 保存byte | capture時間 | 実効sample rate | 実効byte rate | queue最大 | overflow | duty誤差最大 | edge/lane |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 131,072 | 134,885 us | 7.773 MHz | 0.971 MB/s | 0 | 0 | 1 ppm | 26,214 |
| 2 | 262,144 | 132,862 us | 7.892 MHz | 1.973 MB/s | 0 | 0 | 3 ppm | 26,214 |
| 4 | 524,288 | 131,858 us | 7.952 MHz | 3.976 MB/s | 0 | 0 | 10 ppm | 26,214〜26,215 |
| 8 | 1,048,576 | 131,357 us | 7.982 MHz | 7.982 MB/s | 0 | 0 | 10 ppm | 26,214〜26,215 |
| 16 | 2,097,152 | 131,344 us | 7.983 MHz | 15.966 MB/s | 0 | 0 | 10 ppm | 26,214〜26,215 |

各条件は1,048,576 sampleである。1 / 2 / 4 channelでは1 byte内へLSB側から8 / 4 / 2 sampleがpackされ、8 channelは1 byte/sample、16 channelはlittle-endian 2 byte/sampleとして復元できた。16 channelでGPIO 2〜9をlane 8〜15へ複製した結果、不一致は0だった。

最初の2実装runではAPI、帯域、queueは正常だったがdata検証が失敗した。原因は、PSRAMへCPU copyした後にC2M flushをせずM2C invalidateしたことで、capture前の`0xA5`を含む古いmemoryを読み戻したためだった。E021と同じC2M→M2C順へ修正後、全幅が正常になった。失敗runも`_runs/E031_20260909T011943Z_default/`等に残している。

## 判定

**PARLIO高速parallel tierは1 / 2 / 4 / 8 / 16 channelを同じ操作modelで実装でき、hardware上限は16 channelである。** 8未満のchannelはhardware bit packingにより保存量も比例して減る。16 channelは2 byte/sampleとなるため、同じsample rateで8 channelの2倍のPSRAM write帯域を使う。

24 / 32 / 32超channelは、P4にPARLIO RX unitが1基しかなく最大widthも16なので、この方式を拡張しては実現できない。CPU GPIO snapshotによる低速tierを別に測る。

## 事実・候補・未決

**事実**: 5幅すべて8 MHz設定、1,048,576 sample、overflow 0、全lane data正常。hardware packingは1 / 2 / 4 / 8 / 16 bit/sample。

**候補**: 1〜16 channelはPARLIOと同じbatch APIを使い、sample encodingだけwidthで決める。

**未決**: width別raw rate上限 / width別trigger上限 / 16本独立pad / 24 channel以上のCPU snapshot。
