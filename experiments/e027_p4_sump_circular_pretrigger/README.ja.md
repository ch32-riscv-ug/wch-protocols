# E027 ESP32-P4 SUMP circular pre-trigger ring

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E026](../e026_p4_sump_prepost_stop/README.ja.md)

## 問い

**1 MiB PSRAM circular ringを1 / 2 / 4回以上wrapした後にtriggerし、20 MHz / 8-bitで直前256 Ki sampleと直後256 Ki sampleの論理windowを欠落なく再構成できるか。**

## 仮説

E026でtrigger後のdescriptor単位停止が成立した。PSRAMへのcopy先を総sample位置のmodulo 1 MiBにすれば、trigger待機中に古い履歴を上書きしながら、停止時点の最後1 MiBを保持できる。要求window 512 Ki sampleはring容量以下なので、物理wrapをまたいでも2区間として再構成できる。

## 反証条件

- queue overflow、入力rateへの非追従、またはwindow data不良が起きる
- trigger前256 Ki / 後256 Kiが停止時点のring保持範囲に収まらない
- physical ring wrapをまたぐ論理windowを正しい順序で検証できない
- 物理停止overshootが1 callback chunk以上になる

## 方法

20 MHz / 8-bit、1 MiB PSRAM ring、512 Ki sampleの50/50 windowを使う。run 0 / 1 / 2は総sample位置1.25 / 2.25 / 4.25 MiB以降の最初のlane 0 rising edgeをtriggerとし、256 Ki sample後を含むdescriptorで停止する。各chunkを`total_offset % ring_size`へcopyし、末尾をまたぐ場合は2回のcopyに分ける。

停止後、論理windowの各sampleをmoduloでringから読み、全8 laneのdutyとedgeを検証する。build・upload・monitorはpytest harness経由だけで行う。

## 対象外

- ring容量を超えるpre-trigger長
- hostへ2区間を転送するwire format
- multi-stage trigger、RLE、圧縮
- 外部pad入力

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

wrap回数、trigger/stop/総sample位置、window physical開始位置、overshoot、queue/overflow、capture rate、window duty誤差とedge数。

## 完了条件

- 1 / 2 / 4回以上のwrap後に50/50 windowを再構成する
- 全runでoverflow 0、入力追従、window data正常を確認する
- 長時間trigger待機へ同じring構造を使えるか判定する

## 影響

有限captureだけでなく、ring容量の範囲でtrigger前履歴を保持し続けるSUMP型captureの実現性を決める。
