# E021 ESP32-P4 PARLIO internal ringからPSRAM退避

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E020](../e020_p4_psram_copy_bandwidth/README.ja.md)

## 問い

**8 MHz / 8-bit PARLIO RXを64 KiB internal DMA ringへ連続取得し、descriptor callbackからtaskへ渡して1 MiB PSRAMへdropなしで退避できるか。**

## 仮説

E020で4〜16 KiBのinternal→PSRAM copyはflush込み181 MB/s以上あり、8 MB/sの入力に対して22倍以上の単体帯域がある。64 KiB internal ringなら約8 msの再利用猶予があり、ISRではdescriptor情報のqueue投入だけ、taskでは`memcpy`だけを行えばdropせず退避できる。

internal RAMのcache alignmentは64 byteで、stock driverの4,032-byte descriptorは64の倍数なので、E019のPSRAM direct経路で起きたdescriptor cache sync errorも避けられる。

## 反証条件

- internal partial ringが開始または最初のsoft EOF後に継続しない
- ISR→task queueがoverflowする
- 1 MiB到達前にtimeoutまたはWDTが起きる
- PSRAMへ退避後のPWM duty / edge数が期待範囲を外れる

## 方法

1. GPIO 2〜9に100 kHz PWM 8 laneを生成し、PARLIO RXを8 MHz、8-bitにする
2. 64 KiBのinternal DMA ringと1 MiBの128-byte整列PSRAM destinationを確保する
3. soft delimiter EOF長を65,408 byte、`partial_rx_en=true`、`indirect_mount=false`とする
4. partial callbackはdata pointerとlengthだけをFreeRTOS queueへ送る。ISR内でPSRAM copyや解析をしない
5. capture taskはqueue順にPSRAMへcopyし、1 MiB到達後にsoft delimiterを停止してRX unitをdisableする
6. queue overflow、callback / dequeue件数、min/max descriptor長、取得時間を記録する
7. PSRAM全体をC2M、M2C syncし、全laneのdutyとedge数を検証する
8. receiver、queue、bufferを保持したままcapture stateを初期化して3回実行する
9. build・upload・monitorはpytest harness経由だけで行う

## 対象外

- 8 MHzを超えるsample rate
- trigger検索
- 1 MiBを超えるcapture
- pre/post trigger ring
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
| API結果 | receive / start / stop / disable / sync、3回 |
| callback / dequeue | 件/run |
| queue overflow | 件/run |
| copied / extra byte | byte/run |
| descriptor length | min/max byte |
| 1 MiB取得時間・実効rate | us、MB/s |
| maximum duty error | ppm/run |
| edge range | edge/run |

## 完了条件

- 1 MiB × 3回のAPI、queue、byte数、時間、data検証をraw logへ残す
- dropなしならsample rate掃引へ進む
- 失敗ならEOF継続、queue、copy、停止、dataのどこかを特定する

## 影響

stock Arduino環境で深いbatch captureを実装する基本経路のgate。成功は8 MHzでの成立だけを意味し、rate上限とtrigger負荷は別実験で測る。
