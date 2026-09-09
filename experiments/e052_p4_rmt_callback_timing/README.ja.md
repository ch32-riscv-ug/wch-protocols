# E052 ESP32-P4 RMT callbackの発火時刻と間隔

状態: **完了 — 初回48 symbol、以降24 symbolごと。CPU負荷は無関係**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E051](../e051_p4_rmt_partial_threshold/README.ja.md)・[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)

## 問い

**RMTの`on_recv_done`は、armしてから何ms後に最初に発火し、以後どの間隔で何symbolずつ届くのか。PARLIO側のPSRAM copy loopがCPUを飽和させていることは発火に影響するか。**

## 仮説

[E051](../e051_p4_rmt_partial_threshold/README.ja.md)でuser bufferのsymbol数と`mem_block_symbols`はどちらも閾値でないと分かり、回収時間を増やせば発火することだけが確定した。E050とE051の実測callback回数は、buffer基準・block基準・block半分基準のどのmodelとも合わず、長いgate loop周期では常に1回少ない。

回数だけを見ているのが限界なので、callbackごとに`esp_timer_get_time()`と`event->num_symbols`を記録する。これで起動遅延、間隔、1回あたりのsymbol数が直接分かり、どのmodelが正しいか、あるいは別の要因があるかが決まる。

もう一つ、E051で説明できなかった「長いloopで1回少ない」に関わりうる要因を切り分ける。PARLIO側の回収loopは160 MHz・duty 50%の入力に対して約41.7 MB/sでPSRAMへcopyし続けており、CPUを飽和させている。この負荷がRMTの発火を遅らせているなら、copyを止めて`dequeue`だけにすれば発火が早まるはずである。

case:

| # | gate幅 | loop周期 | PARLIO copy | 回収時間 |
|---:|---:|---:|---|---:|
| 1 | 6,000 | 2.4 ms | する | 400 ms |
| 2 | 6,000 | 2.4 ms | **しない**(dequeueのみ) | 400 ms |
| 3 | 1,000 | 0.4 ms | する | 400 ms |

case 1と2の差でCPU負荷の影響が出る。case 3はE050で9回発火した短いloopで、間隔とsymbol数の比較対象になる。

## 反証条件

- callbackが1回も発火しない(400 msで発火することはE051で確認済み)
- 記録した時刻が単調でない、またはsymbol数が0
- case 1と2で発火時刻が変わらない(CPU負荷は無関係)かつ間隔がどのmodelとも合わない
- copyを止めたcaseでPARLIO側のqueueが溢れる(dequeueが追いつかない)

## 方法

[E051](../e051_p4_rmt_partial_threshold/README.ja.md)の構成を使い、callbackごとの記録を追加する。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 9、`resolution_hz` = 20,000,000、`mem_block_symbols` = 48、user buffer 32 symbol、`en_partial_rx=true`
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは`gate幅 × 2`で先頭`gate幅`だけbit 7をhigh(duty 50%)
- `rmt_receive`の直前の時刻を基準にし、callback内で`esp_timer_get_time()`と`num_symbols`を先頭16回まで退避する
- destinationは4 MiB PSRAM。case 2ではmemcpyを行わずdequeueだけを続ける

期待durationはgate 6,000でhigh / lowともに24,000 tick、gate 1,000で4,000 tickである。

APIの不成立も結果として最後まで記録する。

## 対象外

RMT分解能の掃引、`signal_range_max_ns`の掃引、`en_partial_rx=false`との比較、DMA modeのRMT、他coreへのISR割り当て、sample rateとdutyの掃引。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、gate幅、copyの有無、各API結果、callback数、先頭16回の発火時刻(armからの相対us)と`num_symbols`、level別duration min / max、PARLIO側のcallback数・dequeue数・queue overflow・回収byte・飛びの数と階差一致数、経過us。

## 完了条件

起動遅延・間隔・1回あたりsymbol数を数値で確定し、E050とE051の回数の食い違いが説明できるかを判定する。CPU負荷の影響の有無も確定する。

## 影響

発火条件が数値で決まれば、qualificationのwindow timestampを実装するときにcallback遅延を設計値として扱える。CPU負荷が効くなら、回収loopとtimestamp回収を別coreへ分ける必要があることになる。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のRMT設定指針を経験則から数値へ置き換えられる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E052_20260909T092437Z_default/test_rmt_callback_timing/dut.log`

| # | gate幅 | symbol周期 | copy | callback数 | 最初の発火 | 以降の間隔 | 1回のsymbol数 |
|---:|---:|---:|---|---:|---:|---:|---:|
| 1 | 6,000 | 2.4 ms | する | 5 | **116,209 us** | 57,598〜57,605 us | **24** |
| 2 | 6,000 | 2.4 ms | **しない** | 5 | **116,212 us** | 57,590〜57,610 us | **24** |
| 3 | 1,000 | 0.4 ms | する | 40 | **19,398 us** | 9,600 us | **24** |

発火時刻の実測列:

```
case 1: 116209 173814 231412 289009 346610
case 2: 116212 173802 231402 289012 346605
case 3: 19398 28998 38598 48198 57798 67398 76998 86598 96198 105798 ...
```

**規則が確定した。**

```
1 callbackあたりのsymbol数 = mem_block_symbols ÷ 2 = 24
最初の発火 = mem_block_symbols 個(48)溜まった時点
以降の間隔 = mem_block_symbols ÷ 2 個(24)ごと
```

- 間隔は24 symbol分そのものである。gate 6,000は24 × 2.4 ms = 57.6 ms、gate 1,000は24 × 0.4 ms = 9.6 msで、実測と一致する
- 最初の発火だけは間隔の2倍、つまり48 symbol分である。gate 6,000で48 × 2.4 = 115.2 ms(実測116.2)、gate 1,000で48 × 0.4 = 19.2 ms(実測19.4)。差の約1 msはarmしてから最初のedgeが来るまでの待ちで説明できる
- `mem_block_symbols`は48なので、この48と24はhardware memory blockの全体とping-pongの半分に対応する

**CPU負荷は無関係だった。** copyの有無で最初の発火は116,209 usと116,212 us、差は3 usである。以降の間隔も同一である。PARLIO側が160 MHz・duty 50%の入力を41.7 MB/sでPSRAMへcopyし続けてCPUを飽和させていても、RMTの発火は変わらない。**「PSRAM copy loopがRMTを遅らせている」という仮説は反証された。**

case 2はcopyを止めたのでPARLIO側の検証はできないが(`harvested=0`、`violations=0`)、`dequeue`は8,304回でcase 1と同じであり、`inflight_max`は2,560 byteに下がっていた。負荷を外した対照として機能している。

durationは全caseで期待値と完全一致した(gate 6,000でhigh / lowともに24,000 tick、gate 1,000で4,000 tick、いずれもmin = max)。

## 判定

**この規則でE045からE052までの全観測が説明できる。** 回数だけを見ていた実験の食い違いはすべて「最初の発火が48 symbol分」という一点に帰着する。

| 実験 | symbol周期 | 回収時間 | 予測(48 → 以降24ごと) | 実測 |
|---|---:|---:|---:|---:|
| [E045](../e045_p4_gate_rmt_order/README.ja.md) | 1.6384 ms | 50 ms | 78.6 ms > 50 → 0 | 0 |
| E045 | 1.6384 ms | 300 ms | 78.6 + k×39.3 → 6 | **6** |
| [E046](../e046_p4_gate_variable_width/README.ja.md) | 0.8192 ms | 150 ms | 39.3 + k×19.7 → 6 | **6** |
| [E047](../e047_p4_gate_three_way_share/README.ja.md) | 0.8192 ms | 150 ms | 同 → 6 | **6** |
| [E048](../e048_p4_gated_rate_ceiling/README.ja.md) 全6 rate | 0.8192 ms | 150 ms | 同 → 6 | **6** |
| [E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md) gate 1,000 | 0.4 ms | 100 ms | 19.2 + k×9.6 → 9 | **9** |
| E050 gate 2,000 | 0.8 ms | 100 ms | 38.4 + k×19.2 → 4 | **4** |
| E050 gate 4,000 | 1.6 ms | 100 ms | 76.8、次は115.2 → 1 | **1** |
| E050 gate 6,000 | 2.4 ms | 100 ms | 115.2 > 100 → 0 | **0** |
| E050 gate 7,000 | 2.8 ms | 100 ms | 134.4 > 100 → 0 | **0** |
| [E051](../e051_p4_rmt_partial_threshold/README.ja.md) buffer 8 | 2.4 ms | 100 ms | 115.2 > 100 → 0 | **0** |
| E051 block 96 | 2.4 ms | 100 ms | 96 × 2.4 = 230 > 100 → 0 | **0** |
| E051 / E052 400 ms | 2.4 ms | 400 ms | 116.2 + k×57.6、6回目は404 > 400 → 5 | **5** |
| E052 gate 1,000 | 0.4 ms | 400 ms | 19.4 + k×9.6 → 40 | **40** |

14条件すべてが一致する。user bufferのsymbol数は関与しない(E051 case 2が示したとおり)。`mem_block_symbols`は間隔と初回遅延の両方を決めるので、E051 case 4の0回もこの規則で説明できる。

実装への帰結は次のとおりである。

- **window timestampの初回遅延 = `mem_block_symbols` × gate周期。** `mem_block_symbols` 48、gate周期2.4 msなら115 msである
- **以降の更新間隔 = その半分。** 24 × gate周期
- **P4のnon-DMA modeでは`mem_block_symbols`の最小が48**(`SOC_RMT_MEM_WORDS_PER_CHANNEL`)なので、初回遅延を縮めるにはDMA modeを試すしかない
- **CPU負荷では変わらない。** 回収loopと同じcoreで動かしてよい
- gate周期が遅い信号では初回遅延が数百msに達する。捕り始めの数十windowのtimestampが遅れて届くだけで、durationの正確さは損なわれない

## 事実・候補・未決

**事実**

1. **1 callbackあたりのsymbol数は全caseで24で、`mem_block_symbols` 48の半分である。**
2. **最初の発火は48 symbol分、以降の間隔は24 symbol分である。** gate 6,000で116,209 us(予測115.2 ms)と57,598〜57,605 us、gate 1,000で19,398 us(予測19.2 ms)と9,600 us。差の約1 msはarmから最初のedgeまでの待ちで説明できる。
3. **CPU負荷は無関係。** PSRAM copyの有無で最初の発火は116,209 usと116,212 us、差3 us。以降の間隔も同一。「copy loopがRMTを遅らせている」は反証された。
4. durationは全caseで期待値と完全一致した(gate 6,000で24,000 tick、gate 1,000で4,000 tick、min = max)。
5. **この規則でE045からE052までの14条件すべての実測callback回数が説明できる。** E045とE051で未特定だった閾値はこれで確定した。user bufferのsymbol数は関与しない。

**候補**: qualificationのwindow timestampの設計値を`初回遅延 = mem_block_symbols × gate周期`、`更新間隔 = その半分`として扱う。回収loopと同じcoreで動かしてよい。初回遅延を縮めたいならDMA modeを検討する。

**未決**: DMA mode(`flags.with_dma`)で`mem_block_symbols`を小さくできるか / `en_partial_rx=false`のときの発火条件 / 48 symbol溜まる前にreceiveを止めた場合に取れる分だけ回収する方法 / RMT分解能を落としたときの挙動。

## 反映

- [E045](../e045_p4_gate_rmt_order/README.ja.md)・[E051](../e051_p4_rmt_partial_threshold/README.ja.md): 閾値が確定したことを追記する
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): RMT設定指針を経験則から数値の規則へ置き換える
- [LEDGER](../LEDGER.ja.md): E052の節
