# E029 ESP32-P4 circular ring soak

状態: **完了 — 独立reset 2回、合計60/60成功**

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

## 結果

実施日: 2026-09-09

採用run:

- `_runs/E029_20260908T213123Z_default/test_circular_ring_soak/dut.log`
- `_runs/E029_20260908T213209Z_default/test_circular_ring_soak/dut.log`

| 独立実行 | capture | 総sample | 実効rate範囲 | overshoot範囲 | 結果 |
|---:|---:|---:|---:|---:|:---:|
| 1 | 30 | 89,196,800 | 19.954〜19.985 MB/s | 772〜2,922 sample | 30/30成功 |
| 2 | 30 | 89,196,800 | 19.954〜19.985 MB/s | 766〜2,924 sample | 30/30成功 |

合計178,393,600 sampleを取得した。60 captureすべてでAPIは`ESP_OK`、callback数とdequeue数は一致、extra byte 0、queue最大1、overflow 0だった。ring全体のduty誤差は最大2,516 ppm、論理512 Ki sample windowは最大2,541 ppmで、edge数も全runで期待範囲内だった。

1 / 2 / 4 wrap、trigger位置、保持範囲、physical開始位置、post停止条件は全60 captureで整合した。停止overshootは最大2,924 sampleで、最大callback chunk 4,032 sample未満だった。

各pytest実行は個別にbuild・upload・resetした。両方とも最初のcaptureから正常で、E027の最初に一度だけ見えた異常は再現しなかった。

## 判定

**20 MHz / 1 MiB PSRAM circular pre-trigger ringは、短期soakの範囲で再現性あり。** E027の単発異常は合計60 captureでは再現せず、継続的なbandwidth不足、receiver再生成不良、wrap計算不良を示す結果はなかった。

これは数時間の耐久性、電源cycle、USB転送との同時動作を保証しない。現段階では方式を棄却する理由はなく、実装例の有力data pathとして次のhost download統合試験へ進める。

## 事実・候補・未決

**事実**: 独立reset 2回、合計60/60 capture、178,393,600 sampleでqueue最大1、overflow 0、ring/window data正常。

**候補**: 20 MHz circular captureをhost download統合の基準構成にする。

**未決**: captureとUSB downloadの同時実行 / 数時間soak / 電源cycle / 外部pad入力。
