# E048 ESP32-P4 qualification構成でのgated capture rate上限

状態: **完了 — 160 MHzまで成立。gateはrate上限を引き上げる**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E047](../e047_p4_gate_three_way_share/README.ja.md)・[E046](../e046_p4_gate_variable_width/README.ja.md)・[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)

## 問い

**[E047](../e047_p4_gate_three_way_share/README.ja.md)の3者共有qualification構成で、gated captureがdropなしで成立する最大sample rateはどこか。dutyの分だけspool帯域が緩むことで、triggerなしraw captureの限界(約98 MB/s)を超えるsample rateが持続するか。**

## 仮説

[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)で、triggerなしraw captureの持続限界はinternal ring → PSRAMのtask copyの約98 MB/sだと分かった。8 channelでは1 byte/sampleなので約98 MHzが持続上限である。

hardware qualificationはgate区間のsampleだけをDMAへ渡すので([E040](../e040_p4_parlio_level_open_frame/README.ja.md))、copy段へ流れるbyte rateはdutyに比例して下がる。dutyが27.7%なら、sample rate 160 MHzでもcopy段には約44 MB/sしか流れない。つまり**gateがあればsampling上限(内部clock源の160 MHz)まで持続できるはずである**。raw captureでは104 MHzのburstすら1 Mi sampleで限界だった([E036](../e036_p4_parlio_rate_seq_verify/README.ja.md))。

測り方を[E047](../e047_p4_gate_three_way_share/README.ja.md)から一点変える。信号源のTX rateを5 MHzに固定し、RXのsample rateだけを掃引する。こうするとRMT側は不変(gateの実時間長が変わらないのでtick数も変わらない)で、PARLIO側のwindow sample数だけがrateに比例する。RMT分解能を20 MHzに固定すると、

```
window byte長 = RMT high duration × (sample rate ÷ 20 MHz)
```

が成り立つ。sample rate 20 / 40 / 80 / 100 / 120 / 160 MHzに対して倍率は1 / 2 / 4 / 5 / 6 / 8で、すべて整数になる。gray7 rampのrun長も`sample rate ÷ 5 MHz`で4 / 8 / 16 / 20 / 24 / 32となる。

drop判定は[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)と同じく、ring上の未読byte数(`callback_bytes - consumed_bytes`)の最大値をring容量65,536 byteと比較する。

## 反証条件

- 高いrateでring未読がring容量を超える、またはgray7 stepに窓境界以外の飛びが出る
- 飛びの階差が`high duration × 倍率`から外れる
- bit 7が0のsampleが出る(共有線がdataとして読めていない)
- RMTのdurationがrateによって変わる(TXを固定しているので変わってはならない)
- 回収byte rateがdutyから計算した値に届かない

## 方法

E047の構成をそのまま使い、TX rateを固定してRX rateを掃引する。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 9、`resolution_hz` = 20,000,000固定、`mem_block_symbols` = 48、`en_partial_rx=true`、buffer 32 symbol
- TX: data_width 8、GPIO 2〜9、**5 MHz固定**、16,384 wordを`loop_transmission`。bit 0〜6は`gray7`、bit 7はE046・E047と同じ可変幅4 window
- 回収は150 msの固定window、destinationは1 MiB PSRAM
- RX rate掃引: 20 / 40 / 80 / 100 / 120 / 160 MHz

期待値:

| sample rate | 倍率 | run長 | duty 27.7%時のbyte rate |
|---:|---:|---:|---:|
| 20 MHz | 1 | 4 | 5.5 MB/s |
| 40 MHz | 2 | 8 | 11.1 MB/s |
| 80 MHz | 4 | 16 | 22.2 MB/s |
| 100 MHz | 5 | 20 | 27.7 MB/s |
| 120 MHz | 6 | 24 | 33.2 MB/s |
| 160 MHz | 8 | 32 | 44.3 MB/s |

飛びの階差は、rateごとに`RMT high duration × 倍率`の循環になるはずである。gate edgeがsample境界に一致しない分の±1 sample程度のずれは許容し、ずれの最大値を記録する。

APIの不成立も結果として最後まで記録する。

## 対象外

data_width 16、RMT分解能の掃引、32,767 tickを超えるgap、dutyの掃引、深度を伸ばしたときの境界、pulse delimiterとの併用、hostへ渡すformat。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

sample rate、倍率、各API結果、RMT callback数・退避symbol数・level別duration min / max、bit 7が0だったsample数、gray7飛びの総数と先頭16 offset、ring未読最大とring超過判定、queue overflow、回収byte、回収rate、期待byte rate、経過us。

## 完了条件

各rateについてring未読量・飛びの階差・bit 7の読み戻しを記録し、gated captureがdropなしで成立する最大sample rateを確定する。約98 MB/sを超えるsample rateが持続するかを明示する。

## 影響

160 MHzまで持続するなら、qualificationは「保存量を減らす手段」だけでなく**sample rateの上限を引き上げる手段**でもあることが確定する。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)の限界matrixに、gate前提のrate行を別に立てる必要がある。

## 結果

実施日: 2026-09-09

採用run: `_runs/E048_20260909T085853Z_default/test_gated_rate_ceiling/dut.log`

| sample rate | 倍率 | ring未読最大 | ring超過 | queue overflow | bit 7が0 | 飛び | 回収rate | 期待rate |
|---:|---:|---:|:-:|---:|---:|---:|---:|---:|
| 20 MHz | 1 | 0 | — | 0 | **0** | 184 | 5,548 KB/s | 5,546 |
| 40 MHz | 2 | 0 | — | 0 | **0** | 114 | (上限) | 11,093 |
| 80 MHz | 4 | 4,032 | — | 0 | **0** | 57 | (上限) | 22,187 |
| 100 MHz | 5 | 5,056 | — | 0 | **0** | 46 | (上限) | 27,734 |
| 120 MHz | 6 | 12,096 | — | 0 | **0** | 38 | (上限) | 33,281 |
| 160 MHz | 8 | **33,280** | — | 0 | **0** | 29 | (上限) | 44,375 |

**6条件すべてが成立した。** ring未読は最大でも33,280 byteでring容量65,536を超えず、queue overflowは0、bit 7が0だったsampleも全条件で0件である。

RMTのdurationは全rateで不変だった(high 1,200 / 2,800 / 6,000 / 8,176、low 6,800 / 9,200 / 10,000 / 21,360)。TX rateを固定した設計どおりで、RMT側はsample rateに依存しない。

160 MHzの飛びのoffsetと階差:

```
offset: 22400 70400 135808 145408 167808 215808 281216 290816 313216 361216 ...
階差:   48000 65408 9600   22400  48000  65408  9600   22400  48000  ...
```

期待値は`high duration × 倍率8` = 9,600 / 22,400 / 48,000 / 65,408である。**160 MHzでも階差は期待値と完全に一致し、ずれは0だった。** 20 MHzから160 MHzまで全rateで一致している。

回収rateは20 MHzだけ期待値と一致し(5,548 対 5,546 KB/s)、40 MHz以上は6,961 KB/sで頭打ちになっている。これは1 MiBのdestinationが埋まってmemcpyを止めた後も回収windowが150 ms続いたためである。callback数からは入力側が期待どおり動いていることが確認できる。160 MHzではcallback 1,733 × 4,032 byte = 6.99 MBを150.6 msで受けており、46.4 MB/sで期待値44.4 MB/sに一致する。

## 判定

**hardware qualificationはsample rateの上限を引き上げる。gated captureは内部clock源の上限160 MHzまで成立し、triggerなしraw captureの持続限界(約98 MB/s = 8 channelで約98 MHz)を超える。**

機構は「dutyで平均を下げる」だけではない。gate区間の中では瞬間のbyte rateはsample rateそのもの(160 MHzなら160 MB/s)であり、copy段の約98 MB/sを上回っている。つまりgateは**持続的な過負荷をringが吸収できるburstへ変換している**。gapの間にringが空になるので次のwindowを再び吸収できる。

したがって成立条件はdutyではなく**1つのwindowの過負荷分がringに収まるか**である。

```
window byte長 × (1 − spool帯域 ÷ sample rate) < ring容量
```

160 MHzの最大window 65,408 byteで計算すると65,408 × (1 − 98/160) = 25.3 KBとなり、実測のring未読最大33,280 byteと同じ桁である。ring 64 KiBなら160 MHzで**1 windowあたり約169 KBまで**吸収できる。level delimiterは`eof_data_len` = 0でwindow長に上限が無いので、この条件は実際に効く。

**測定の限界を明記する。** destinationが1 MiBなので、160 MHzでは約23.6 msで埋まりmemcpyが止まる。それ以降もdequeueは続けているが、taskの負荷は実際のcaptureより軽い。したがってring未読の実測値は長時間captureに対して楽観側であり、**data検証が効いているのは1 MiB分**である。それより長い持続はdestinationを大きくして測り直す必要がある。

## 事実・候補・未決

**事実**

1. 3者共有qualification構成で20 / 40 / 80 / 100 / 120 / 160 MHzの6条件すべてが成立した。ring未読最大33,280 byte(容量65,536)、queue overflow 0、bit 7が0のsampleは0件。
2. **飛びの階差は全rateで`RMT high duration × 倍率`と完全に一致した。** 160 MHzでも9,600 / 22,400 / 48,000 / 65,408でずれ0。
3. RMTのdurationはsample rateに依存せず不変だった(TX rate固定の設計どおり)。
4. callback数から入力側のbyte rateを求めると160 MHzで46.4 MB/sとなり、duty 27.7%からの期待値44.4 MB/sに一致した。
5. **gateの中では瞬間byte rateがsample rateそのもの**であり、copy段の約98 MB/sを上回っている。成立しているのはringがwindow単位の過負荷を吸収しgapで空になるためである。1 windowの過負荷分は`window byte長 × (1 − spool ÷ sample rate)`で、160 MHz・65,408 byteなら25.3 KBと計算でき、実測33,280 byteと同じ桁である。
6. destination 1 MiBが160 MHzでは約23.6 msで埋まるため、それ以降のtask負荷は実際より軽い。data検証が効くのは1 MiB分である。

**候補**: 限界matrixにgate前提のrate行を別に立てる。gated captureの成立条件を`window byte長 × (1 − spool ÷ sample rate) < ring容量`として申告し、dutyではなく最大window長で規定する。ring容量を上げればこの上限も上がる。

**未決**: destinationを大きくした長時間の持続確認 / 1 windowが吸収限界を超える条件の実測(現在はmodelのみ) / ring容量を変えたときの限界の線形性 / data_width 16でのgated rate / dutyを変えたときの挙動 / gate区間が連続して続く(gapが短い)場合の限界。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): 限界matrixにgate前提の行を立て、rate限界の三分割にgateの場合を追加する
- [LEDGER](../LEDGER.ja.md): E048の節
