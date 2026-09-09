# E051 ESP32-P4 RMT partial受信が通知される条件

状態: **完了 — buffer・blockは閾値でない。正体は未特定**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)・[E049](../e049_p4_gated_window_absorption/README.ja.md)・[E045](../e045_p4_gate_rmt_order/README.ja.md)

## 問い

**gate loop周期が2.4 msのとき、RMT RXが`on_recv_done`を発火する条件はuser bufferのsymbol数か、`mem_block_symbols`か、回収時間か。**

## 仮説

[E045](../e045_p4_gate_rmt_order/README.ja.md)で、`en_partial_rx`のcallbackはuser bufferが埋まったときに起きると結論した(32 symbol × loop周期1.6384 ms = 52.43 msが必要で、51.3 msでは届かなかった)。しかし[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)はこの説明では合わない結果を出した。

| 実験 | gate | loop周期 | 回収時間 | 到達symbol数 | buffer 32の所要 | 実測callback |
|---|---:|---:|---:|---:|---:|---:|
| E050 | 1,000 | 0.4 ms | 100 ms | 250 | 12.8 ms | 9 |
| E050 | 2,000 | 0.8 ms | 100 ms | 125 | 25.6 ms | 4 |
| E050 | 4,000 | 1.6 ms | 100 ms | 62 | 51.2 ms | 1 |
| E050 | 6,000 | 2.4 ms | 100 ms | 41 | 76.8 ms | **0** |
| E050 | 7,000 | 2.8 ms | 100 ms | 35 | 89.6 ms | **0** |

buffer 32 symbolが閾値なら、gate 6,000は41 symbol到達で76.8 msに1回発火するはずである。実測は0である。逆に`mem_block_symbols` 48が閾値なら41 < 48で0は説明できるが、gate 1,000の9回とgate 2,000の4回(symbol/callbackが約28〜31)が説明できない。**どちらの単独仮説も5点を説明しない。**

[E049](../e049_p4_gated_window_absorption/README.ja.md)で`signal_range_max_ns`への到達を疑ったが、E050のgate 6,000はhigh 24,000 tickで閾値32,000を大きく下回るのでこれも成り立たない。

そこでgateをE050で失敗したgate 6,000(loop 2.4 ms、duty 50%)に固定し、三つの候補を1つずつ動かす。

| # | user buffer | `mem_block_symbols` | 回収時間 | 期待所要 | この候補が真なら |
|---:|---:|---:|---:|---:|---|
| 1 | 32 symbol | 48 | 100 ms | — | 0回(E050の再現) |
| 2 | **8 symbol** | 48 | 100 ms | 8 × 2.4 = 19.2 ms | 発火する |
| 3 | 32 symbol | 48 | **400 ms** | 32 × 2.4 = 76.8 ms | 発火する |
| 4 | 32 symbol | **96** | 100 ms | 48 × 2.4 = 115 ms | 0回のまま |

case 2が発火すればuser bufferが閾値で、E050のgate 6,000が0だったのは別の要因(bufferを埋める前に何かが止めている)になる。case 3だけが発火すれば単に時間が足りていなかったことになる。case 4がcase 1と同じなら`mem_block_symbols`は関与しない。

PARLIO側のgated captureも全caseで並走させ、RMTの設定を変えてもcapture側が壊れないことを確認する。

## 反証条件

- 4条件すべてで0回(原因がこの三つの外)
- case 1が発火する(E050が再現しない)
- 発火してもdurationが期待値(high / low 24,000 tick)から外れる
- RMTの設定変更でPARLIO側のgated captureが壊れる

## 方法

E050の構成からgate幅を6,000に固定し、RMTの設定と回収時間だけを変える。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 9、`resolution_hz` = 20,000,000、`signal_range_min_ns` = 500、`signal_range_max_ns` = 1,600,000、`en_partial_rx=true`
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは12,000 wordで先頭6,000だけbit 7をhigh(duty 50%)。bit 0〜6は`gray7(index & 0x7F)`
- destinationは4 MiB PSRAM

期待durationはhigh 24,000 tick、low 24,000 tickである。

APIの不成立も結果として最後まで記録する。

## 対象外

RMT分解能の掃引、`signal_range_max_ns`の掃引、DMA modeのRMT、gate幅とdutyの掃引、`en_partial_rx=false`との比較、sample rateの掃引。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、user buffer symbol数、`mem_block_symbols`、回収時間、各API結果、RMT callback数、退避symbol数、level別duration count / min / max、期待duration、PARLIO側のring未読最大・queue overflow・bit 7が0のsample数・飛びの数と階差一致数、回収byte、経過us。

## 完了条件

4条件のうちどれで発火するかを記録し、閾値がuser buffer・`mem_block_symbols`・回収時間のどれかを確定する。どれも発火しない場合は原因がこの三つの外にあることを確定して完了とする。

## 影響

閾値が分かれば、qualificationのwindow timestampをgate周期の遅い信号でも取れるようになる。callback遅延 = 閾値 × gate周期なので、実装ではbuffer sizeを応答要件から決める。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のqualification記述にRMT設定の指針を書ける。

## 結果

実施日: 2026-09-09

採用run: `_runs/E051_20260909T091955Z_default/test_rmt_partial_threshold/dut.log`

| # | user buffer | `mem_block_symbols` | 回収時間 | bufferを埋める所要 | rmt_callbacks | duration |
|---:|---:|---:|---:|---:|---:|---|
| 1 | 32 symbol | 48 | 100 ms | 76.8 ms | **0** | — |
| 2 | **8 symbol** | 48 | 100 ms | **19.2 ms** | **0** | — |
| 3 | 32 symbol | 48 | **400 ms** | 76.8 ms | **5** | high / low ともに24,000 tick固定 |
| 4 | 32 symbol | **96** | 100 ms | 76.8 ms | **0** | — |

**候補を二つ消せた。**

- **user bufferのsymbol数は閾値ではない。** case 2はbufferを8 symbolにして所要19.2 msまで下げたのに、100 msで0回だった。bufferが閾値ならここで発火する
- **`mem_block_symbols`も閾値ではない。** case 4で48から96へ倍にしても、case 1と同じ0回である

**時間を増やせば発火する。** case 3は400 msで5回発火し、durationはhigh / lowともに24,000 tick固定で期待値と完全一致した(min = max)。取れたdata自体は正しい。

PARLIO側は4条件すべてで同一かつ正常だった(飛び22、階差一致15 / 15、queue overflow 0、bit 7が0のsample 0)。**RMTの設定を変えてもgated captureは影響を受けない。**

## 判定

**[E045](../e045_p4_gate_rmt_order/README.ja.md)で立てた「`en_partial_rx`のcallbackはuser bufferが埋まったときに起きる」という規則は一般則としては成り立たない。** E045の数値には合っていたが、case 2が直接反証した。`mem_block_symbols`も関与しない。残ったのは「時間を増やせば発火する」という事実だけで、**閾値の正体は特定できていない。**

三つの実験の数値を並べると、単純なmodelはどれも合わない。

| 実験 | gate loop周期 | 回収時間 | 実測callback | buffer 32基準の予測 | block 48基準 | block半分(24)基準 |
|---|---:|---:|---:|---:|---:|---:|
| E050 | 0.4 ms | 100 ms | 9 | 7.8 | 5.2 | 10.4 |
| E050 | 0.8 ms | 100 ms | 4 | 3.9 | 2.6 | 5.2 |
| E050 | 1.6 ms | 100 ms | 1 | 1.9 | 1.3 | 2.6 |
| E051 | 2.4 ms | 100 ms | **0** | 1.3 | 0.9 | 1.7 |
| E051 | 2.4 ms | 400 ms | 5 | 5.2 | 3.5 | 6.9 |

短いloopではblock半分(24 symbol)基準に近く、長いloopでは**どのmodelよりも1回少ない**。固定の起動遅延を1つ足しても、短いloop側の回数が合わなくなる。

実用上の規則は経験的なものに留める。

- **gate loop周期が1.6 ms以下なら、100 msの回収でsymbolが取れる**
- **2.4 ms以上では100 msでは取れず、400 msで取れる**
- 取れたdurationはどの条件でも正確である

次に測るべきものは明確である。callbackの**発火時刻**を`esp_timer`で記録すれば、起動遅延と間隔が直接分かり、この表の食い違いが解ける。本実験は回数しか見ていないのでそこまで届いていない。

## 記録した不具合(修正済み)

最初のrunで二つのinstrumentation不具合が出た。どちらも計測値ではなく表示と副作用の問題で、修正後の採用runでは消えている。失敗runは`_runs/E051_20260909T091755Z_default/`に残した。

1. **期待値表示の32 bit溢れ。** `gate_words * kRmtResolutionHz / kSourceRateHz`を左から評価すると6,000 × 20,000,000 = 1.2e11が`size_t`を溢れ、`expected_high`が807と表示された。実測durationは24,000で正しい。除算を先に行う形へ直した
2. **cache line非整列のmsync。** [E049](../e049_p4_gated_window_absorption/README.ja.md)から引き継いだ`build_pattern`が`loop_words`(12,000 byte)だけをmsyncしており、64 byte境界に載らないため`esp_cache_msync`がerror logを出していた。buffer全体(16,384 byte)をsyncする形へ直した。**E049とE050でも同じerrorが出ていたが、両実験のdataは検証を通っている**ので、この環境ではflushが失敗してもCPUの書き込みはDMAから見えていたことになる。E049・E050の結論は変わらない

## 事実・候補・未決

**事実**

1. **user bufferを8 symbol(所要19.2 ms)にしても100 msの回収でcallbackは0回だった。** user bufferのsymbol数は発火の閾値ではない。
2. **`mem_block_symbols`を48から96へ変えても0回のままだった。** これも閾値ではない。
3. 回収時間を400 msにすると5回発火し、durationはhigh / lowともに24,000 tick固定で期待値と完全一致した。
4. PARLIO側のgated captureは4条件すべてで同一かつ正常(飛び22、階差一致15 / 15、overflow 0、bit 7が0のsample 0)。RMTの設定はcaptureに影響しない。
5. E045の「callbackはuser bufferが埋まったときに起きる」は一般則として反証された。
6. E050とE051の実測callback回数は、buffer基準・block基準・block半分基準のどのmodelとも合わない。長いloopでは常に1回少ない。
7. instrumentationの不具合を2件見つけて直した(32 bit溢れの期待値表示、cache line非整列のmsync)。後者はE049・E050にも存在したが、両実験のdataは検証を通っている。

**候補**: 実装では経験的な規則を使う。gate loop周期1.6 ms以下なら100 ms程度の回収でwindow timestampが取れる。2.4 ms以上では回収時間を数百msへ伸ばす。durationの正確さは条件に依存しない。

**未決**: **callbackの発火時刻を記録して起動遅延と間隔を直接測る**(この表の食い違いを解く鍵) / RMT ISRとPARLIOのPSRAM copy loopの競合の有無 / `en_partial_rx=false`との比較 / RMT分解能を落としたときの挙動 / gate loop周期1.6〜2.4 msの間の境界。

## 反映

- [E045](../e045_p4_gate_rmt_order/README.ja.md): user buffer基準の規則が一般則ではないことを追記する
- [E049](../e049_p4_gated_window_absorption/README.ja.md)・[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md): msync非整列errorが出ていたが結論に影響しないことを追記する
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): qualificationのRMT設定指針を経験的な規則として書く
- [LEDGER](../LEDGER.ja.md): E051の節

## 追記 — E052で閾値が確定(2026-09-09)

本レポートは書き換えない。[E052](../e052_p4_rmt_callback_timing/README.ja.md)がcallbackごとの発火時刻と`num_symbols`を記録し、規則が確定した。

```
1 callbackあたりのsymbol数 = mem_block_symbols ÷ 2
最初の発火 = mem_block_symbols 個溜まった時点
以降の間隔 = mem_block_symbols ÷ 2 個ごと
```

`mem_block_symbols` 48では、初回が48 symbol分、以降24 symbol分である。user bufferのsymbol数は関与しない。CPU負荷も無関係である(PSRAM copyの有無で発火時刻の差は3 us)。この規則でE045からE052までの14条件すべての実測callback回数が説明できる。
