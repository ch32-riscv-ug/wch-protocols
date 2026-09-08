# E029 ESP32-P4 circular ring soak

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E027](../e027_p4_sump_circular_pretrigger/README.ja.md)

## 問い

**E027の20 MHz / 1 MiB PSRAM circular pre-trigger ringは、1 / 2 / 4 wrapを10組繰り返す30 captureを、別々のupload/resetで2回、異常なく完了できるか。**

## 仮説

E027は同じ3条件に3回連続で合格したため、最初の異常は継続的な帯域不足ではない。receiverをcaptureごとに再生成する現在の方式なら、合計60 captureでもqueue overflowやwindow不良を起こさない。

## 反証条件

- API error、timeout、queue overflow、入力rateへの非追従が1回でも起きる
- physical ring全体または論理512 Ki sample windowのdata検証が1回でも外れる
- trigger、wrap回数、停止overshootが期待範囲を外れる
- 2回の独立したupload/resetのどちらかで最初のcaptureだけ異常になる

## 方法

E027と同じ20 MHz / 8-bit、1 MiB PSRAM ring、256 Ki / 256 Ki pre/post windowを使う。1 / 2 / 4 wrap後にtriggerする3条件を10組、計30 capture実行する。pytestを明示指定で2回実行し、各実行でbuild・upload・reset・monitorをやり直す。

各captureでbase側のring全体検証と、modulo参照した論理window検証の両方をassertする。build・upload・monitorはpytest harness経由だけで行う。

## 対象外

- 数時間規模の耐久試験
- USB downloadとの同時動作
- 20 MHzを超えるcircular trigger capture
- 外部pad入力、電源cycle

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

実行ごとの30 captureについて、wrap、総sample、trigger/stop/overshoot、queue/overflow、capture rate、ring全体とwindowのduty誤差・edge数。

## 完了条件

- 2回の独立実行、合計60 captureをraw logへ残す
- 60/60でoverflow 0、入力追従、ring/window data正常を確認する
- E027の単発異常を再現できたか、短期soakでは再現しないかを判定する

## 影響

PSRAM circular ringを実装例の有力方式として扱える再現性があるかを判断する。
