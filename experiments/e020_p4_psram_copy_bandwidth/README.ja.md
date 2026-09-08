# E020 ESP32-P4 PSRAM copy帯域

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E019](../e019_p4_parlio_psram_partial_ring/README.ja.md)

## 問い

**ESP32-P4のinternal RAM→PSRAM書込とPSRAM→internal RAM読出しは、4 / 16 / 64 KiB chunkで8-bit logic captureを退避できる実効帯域を持つか。**

## 仮説

cache flushを含むinternal→PSRAMの持続帯域が40 MB/s以上なら40 MS/s、80 MB/s以上なら80 MS/sの8-bit streamを帯域上は退避できる。単純なCPU `memcpy`で不足しても、測定値はdriver修正によるdirect DMAとspool方式を選ぶ基準になる。

## 反証条件

- 8 MiBのcopyまたはcache syncが失敗する
- data比較が不一致になる
- flush込みwrite帯域が8 MB/s未満で、現在確認済みの8 MS/sさえ退避できない

## 方法

1. 8 MiBの128-byte整列PSRAM bufferと、最大64 KiBのinternal RAM bufferを確保する
2. internal bufferに既知patternを作る
3. 4 / 16 / 64 KiBごとに、internal bufferを8 MiB全域へ`memcpy`する時間と、その後のPSRAM全体C2M sync時間を分けて測る
4. sync後に全chunkを`memcmp`して書込結果を検証する
5. PSRAM全体をM2C syncした後、8 MiB全域をinternal bufferへchunkごとに`memcpy`する時間を測り、各chunkを比較する
6. 各chunk sizeを3回測る
7. build・upload・monitorはpytest harness経由だけで行う

## 対象外

- PARLIOとcopyを同時実行したときの競合・drop
- GDMAによるmemory-to-memory copy
- 8 MiB以外の総転送量
- sample rate上限
- USBやnetworkとの同時転送

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

| 項目 | 単位・回数 |
|---|---|
| PSRAM容量・空き | byte |
| chunk size | byte、3条件 |
| internal→PSRAM copy | us、MB/s、各3回 |
| PSRAM C2M flush | us、各3回 |
| copy + flush | MB/s、各3回 |
| PSRAM→internal copy | us、MB/s、各3回 |
| write/read data mismatch | 件/run |

MB/sは10進のbyte/sで計算する。

## 完了条件

- 3 chunk size × 3回についてcopy、sync、比較、帯域をraw logへ残す
- 8 / 40 / 80 MB/sの各required rateに対して、帯域だけを見た成立余裕を判定できる
- 次にstock driverのinternal ring spoolを試すか、driver修正directを優先するか決められる

## 影響

E019で残った2候補のうち、internal DMA ring→PSRAM退避を実測する価値があるかを決める。帯域成立はPARLIOとの同時動作成立を意味しない。
