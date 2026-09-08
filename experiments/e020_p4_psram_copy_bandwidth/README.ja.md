# E020 ESP32-P4 PSRAM copy帯域

状態: **完了 — CPU copy単体は80 MB/sを上回る**

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

## 結果

実施日: 2026-09-09

採用run: `_runs/E020_20260908T150231Z_default/test_psram_copy_bandwidth/dut.log`

ESP32-P4 rev 1.3の32 MiB PSRAMから8 MiBを確保し、全9条件でcache syncと全chunkのbyte比較が成功した。帯域は10進MB/s。

| chunk | internal→PSRAM copy | flush込みwrite | PSRAM→internal read | 8 MiB flush |
|---:|---:|---:|---:|---:|
| 4 KiB | 183.353〜183.389 MB/s | 181.085〜181.128 MB/s | 182.591〜182.646 MB/s | 571〜575 us |
| 16 KiB | 184.942〜185.031 MB/s | 182.654〜182.746 MB/s | 183.787〜183.815 MB/s | 567〜568 us |
| 64 KiB | 139.894〜139.917 MB/s | 138.590〜138.622 MB/s | 170.917〜170.941 MB/s | 560〜565 us |

16 KiBが今回の最大write/read帯域だった。64 KiBではwriteが約24%低下したが、最小値でもflush込み138.590 MB/sだった。write / readとも全runでmismatchは0件。

## 判定

CPU `memcpy`単体の帯域だけを見れば、16 KiB chunkのflush込みwriteは8-bit 80 MS/sに対して2.28倍、40 MS/sに対して4.57倍、8 MS/sに対して22.8倍ある。したがってinternal DMA ringからPSRAMへ退避する候補は、少なくともmemory帯域だけを理由に棄却する必要はない。

ただし、この測定はPARLIO ISR、DMA、trigger検索、USB処理と同時実行していない。80 MS/sで2.28倍は有望だが十分条件ではなく、実際のspoolでdropとCPU占有率を測る必要がある。

## 事実・候補・未決

**事実**: 8 MiBのinternal RAM↔PSRAM CPU copyは全条件で正しく、flush込みwriteの最小は138.590 MB/s、最大は182.746 MB/sだった。

**候補**: 16 KiB前後のinternal ping-pong / ring bufferをtaskでPSRAMへ退避する。

**未決**: PARLIO同時動作時のdropなしrate / internal buffer個数 / core分離 / trigger検索を同時に行った帯域 / GDMA memory copyとの比較。
