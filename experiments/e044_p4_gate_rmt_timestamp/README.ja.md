# E044 ESP32-P4 gate線をRMT RXへ分岐してwindow境界をhardwareで記録する

状態: **中断 — GPIO共有は成立、RMTがsymbolを返さず。順序とthresholdを[E045](../e045_p4_gate_rmt_order/README.ja.md)へ**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E043](../e043_p4_parlio_gate_window_boundary/README.ja.md)・[E041](../e041_p4_parlio_shared_valid_line/README.ja.md)・[E040](../e040_p4_parlio_level_open_frame/README.ja.md)

## 問い

**hardware gateに使っているGPIOを同時にRMT RXへ入力し、gate windowの長さと間隔をPARLIO captureと並行してhardwareで記録できるか。**

## 仮説

[E043](../e043_p4_parlio_gate_window_boundary/README.ja.md)で、間引かれたcapture dataの中にwindow境界は残らないと分かった。window長自体はgate幅から一意に決まるが、callbackはDMA descriptorの4,032 byte単位で切れるだけである。したがってgate幅が可変な実信号では時間軸を再構成できない。

境界を別経路で取れればこれは解ける。RMT RXはpulseのlevelと継続時間の列をhardwareで記録するperipheralで、P4では4 channelがRX可能である。gate線はGPIOなので、[E041](../e041_p4_parlio_shared_valid_line/README.ja.md)で確認したGPIO matrixのfan-out(1つの入力GPIOを複数のperipheral入力信号へ配る)を使えば、同じGPIOをPARLIOのvalid入力とRMT RXの入力の両方へ渡せるはずである。配線は変えない。

RMT symbolのduration fieldは15 bitなので、1 symbolあたり32,767 tickが上限である。sample rate 20 MHzに対しRMTの分解能を20 MHz(1 tick = 50 ns = 1 sample)にすると、gate high / lowの両方をtick数で直接sample数として読める。ただしlow区間が32,767 tickを超えないようsource loopを短くする必要があるので、`kSourceWords`をE043の16,384から8,192へ変える。

- gate幅2,044 word → high 8,176 tick、low 24,592 tick
- gate幅1,020 word → high 4,080 tick、low 28,688 tick

どちらも15 bitに収まる。`signal_range_max_ns`は1.6 ms(32,000 tick)にして、lowをidleと誤判定させない。

## 反証条件

- 同じGPIOをPARLIOのvalidとRMT RXの両方に割り当てられない
- RMTのsymbolが取れない、または`en_partial_rx`でcallbackが来ない
- high / lowのtick数が期待値から外れる、またはばらつく
- RMTを足したことでPARLIO側のgated captureが壊れる(連番違反やduty比の変化)

## 方法

E043の構成にRMT RXを足し、`kSourceWords`だけ8,192へ変える。

- RX: data_width 4、`data_gpio_nums[0..3]` = GPIO 2〜5、`valid_gpio_num` = GPIO 6、`valid_sig_line_id` = 4、20 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 6(gate線と同一)、`resolution_hz` = 20,000,000、`mem_block_symbols` = 48、`flags.invert_in=0`、`with_dma=0`
- `rmt_receive`は32 symbolのbufferに`flags.en_partial_rx=true`で行い、`on_recv_done`から先頭16 symbolのduration/levelを退避する
- TX: data_width 8、GPIO 2〜9、5 MHz、8,192 wordを`loop_transmission`
  - bit 0〜3: `gray4(index & 0xF)`
  - bit 4: index 2048から`kGateWords`だけhigh
- PARLIOのchunkを回収しながら256 KiB PSRAMへ連結する。回収は50 msの固定windowで打ち切る

case:

| # | gate幅 | 期待window(byte) | 期待high(tick) | 期待low(tick) |
|---:|---:|---:|---:|---:|
| 1 | 2,044 word | 4,088 | 8,176 | 24,592 |
| 2 | 1,020 word | 4,080 ÷ 2 = 2,040 | 4,080 | 28,688 |

PARLIO側はE043と同じ検証(gray stepの飛びが期待window長の整数倍にあるか)を並走させ、RMTを足してもgated captureが壊れないことを確認する。

APIの不成立も結果として最後まで記録する。

## 対象外

RMTの分解能掃引、gate幅が実際に可変な信号でのwindow復元(ここでは固定幅でRMTが正しい値を返すことまで)、RMT symbolのDMA mode、32,767 tickを超えるgapの扱い、ETMやGPTimerによる別経路、gating時の最大sample rate、data_width 8 / 16。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、gate幅、期待window長、期待high / low tick、各API結果、RMT callback数、退避したsymbol数、先頭16 symbolのduration/level、level 1とlevel 0のduration min/max、PARLIO側のcallback数・回収byte・gray step飛びの数と期待倍数一致数・回収rate、経過us。

## 完了条件

同一GPIOをPARLIO validとRMT RXへ同時に割り当てられるかを確定し、割り当てられる場合はhigh / lowのtick数が期待値と一致するかを記録する。RMTを足してPARLIO側が壊れないことも確認する。

## 影響

成立すれば、capture qualificationは「hardwareが間引き、hardwareが境界をtimestampする」形になり、gate幅が可変でも時間軸を再構成できる。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)の内部圧縮節にあるqualificationの制約(可変幅では時間軸を再構成できない)が外れる。RMT channelを1つ消費するが、channel数もCPUも払わない。

## 結果

実施日: 2026-09-09

採用run: `_runs/E044_20260909T083102Z_default/test_gate_rmt_timestamp/dut.log`

| # | gate幅 | 期待high | 期待low | RMT API | rmt_callbacks | rmt_stored | PARLIO回収byte | 飛び / 期待倍数一致 | 回収rate |
|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|
| 1 | 2,044 word | 8,176 tick | 24,592 tick | 全て`ESP_OK` | **0** | **0** | 128,512 | 31 / 31 | 2,505 KB/s |
| 2 | 1,020 word | 4,080 tick | 28,688 tick | 全て`ESP_OK` | **0** | **0** | 65,536 | 32 / 32 | 1,241 KB/s |

**同一GPIOをPARLIOのvalid入力とRMT RXの入力に同時に割り当てること自体は通った。** `rmt_new_rx_channel`、`rmt_enable`、`rmt_receive`はすべて`ESP_OK`で、PARLIO側のAPIも全て`ESP_OK`だった。

**PARLIO側のgated captureはRMTを足しても壊れていない。** gray stepの飛びは31 / 32件で、すべて期待window長の整数倍にあった([E043](../e043_p4_parlio_gate_window_boundary/README.ja.md)と同じ結果)。回収rateもduty比と一致する(case 1はgate 2,044 / source 8,192 = duty 25%に対し2,505 KB/s = raw 10,000 KB/sの25.1%)。

**しかしRMTのcallbackが一度も発火せず、symbolを1つも取得できなかった。** 50 msの回収window中に`on_recv_done`は0回である。

durationの期待値は15 bitに収まっており(最大28,688 < 32,768)、`signal_range_max_ns` 1.6 msは32,000 tickで上限内、`signal_range_min_ns` 500 nsは10 tickである。設定値そのものが範囲外だったわけではない。

## 判定

**この構成では反証された。** 問いの前半(同一GPIOをPARLIO validとRMT RXへ同時に割り当てられるか)はAPIレベルで成立したが、後半(durationを記録できるか)には到達していない。RMTがdataを返さない原因として、この実験の記録から絞れる候補は二つある。

1. **peripheral生成順** — 本実験はPARLIO RX → RMT → PARLIO TXの順で作った。PARLIO TXがGPIO 6を出力として設定する際に、RMTがGPIO matrixへ張った入力経路を壊した可能性がある。[E014](../e014_p4_parlio_internal_capture/README.ja.md)と[E015](../e015_p4_parlio_routing_order/README.ja.md)で、この platform では初期化順とGPIOの経路上書きが実際に問題になることが分かっている。ただしPARLIO RXのvalid入力は同じGPIOで生きているので、入力経路が一律に壊れたわけではない
2. **partial受信のthresholdに届いていない** — `mem_block_symbols` 48に対し、non-DMAのping-pong通知は memory block を単位に起きる。1 gate cycleが1 symbolなので、48 symbolを埋めるにはsource loop周期1.6384 ms × 48 = 78.6 msかかる。回収windowは50 msなので、単に短かった可能性がある

どちらも安い条件変更で切り分けられるが、本実験の計画が固定した条件の外なので、**[E015](../e015_p4_parlio_routing_order/README.ja.md)がE014に対して行ったのと同じ形で、順序と回収時間を変数にした新しいIDを起こす**。

なお本実験のpytestは`rmt_ok`の条件を満たさないため失敗する。これは記録として意図したもので、期待が満たされなかったことをそのまま残す(§3.3)。

## 事実・候補・未決

**事実**

1. 同一GPIO(GPIO 6)をPARLIO RXの`valid_gpio_num`とRMT RXの`gpio_num`へ同時に割り当てて、両peripheralの生成・enable・receiveがすべて`ESP_OK`になった。
2. RMTを足してもPARLIO側のgated captureは壊れなかった。gray stepの飛びは31 / 32件すべてが期待window長の整数倍で、回収rateはduty比と一致した。
3. **RMTの`on_recv_done`は50 msの回収window中に0回発火し、symbolを1つも取得できなかった。**
4. duration期待値(最大28,688 tick)、`signal_range_max_ns`(32,000 tick)、`signal_range_min_ns`(10 tick)はいずれもhardwareの範囲内である。
5. source loop周期は1.6384 msで、`mem_block_symbols` 48を埋めるには78.6 msかかる。回収windowの50 msより長い。

**候補**: peripheral生成順をPARLIO TXの後へ動かす。回収windowをsource loop周期 × `mem_block_symbols`より長く取る。この二つを変数にして切り分ける。

**未決**: RMTがdataを返さない原因(生成順かthresholdか) / 生成順を変えたときにPARLIO TXの出力とRMTの入力が共存するか / `mem_block_symbols`とping-pong通知の関係 / 32,767 tickを超えるgapの扱い / ETMやGPTimerによる別経路。次は[E045](../e045_p4_gate_rmt_order/README.ja.md)。

## 反映

- [LEDGER](../LEDGER.ja.md): E044の節。状態は中断
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)は動かさない。qualificationの境界復元は未解決のまま

## 追記 — E045による原因の確定(2026-09-09)

本レポートは書き換えない。[E045](../e045_p4_gate_rmt_order/README.ja.md)が生成順と回収時間を2 × 2で切り分けた結果、次が確定した。

- **生成順は無関係で、候補1は反証された。** RMTをPARLIO TXの前に作っても後に作っても結果は同一である。GPIO matrixのfan-outはRMTに対しても効き、PARLIO TXが同じGPIOを出力として設定してもRMTの入力経路は壊れない。
- **原因は候補2、回収時間だった。** `en_partial_rx`のcallbackはmemory blockではなく**user bufferが埋まったとき**に起きる。32 symbolのbufferはgate周期1.6384 msの32倍 = 52.43 msで埋まるので、本実験の51.3 msではわずかに届いていなかった。300 msでは両順序ともsymbolが取れ、durationは期待値と完全一致した(high 8,176 tick、low 24,592 tick、min = max)。

したがって本実験の構成そのものは正しく、回収時間だけが足りなかった。gate線をPARLIO validとRMT RXへ共有してwindowの長さと間隔をhardwareで記録することは成立する。
