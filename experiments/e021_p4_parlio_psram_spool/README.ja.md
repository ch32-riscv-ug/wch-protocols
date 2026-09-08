# E021 ESP32-P4 PARLIO internal ringからPSRAM退避

状態: **完了 — 8 MHz / 1 MiBをdropなしで退避**

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

## 結果

実施日: 2026-09-09

採用run: `_runs/E021_20260908T151406Z_default/test_parlio_psram_spool/dut.log`

最終構成ではcaptureごとにPARLIO receiverを作成・破棄した。64 KiB internal DMA ringからcallbackでdescriptorをqueueへ渡し、taskで1 MiB PSRAMへcopyした結果は次のとおり。

| run | callback / dequeue | callback byte | copied | stop時超過 | queue overflow | capture | 実効rate | duty最大誤差 | edge範囲 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 273 / 273 | 1,050,560 | 1,048,576 | 1,984 byte | 0 | 131,362 us | 7.982 MB/s | 7 ppm | 26,215〜26,216 |
| 1 | 273 / 273 | 1,050,560 | 1,048,576 | 1,984 byte | 0 | 131,361 us | 7.982 MB/s | 12 ppm | 26,214〜26,215 |
| 2 | 273 / 273 | 1,050,560 | 1,048,576 | 1,984 byte | 0 | 131,361 us | 7.982 MB/s | 12 ppm | 26,214〜26,215 |

全runでconfig / enable / receive / start / stop / disable / PSRAM syncが`ESP_OK`だった。descriptor長は2,432〜4,032 byte、観測時のqueue待ちは0、PSRAM全体syncは568〜571 us。cache alignment errorは出なかった。

最初のrun (`E021_20260908T151240Z_default`) では計画どおりreceiverを保持してstop→disable→enableした。run 0は正しかったがrun 1はAPI成功・overflow 0にもかかわらず最大duty誤差53,494 ppm、edge数24,713〜25,023となった。receiverをcaptureごとに再生成すると上表のとおり3回一致したため、stock driverのpartial transaction再利用は暫定的に避ける。

## 判定

仮説は条件付きで成立した。**stock PARLIO driverでも、internal DMA ring→task copy→PSRAMという経路なら、8 MHz / 8-bitの連続1 MiB captureをdropなしで構成できる。** E019のPSRAM direct descriptor alignment問題を回避でき、soft delimiterの65,535 byte上限もpartial ringで越えられた。

ただしreceiver再利用時に2回目のdataが崩れたため、現時点ではcaptureごとの再生成を必要条件とする。これはarm latencyや反復capture性能に影響する可能性があるが、batch logic analyzerの成立を妨げるものではない。

## 事実・候補・未決

**事実**: receiver再生成構成で1 MiB × 3回、overflow 0、実効7.982 MB/s、PWM data正常。receiver再利用構成では2回目だけ約5%相当の波形欠落が出た。

**候補**: 64 KiB internal ringとtask copyをstock Arduino向けbatch captureの基準経路にし、各armでreceiverを再生成する。

**未決**: sample rate上限 / rate別に必要なring・queue条件 / receiver再利用不良の原因 / basic trigger検索をcopy taskへ追加した負荷 / pre/post trigger。
