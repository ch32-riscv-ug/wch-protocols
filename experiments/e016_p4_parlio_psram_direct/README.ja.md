# E016 ESP32-P4 PARLIO RXからPSRAMへの直接DMA

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E015](../e015_p4_parlio_routing_order/README.ja.md)

## 問い

**Arduino-ESP32 3.3.11上でESP-IDFのPARLIO driver APIを直接呼び、有限長8-bit PARLIO RXのpayloadをESP32-P4のPSRAMへ直接DMAできるか。**

## 予備調査

E015の実ビルドは`ChipVariant=prev3`により`esp32p4_es-libs`をリンクしている。この構成はESP-IDF 5.5.5で、次を含む。

- `CONFIG_SPIRAM=y`、200 MHz hex PSRAM
- `CONFIG_SOC_PSRAM_DMA_CAPABLE=y`
- `CONFIG_SOC_AHB_GDMA_SUPPORT_PSRAM=y`、`CONFIG_SOC_AXI_GDMA_SUPPORT_PSRAM=y`
- `CONFIG_PARLIO_RX_ISR_CACHE_SAFE`は無効

ESP-IDF 5.5系のPARLIO RX実装はGDMA transferへ`access_ext_mem=true`を設定し、受信payloadを内部・外部memoryそれぞれのalignmentに合わせて分割する。公開headerにも、通常の有限長transactionではuser payloadをDMA descriptorへ直接mountすると記載されている。

したがって、**Arduino環境でESP-IDF driverを直接呼ぶこと自体は障害ではなく、現在のprebuilt IDF構成ではPSRAMへの直接受信が意図された経路に見える**。ただし、これはsourceとbuild設定からの推定であり、実機結果ではない。

参照:

- [ESP-IDF 5.5 PARLIO RX source](https://github.com/espressif/esp-idf/blob/release/v5.5/components/esp_driver_parlio/src/parlio_rx.c)
- local build構成: `/home/mt/.arduino15/internal/esp32_esp32p4_es-libs_3.3.11_556d78d3bbbdd6a7/sdkconfig`
- local API header: `/home/mt/.arduino15/packages/esp32/tools/esp32p4_es-libs/3.3.11/include/esp_driver_parlio/include/driver/parlio_rx.h`

## 仮説

`PSRAM=enabled`で起動し、実行時に取得したexternal-memory alignmentを満たす`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` bufferを渡せば、`partial_rx_en=false`の有限長受信が`ESP_OK`となり、E015と同じPWM patternをPSRAM上で復元できる。

## 反証条件

PSRAMが実機で利用可能なのに、整列済みPSRAM payloadを使う有限長受信が拒否される、完了しない、または同条件のinternal RAM受信では正しいPWM patternがPSRAM受信で化ける場合、現在のArduino-ESP32構成での直接受信仮説を反証する。

## 方法

1. E015を複製せず、必要最小限のLEDC + PARLIO構成を新しい実験として用意する
2. `sketch.yaml`で`PSRAM=enabled`を明示する
3. 起動時にArduino core、ESP-IDF、PSRAM容量、PSRAM free sizeを記録する
4. `esp_cache_get_alignment()`で内部・外部memoryのalignmentを取得して記録し、同じcapture sizeについて各alignmentを満たす内部DMA RAMとPSRAM bufferを明示的に確保する
5. pointerごとに`esp_ptr_internal()`、`esp_ptr_external_ram()`、`esp_ptr_dma_capable()`、alignment、heap capabilitiesを記録する
6. E015と同じ`io_loop_back=false`、GPIO 2〜9、100 kHz PWM、8 MHz PARLIO RXを使い、internal RAMをcontrol、PSRAMをtestとして各3回有限長captureする
7. `parlio_rx_unit_receive()`、start、wait、stop、cache syncの各戻り値を個別に残す
8. 全laneのhigh/low、edge数、duty比をinternal RAMとPSRAMで比較する
9. build・upload・monitorはpytest harness経由だけで行う

capture sizeはE015と同じ8,192 byteに固定する。PSRAMを使った事実はpointerの所在で確認できるため、この実験ではbuffer容量やsample rateを掃引しない。

## 分岐

- **直接DMA成立**: `p4-parlio-psram-spool`より先に、直接DMAで大容量有限長captureとsample rate上限を分けて計画する
- **driver/APIが拒否**: exact errorとpointer capabilityを根拠に、`partial_rx_en + indirect_mount`またはinternal DMA bufferからPSRAMへの退避を別候補として比較する
- **driverを通るがdataが化ける**: cache coherencyとexternal-memory alignmentだけを問う追試を立てる

公開APIが拒否しても、ESP32-P4のGDMA hardwareがPSRAMへ到達不能とは結論しない。必要なら低水準GDMAまたはESP-IDF構成変更を別の問いにする。

## 対象外

- 最大sample rate
- PSRAM bandwidth単体の上限
- continuous streamingとdrop率
- internal DMA ping-pong bufferからPSRAMへのcopy性能
- USBやnetworkへの転送

## 必要な環境

- ESP32-P4 rev 1.3、MAC `e8:f6:0a:e0:aa:24`
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11
- boardは接続確認済み。PSRAMの有無・容量はUSB識別情報から確定できないため、実験開始時のpreflightで確認する
- 外部配線・target・logic analyzerは不要

portとGPIO番号は`experiments/.env`だけに置く。pytest harnessのdevice lockを無効化しない。

## 完了条件

次のどちらかを満たす。

1. PSRAMが存在しない場合: 容量0と初期化状態をraw logへ残し、直接DMAを評価せず「環境不成立」として中断する
2. PSRAMが存在する場合: internal RAM controlが成功し、PSRAM bufferの所在・alignmentと、直接payloadにした有限長PARLIO RXの成功・拒否・timeout・data不一致のいずれかをraw logで確定し、結果に応じた後続候補を一つに絞る
