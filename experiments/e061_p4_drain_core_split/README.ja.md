# E061 ESP32-P4 回収を別coreへ移すとdrainは上がるか

状態: **完了 — core分離でdrainが82.3→119.7 MB/s**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E060](../e060_p4_drain_batch_coalesce/README.ja.md)・[E059](../e059_p4_drain_breakdown/README.ja.md)

## 問い

**PARLIOのISRが走るcoreとmemcpyするcoreを分けると、window中のdrain帯域は上がるか。**

## 仮説

[E059](../e059_p4_drain_breakdown/README.ja.md)でdrain低下の原因が1 chunkあたり3.6〜5.2 usの固定costだと分かり、[E060](../e060_p4_drain_batch_coalesce/README.ja.md)で`xQueueReceive`のまとめ取りもmemcpyのまとめも効かないことが分かった。memcpyの累積時間は3条件でほぼ一定だったので、固定costはdriver側のISRが支配している。**task側で残る手は回収を別coreへ移すことだけである。**

P4は`SOC_CPU_CORES_NUM` = 2でHP coreが2基ある。ESP-IDFのinterruptは`esp_intr_alloc`を呼んだcoreに割り当てられるので、driverの生成・enableをArduinoのloop task上で行えばISRはそのcoreに載る。回収loopだけを`xTaskCreatePinnedToCore`で別coreへ置けば、**ISRの実行とmemcpyが別coreに分かれる。**

ISRがmemcpyを止めている分だけdrainが上がるはずである。[E059](../e059_p4_drain_breakdown/README.ja.md)の160 MHzではmemcpy占有率が77%だったので、ISRの分がまるごと消えればdrainはmemcpy帯域107 MB/sへ近づく。

ただし二つのcoreは同じcacheとPSRAM controllerを共有するので、memcpy帯域そのものは上がらないか、cache競合で下がる可能性もある。

case:

| # | driver生成・ISR | 回収loop | 分かること |
|---:|---|---|---|
| 1 | loop taskのcore | **同じcore** | [E060](../e060_p4_drain_batch_coalesce/README.ja.md)基準の再現 |
| 2 | loop taskのcore | **core 0へpin** | 片方のcoreへ寄せた場合 |
| 3 | loop taskのcore | **core 1へpin** | もう片方へ寄せた場合 |

Arduinoのloopがどのcoreにいるかは`xPortGetCoreID()`で記録する。case 2と3のどちらかがloop taskと同じcoreになるので、その組が実質case 1の対照になり、もう一方が分離条件になる。

sample rateは160 MHz、window byte長は96,000、回収はper-chunk copy(まとめ取りもmemcpyまとめもしない)に固定する。[E060](../e060_p4_drain_batch_coalesce/README.ja.md)で基準のISR側未読最大は54,656、drainは82.3 MB/s、memcpy帯域は107.3 MB/sだった。

## 反証条件

- 3条件でISR側未読最大とmemcpy帯域が変わらない(core分離が効かない)
- 分離するとdrainが下がる(cache競合が勝つ)
- pinしたcoreでtaskが起動しない、またはwatchdogが出る
- どれかの条件で飛びの急増やqueue overflowが出る

## 方法

[E060](../e060_p4_drain_batch_coalesce/README.ja.md)の構成から回収loopをtaskへ切り出し、pinするcoreだけを変える。captureの設定は変えない。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`。`max_recv_size`と受信sizeは62,720
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは6,000 wordで先頭3,000だけbit 7をhigh(duty 50%、window 96,000 byte)
- queue深さ64、回収は100 ms、destinationは4 MiB PSRAM、未読はISR内で標本化
- driverの生成・enable・start、およびstop・検証・報告はArduinoのloop task上で行う。**回収loopだけをtaskへ出し、case 1は同じcore、case 2 / 3はcore 0 / 1へpinする**
- 回収taskの優先度は3条件とも5で揃える
- `xPortGetCoreID()`でloop taskのcoreと回収taskのcoreを記録する

APIの不成立も結果として最後まで記録する。

## 対象外

ISR自体のcore affinityを明示的に変える方法、ISRの実行時間の直接測定、優先度の掃引、sample rateとwindow長の掃引、destinationをinternal RAMへ置く構成、triggerなしspool経路での同じ測定。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、loop taskのcore、回収taskのcore、各API結果、memcpyの累積時間・累積byte・呼び出し回数、memcpy帯域、ISR側とtask側の未読最大、そこから逆算したdrain、queue overflow、飛びの総数と期待境界数、階差一致数、回収byte、経過us。

## 完了条件

3条件のdrainとmemcpy帯域を記録し、core分離がdrainを上げるか否かを確定する。上がる場合はその量を記録する。

## 影響

上がるなら、gated captureのdrainは実装のcore配置で決まることになり、条件2の`drain(rate)`は「ISRと回収を分けたときの値」として申告できる。上がらないなら、drainの改善手段は尽きたことになり、条件2の`drain(rate)`表がそのまま上限として確定する。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のdrain表が閉じる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E061_20260909T132131Z_default/test_drain_core_split/dut.log`

Arduinoのloop taskはcore 1にいた。したがってcase 1(要求 −1)とcase 3(要求 1)が同一core、case 2(要求 0)が分離条件になる。

| # | 要求 | setup core | 回収core | 分離 | ISR側の未読最大 | memcpy時間 | **memcpy帯域** | 逆算したdrain |
|---:|---:|---:|---:|---|---:|---:|---:|---:|
| 1 | −1 | 1 | 1 | 同一 | 54,656 | 38,984 us | 107.6 MB/s | 82.3 MB/s |
| 2 | **0** | 1 | **0** | **分離** | **32,256** | **32,018 us** | **131.0 MB/s** | **119.7 MB/s** |
| 3 | 1 | 1 | 1 | 同一 | 54,656 | 38,974 us | 107.6 MB/s | 82.3 MB/s |

3条件すべて正常に取れた(飛び44対期待43、階差15 / 15、queue overflow 0)。同一coreの2条件(case 1と3)は未読最大が54,656で完全に一致し、memcpy時間も38,984と38,974 usなので、対照として機能している。

**core分離でdrainが82.3から119.7 MB/sへ上がった(+45%)。** ISR側の未読最大は54,656から32,256へ41%下がっている。

**memcpy帯域自体も107.6から131.0 MB/sへ上がった(+22%)。** これは[E059](../e059_p4_drain_breakdown/README.ja.md)の解釈を修正する。E059はmemcpyを`esp_timer_get_time()`で挟んで107 MB/sと測り「DMAが動いていること自体で25〜40%失っている」と書いたが、**計時区間の内側でISRがmemcpyを中断していた分が入っていた**。別coreに置けば131.0 MB/sになる。[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)のDMA無し138.6〜182.7 MB/sとの残差(5〜25%)が本当のmemory競合分である。

**尖頭未読の式は分離条件でもそのまま当たる。** drain 119.7 MB/sで計算すると`8,064 + 96,000 × (1 − 119.7/160)` = 32,244 byteで、実測32,256との差は**12 byte**である。

## 判定

**回収を別coreへ移すとdrainは上がる。** [E060](../e060_p4_drain_batch_coalesce/README.ja.md)が「task側の書き方では下がらない、残る手はcore分離だけ」と結論した通りで、その残った手は実際に効いた。

内訳は二つある。

- **ISRがmemcpyを中断しなくなる** — memcpy帯域が107.6から131.0 MB/sへ。E059が固定costと呼んだ3.6〜5.2 us/chunkのうち、ISR本体がmemcpyの内側で消費していた分がこれで消える
- **残る非memcpy時間** — 分離後もdrain 119.7はmemcpy帯域131.0の91%で、9%は`xQueueReceive`とloop本体である。ここはcore分離では消えない

条件2への効き方を8 channel・160 MHz・ring 15 chunk(60,480 byte)で計算すると次になる。

| 回収core | drain | 許容window byte長 |
|---|---:|---:|
| ISRと同一 | 82.3 MB/s | 約107,900 |
| **分離** | **119.7 MB/s** | **約208,000** |

**同じringで約1.9倍長いwindowが通る。** 逆にwindow長を固定するなら、必要なring容量が半分で済む。

したがってgated captureの実装では**回収をISRと別のcoreへ置く**。Arduinoからは、driverの生成・enableをloop task上で行い(ISRはそのcoreに載る)、回収loopだけを`xTaskCreatePinnedToCore`で別coreへ出せばよい。

## 事実・候補・未決

**事実**

1. Arduinoのloop taskはcore 1にいた。要求 −1と1は同一core、要求 0が分離条件になった。同一coreの2条件は未読最大54,656で完全一致し対照として機能した。
2. **core分離でISR側の未読最大が54,656から32,256へ41%下がり、逆算したdrainは82.3から119.7 MB/sへ45%上がった。**
3. **memcpy帯域も107.6から131.0 MB/sへ22%上がった。** [E059](../e059_p4_drain_breakdown/README.ja.md)が測った107 MB/sは、計時区間の内側でISRがmemcpyを中断していた分を含んでいた。[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)のDMA無し138.6〜182.7 MB/sとの残差5〜25%が本当のmemory競合分である。
4. 分離後もdrain 119.7はmemcpy帯域131.0の91%で、残る9%は`xQueueReceive`とloop本体である。core分離では消えない。
5. **尖頭未読の式は分離条件でも当たる。** drain 119.7で予測32,244 byteに対し実測32,256で差は12 byteである。
6. 3条件すべて正常に取れた(飛び44対期待43、階差15 / 15、queue overflow 0)。
7. 条件2への効果は、8 channel・160 MHz・ring 15 chunkで許容window byte長が約107,900から約208,000へ、**約1.9倍**である。

**候補**: gated captureの実装では回収をISRと別coreへ置く。driverの生成・enableをloop task上で行い、回収loopだけを`xTaskCreatePinnedToCore`で別coreへ出す。条件2の`drain(rate)`はcore配置ごとの値として持つ。

**未決**: 分離時のdrainのrate依存(本実験は160 MHzのみ) / 残る9%の非memcpy時間の内訳 / 回収coreで他のtaskが走っている場合の劣化 / ISR自体を明示的に別coreへ割り当てる方法 / triggerなしspool経路でも同じ改善が出るか。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): drain表にcore配置の軸を追加し、条件2への効き方を書く
- [E059](../e059_p4_drain_breakdown/README.ja.md): memcpy帯域107 MB/sがISR中断込みの値だったことを追記する
- [LEDGER](../LEDGER.ja.md): E061の節
