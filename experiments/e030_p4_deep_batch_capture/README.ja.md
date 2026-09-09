# E030 ESP32-P4 deep batch capture

状態: **完了 — 16 MiB、19.995 MB/s、overflow 0**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E021](../e021_p4_parlio_psram_spool/README.ja.md)、[E029](../e029_p4_circular_ring_soak/README.ja.md)

## 問い

**20 MHz / 8-bit PARLIO RXを64 KiB internal DMA ringから16 MiB PSRAMへ連続退避し、dropやdata化けなしで保持できるか。**

## 仮説

E020ではinternal RAM→PSRAMのflush込みwriteが最低138.590 MB/s、E025とE029ではtrigger処理を含む20 MHz経路が安定した。したがって、32 MiB PSRAMの半分をcapture bufferに使う16 MiB連続取得も、約0.84秒にわたり20 MB/sへ追従できる。

## 反証条件

- 16 MiBのPSRAM bufferを確保できない
- API error、timeout、queue overflow、入力rateへの非追従が起きる
- 16 MiB全域のPWM dutyまたはedge検証が外れる
- capture完了後のcache syncを含めて異常終了する

## 方法

E021のinternal DMA ring→PSRAM spool経路を再利用し、sample rateを20 MHz、保存量を16 MiB、実行回数を1回にする。GPIO 2〜9で生成する8 laneの100 kHz PWMをGPIO matrix経由でPARLIO RXへ入力する。pytest harnessからbuild・upload・reset・monitorを行い、16 MiB全域について各laneのdutyとedge数を検証する。

## 対象外

- circular pre-trigger、multi-stage trigger
- USBまたはnetworkへのdownload
- captureとhost転送の同時実行
- 外部pad入力とsignal integrity
- 16 MiBを超える容量の掃引

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

PSRAM容量、allocation成否、callback/dequeue数、copy量、queue最大深さ、overflow数、capture時間、実効MB/s、cache sync時間、全laneの最大duty誤差とedge数。

## 完了条件

- 16 MiBを1回取得し、overflow 0、入力追従、全域data正常のいずれかをraw logへ残す
- 失敗した場合も、allocation、帯域、queue、data、cache syncのどこが境界かを特定する

## 影響

ESP32-P4実装例で提供できるbatch captureの実用的な深さと、host download試験で使う基準データ量を判断する。

## 結果

実施日: 2026-09-09

採用run: `_runs/E030_20260909T005932Z_default/test_deep_batch_capture/dut.log`

| 保存量 | sample数 | sample rate | capture時間 | 実効rate | callback / dequeue | queue最大 | overflow | data検証 |
|---:|---:|---:|---:|---:|---:|---:|---:|:---:|
| 16 MiB | 16,777,216 | 20 MHz / 8-bit | 839,063 us | 19.995 MB/s | 4,361 / 4,361 | 0 | 0 | 成功 |

32 MiB PSRAMから16 MiB destinationと64 KiB internal DMA ringを確保できた。全APIとcapture後のcache syncは`ESP_OK`、余分に到着した3,520 byteは保存量の境界で切り捨てた。全8 laneのedge数は各167,772、最大duty誤差は2,501 ppmで、16 MiB全域の検証に成功した。

20 MHzで1 sampleを8-bitの1 byteへpackするため、生成量は20,000,000 byte/sである。今回の実効値19.995 MB/sは入力rateへ追従しており、16 MiBは約0.839秒分に相当する。

## 判定

**20 MHz / 8-bitの約20 MB/sを、16 MiB・約0.84秒にわたりPSRAMへ連続保存できる。** E029の1 MiB circular ringだけでなく、PSRAM容量の半分を使うdeep batchでもinternal ring→PSRAM経路に帯域・queue・data上の異常はなかった。

これはhostへ20 MB/sで転送できることを意味しない。次のdownload試験では16 MiBを基準データ量として使えるが、USB device→PC方向の実効帯域と、captureとの同時動作は別に測る必要がある。

## 事実・候補・未決

**事実**: 16 MiB、20 MHz / 8-bit、19.995 MB/s、queue最大0、overflow 0、全域data正常。

**候補**: 16 MiBをESP32-P4実装例のdeep batch基準容量、20 MHzをtrigger付き基準rateとする。

**未決**: USB device→PC download帯域 / captureとdownloadの同時実行 / 16 MiB circular trigger / 外部pad入力。
