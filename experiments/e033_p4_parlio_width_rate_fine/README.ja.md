# E033 ESP32-P4 PARLIO 8/16 channel rate精密探索

状態: **完了 — 8 channelは100 MHz、16 channelは48 MHzまで成立**

規則: [実測の規則](../README.ja.md) / 先行実験: [E032](../e032_p4_parlio_width_rate_coarse/README.ja.md)

## 問い

**triggerなしbatch captureの安定境界は、8 channelの84〜120 MHz、16 channelの44〜80 MHzのどこか。**

## 仮説

E032では両幅とも約80 MB/sまで成立したため、channel数ではなくpacking後byte rateに対応し、8chと16chの境界byte rateは近くなる。

## 反証条件

- 成立/不成立がrate順に単調でない
- 8chと16chの境界byte rateが大きく異なる
- E032の成立点または不成立点を再現しない

## 方法

E032と同じ1,048,576 sample、PWM、PSRAM spool、成立条件を使う。8chは84〜120 MHz、16chは44〜80 MHzを4 MHz刻みで測る。

## 対象外

trigger、deep capture、外部pad、ring/chunk tuning。

## 必要な環境

32 MiB PSRAM搭載ESP32-P4、既存stable port、配線不要。ベンチ種別: **一時・配線なし**。

## 記録する数値

設定/実効sample rate、byte rate、queue最大、overflow、data検証。

## 完了条件

各幅で最大成立点と最初の不成立点を4 MHz幅で確定する。

## 影響

raw batch capabilityの上限と、後続trigger/圧縮試験の最大入力rateを決める。

## 結果

実施日: 2026-09-09

採用run: `_runs/E033_20260909T013637Z_default/test_parlio_width_rate_fine/dut.log`

| channel | 最大成立設定 | 実効sample rate | 実効byte rate | 最初の不成立設定 | 不成立時の主因 |
|---:|---:|---:|---:|---:|---|
| 8 | 100 MHz | 97.869 MHz | 97.869 MB/s | 104 MHz | 実効98.089 MHzで設定の95%未満 |
| 16 | 48 MHz | 47.838 MHz | 95.677 MB/s | 52 MHz | queue 36、実効48.687 MHzで設定の95%未満 |

8 channel / 104 MHzはoverflow 0、queue最大15でdata検証も通ったが、入力側は約98 MB/sで飽和し、設定rateへ追従しなかった。108 MHz以降はqueueが27、38、48、63と増えた。16 channel / 52 MHzもoverflow前に同じ約97 MB/sで飽和し、56 MHzからoverflowした。

最初のpytest runは、全条件を実行中のfirmwareへmonitorが途中接続したため、5秒のbanner待ちを超えて中断した。firmware側の測定は完走している。待受上限を45秒へ変更した採用runは全20条件とhost判定を完走した。

## 判定

**現在のinternal DMA ringからPSRAMへtask退避する実装では、triggerなしraw batchの境界は約96〜98 MB/sである。** 8 / 16 channelでほぼ同じbyte rateに境界が現れ、packing後byte rateが律速という仮説を支持した。

安全側の公称値は、境界値ではなくE032で全widthが余裕を持って成立した80 MB/sを維持する。100 MHz / 8 channelと48 MHz / 16 channelは短い1 Mi sampleで成立した実測上限として区別する。

## 事実・候補・未決

**事実**: 8 channelは100 MHz、16 channelは48 MHzまで成立。104 / 52 MHzではoverflow前に設定rateへ追従できなくなった。

**候補**: 80 MB/sを安定tier、約96 MB/sを短時間burst tierとしてcapabilityを分ける。

**未決**: deep capture時の境界、ring/chunk/task配置による改善余地、trigger・圧縮を加えた場合の上限。

## 追記 — E036による再解析(2026-09-09)

本レポートは書き換えない。[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)がsample単位の検証器とring未読byteによるdrop判定で同じ条件を測り直した結果、次の2点を訂正する。

1. **律速はsampling側ではない。** 「実効rateが設定へ追従しない」と書いた条件では、PARLIOは設定どおりsamplingしていた(120 MHz設定で119.560 MB/s = 設定の99.6%)。約98 MB/sの飽和はinternal ring → PSRAMのtask copy側の限界である。本レポートの「実効sample rate」はcapture開始からPSRAMへの退避完了までを分母にしており、samplingとspoolを1つの値へ潰していた。
2. **dutyとedge数による検証はsample単位の欠落を検出できない。** 信号源が定常・周期的な100 kHz PWMなので、ringがcopy前に上書きされても同じ波形が見え、検証を通過する。E036では112 / 120 MHzで連番違反が18 / 27件出たが、`result`は`ESP_OK`、`overflows`は0だった。

数値そのものは有効である。訂正は「その数値が何の限界か」の帰属と、data検証の有効範囲についてである。

なお8 channel / 104 MHzの不成立判定はE036で覆った。ring未読byteは60,480 byteでring 65,536 byteを超えず、連番違反は0件で、1,048,576 sampleをdropなく取得できていた。本レポートが不成立としたのはend-to-endのspool rateが設定rateの95%へ届かなかったためで、sampleは失われていない。1 Mi burstのdropなし境界は104〜112 MHzの間にある。
