# E054 ESP32-P4 RMT DMA modeが受理する`mem_block_symbols`の最小値

状態: **完了 — DMAは48未満を拒否し48も64相当。下限は`48 × 周期`で確定**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E053](../e053_p4_rmt_dma_block/README.ja.md)・[E052](../e052_p4_rmt_callback_timing/README.ja.md)

## 問い

**RMT RXのDMA modeが受理する`mem_block_symbols`の最小値はいくつで、そのときのwindow timestampの初回遅延はnon-DMAの`48 × symbol周期`より良いか。**

## 仮説

[E053](../e053_p4_rmt_dma_block/README.ja.md)でDMA modeは8と16を`ESP_ERR_INVALID_ARG`で拒否し、64を受理した。64のときの初回発火は156,935 us(= 約65 × symbol周期2.4 ms)で、non-DMAの48による115.2 ms相当より遅い。しかし**8と64の間は試していない。**

DMA modeでは1 callbackが`mem_block_symbols`個そのものを運び、初回発火も間隔も`mem_block_symbols × symbol周期`だった。したがって受理される最小値が48未満なら、初回遅延はnon-DMAより短くなる。

| `mem_block_symbols` | 受理されたときの予測初回遅延 | non-DMA 48(115.2 ms)との比較 |
|---:|---:|---|
| 24 | 57.6 ms | **良い** |
| 32 | 76.8 ms | **良い** |
| 40 | 96.0 ms | **良い** |
| 48 | 115.2 ms | 同等 |

24が通ればnon-DMAの半分になる。すべて拒否されるなら、初回遅延の下限は`48 × symbol周期`で確定し、DMA modeはこの用途では使えないと結論できる。

## 反証条件

- 4条件すべてが拒否される(下限は48より上で確定)
- 受理されたのに発火しない、またはdurationが期待値から外れる
- 発火時刻が`mem_block_symbols × symbol周期`から外れる
- RMTのDMAがPARLIO側のcaptureを壊す

## 方法

[E053](../e053_p4_rmt_dma_block/README.ja.md)の構成から`with_dma`を1に固定し、`mem_block_symbols`だけを変える。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 9、`resolution_hz` = 20,000,000、`flags.with_dma=1`、user buffer 128 symbol(E053のDMA caseと同一にして比較可能にする)、`en_partial_rx=true`
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは12,000 wordで先頭6,000だけbit 7をhigh(duty 50%、symbol周期2.4 ms)
- 回収は400 msの固定window、destinationは4 MiB PSRAM
- `mem_block_symbols`掃引: 24 / 32 / 40 / 48

期待durationはhigh / lowともに24,000 tickである。

APIの不成立も結果として最後まで記録する。

## 対象外

non-DMAとの再比較(E053で測済み)、user bufferの掃引、gate周期とdutyの掃引、RMT分解能の掃引、`en_partial_rx=false`との比較。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、`mem_block_symbols`、各API結果、callback数、先頭16回の発火時刻と`num_symbols`、level別duration min / max、`mem_block_symbols × symbol周期`による予測初回遅延、PARLIO側のqueue overflow・回収byte・飛びの数と階差一致数、経過us。

## 完了条件

DMA modeが受理する最小の`mem_block_symbols`を確定し、そのときの初回遅延をnon-DMAの115.2 msと比較する。すべて拒否される場合は下限が48より上であることを確定して完了とする。

## 影響

48未満が通れば、qualificationのwindow timestampの初回遅延を縮める手段が確定する。通らなければ初回遅延`48 × symbol周期`が下限として確定し、[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のRMT設定指針が閉じる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E054_20260909T094215Z_default/test_rmt_dma_block_min/dut.log`

| `mem_block_symbols` | `rmt_new_rx_channel` | callback数 | 初回発火 | 間隔 | 1回のsymbol数 | 予測初回(block × 2.4 ms) |
|---:|---|---:|---:|---:|---:|---:|
| 24 | **`ESP_ERR_INVALID_ARG`** | 0 | — | — | — | 57.6 ms |
| 32 | **`ESP_ERR_INVALID_ARG`** | 0 | — | — | — | 76.8 ms |
| 40 | **`ESP_ERR_INVALID_ARG`** | 0 | — | — | — | 96.0 ms |
| 48 | `ESP_OK` | 2 | **156,934 us** | 153,611 us | **64** | 115.2 ms |

**DMA modeは48未満をすべて拒否した。** 24 / 32 / 40はいずれも`ESP_ERR_INVALID_ARG`である。

**受理された48は64として振る舞う。** 1 callbackが運ぶのは64 symbol、間隔は153,611 us(= 64 × symbol周期2.4 ms)、初回発火は156,934 usである。[E053](../e053_p4_rmt_dma_block/README.ja.md)で`mem_block_symbols` 64を指定したときの値(64 symbol、153,601 us、156,935 us)と**完全に一致する**。つまりDMA modeは48を64へ丸めている。予測初回115.2 msに対し実測156.9 msなのはこのためである。

## 判定

**DMA modeでは初回遅延を縮められない。** 受理される最小値は48だが実効的には64として動くので、初回遅延は常に`64 × symbol周期`である。non-DMAの`48 × symbol周期`より遅い。

**window timestampの初回遅延の下限は`48 × symbol周期`で確定した。** 構成はnon-DMA、`mem_block_symbols` = 48(P4の`SOC_RMT_MEM_WORDS_PER_CHANNEL`)、user buffer = 24(= `mem_block_symbols ÷ 2`)である。

| symbol周期 | 初回遅延 | 更新間隔 |
|---:|---:|---:|
| 0.4 ms | 19.2 ms | 9.6 ms |
| 0.8 ms | 38.4 ms | 19.2 ms |
| 1.6 ms | 76.8 ms | 38.4 ms |
| 2.4 ms | 115.2 ms | 57.6 ms |

これでE045から続いたRMTの発火条件の系列が閉じる。qualificationのwindow timestampは、gate周期の48倍という計算可能な遅延で最初のtimestampが届き、以降はその半分の間隔で更新される。durationの正確さは条件に依存せず、CPU負荷でも変わらない([E052](../e052_p4_rmt_callback_timing/README.ja.md))。

## 記録した不具合(修正済み)

最初のrunで、[E053](../e053_p4_rmt_dma_block/README.ja.md)から引き継いだ「non-DMA対照が発火すること」という判定が残っており、本実験にnon-DMA対照が無いためpytestが失敗した。計測値は完走している。判定を本実験の設計(どちらの結果も記録する)へ合わせた採用runでは通っている。失敗runは`_runs/E054_20260909T094100Z_default/`に残した。

## 事実・候補・未決

**事実**

1. **DMA modeは`mem_block_symbols` 24 / 32 / 40をすべて`ESP_ERR_INVALID_ARG`で拒否した。** 48は受理された。
2. **受理された48は64として振る舞う。** 1 callbackが64 symbol、間隔153,611 us(= 64 × symbol周期)、初回156,934 us。E053のblock 64指定時(64 symbol、153,601 us、156,935 us)と完全に一致する。
3. **DMA modeの初回遅延は常に`64 × symbol周期`で、non-DMAの`48 × symbol周期`より遅い。** DMAでは縮められない。
4. **window timestampの初回遅延の下限は`48 × symbol周期`で確定した。** 構成はnon-DMA、`mem_block_symbols` 48、user buffer 24。
5. RMTのchannel生成が失敗したcaseではPARLIO側も起動していない(本実験の実装はRMTの成功を前提に順序を組んでいる)。48のcaseではPARLIO側も正常だった。

**候補**: qualificationのwindow timestampをnon-DMA・`mem_block_symbols` 48・user buffer 24で構成する。初回遅延`48 × gate周期`、更新間隔`24 × gate周期`を仕様値として扱う。DMA modeは使わない。

**未決**: `en_partial_rx=false`のときの発火条件 / 48 symbol溜まる前に`rmt_disable`して取れる分だけ回収できるか(応答性が要る用途の逃げ道になりうる) / RMT分解能を落としたときの挙動。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): RMT設定指針にDMA modeを使わない理由と初回遅延の下限を書く
- [LEDGER](../LEDGER.ja.md): E054の節
