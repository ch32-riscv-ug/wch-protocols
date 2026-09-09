# E038 ESP32-P4 hardware pulse triggerのrate上限

状態: **完了 — 160 MHzまで成立。上限は内部clock源**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E037](../e037_p4_parlio_pulse_trigger/README.ja.md)・[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)

## 問い

**pulse delimiterによるhardware triggerでinternal RAMへ有限frameを取るとき、frameが欠落なく成立する最大sample rateはどこか。**

## 仮説

E036で確定した約98 MB/sの限界は、internal DMA ring → PSRAMをtaskがcopyする経路の限界だった。E037の有限frameはDMAがinternal RAMへ一度書くだけでcopy段が無いので、この限界は効かない。したがってhardware trigger付きの有限frameは、E036のspool経路より高いrateまで成立するはずである。sampling側の構造上の天井は内部clock源PLL_F160Mの160 MHzである。

data_width 4ではpacking後のbyte rateがsample rateの半分なので、160 MHz設定でも80 MB/sである。E020で測ったinternal RAM帯域から見て、DMAのwrite先としては余裕がある。

またvalid線を含めて5線で足りる構成は、`TEST_PARLIO_PINS`の8線の内側に収まる。driverが受け付けるwidthは1 / 2 / 4 / 8 / 16なので、8線でhardware triggerを使える最大widthは4である。8 channel + valid線には9線が必要で、現在の宣言では作れない。

rateの実測方法をE037から変える。source loop周期はframe長のちょうど2倍(frame 32,768 sample、loop 65,536 sample相当)なので、1 caseで2 frameを連続してqueueに積むと、2つの`on_receive_done`の時刻差がsource loop周期そのものになる。この差から`65,536 ÷ 差`でsample rateを直接求められる。armしてからpulseが来るまでの待ちは差に入らない。

## 反証条件

- 高いrateでframeが始まらない、または`eof_data_len`で止まらない
- 2 frame目がqueueから開始しない(時刻差が取れない)
- 時刻差から求めたrateが設定rateから外れる
- gray step違反が出る、run長が4から外れる
- 2 frame間でheadが一致しない(trigger位置が再現しない)

## 方法

E037の構成を固定したまま、RX rateだけ掃引する。

- RX: data_width 4、`data_gpio_nums[0..3]` = GPIO 2〜5、`valid_gpio_num` = GPIO 6、`valid_sig_line_id` = 4
- TX: data_width 8、GPIO 2〜9、`output_clk_freq_hz` = RX rate ÷ 4、16,384 wordを`loop_transmission`
  - bit 0〜3: `gray4(index & 0xF)`、bit 4: index 2048から4 wordだけhigh
- delimiter: pulse delimiter、`eof_data_len` 16,384 byte(32,768 sample)、`timeout_ticks` 0、`has_end_pulse=false`
- 1 caseで16 KiBのpayloadを2枚使い、`parlio_rx_unit_receive`を2回積む(`trans_queue_depth` 2)。両frameを検証する
- 完了待ちは`parlio_rx_unit_wait_all_done`の1,000 ms software timeout
- RX rate掃引: 20 / 40 / 80 / 100 / 120 / 160 MHz

APIの不成立も結果として最後まで記録する。

## 対象外

data_width 8 / 16でのhardware trigger(必要pin数が現在の宣言を超える)、level delimiter、`has_end_pulse`、`pulse_invert`、PSRAMへの直接受信、`eof_data_len` 65,535超のpost長、pre-trigger、software triggerとの併用、連続frameのsoak。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

RX設定rate、TX設定rate、各API結果、`wait_all_done`結果、receive_done回数、frameごとの受信byte、2 frameの時刻差、時刻差から求めたsample rate、設定rateとの比、run数、run長min/max、gray step違反数、frameごとの先頭4 sample、経過us。

## 完了条件

各rateについて成立・不成立と実測rateを記録し、hardware trigger付き有限frameの最大sample rateを確定する。E036のspool経路の限界(約98 MB/s)を超えるかどうかを明示する。

## 影響

[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のTrigger節にあるhardware pulse tierの「rate上限は未測定」を実測値へ置き換える。有限frameとspool captureで律速が違うことが確定すれば、capabilityは「取得方式ごとにrateが違う」という形になる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E038_20260909T031300Z_default/test_parlio_pulse_trigger_rate/dut.log`

| RX設定 | TX設定 | frame間隔 | 実測rate | 設定比 | runs(frame0 / 1) | run長 | 違反 | head(frame0 / 1) | 経過 |
|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|
| 20 MHz | 5 MHz | 3,277 us | 19,998,779 Hz | 0.999 | 8,192 / 8,192 | 4〜4 | 0 | 同一 | 5,336 us |
| 40 MHz | 10 MHz | 1,638 us | 40,009,768 Hz | 1.000 | 8,192 / 8,192 | 4〜4 | 0 | 同一 | 2,673 us |
| 80 MHz | 20 MHz | 819 us | 80,019,536 Hz | 1.000 | 8,192 / 8,192 | 4〜4 | 0 | 同一 | 1,342 us |
| 100 MHz | 25 MHz | 655 us | 100,054,961 Hz | 1.000 | 8,192 / 8,191 | 3〜5 | 0 | 同一 | 1,076 us |
| 120 MHz | 30 MHz | 546 us | 120,029,304 Hz | 1.000 | 8,192 / 8,192 | 3〜5 | 0 | 1 sampleずれ | 898 us |
| 160 MHz | 40 MHz | 409 us | 160,234,718 Hz | 1.001 | 8,192 / 8,192 | 4〜4 | 0 | 同一 | 677 us |

**6条件すべてが成立した。** 全APIが`ESP_OK`、`on_receive_done`は各caseで2回、受信byteは両frameとも`eof_data_len` 16,384と完全一致、gray step違反は全条件で0件だった。

frame間隔から求めた実測rateは全条件で設定の99.9〜100.1%だった。これはcallback数からの推定(E036)ではなく、source loop周期という独立した時間基準による絶対測定である。**PARLIO RXのsample clockが160 MHzまで設定どおり動いていることが、これで直接確認された。**

run長は20 / 40 / 80 / 160 MHzで厳密に4固定、100 / 120 MHzでは3〜5に散った。前者は160 MHzの整数分周(÷8 / ÷4 / ÷2 / ÷1)、後者は非整数分周(÷1.6 / ÷1.333)である。TX側も同じ関係(5 / 10 / 20 / 40 MHzが整数分周、25 / 30 MHzが非整数)なので、この散らばりがRX側の分周かTX側の分周かは本実験では切り分けられない。ただしどちらであっても、**160 MHzの整数分周でないrateではsample間隔が±1 sample揺れる**という観測は成立する。

trigger位置の再現性も同じ境界で分かれた。整数分周のrateでは2 frameのheadが完全に一致し、120 MHzだけ1 sampleずれた(`00001100`と`00001000`)。

## 判定

**hardware pulse trigger付きの有限frameは、data_width 4で内部clock源の上限160 MHzまで欠落なく成立する。E036の約98 MB/sという限界はこの経路には効かない。**

E036の限界はinternal ring → PSRAMをtaskがcopyする段のものだった。有限frameはDMAがinternal RAMへ一度書くだけでcopy段が無いため、sample rateの制約は内部clock源そのもの(PLL_F160M)まで後退する。

ただし帯域について過大に読まないこと。data_width 4では160 MHz設定でもpacking後は80 MB/sであり、**98 MB/sを超えるbyte rateはこの実験では試していない**。「内部RAMへの有限frameがspool経路よりbyte rateで速い」ことは示していない。示したのは、4 channelではbyte rateが制約にならず、sample rateの天井がclock源であることである。data_width 8で160 MHz(160 MB/s)が通るかは別の問いで、valid線を含めて9線が必要なため現在の`TEST_PARLIO_PINS`では作れない。

実装上の帰結を三つ挙げる。

1. **hardware trigger tierの公称rateは160 MHzである。** software走査tierの24 MHzに対して6.7倍で、CPU負荷はゼロ。代償はvalid線1本、条件がpulse 1種類、pre-trigger不可、post長が`eof_data_len`の65,535 byte以内
2. **sample rateは160 MHzの整数分周から選ぶ。** 160 / 80 / 53.3 / 40 / 32 / 26.7 / 20 MHzなどではsample間隔が均一で、trigger位置も完全に再現した。非整数分周では±1 sampleの揺れが入る。logic analyzerの時間精度を申告するなら、この二種類は区別して出す
3. **frameの深度と引き換えである。** 16 KiB frameは160 MHzで205 us分にすぎない。深い取得はspool経路(98 MB/s、E036)に戻るので、「速いが浅い」と「遅いが深い」の二つのmodeになる

## 事実・候補・未決

**事実**

1. data_width 4 + valid線1本のhardware pulse triggerは、20 / 40 / 80 / 100 / 120 / 160 MHzの6条件すべてで成立した。受信byteは両frameとも`eof_data_len`と完全一致、gray step違反0件。
2. frame間隔から求めた実測rateは設定の99.9〜100.1%だった。source loop周期による絶対測定で、160 MHzまでsample clockが設定どおりであることを直接確認した。
3. run長は160 MHzの整数分周(20 / 40 / 80 / 160 MHz)で厳密に4固定、非整数分周(100 / 120 MHz)で3〜5に散った。RX側の分周かTX側かは切り分けていない。
4. trigger位置は整数分周のrateでは2 frameのheadが完全一致し、120 MHzだけ1 sampleずれた。
5. data_width 4の160 MHzはpacking後80 MB/sであり、98 MB/sを超えるbyte rateは試していない。

**候補**: capabilityを取得方式ごとに分ける。hardware trigger + 有限frame = 160 MHz / 深度65,535 byte以内 / pre-trigger不可、software走査 + spool = 24 MHz / 深度16 MiB / pre-trigger可。公称sample rateは160 MHzの整数分周に限定し、非整数分周は「±1 sample揺れ」を明記して別枠にする。

**未決**: data_width 8 / 16でのhardware trigger(valid線を含めて9 / 17線が必要。pin宣言の拡張から) / 有限frameのbyte rate上限(98 MB/s超が通るか) / `eof_data_len` 65,535超のpost長 / level delimiterのgating / `has_end_pulse`と`pulse_invert` / 連続frameのsoakと再arm周期 / hardware triggerとcircular ringの併用可否。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): Trigger節のhardware pulse tierに実測160 MHzを入れ、整数分周の制約と深度の引き換えを書く
- [LEDGER](../LEDGER.ja.md): E038の節

## 追記 — E041による判定範囲の限定(2026-09-09)

本レポートは書き換えない。[E041](../e041_p4_parlio_shared_valid_line/README.ja.md)の結果から、次の2点を限定する。

1. **「data_width 8のhardware triggerにはvalid線を含めて9線が必要」は誤りだった。** `valid_gpio_num`はdata線のいずれかと同一GPIOに設定でき、その線はdataとして記録されつつtriggerにも使える。8 channel + hardware triggerは8 pinで成立する。したがってhardware trigger tierの代償は「channel 1本」ではなく「trigger源となるchannelを1つ選ぶ」ことである。
2. **「160 MHzの整数分周ならrun長が均一」は十分条件ではない。** E041はdata_width 8で80 / 160 MHz(どちらも整数分周)のrun長が3〜5に散った。本レポートの6条件で整数分周と非整数分周がきれいに分かれたのは、二つの独立した分周器の位相関係がarmごとに変わることの一側面だった可能性がある。step違反は両実験とも0なので取得の正しさには影響しないが、**sample間隔の均一性を分周比だけから申告してはならない。**

またE041はdata_width 8の160 MHz(160 MB/s)で4,096 byteを違反0で取得したので、本レポートで「98 MB/s超のbyte rateは試していない」とした点は、少なくとも4 KiB burstについては解消した。持続帯域は未測定のままである。
