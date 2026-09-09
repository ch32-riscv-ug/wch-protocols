# E042 ESP32-P4 16 channel rate境界のsample単位再検証

状態: **完了 — 48 MHzまで成立。複製lane不一致を検出**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)・[E033](../e033_p4_parlio_width_rate_fine/README.ja.md)・[E031](../e031_p4_parlio_channel_width/README.ja.md)

## 問い

**16 channelのtriggerなしbatchで、gray code rampによるsample単位検証とring未読量によるdrop判定を行うと、dropなし境界はどこか。E033の「48 MHz成立、52 MHz不成立」は保たれるか。**

## 仮説

E036は8 channelについて、raw rateがsampling上限・持続spool帯域・burst深度の三つに分かれることを実測した。16 channelの行はまだ100 kHz PWMのdutyとedge数による検証しか通っていない([調査地図](../../references/p4-logic-analyzer-investigation.ja.md)の※行)。

E036のmodelから予測が立つ。持続spool帯域は約98 MB/sで、16 channelはpacking後2 byte/sampleなので、

- 48 MHz設定 → 96 MB/s。spoolがsamplingを上回るのでring未読は増えず、成立する
- 52 MHz設定 → 104 MB/s。差分6 MB/sをring 64 KiBが吸収できるのは約10.9 msで、その間に取得できるのは約0.57 Mi sample。1 Mi sampleの取得では途中で上書きが起きて連番違反が出る

E033は52 MHzでmax_queue 36を記録しており、ring容量(約17 chunk相当)を超えていた。したがって52 MHzは「設定rateへ追従しなかった」のではなく実際にsampleを失っていたはずである。

信号源はPARLIO TXのdata_width 8で8-bit gray rampを出す。RXはdata_width 16で、lane 8〜15にlane 0〜7と同じGPIOを入力する(E031と同じ内部観測)。したがって16-bit sampleの上位byteは下位byteと一致するはずで、これがpackingとlane対応の検証になる。**16本の独立した外部padは評価しない。**

## 反証条件

- 48 MHzで連番違反またはring未読のring容量超過が出る
- 52 MHzで違反0かつring未読が容量以内(= E036のmodelが16 channelに当てはまらない)
- 上位byteと下位byteが一致しない
- sampling rate(callback byte由来)が設定rateへ追従しない
- PARLIO TX data_width 8とRX data_width 16を同時に構成できない

## 方法

E036の構成をwidthだけ変えて再利用し、chunk分割をE031〜E033と同じに保って比較可能にする。

- TX: data_width 8、GPIO 2〜9、`output_clk_freq_hz` = RX rate ÷ 4、8,192 byteの8-bit gray rampを`loop_transmission`(8,192は256の倍数なのでloopの継ぎ目も+1 step)
- RX: data_width 16、`data_gpio_nums[lane]` = GPIO 2〜9を lane 0〜7 と lane 8〜15 の両方へ、`io_loop_back=false`、64 KiB internal ring、`eof_data_len` 65,408、queue 64段
- 1,048,576 sampleを2 MiB PSRAMへ退避する
- drop検出は`callback_bytes - consumed_bytes`の最大値をring容量65,536 byteと比較する
- data検証は2 MiB全域について、下位byteをgray8として復元しrun崩し後のstepが+1 mod 256であることと、上位byteが下位byteと一致することを確認する
- RX rate掃引: 20 / 40 / 48 / 52 / 56 MHz

APIの不成立も結果として最後まで記録する。

## 対象外

16本の独立した外部pad、trigger、圧縮、hardware delimiter、1 Mi以外の深度、ring・chunk sizeのtuning、1 / 2 / 4 channelの再検証。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

RX設定rate、TX設定rate、各API結果、copied、callbacks、dequeues、max_queue、queue overflow、inflight最大、ring容量、ring超過、runs、run長min/max、gray step違反数、最初の違反位置、上位byte不一致数、capture時間、sampling rate、spool rate。

## 完了条件

各rateについてring未読量と連番違反を記録し、16 channelのdropなし境界を確定する。E036のburst modelによる予測(48 MHzは成立、52 MHzは違反)と一致するかを明示する。

## 影響

[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)の限界matrixから16 channel行の※(duty/edge検証のみ)が外れる。E036のmodelがwidthをまたいで成立するなら、残る1 / 2 / 4 channel行はbyte rateが低くspool帯域に余裕があるため、再検証の優先度を下げられる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E042_20260909T065702Z_default/test_parlio_16ch_seq_verify/dut.log`

| RX設定 | TX設定 | sampling | spool | inflight最大 | ring超過 | 連番違反 | 最初の違反 | run長 | 複製lane不一致 | capture_us |
|---:|---:|---:|---:|---:|:-:|---:|---:|---:|---:|---:|
| 20 MHz | 5 MHz | 39.978 MB/s | 39.903 MB/s | 0 | — | 0 | — | 4〜4 | **0** | 52,556 |
| 40 MHz | 10 MHz | 79.920 MB/s | 79.769 MB/s | 4,032 | — | 0 | — | 3〜5 | **1,066** | 26,290 |
| 48 MHz | 12 MHz | 95.884 MB/s | 95.703 MB/s | 4,928 | — | 0 | — | 3〜5 | **373** | 21,913 |
| 52 MHz | 13 MHz | 103.878 MB/s | 97.605 MB/s | 134,848 | **超過** | 44 | 578,400 | 1〜5 | 345 | 21,486 |
| 56 MHz | 14 MHz | 111.803 MB/s | 96.884 MB/s | 323,904 | **超過** | 38 | 304,736 | 1〜5 | 480 | 21,646 |

**E036のmodelはwidthをまたいで成立した。** 48 MHzは連番違反0・ring未読4,928 byteで成立し、52 MHzはring未読134,848 byteでring容量を超え違反44件を出した。予測(48成立 / 52違反)と一致する。spool rateは52 / 56 MHzで97.6 / 96.9 MB/sに飽和し、8 channelで見た約98 MB/sと同じ値である。**律速はchannel数ではなくpacking後のbyte rateである**という結論が16 channelでも確認された。

E033が52 MHzを「設定rateへ追従しなかった」としたのは、8 channelと同じく律速の帰属が逆であり、実際にはsampleを失っていた。

**新しい事実として、複製laneの不一致が出た。** lane 8〜15はlane 0〜7と同じGPIOを入力しているので、16-bit sampleの上位byteは下位byteと常に一致するはずである。ところが20 MHzでは不一致0だったのに対し、40 MHzで1,066件、48 MHzで373件、52 / 56 MHzで345 / 480件が出た。1,048,576 sampleに対して0.03〜0.10%である。

不一致の有無はrun長の散らばりと完全に対応している。

| run長 | 意味 | 複製lane不一致 |
|---|---|---:|
| 4〜4(20 MHz) | 信号遷移がsampling edgeから離れている | 0 |
| 3〜5(40 MHz以上) | 遷移がsampling edge付近に来ている | 345〜1,066 |

信号源の遷移回数はrun数と同じ262,144回なので、不一致は遷移1回あたり0.13〜0.41%である。**同一のGPIOを二つのlane slotへ入力しても、遷移の瞬間には両者が別の値を読むことがある。** 原因はlane slot間の到達時間差か、二つの経路が遷移中の信号を独立に確定させるためのどちらかで、本実験では切り分けていない。

連番違反が40 / 48 MHzで0のままなのは、検証を下位byteのgray codeで行っているためである。gray codeでは遷移中のsampleは必ず前後どちらかの値になるので、run長が3や5に揺れても違反にはならない。**同じ現象がgray検証には現れず複製lane検証にだけ現れた**ことが、両方を同時に測った意味である。

E031は8 MHz・100 kHz PWMの信号源で複製lane不一致0を記録している。100 kHzは8 MHz samplingの1/80であり遷移がsampling周期に対して極めて疎なので、この現象は現れなかった。

## 判定

**16 channelのtriggerなしbatchは48 MHz(95.9 MB/s)までsample単位で成立し、52 MHz以上ではring未読がring容量を超えてsampleを失う。** 限界matrixの16 channel行はduty/edge検証からsample単位検証へ格上げできる。

同時に、この実験は**channel間のedge一致精度を初めて測った**。同一信号を二つのlaneへ入れても、遷移の瞬間には0.1〜0.4%の確率で1 sample分の食い違いが出る。実装上の帰結は次のとおりである。

- **channel間のedge位置は±1 sampleの精度で申告する。** それより細かいtiming差をsample列から読み取ってはならない
- **sample rateを上げるほどこの食い違いは出やすくなる。** 20 MHzでは0件、40 MHz以上では常に出た。遷移がsampling周期に対して速くなるためである
- **protocol decodeでは、同時に変わるべき複数channelが1 sampleずれて見える前提を置く。** 例えばclockとdataが同時に変わるbusでは、setup/hold判定をsample列から行うのは危険である

1 / 2 / 4 channelの行はまだduty/edge検証のままだが、byte rateがそれぞれ20 / 40 / 80 MB/sで、160 MHz設定でもspool帯域に余裕がある。E036とE042でmodelがwidthをまたいで成立したので、再検証の優先度は下げてよい。ただしrunの揺れとlane間食い違いは狭幅でも同じように起こるはずで、そこは別の問いである。

## 事実・候補・未決

**事実**

1. 16 channelは48 MHz設定(実効95.884 MB/s)まで連番違反0・ring未読4,928 byteで成立した。52 MHzはring未読134,848 byteでring容量65,536を超え、違反44件を出した。
2. spool rateは52 / 56 MHzで97.6 / 96.9 MB/sに飽和した。8 channelの約98 MB/sと一致し、律速はpacking後のbyte rateである。
3. PARLIO TX data_width 8とRX data_width 16は同一group・同一GPIOで共存し、上位byteが下位byteの複製として復元できた。
4. **複製laneの不一致は20 MHzで0件、40 / 48 / 52 / 56 MHzで1,066 / 373 / 345 / 480件だった。** 遷移1回あたり0.13〜0.41%である。不一致の有無はrun長の散らばり(4固定か3〜5か)と完全に対応した。
5. gray code検証は40 / 48 MHzで違反0のままだった。遷移中のsampleは前後どちらかの値になるため、gray検証には現れず複製lane検証にだけ現れる。

**候補**: 限界matrixの16 channel行をsample単位検証済みにする。channel間のedge位置は±1 sampleの精度として申告し、それ以上細かいtiming差をsample列から読まない方針を明記する。1 / 2 / 4 channelはbyte rateに余裕があるため再検証の優先度を下げる。

**未決**: 複製lane不一致の原因(lane slot間の到達時間差か遷移中信号の独立確定か) / 不一致率のrate依存性を細かく測ること / 1 / 2 / 4 channelでのrun揺れとlane間食い違い / 独立した16 padでの同じ測定(要配線) / 48〜52 MHz間の1 Mi境界 / 深度を変えたときのburst境界。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): 限界matrixの16 channel行から※を外し、channel間edge一致精度の項目を追加する
- [E031](../e031_p4_parlio_channel_width/README.ja.md): 複製lane不一致0という記録の成立条件を追記で限定する
- [LEDGER](../LEDGER.ja.md): E042の節
