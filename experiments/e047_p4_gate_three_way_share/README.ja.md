# E047 ESP32-P4 data線・valid線・RMT RXを同一GPIOで3者共有する

状態: **完了 — 3者共有成立。8 pinで qualification + timestamp**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E046](../e046_p4_gate_variable_width/README.ja.md)・[E041](../e041_p4_parlio_shared_valid_line/README.ja.md)・[E045](../e045_p4_gate_rmt_order/README.ja.md)

## 問い

**1本のGPIOをPARLIOのdata線・PARLIOのvalid線・RMT RXの入力の3つへ同時に渡して、8 channel + capture qualification + window timestampが8 pinで成立するか。**

## 仮説

ここまでで2種類の共有が別々に成立している。

- [E041](../e041_p4_parlio_shared_valid_line/README.ja.md): `valid_gpio_num`をdata線と同一GPIOにできる。共有した線はdataとしても正しく取得され、hardware triggerはchannelを消費しない
- [E045](../e045_p4_gate_rmt_order/README.ja.md): gate線をRMT RXへも渡せる。生成順の制約は無く、durationはsample単位で正しい

どちらもGPIO matrixが1つの入力GPIOを複数のperipheral入力信号へfan-outできることに依っている。3者でも同じはずである。成立すれば[E046](../e046_p4_gate_variable_width/README.ja.md)で確定したqualificationが、capture channelもpinも追加せずに使えることになる。

data_width 8にすると1 sample = 1 byte、RMT分解能20 MHzで1 tick = 1 sampleなので、**window byte長 = RMT high duration**が割り算なしで成り立つ。E046のdata_width 4では÷2が必要だった。

信号源はbit 0〜6に7-bit gray ramp、bit 7にE046と同じ可変幅gateを載せる。bit 7はdata channel 7でありqualifierでもありRMTの入力でもある。gate区間の内側だけを取得するのでbit 7は全sampleで1が期待値であり、これが「共有した線がdataとしても読めている」ことの直接の確認になる。

gapのindex差1,701 / 2,301 / 2,501 / 5,341は128(gray7の周期)で割った余りが37 / 125 / 69 / 93で、どれも1でないので全境界がrampの飛びとして現れる。

## 反証条件

- 3者共有をdriverが拒否する、またはどれか1つが動かない
- bit 7が0のsampleが出る(共有線がdataとして読めていない)
- gray7 rampにwindow境界以外の飛びが出る
- 飛びの階差がRMT high durationの列と一致しない
- RMTのdurationが4つの期待値の循環にならない

## 方法

E046の構成をdata_width 8へ広げ、valid線をdata線7と共有する。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、**`valid_gpio_num` = GPIO 9(data線7と同一)**、`valid_sig_line_id` = 8、20 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: **`gpio_num` = GPIO 9(同一)**、`resolution_hz` = 20,000,000、`mem_block_symbols` = 48、`signal_range_min_ns` = 500、`signal_range_max_ns` = 1,600,000、`en_partial_rx=true`、buffer 32 symbol
- TX: data_width 8、GPIO 2〜9、5 MHz、16,384 wordを`loop_transmission`
  - bit 0〜6: `gray7(index & 0x7F)`
  - bit 7: index 1,000から300 / 3,000から700 / 6,000から1,500 / 10,000から2,044だけhigh
- RMTはPARLIO TXの後に作る(E045で順序は無関係)
- 回収は150 msの固定window。chunkは256 KiB PSRAMへ連結する

期待値は次のとおりである。

| window | 幅(word) | 期待high(tick) | 期待window(byte) |
|---:|---:|---:|---:|
| 0 | 300 | 1,200 | 1,200 |
| 1 | 700 | 2,800 | 2,800 |
| 2 | 1,500 | 6,000 | 6,000 |
| 3 | 2,044 | 8,176 | 8,176 |

1 loopあたり保存量は18,176 byte、dutyは27.7%である。

検証は三つ行う。全sampleでbit 7が1であること、bit 0〜6を`gray7`として復元してstepが+1 mod 128であること、飛びの階差がRMT high durationの循環列と一致すること。

APIの不成立も結果として最後まで記録する。

## 対象外

gating時の最大sample rate、data_width 16での3者共有、32,767 tickを超えるgap、RMT分解能を落としたときの精度、pulse delimiterとの3者共有、run中の先頭同期の確立、hostへ渡すformatの設計。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

各API結果、RMT callback数、退避symbol数、先頭16 symbolのlevel / duration、level別duration min / max、bit 7が0だったsample数、gray7 step飛びの総数と先頭16個のoffset、回収byte、回収rate、期待duty、経過us。

## 完了条件

3者共有が成立するか否かを確定する。成立する場合はbit 7の読み戻し、gray7の連続性、飛びの階差とRMT durationの一致を記録する。拒否される場合はどのAPIで外れたかを特定して完了とする。

## 影響

成立すれば、8 channel logic capture + hardware qualification + hardware window timestampが**8 pin・追加channel 0・CPU負荷0**で成立することが確定する。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のqualification記述から「未実証」が外れ、実装構成として書ける。

## 結果

実施日: 2026-09-09

採用run: `_runs/E047_20260909T085436Z_default/test_gate_three_way_share/dut.log`

| 項目 | 期待 | 実測 |
|---|---:|---:|
| 全API | `ESP_OK` | すべて`ESP_OK` |
| bit 7が0だったsample | 0 | **0 / 262,144** |
| RMT high duration | 1,200 / 2,800 / 6,000 / 8,176 | 同一(min 1,200、max 8,176) |
| RMT low duration | 6,800 / 9,200 / 10,000 / 21,360 | 同一 |
| 飛びの階差 | high durationの循環 | **15 / 15一致** |
| 1 loop保存量 | 18,176 byte | 18,176 |
| duty | 27.7% | 27% |
| 飛びの総数 | 262,144 ÷ 18,176 × 4 ≒ 58 | 57 |

**1本のGPIO(GPIO 9)がPARLIOのdata線7、PARLIOのvalid線、RMT RXの入力の3役を同時に果たした。** driverはどれも拒否せず、`parlio_new_rx_unit`・`parlio_new_rx_level_delimiter`・`rmt_new_rx_channel`・`rmt_receive`・`parlio_rx_unit_receive`のすべてが`ESP_OK`である。

**共有した線はdataとしても完全に読めている。** bit 7が0だったsampleは262,144 sample中0件だった。gate区間の内側だけを取得しているのでbit 7は常に1が期待値であり、それが全域で満たされた。

RMTのsymbol列はE046と同一である。

```
0:6800 1:2800 0:9200 1:6000 0:10000 1:8176 0:21360 1:1200  ← 以降同じ循環
```

capture data中のgray7飛びのoffsetと階差:

```
offset: 2800 8800 16976 18176 20976 26976 35152 36352 39152 45152 53328 54528 57328 63328 71504 72704
階差:   6000 8176 1200  2800  6000  8176  1200  2800  6000  8176  1200  2800  6000  8176  1200
```

**階差はRMTのhigh durationそのものである。** data_width 8では1 sample = 1 byteで、RMT分解能20 MHzでは1 tick = 1 sampleなので、E046で必要だった÷2が不要になり`window byte長 = high duration`が直接成り立つ。

## 判定

**8 channel logic capture + hardware capture qualification + hardware window timestampは、8 pin・追加channel 0・CPU負荷0で成立する。**

構成は次の1本の線に集約される。

```
GPIO 9(1本)
   ├─→ PARLIO RX data line 7 : channel 7としてsampleに記録される
   ├─→ PARLIO RX valid       : gate区間のsampleだけをDMAでPSRAMへ
   └─→ RMT RX                : 各high / lowの長さをsample単位で記録
```

GPIO 2〜9の8本で8 channelを取り、そのうち1本をqualifierに兼用する。qualifierに選んだchannelはdataとしても残るので情報は失われない。RMTがwindowの長さとgapの長さを別経路で持つので、間引いたsample列から時間軸を再構成できる([E046](e046_p4_gate_variable_width/README.ja.md))。

払うものはRMT RX channel 1つ(P4は4 channelがRX可能)と、1 levelあたり32,767 tickの上限だけである。**data_width 8を選べばhost側の計算も`window byte長 = tick数`で済む。**

qualifierに使うchannelには制約が付く。gate区間の内側では常にactiveなので、そのchannelの波形情報は「activeだった」以外に残らない。逆に言えば、qualifierは「値が変わることに意味がない線」(CS、enable、frame同期)に割り当てるのが正しく、dataとして観測したい線に割り当ててはならない。

## 事実・候補・未決

**事実**

1. GPIO 9をPARLIOのdata線7・PARLIOのvalid線・RMT RXの入力へ同時に割り当てて、全APIが`ESP_OK`になった。
2. **bit 7が0だったsampleは262,144 sample中0件。** 共有した線はdata channelとしても正しく取得される。
3. RMTのhigh / low durationはE046と同一で期待値と完全一致した(high 1,200 / 2,800 / 6,000 / 8,176、low 6,800 / 9,200 / 10,000 / 21,360)。
4. **gray7飛びの階差15箇所すべてがRMT high durationと一致した。** data_width 8では`window byte長 = high duration`が割り算なしで成立する。
5. 1 loop保存量18,176 byte、duty 27%、飛びの総数57は期待どおりだった。

**候補**: 実装の標準構成を「GPIO 8本 = 8 channel、うち1本をqualifier兼用、RMT RX 1 channelでwindow timestamp、data_width 8」とする。qualifierは値の変化に意味のない線(CS / enable / frame同期)へ割り当てる。

**未決**: gating時の最大sample rate / data_width 16での3者共有(`valid_sig_line_id`に空きslotが無い可能性) / 32,767 tickを超えるgapの扱い / RMT分解能を落としたときの精度 / pulse delimiterとの3者共有 / run中の先頭同期の確立 / hostへ渡すformatの設計。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): qualificationの実装構成として8 pin構成を書き、「未実証」を外す
- [LEDGER](../LEDGER.ja.md): E047の節
