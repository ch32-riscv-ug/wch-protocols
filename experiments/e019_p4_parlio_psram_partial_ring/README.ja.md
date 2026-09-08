# E019 ESP32-P4 PARLIO PSRAM partial ring

状態: **完了 — descriptor cache errorでInterrupt WDT**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E018](../e018_p4_parlio_psram_log_suppression/README.ja.md)

## 問い

**stock PARLIO driverの`partial_rx_en`で1 MiB PSRAMをdirect DMA ringとして使い、descriptor callbackで一周を検出して公開APIだけで停止した後、正しい8-bit連続captureを得られるか。**

## 仮説

`partial_rx_en=true`、`indirect_mount=false`ではuser payloadがDMA descriptorへ直接mountされ、末尾から先頭へring接続される。partial callbackが通知した直後にtask側からsoft delimiterを停止してRX unitをdisableすれば、一周分を大きく超えて先頭を上書きする前に止められる。

E018でsoft delimiterのEOF長は65,535 byte以下に制限された。しかしpartial transactionではEOFは区間通知であり、DMA ring全体の大きさは`max_recv_size`とpayload sizeで1 MiBにできると予想する。

## 反証条件

- PSRAM payloadを使うpartial direct transactionがAPIに拒否される
- soft delimiterの最初のEOF後にcaptureが継続しない
- 一周通知後、公開APIで停止できずtimeout・crash・大幅な上書きが起きる
- payload全体M2C sync後のPWM dutyまたはedge数が期待範囲を外れる

## 方法

1. GPIO 2〜9で8 laneの100 kHz PWMを生成し、PARLIO RXを8 MHz、8-bitにする
2. 128-byte整列した1 MiB PSRAM payloadを確保する
3. soft delimiterのEOF長を128-byte整列した65,408 byteにする
4. `partial_rx_en=true`、`indirect_mount=false`でpayloadをdirect ring mountする
5. `on_partial_receive`でdescriptor完了byteを加算し、1 MiB以上でcapture taskへISR-safe通知する
6. taskは通知を受けてsoft delimiterを停止し、RX unitをdisableする。private APIは使わない
7. capture前にC2M sync、停止後にpayload全体M2C syncを行い、全laneのdutyとedge数を検証する
8. `cache` tagはruntimeで`ESP_LOG_NONE`にし、host側でもcache errorが出ないことを確認する
9. receiverを作り直して3回実行する
10. build・upload・monitorはpytest harness経由だけで行う

## 対象外

- 1 MiBを超える容量
- sample rate上限
- 停止遅延をゼロにするdriver修正またはprivate API
- trigger前後比率
- USBやnetworkへのdownload

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

| 項目 | 単位・回数 |
|---|---|
| API結果 | receive / start / notify / stop / disable / sync、3回 |
| descriptor callback | 件/run |
| callback累計byteと一周超過 | byte/run |
| capture時間 | us/run |
| payload全体sync時間 | us/run |
| cache error | 件/run |
| maximum duty error | ppm/run |
| edge range | edge/run |

## 完了条件

- 1 MiB partial direct ringを3回開始・停止し、callback累計、停止時超過、API結果、時間、data検証をraw logへ残す
- 失敗した場合も、API拒否・EOF後停止・停止race・coherencyのどこで失敗したかを特定する
- 容量・sample rate掃引へ進めるか、停止方法またはdriverを先に変えるか判定できる

## 影響

深いbatch logic captureの基本経路を決めるgate。成功しても実用性判定には容量、sample rate、trigger、host downloadを別に測る。

## 結果

実施日: 2026-09-08

採用run: `_runs/E019_20260908T145625Z_default/test_psram_partial_ring/dut.log`

1 MiB、128-byte整列、external-DMA-capableのPSRAM payloadを確保し、`partial_rx_en=true`、`indirect_mount=false`のtransaction開始までは成功した。しかし、`esp_log_level_set("cache", ESP_LOG_NONE)`を設定した後もdescriptorごとに次のerrorが出力された。

```text
cache: esp_cache_msync: ... size: 0xfc0 ... not aligned with cache line size (0x80)B
```

27件を出力した時点でCore 1が`Interrupt wdt timeout`となりpanicした。pytestは再実行でも同じ27件とWDTを期待結果として確認した。最初のrun (`E019_20260908T145407Z_default`) も同じ位置で失敗している。

ELFを`addr2line`で復号すると、stack上の`0x4ff05de6`は`esp_cache_msync.c:122`、`0x4ff028ae`は`parlio_rx_default_desc_done_callback`、`0x4ff03566`は`gdma_default_rx_isr`だった。E017で確認した不整列syncがGDMA ISR内で繰り返され、serial error出力を伴ってInterrupt WDTへ至ったことと整合する。

## 判定

仮説は反証された。PSRAM direct partial ringはAPIに拒否されず開始できるが、runtime log levelでは`esp_cache_msync()`自身のerror出力を止められず、一周の通知より前にWDT resetする。したがって、**Arduino-ESP32 3.3.11のstock PARLIO driverをそのまま使う大容量PSRAM direct captureは実用経路にできない**。

driver sourceでは大容量bufferを`0xFC0` (4,032) byte単位にmountする。一方、PSRAM cache lineは128 byteであり、4,032は128の倍数ではない。E017で7,936 byteが3,968 byte × 2へ分割され無警告だったことも合わせると、external-memory transactionのmount alignmentにinternal-memory側の64 byte条件を使う実装が直接の修正候補になる。

## 事実・候補・未決

**事実**: 1 MiB PSRAM direct partial transactionは開始した。`cache` tagを`ESP_LOG_NONE`にしても4,032-byte descriptorのerrorは止まらず、27件でInterrupt WDTになった。

**候補**: (a) external-memory alignmentを使うようPARLIO driverを修正する、(b) internal DMA ringからPSRAMへtaskで退避する。stock driver direct経路の設定変更は候補から外す。

**未決**: PSRAM copy帯域 / internal ringからdropなしで退避できるsample rate / driver修正をArduino buildへ組み込む最小方法 / 修正版direct ringの停止精度。
