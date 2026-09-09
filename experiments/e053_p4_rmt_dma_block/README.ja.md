# E053 ESP32-P4 RMT DMA modeで初回遅延を縮められるか

状態: **完了 — DMAでは縮まらない。規則が完成し、E051の解釈を訂正**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E052](../e052_p4_rmt_callback_timing/README.ja.md)・[E051](../e051_p4_rmt_partial_threshold/README.ja.md)

## 問い

**RMT RXを`flags.with_dma=1`にすると`mem_block_symbols`を48より小さくでき、window timestampの初回遅延を縮められるか。E052で確定した発火規則はDMA modeでも同じか。**

## 仮説

[E052](../e052_p4_rmt_callback_timing/README.ja.md)で発火規則が確定した。

```
1 callbackあたりのsymbol数 = mem_block_symbols ÷ 2
初回遅延 = mem_block_symbols × gate周期
以降の間隔 = (mem_block_symbols ÷ 2) × gate周期
```

non-DMA modeでは`mem_block_symbols`がhardware memory blockの個数を意味し、P4の`SOC_RMT_MEM_WORDS_PER_CHANNEL`は48なので48が最小である。gate周期2.4 msでは初回遅延が115 msになる。

headerは`mem_block_symbols`について「DMA modeではこのfieldがDMA buffer sizeを制御し、大きな値(例えば1024)に設定できる」と書いている。DMA modeならhardware memory blockの粒度から外れるので、**小さい値も指定できて初回遅延が縮む可能性がある**。`SOC_RMT_SUPPORT_DMA`は1でP4はDMAに対応している。

E052の規則がDMA modeでも成り立つなら、gate周期2.4 msに対して予測は次のようになる。

| `mem_block_symbols` | 予測初回遅延 | 予測間隔 | 400 msでの予測callback数 |
|---:|---:|---:|---:|
| 8 | 19.2 ms | 9.6 ms | 約40 |
| 16 | 38.4 ms | 19.2 ms | 約19 |
| 64 | 153.6 ms | 76.8 ms | 約4 |
| 48(non-DMA、対照) | 115.2 ms | 57.6 ms | 5 |

DMA modeで規則が変わるなら、この予測から外れる形で分かる。

## 反証条件

- `flags.with_dma=1`で`rmt_new_rx_channel`が失敗する
- 小さい`mem_block_symbols`が拒否される
- DMA modeでcallbackが発火しない、またはdurationが期待値から外れる
- 発火時刻がE052の規則から外れる
- RMTのDMAがPARLIO側のDMAと衝突してcaptureが壊れる

## 方法

[E052](../e052_p4_rmt_callback_timing/README.ja.md)の構成からgateとsample rateを固定し、RMTの`flags.with_dma`と`mem_block_symbols`だけを変える。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 9、`resolution_hz` = 20,000,000、user buffer 128 symbol(全条件の`mem_block_symbols`以上)、`en_partial_rx=true`。bufferはDMA可能なinternal RAMに置く
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは12,000 wordで先頭6,000だけbit 7をhigh(duty 50%、symbol周期2.4 ms)
- 回収は400 msの固定window、destinationは4 MiB PSRAM。PARLIO側のcopyも行う
- callbackごとに`esp_timer_get_time()`と`num_symbols`を先頭16回まで記録する

case:

| # | `with_dma` | `mem_block_symbols` |
|---:|---:|---:|
| 1 | 0 | 48(E052の対照) |
| 2 | **1** | **8** |
| 3 | **1** | **16** |
| 4 | **1** | **64** |

期待durationはhigh / lowともに24,000 tickである。

APIの不成立も結果として最後まで記録する。

## 対象外

RMT分解能の掃引、`en_partial_rx=false`との比較、gate周期とdutyの掃引、sample rateの掃引、DMA channel数の上限確認、`mem_block_symbols`の更に細かい掃引。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、`with_dma`、`mem_block_symbols`、各API結果、callback数、先頭16回の発火時刻(armからの相対us)と`num_symbols`、level別duration min / max、E052の規則からの予測初回遅延と間隔、PARLIO側のcallback数・queue overflow・回収byte・飛びの数と階差一致数、経過us。

## 完了条件

DMA modeが受理されるか、`mem_block_symbols`をどこまで小さくできるか、発火規則がE052と同じかを確定する。DMA modeが使えない場合は初回遅延の下限が`48 × gate周期`であることを確定して完了とする。

## 影響

初回遅延を縮められるなら、qualificationのwindow timestampを応答性の要る用途にも使える。使えないなら、初回遅延`48 × gate周期`を仕様として受け入れ、hostへ渡すformatで「最初の数十windowのtimestampは遅れて届く」ことを前提にする必要がある。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のRMT設定指針が確定する。

## 結果

実施日: 2026-09-09

採用run: `_runs/E053_20260909T093735Z_default/test_rmt_dma_block/dut.log`

| # | `with_dma` | `mem_block_symbols` | `rmt_new_rx_channel` | callback数 | 発火時刻 | 1回のsymbol数 |
|---:|---:|---:|---|---:|---|---:|
| 1 | 0 | 48 | `ESP_OK` | 1 | 346,598 us | **120** |
| 2 | **1** | **8** | **`ESP_ERR_INVALID_ARG`** | 0 | — | — |
| 3 | **1** | **16** | **`ESP_ERR_INVALID_ARG`** | 0 | — | — |
| 4 | 1 | 64 | `ESP_OK` | 2 | 156,935 / 310,536 us | **64** |

**DMA modeは小さい`mem_block_symbols`を拒否した。** 8と16はどちらも`ESP_ERR_INVALID_ARG`である。64は受理された。

**DMA modeの初回遅延はnon-DMAより悪い。** 受理された64での初回発火は156,935 us、間隔は153,601 us(= 64 × symbol周期2.4 ms)である。non-DMAの48では初回115.2 ms相当なので、**DMA modeでは初回遅延を縮められない。**

DMA modeでは1 callbackが`mem_block_symbols`個そのもの(64 symbol)を運び、間隔も`mem_block_symbols`分である。non-DMAの半分ずつという挙動とは違う。

**対照のcase 1がE052と違う結果を出した。** E052は同じnon-DMA・block 48で5回・各24 symbolだったが、本実験は1回・120 symbolである。違いは**user bufferのsymbol数**で、E052は32、本実験は128(全条件の`mem_block_symbols`以上にするため)である。

これで規則が完成する。

```
group = mem_block_symbols ÷ 2            （non-DMA）
n = floor(user_buffer ÷ group)           ← n = 0 なら永久に発火しない
1 callbackあたりのsymbol数 = n × group
初回発火 = (mem_block_symbols + (n − 1) × group) × symbol周期
以降の間隔 = n × group × symbol周期
```

case 1で確かめると、group 24、n = floor(128 ÷ 24) = 5、1回あたり120 symbol、初回は(48 + 4 × 24) × 2.4 ms = 345.6 ms(実測346.6)、間隔は120 × 2.4 = 288 msなので400 msでは1回だけ、となりすべて一致する。

## 判定

**DMA modeでは初回遅延を縮められない。** 8と16は拒否され、受理された64はnon-DMAの48より遅い。初回遅延の下限は現状`48 × symbol周期`である。ただし**DMA modeの32は本実験で試していない**ので、そこだけ確認の余地が残る。

より重要な副産物として、**user bufferが`mem_block_symbols ÷ 2`未満だとcallbackが永久に発火しない**ことが分かった。これで[E051](../e051_p4_rmt_partial_threshold/README.ja.md)が説明できなかった二つのcaseが解ける。

| E051のcase | user buffer | `mem_block_symbols` | group | n | 実測 |
|---|---:|---:|---:|---:|---:|
| buffer 8 | 8 | 48 | 24 | **0** | 0回(時間に関係なく発火しない) |
| block 96 | 32 | 96 | 48 | **0** | 0回(同) |

E051はこの二つを「buffer / blockは閾値ではない」「時間が足りない」と解釈したが、**正しくは`n = 0`で発火不能だった。** user bufferは発火のtriggerではないが、`group`以上でなければならないという**必要条件**である。

E045からE053までの全観測がこの規則で説明できる。

| 実験 | buffer | block | symbol周期 | n | 予測初回 / 間隔 | 回収 | 予測回数 | 実測 |
|---|---:|---:|---:|---:|---|---:|---:|---:|
| [E045](../e045_p4_gate_rmt_order/README.ja.md) | 32 | 48 | 1.6384 ms | 1 | 78.6 / 39.3 ms | 50 ms | 0 | **0** |
| E045 | 32 | 48 | 1.6384 ms | 1 | 78.6 / 39.3 ms | 300 ms | 6 | **6** |
| [E046](../e046_p4_gate_variable_width/README.ja.md)・[E047](../e047_p4_gate_three_way_share/README.ja.md)・[E048](../e048_p4_gated_rate_ceiling/README.ja.md) | 32 | 48 | 0.8192 ms | 1 | 39.3 / 19.7 ms | 150 ms | 6 | **6** |
| [E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md) gate 1,000〜7,000 | 32 | 48 | 0.4〜2.8 ms | 1 | 48T / 24T | 100 ms | 9/4/1/0/0 | **9/4/1/0/0** |
| E051 buffer 8 | 8 | 48 | 2.4 ms | **0** | 発火しない | 100 ms | 0 | **0** |
| E051 block 96 | 32 | 96 | 2.4 ms | **0** | 発火しない | 100 ms | 0 | **0** |
| E051・[E052](../e052_p4_rmt_callback_timing/README.ja.md) | 32 | 48 | 2.4 ms | 1 | 115.2 / 57.6 ms | 400 ms | 5 | **5** |
| E052 gate 1,000 | 32 | 48 | 0.4 ms | 1 | 19.2 / 9.6 ms | 400 ms | 40 | **40** |
| E053 case 1 | **128** | 48 | 2.4 ms | **5** | 345.6 / 288 ms | 400 ms | 1 | **1** |

16条件すべてが一致する。

実装への帰結。

- **user bufferは`mem_block_symbols ÷ 2`以上にする。** 満たさないとcallbackは来ない
- **応答性を最優先するならuser bufferを`mem_block_symbols ÷ 2`ちょうどにする。** 初回遅延が最小の`mem_block_symbols × symbol周期`になり、更新間隔も最小の`(mem_block_symbols ÷ 2) × symbol周期`になる
- **user bufferを大きくすると1回あたりのsymbol数は増えるが、初回遅延と間隔も同じ比率で伸びる。** 割り込み回数を減らしたいときだけ広げる
- **DMA modeは使わない。** 小さいblockが拒否され、初回遅延も悪い

## 事実・候補・未決

**事実**

1. **DMA modeは`mem_block_symbols` 8と16を`ESP_ERR_INVALID_ARG`で拒否した。** 64は受理された。
2. **DMA modeの初回遅延はnon-DMAより悪い。** block 64で初回156,935 us、間隔153,601 us(= 64 × symbol周期)。1 callbackは64 symbolを運ぶ。non-DMAの48では初回115.2 ms相当である。
3. **non-DMAでuser bufferを32から128へ変えると挙動が変わった。** E052は5回・各24 symbolだったが、本実験は1回・120 symbolである。
4. **規則が完成した。** `group = mem_block ÷ 2`、`n = floor(buffer ÷ group)`、1回あたり`n × group` symbol、初回`(mem_block + (n−1) × group) × 周期`、間隔`n × group × 周期`。case 1は予測345.6 msに対し実測346.6 msである。
5. **`n = 0`だとcallbackは永久に発火しない。** これでE051のbuffer 8とblock 96の両caseが説明できる。E051の「buffer / blockは閾値ではない」「時間が足りない」という解釈は正しくなく、発火不能だった。
6. E045からE053までの16条件すべてがこの規則で説明できる。
7. PARLIO側はRMTが失敗したcaseを除きすべて正常だった(飛び22、階差一致15 / 15、overflow 0、bit 7が0のsample 0)。

**候補**: user bufferを`mem_block_symbols ÷ 2`ちょうどにして初回遅延と更新間隔を最小化する。DMA modeは使わない。

**未決**: **DMA modeの`mem_block_symbols` 32が受理されるか**(受理されれば初回遅延76.8 msでnon-DMAの115.2 msより良い可能性) / `en_partial_rx=false`の発火条件 / 48 symbol溜まる前に`rmt_disable`して取れる分だけ回収できるか / RMT分解能を落としたときの挙動。

## 反映

- [E051](../e051_p4_rmt_partial_threshold/README.ja.md)・[E052](../e052_p4_rmt_callback_timing/README.ja.md): 規則が完成したこと、E051の解釈が誤りだったことを追記する
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): RMT設定指針を完成した規則へ置き換える
- [LEDGER](../LEDGER.ja.md): E053の節
