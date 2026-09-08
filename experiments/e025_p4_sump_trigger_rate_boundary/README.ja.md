# E025 ESP32-P4 SUMP基本trigger rate境界

状態: **完了 — 全基本条件は24 MHzまで成立**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E024](../e024_p4_sump_swar_trigger_80mhz/README.ja.md)

## 問い

**32-bit software検索によるSUMP基本trigger相当のpattern/mask・edgeを使いながら、8-bit captureをdropなしで継続できるsample rate境界はどこか。**

## 仮説

E024の検索単体throughputは25.605〜30.926 MB/sだったため、16 MHzと20 MHzは全条件で成立し、28 MHz以上では少なくとも重い条件がoverflowする。24 MHz付近が共通capabilityの境界候補になる。

## 反証条件

- 16 MHzでもいずれかの条件がoverflowまたはdata不良になる
- 32 MHzでも全条件が余裕をもって成立し、掃引範囲内に境界がない
- trigger条件が期待と異なるmatch結果になる

## 方法

E024の32-bit検索を使い、16 / 20 / 24 / 28 / 32 MHzの各rateで1 MiB captureを行う。各rateにつき、存在しない`mask=0xFF, value=0x55`、lane 0 rising edge、`mask=0x0F, value=0x00`の3条件を評価する。match後も全sampleを検索する。rateごとのcapture/queue/data/trigger指標を記録し、全条件がoverflow 0かつdata正常となる最高rateを求める。

build・upload・monitorはpytest harness経由だけで行う。

## 対象外

- 4 MHz未満の刻みによる厳密な最大値探索
- trigger処理の別core化またはESP32-P4固有SIMD化
- pre/post trigger、発生回数、multi-stage trigger
- 外部pad入力

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- 外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

sample rate、trigger mode、match/index、scan throughput、callback/dequeue/overflow/queue深さ、capture実効rate、dataのduty誤差とedge数。

## 完了条件

- 5 rate × 3条件をraw logへ残す
- 全基本条件が成立する最高rateと、最初に不成立となるrateを確定する
- pre/post trigger実験に使う安全なsample rateを選べる

## 影響

ESP32-P4実装例がadvertiseする「software basic trigger付き8-bit capture」のrate tierを定める。

## 結果

実施日: 2026-09-09

採用run: `_runs/E025_20260908T154529Z_default/test_sump_trigger_rate_boundary/dut.log`

表中は`capture実効rate / queue最大深さ`。`overflow`は件数を示す。

| sample rate | no-match | rising | pattern | 全条件で持続可能 |
|---:|---:|---:|---:|:---:|
| 16 MHz | 15.945 MB/s / 1 | 15.944 MB/s / 1 | 15.947 MB/s / 0 | Yes |
| 20 MHz | 19.923 MB/s / 1 | 19.922 MB/s / 1 | 19.926 MB/s / 1 | Yes |
| 24 MHz | 23.898 MB/s / 1 | 23.892 MB/s / 1 | 23.905 MB/s / 1 | **Yes** |
| 28 MHz | 25.013 MB/s / 32 | 24.107 MB/s / 43 | 27.871 MB/s / 1 | No |
| 32 MHz | 24.796 MB/s / 64、14 overflow | 23.887 MB/s / 64、28 overflow | 27.955 MB/s / 39 | No |

16〜24 MHzでは全条件がcallback 273、dequeue 273、extra 1,984 byte、overflow 0で、queue最大深さも0〜1だった。capture dataはduty誤差2,519 ppm以下、edge数も期待範囲内だった。triggerもno-matchは非検出、risingとpatternは検出となった。

28 MHzではpatternだけが入力に追従した。no-matchとrisingはfirmware上のoverflowこそ0だったが、1 MiB終了時点でqueueが32 / 43まで蓄積し、実効処理rateが25.013 / 24.107 MB/sへ低下した。有限長の終了に救われた状態であり、継続可能とは判定しない。64 KiB DMA ring内のbuffer位置をqueueで参照する方式なので、深い滞留は未処理位置が再利用される危険もある。

32 MHzではno-matchとrisingが14 / 28件overflowした。patternもoverflow 0ながらqueue 39、実効27.955 MB/sで入力に追従しなかった。

検索単体throughputは、no-match 29.885〜32.103、rising 28.637〜30.690、pattern 34.614〜37.176 MB/sだった。取り込み・copyを含めた持続可能rateはこれより低い。

## 判定

**全3条件を同じcapabilityとして提供できる最高rateは、今回の4 MHz刻みでは24 MHz。最初の不成立rateは28 MHz。** pattern条件だけなら28 MHzも成立したが、共通仕様の基本trigger tierは最も重い条件に合わせて24 MHzとするのが安全である。

pre/post triggerの次実験は、trigger処理以外のbuffer制御余裕も残すため24 MHz以下を使える。実装例の保守的な初期値として20 MHz、測定済み上限として24 MHzを区別する余地がある。

## 事実・候補・未決

**事実**: 16 / 20 / 24 MHzは全3条件でqueue最大1以下、overflow 0、data正常。28 MHzではno-match/risingが入力に追従しなかった。

**候補**: 共通basic-trigger tier 24 MHz、保守的default 20 MHz、pattern-only tier 28 MHz。

**未決**: 24〜28 MHz間の厳密な境界 / pre/post制御の追加負荷 / multi-stage triggerのrate tier。
