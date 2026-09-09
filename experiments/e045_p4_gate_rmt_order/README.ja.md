# E045 ESP32-P4 RMT RXがgate durationを返す条件(生成順と回収時間)

状態: **完了 — 原因は回収時間。durationは期待値と完全一致**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E044](../e044_p4_gate_rmt_timestamp/README.ja.md)(中断)・[E015](../e015_p4_parlio_routing_order/README.ja.md)

## 問い

**RMT RXがgate線のhigh / low durationを返すのは、どのperipheral生成順とどの回収時間のときか。**

## 仮説

[E044](../e044_p4_gate_rmt_timestamp/README.ja.md)では、同一GPIOをPARLIO RXの`valid_gpio_num`とRMT RXの`gpio_num`へ割り当てて全APIが`ESP_OK`になり、PARLIO側のgated captureも壊れなかったが、RMTの`on_recv_done`が50 msの回収window中に0回だった。原因の候補は二つに絞れている。

1. **生成順** — E044はPARLIO RX → RMT → PARLIO TXの順だった。PARLIO TXがGPIO 6を出力に設定する際、RMTがGPIO matrixへ張った入力経路を壊した可能性がある。[E015](../e015_p4_parlio_routing_order/README.ja.md)は、`io_loop_back=false`なら入力だけを後から足せることを示した。同じ原理でRMTをPARLIO TXの**後**に作れば通るはずである
2. **partial受信のthreshold** — `mem_block_symbols` 48に対し、non-DMAのping-pong通知はmemory block単位で起きる。1 gate cycleが1 symbolなので48 symbolを埋めるにはsource loop周期1.6384 ms × 48 = 78.6 msかかる。回収windowの50 msでは届かない

二つは独立なので2 × 2で切り分ける。E044のcase(RMTを先、50 ms)を対照として含める。

## 反証条件

- 4条件すべてでsymbolが取れない(原因はこの二つの外)
- 生成順を変えるとPARLIO TXの出力かPARLIO RXのgatingが壊れる
- symbolが取れてもhigh / lowのtick数が期待値から外れる、またはばらつく

## 方法

E044の構成からgate幅を2,044 wordに固定し、生成順と回収時間だけを変える。

- RX: data_width 4、`data_gpio_nums[0..3]` = GPIO 2〜5、`valid_gpio_num` = GPIO 6、`valid_sig_line_id` = 4、20 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 6、`resolution_hz` = 20,000,000(1 tick = 1 sample)、`mem_block_symbols` = 48、`signal_range_min_ns` = 500、`signal_range_max_ns` = 1,600,000、`flags.en_partial_rx=true`、buffer 32 symbol
- TX: data_width 8、GPIO 2〜9、5 MHz、8,192 wordを`loop_transmission`。bit 0〜3は`gray4`、bit 4はindex 2048から2,044 wordだけhigh
- 期待値: high 8,176 tick、low 24,592 tick、PARLIO window 4,088 byte

case:

| # | RMTの生成位置 | 回収時間 |
|---:|---|---:|
| 1 | PARLIO TXより前(E044と同じ) | 50 ms |
| 2 | PARLIO TXより前 | 300 ms |
| 3 | **PARLIO TXより後** | 50 ms |
| 4 | **PARLIO TXより後** | 300 ms |

各caseでPARLIO側のgated captureも並走させ、gray stepの飛びが期待window長の整数倍にあることを確認する。生成順を変えてPARLIO側が壊れないことの確認を含む。

APIの不成立も結果として最後まで記録する。

## 対象外

`mem_block_symbols`の掃引、RMT分解能の掃引、DMA mode、32,767 tickを超えるgapの扱い、gate幅が実際に可変な信号でのwindow復元、ETM / GPTimerによる別経路、gating時の最大sample rate、data_width 8 / 16。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、RMTの生成位置、回収時間、各API結果、RMT callback数、退避symbol数、level 1 / level 0のduration count・min・max、期待high / low、先頭6 symbol、PARLIO側のcallback数・回収byte・飛びの数と期待倍数一致数・回収rate、経過us。

## 完了条件

4条件のうちどれでRMTがdurationを返すかを確定する。返る条件があれば、high / lowのtick数が期待値と一致するかを記録する。どれも返らない場合は、原因がこの二つの外にあることを確定して完了とする。

## 影響

返る条件が見つかれば、[E043](../e043_p4_parlio_gate_window_boundary/README.ja.md)で残った「可変幅gateでは時間軸を再構成できない」という制約が外れ、qualificationは「hardwareが間引き、hardwareが境界をtimestampする」形になる。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)の内部圧縮節が更新される。

## 結果

実施日: 2026-09-09

採用run: `_runs/E045_20260909T083514Z_default/test_gate_rmt_order/dut.log`

| # | RMTの生成位置 | 回収時間 | rmt_callbacks | 退避symbol | high(期待8,176) | low(期待24,592) | PARLIO飛び / 一致 |
|---:|---|---:|---:|---:|---|---|---:|
| 1 | TXより前 | 50 ms | 0 | 0 | — | — | 31 / 31 |
| 2 | TXより前 | 300 ms | 6 | 32 | **8,176〜8,176** | **24,592〜24,592** | 64 / 64 |
| 3 | TXより後 | 50 ms | 0 | 0 | — | — | 31 / 31 |
| 4 | TXより後 | 300 ms | 6 | 32 | **8,176〜8,176** | **24,592〜24,592** | 64 / 64 |

**生成順は無関係だった。** TXより前・後のどちらでも結果は同一である。E044の候補1(PARLIO TXがGPIOを出力として設定する際にRMTの入力経路を壊す)は**反証された**。GPIO matrixのfan-outはRMTに対しても効き、同じGPIOがPARLIO validとRMT RXの入力を同時に供給する。

**原因は回収時間だった。** 300 msで両順序ともsymbolが取れ、50 msではどちらも0である。E044の候補2が正しい。

内訳も一致する。source loop周期は8,192 word / 5 MHz = 1.6384 msで、1 gate cycleが1 symbolである。`rmt_receive`へ渡したbufferは32 symbolなので、埋まるまでに32 × 1.6384 = **52.43 ms**かかる。E044と本実験のcase 1 / 3の回収時間は51.1〜51.3 msで、わずかに足りていなかった。300 msでは183 cycleが流れ、32 symbolのbufferが6回埋まって`rmt_callbacks=6`になった。**`en_partial_rx`のcallbackはmemory blockではなくuser bufferが埋まったときに起きる。**

**durationは期待値と完全に一致した。** high 16個すべてが8,176 tick、low 16個すべてが24,592 tickで、min = maxである。期待値は`gate幅 × 分周比` = 2,044 × 4 = 8,176、`(source幅 - gate幅) × 分周比` = 6,148 × 4 = 24,592。RMT分解能を20 MHzにしたので1 tick = 1 sampleであり、**gate windowの長さと間隔がsample単位で直接読める。**

symbol列も交互に並んだ。

```
1:8176 0:24592 1:8176 0:24592 1:8176 0:24592 ...
```

**PARLIO側は4条件すべてで無傷だった。** gray stepの飛びはすべて期待window長の整数倍にあり、生成順を変えてもgated captureは壊れない。

なお300 msのcaseの回収rateが873 KB/sと低いのは、256 KiB PSRAMのdestinationが埋まってcopyを止めた後も回収windowが続いたためで、間引きの効率が落ちたわけではない。50 msのcaseの2,505 KB/sがduty 25%に対応する値である。

## 判定

**hardware gatingとhardware window timestampingは同時に成立する。** gate線1本をPARLIOのvalid入力とRMT RXの入力に共有すれば、PARLIOがgate区間のsampleだけをDMAで拾い、RMTが各high / low区間の長さをsample単位で記録する。CPUはどちらにも介在しない。生成順の制約も無い。

これで[E043](../e043_p4_parlio_gate_window_boundary/README.ja.md)が残した制約が外れる。gate幅が可変でも、RMTが各windowの長さと間隔を個別に返すので、間引かれたsample列を時間軸へ戻せる。qualificationは「burstの中身だけ」ではなく「時間軸付きのtrace」になる。

払うものは次のとおりである。

- **RMT RX channel 1つ**(P4は4 channelがRX可能)
- **1 levelあたり32,767 tickの上限** — RMT symbolのduration fieldが15 bitである。分解能20 MHz(1 tick = 1 sample)では1 levelが1.638 msまで。これを超えるgapを扱うには分解能を下げる(精度と引き換え)か、`signal_range_max_ns`でidle終了として区切る
- **callback遅延 = buffer symbol数 × gate周期** — `en_partial_rx`はuser bufferが埋まったときに通知する。応答性が要るならbufferを小さくする。本実験の32 symbolは52.43 ms相当だった

channel数は払わない。gate線はPARLIO側でも[E041](../e041_p4_parlio_shared_valid_line/README.ja.md)の手法でdata線と共有できるので、8 channel + qualification + window timestampが8 pinで成立する見込みである(この組み合わせ自体は未実証)。

## 事実・候補・未決

**事実**

1. RMTの生成位置をPARLIO TXの前・後どちらにしても結果は同一だった。**GPIO matrixのfan-outはRMTに対しても効く。** E044の生成順仮説は反証された。
2. 回収時間50 msではsymbolが0、300 msでは両順序とも取れた。**原因は回収時間である。**
3. `en_partial_rx`のcallbackはuser bufferが埋まったときに起きる。32 symbol buffer × gate周期1.6384 ms = 52.43 msが必要で、50 msはわずかに足りていなかった。300 msでは`rmt_callbacks=6`だった。
4. **duration 16個のhighはすべて8,176 tick、16個のlowはすべて24,592 tickで、期待値と完全一致した(min = max)。** 分解能20 MHzなので1 tick = 1 sampleである。
5. PARLIO側のgated captureは4条件すべてで無傷で、gray stepの飛びはすべて期待window長の整数倍にあった。

**候補**: qualificationをhardware 2段構成にする。PARLIO level delimiterがsampleを間引き、RMT RXが同じgate線からwindowのlengthとgapを記録する。hostへはsample列とwindow長の列を組で渡す。RMTのbuffer symbol数を応答遅延の設計値として扱い、1 levelが32,767 tickを超える用途では分解能を落とす。

**未決**: gate幅が実際に可変な信号でのwindow復元 / 32,767 tickを超えるgapの扱い方 / PARLIO側でもgate線をdata線と共有した3者同時共有([E041](../e041_p4_parlio_shared_valid_line/README.ja.md)の手法との組) / RMT分解能を落としたときの精度 / gating時の最大sample rate / RMTのsymbolとPARLIOのsample列の対応付け(先頭windowの同期) / data_width 8 / 16でのqualification。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): 内部圧縮節のqualificationから「可変幅では時間軸を再構成できない」を外し、RMTによるwindow timestampを追加する
- [E044](../e044_p4_gate_rmt_timestamp/README.ja.md): 生成順仮説が反証されたことを追記する
- [LEDGER](../LEDGER.ja.md): E045の節

## 追記 — E051による限定(2026-09-09)

本レポートは書き換えない。[E051](../e051_p4_rmt_partial_threshold/README.ja.md)がuser bufferのsymbol数を8まで下げて測った結果、**「`en_partial_rx`のcallbackはuser bufferが埋まったときに起きる」という規則は一般則としては成り立たない**ことが分かった。buffer 8 symbol(所要19.2 ms)でも100 msの回収でcallbackは0回だった。`mem_block_symbols`も48と96で差が無い。

本レポートの数値(32 symbol × 1.6384 ms = 52.43 msが必要で51.3 msでは届かなかった)はbuffer基準の説明と整合していたが、それは偶然一致していた可能性がある。確定しているのは「回収時間を増やせば発火する」ことと「取れたdurationは正確である」ことだけで、閾値の正体は未特定である。
