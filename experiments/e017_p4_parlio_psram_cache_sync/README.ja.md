# E017 ESP32-P4 PARLIO PSRAM cache sync境界

状態: **完了 — burst sizeでは回避不能、7,936 byteまでは無警告**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E016](../e016_p4_parlio_psram_direct/README.ja.md)

## 問い

**Arduino-ESP32 3.3.11のstock PARLIO driverで、PSRAM有限長captureのdescriptor完了時cache sync警告を避けられる`dma_burst_size` / capture size条件はあるか。**

## 仮説

`dma_burst_size`だけでは警告を解消できない。captureが単一DMA descriptorに収まり、payload addressとlengthの両方がPSRAMの128-byte cache lineへ整列する場合だけ無警告になる。

ESP-IDF 5.5.5 sourceではDMA node数とtransactionの`alignment`に`rx_unit->int_mem_align`を使う。E016ではinternal 64 byte、external 128 byteで、descriptor callbackが`finished_buffer`と`finished_length`をそのままM2C syncした。最大node長が64-byte単位へ切り下げられると、複数nodeのPSRAM transferで128-byte境界を外す可能性がある。

## 反証条件

複数descriptorとなるcapture sizeでも、特定の`dma_burst_size`によって警告がゼロになる場合、または128-byte整列した単一descriptorのcaptureでも警告する場合、仮説を反証する。

## 方法

1. E016と同じGPIO 2〜9、LEDC 100 kHz、PARLIO RX 8 MHz、8-bit、PSRAM direct payloadを使う
2. PSRAM bufferは128-byte alignmentを実測して確保し、各capture後にpayload全体を明示M2C syncする
3. `dma_burst_size = 0, 64, 128 byte`を比較する
4. capture sizeは128-byte倍数から、`3,968, 4,096, 7,936, 8,064, 8,192 byte`を比較する
5. 各組合せを3回実行し、API結果、driver cache警告数、全laneの最大duty誤差、edge範囲を記録する
6. PARLIO unitとsoft delimiterはburst/size条件ごとに作り直す
7. build・upload・monitorはpytest harness経由だけで行う

3,968 byteはE016で観測した`0xFC0`と異なり128-byte倍数で、4,096 byte未満。7,936 byteは3,968 byte × 2、8,064 byteは`0xFC0` × 2である。これにより単なる総size整列とnode境界を区別する。

## 対象外

- sample rate上限
- 8,192 byteを超える大容量capture
- log level抑制による性能回避
- driver sourceの修正
- partial / continuous receive
- USBやnetworkへの転送

## 必要な環境

- E016で32 MiB PSRAMを確認したESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

## 記録する数値

| 項目 | 単位・回数 |
|---|---|
| burst size | byte、3条件 |
| capture size | byte、5条件 |
| API結果 | 条件ごと × 3回 |
| cache alignment警告 | 件/run |
| maximum duty error | ppm/run |
| edge range | edge/run |

## 完了条件

- 15条件 × 3回の成功・失敗と警告件数がraw logにある
- 無警告で使える条件があるか、設定では大容量captureの警告を避けられないかを判定できる
- 結果に基づき、stock driver + final syncで大容量試験へ進むか、driver修正を先にするかを決められる

## 影響

実用的なbatch logic captureのPSRAM経路をstock Arduino環境で構成できるかのgate。成功しても速度・大容量安定性は別実験で測る。

## 結果

実施日: 2026-09-08

採用run: `_runs/E017_20260908T143447Z_default/test_psram_cache_sync_boundary/dut.log`

環境はArduino-ESP32 3.3.11 / ESP-IDF 5.5.5、ESP32-P4 rev 1.3、32 MiB PSRAM。PSRAM bufferの先頭は128 byte境界に整列し、external DMA capableだった。

全15条件を各3回、合計45回captureした。全条件でreceive / start / wait / stopと完了後のpayload全体M2C syncが`ESP_OK`となり、全8 laneのPWMを復元できた。最大duty誤差は6,048 ppm、edge数はcapture sizeに応じて98〜207だった。

| capture size | descriptor callbackのcache警告/run | burst 0 / 64 / 128の差 |
|---:|---:|---|
| 3,968 byte | 0 / 0 / 0 | なし |
| 4,096 byte | 0 / 0 / 0 | なし |
| 7,936 byte | 0 / 0 / 0 | なし |
| 8,064 byte | 2 / 2 / 2 | なし |
| 8,192 byte | 2 / 2 / 2 | なし |

8,064 byteでは`0xFC0` byteのsyncが2回、8,192 byteでは`0xFC0`と`0x800` byteのsyncが発生し、いずれも128-byte cache lineへの整列違反として記録された。警告はdriver内部のdescriptor callbackから出ており、実験側が行う整列済みpayload全体のsyncは成功した。

## 判定

仮説は一部反証された。`dma_burst_size`だけでは警告を解消できない点は仮説どおりだが、4,096 byteと複数descriptor相当の7,936 byteでも無警告だったため、「単一descriptorの場合だけ無警告」とは言えない。

今回の条件では7,936 byte以下に無警告領域がある。しかし実用的な深いcaptureをこの大きさに制限する解決にはならない。stock driverで大容量PSRAM captureを評価する前に、cache error logを抑制できるかと、完了後の全体syncで大容量dataの一貫性を回復できるかを確認する。

## 残った未決

- 無警告と警告の厳密な境界、および7,936 byteまで無警告になるdriver内部の分割条件
- cache log抑制下での大容量PSRAM direct captureの安定性と所要時間
- sample rate上限
- partial / continuous receiveでのcoherency
