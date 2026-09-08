# E027 ESP32-P4 SUMP circular pre-trigger ring

状態: **完了 — 1/2/4 wrap後のwindowを3回連続再構成**

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

## 結果

実施日: 2026-09-09

採用run: `_runs/E027_20260908T155813Z_default/test_sump_circular_pretrigger/dut.log`

| wrap | trigger index | 総取得sample | physical window開始 | overshoot | 実効rate |
|---:|---:|---:|---:|---:|---:|
| 1 | 1,310,751 | 1,573,824 | 31 | 929 | 19.953 MB/s |
| 2 | 2,359,357 | 2,624,384 | 61 | 2,883 | 19.972 MB/s |
| 4 | 4,456,597 | 4,721,472 | 149 | 2,731 | 19.984 MB/s |

全runでqueue最大1、overflow 0。512 Ki sampleの論理windowは停止時点の最後1 MiB内に収まり、physical ringの開始位置31 / 61 / 149からmodulo参照して正しい時系列へ再構成できた。window全8 laneのduty誤差は最大2,541 ppm、edge数は5,242〜5,243だった。物理停止overshootは929〜2,883 sampleで、最大chunk 4,032 sample未満だった。

採用runに先立つ`155728`と`155752`も同じ3 runに合格し、**3回連続で1 / 2 / 4 wrap後のwindow再構成に成功**した。

最初の`155635` run 0ではqueue 1、overflow 0ながらbase側のbuffer全体検証に異常値が一度出た。ring用window検証を読む前にpytestが停止したため原因を確定できず、その後3回は再現しなかった。方式の成立判定はできるが、製品実装前の長時間soakではこのcold-run相当の異常を監視対象に残す。

## 判定

**20 MHzでPSRAM circular pre-trigger ringを複数回wrapし、trigger前後256 Ki sampleずつを再構成できる。** E026のtrigger後停止と組み合わせ、SUMP型のpre-trigger履歴、post-trigger取得、論理window trimまで主要なdata pathが成立した。

windowはphysical ring上で最大2区間になる。protocolでは連続したsample列として返し、firmwareまたはhost transport層が2区間を順に送ればよく、wire上へring配置を露出する必要はない。

## 事実・候補・未決

**事実**: 1 / 2 / 4 wrap後の50/50 windowを3回連続で再構成。queue最大1、overflow 0、window data正常。

**候補**: PSRAM circular ringを内部実装とし、外部にはtrim済みの連続sample列を返す。

**未決**: cold-run異常の長時間soak / multi-stage trigger / host転送 / 1 MiB超のring容量。
