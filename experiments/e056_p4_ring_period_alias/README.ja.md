# E056 ESP32-P4 ring容量とpattern周期のalias

状態: **完了 — alias で盲だった。ringは実際に上書きされている**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E055](../e055_p4_gated_buffer_source/README.ja.md)・[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)・[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)

## 問い

**ring容量を信号pattern周期の倍数から外すと、未読がring容量を超える条件でdataの破損が現れるか。つまりE048以降の「未読がring容量を超えてもdataは正常」という観測は、検証器がalias で盲になっていた結果ではないか。**

## 仮説

[E055](../e055_p4_gated_buffer_source/README.ja.md)は、ring 64 KiBで未読が93,760 byteに達してもdataが完全に正常だったことを未解明として残した。chunk offsetは`max_recv_size`まで線形に歩くので、次のtransactionで先頭から書き直す時点で未読の古いchunkは上書きされるように見える。

しかしここまでの実験の信号源はbit 0〜6の`gray7(index & 0x7F)`で、**source側の周期は128 wordである**。sample rate 160 MHz・TX 5 MHzではrun長が32なので、sample列でのpattern周期は128 × 32 = 4,096 sample = data_width 8で**4,096 byte**になる。

ここで使ってきたring容量を割ると、

| ring容量 | ÷ 4,096 |
|---:|---:|
| 65,536 | **16.0** |
| 131,072 | **32.0** |

**どちらもちょうど整数倍である。** ringが一周して位置Xを`X + ring容量`のdataで上書きしても、patternの周期がring容量を割り切るので**同じ値**が書かれる。つまり上書きが起きていても検証器には何も見えない。

これは[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)が100 kHz PWMの定常性について指摘した盲点と同じ構造である。ring容量を周期の倍数から外せば、上書きはgray stepの飛びとして現れるはずである。

case:

| # | ring容量 | ÷ 4,096 | 予測 |
|---:|---:|---:|---|
| 1 | 65,536 | 16.0 | 正常に見える(E055の再現、alias で盲) |
| 2 | **63,488** | **15.5** | **破損が現れる** |
| 3 | 61,440 | 15.0 | 正常に見える(alias で盲) |
| 4 | **59,392** | **14.5** | **破損が現れる** |

尖頭の未読は約93,760 byteでwindowの過負荷から決まり([E055](../e055_p4_gated_buffer_source/README.ja.md))、4条件すべてのring容量を超えている。queueは64 entry(258 KiB)固定なので条件2は満たされ、queueは律速にならない。

奇数倍のcaseで飛びが期待するwindow境界数を大きく超えるなら、**E048からE055までの「未読 > ring容量でもdataは正常」は検証器の盲点であり、実際には上書きが起きていた**ことになる。

## 反証条件

- 4条件すべてで飛びが期待境界数のまま(alias ではなく本当に上書きされていない)
- 倍数のcaseで破損が現れる(alias の説明が成り立たない)
- ring容量63,488 / 59,392がAPIに拒否される
- queue overflowが出る(queueが律速になってしまい切り分けにならない)

## 方法

[E055](../e055_p4_gated_buffer_source/README.ja.md)の構成でqueue深さを64に固定し、ring容量だけを変える。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`。`max_recv_size`と受信sizeはcaseのring容量
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは12,000 wordで先頭6,000だけbit 7をhigh(duty 50%、平均80 MB/s)
- 回収は100 msの固定window、destinationは4 MiB PSRAM
- ring容量掃引: 65,536 / 63,488 / 61,440 / 59,392(いずれも64 byteの倍数)
- pattern周期(4,096 byte)とring容量の剰余を記録する

期待するwindow境界数は`回収byte ÷ window byte長` = 4,194,304 ÷ 192,000 ≒ 21.8である。飛びがこれを大きく超えれば破損である。

APIの不成立も結果として最後まで記録する。

## 対象外

triggerなしspool経路での同じ確認、dutyとgate幅の掃引、sample rateの掃引、queue深さの掃引、pattern周期そのものを変える方法、破損量の定量。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、ring容量、pattern周期、剰余、各API結果、PARLIO callback数・dequeue数、queue overflow、未読最大、最大chunk offset、bit 7が0のsample数、飛びの総数と期待境界数、階差一致数、回収byte、回収rate、経過us。

## 完了条件

ring容量が周期の倍数でない条件で破損が現れるか否かを確定する。現れるなら、未読がring容量を超える条件のdata検証が無効だったことを確定し、影響を受ける先行実験を列挙する。

## 影響

破損が現れるなら、[E048](../e048_p4_gated_rate_ceiling/README.ja.md)・[E049](../e049_p4_gated_window_absorption/README.ja.md)・[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)・[E055](../e055_p4_gated_buffer_source/README.ja.md)のうち未読がring容量を超えていた条件のdata検証は無効になり、**drop判定はring容量基準へ戻る**。E055が示したqueueの効果(浅くすると破綻)は変わらないが、ring容量も独立に効くことになる。現れないなら、ringが上書きされない機序を別に探すことになる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E056_20260909T100537Z_default/test_ring_period_alias/dut.log`

| ring容量 | 剰余(÷4,096) | 飛び(期待22) | 階差一致 | queue overflow | 未読最大 |
|---:|---:|---:|---:|---:|---:|
| 65,536 | **0** | **22** | 15 / 15 | 0 | 93,760 |
| 63,488 | **2,048** | **172** | **0 / 15** | 0 | 91,712 |
| 61,440 | **0** | **22** | 15 / 15 | 0 | 93,696 |
| 59,392 | **2,048** | **86** | **0 / 15** | 0 | 91,648 |

**仮説どおりだった。** pattern周期4,096 byteの整数倍のring(65,536と61,440)は飛びが期待どおり22で階差も15 / 15一致する。倍数から外したring(63,488と59,392)は飛びが172と86に跳ね、階差の一致は0 / 15になった。**4条件すべてqueue overflowは0**なので、queueは律速になっていない。

つまり**ringは未読がring容量を超えた時点で実際に上書きされており、これまで「正常」と判定してきたのは検証器がalias で盲になっていたためである。** ring容量がpattern周期を割り切ると、位置Xを`X + ring容量`のdataで上書きしても同じ値が書かれるので痕跡が残らない。

## 判定

**`未読 > ring容量`はやはり破綻の指標である。** [E055](../e055_p4_gated_buffer_source/README.ja.md)で「指標ではない」と結論したのは誤りだった。

正しい条件は次のとおりである。

```
条件1(平均) duty × sample rate × bytes/sample < 持続spool帯域(約98 MB/s)
条件2(尖頭) window byte長 × (1 − window中のdrain ÷ sample rate) < min(ring容量, queue深さ × chunk size)
```

条件2は[E048](../e048_p4_gated_rate_ceiling/README.ja.md)が最初に立てたmodelそのものであり、**容量が`min(ring容量, queue深さ × chunk size)`である**点だけが違う。E048のmodelは形としては正しかった。

### 影響を受ける先行実験

未読がring容量を超えていた条件のdata検証は無効である。未読の実測値で分けると次のようになる。

| 実験・条件 | 未読最大 | ring容量 | 判定 |
|---|---:|---:|---|
| [E048](../e048_p4_gated_rate_ceiling/README.ja.md) 全6 rate | 最大33,280 | 65,536 | **有効**(全条件でring以内) |
| [E049](../e049_p4_gated_window_absorption/README.ja.md) gate 2,044 / 4,000 | 33,280 / 61,504 | 65,536 | **有効** |
| E049 gate 5,000 / 6,000 | 77,632 / 92,288 | 65,536 | **無効**(正常と判定したが超過) |
| E049 gate 8,000 | 442,624 | 65,536 | 破綻の判定自体は変わらない |
| [E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md) gate 1,000 / 2,000 / 4,000 | 18,688 / 33,280 / 61,504 | 65,536 | **有効** |
| E050 gate 6,000 / 7,000 | 92,288 / 106,880 | 65,536 | **無効** |
| [E055](../e055_p4_gated_buffer_source/README.ja.md) ring 64 KiB / queue 64 | 93,760 | 65,536 | **無効** |
| E055 ring 128 KiB / queue 64 | 90,752 | 131,072 | **有効** |
| E055 queue 8の2条件 | 1.9 MB / 1.8 MB | — | 破綻の判定自体は変わらない |

ここから二つの結論が覆る。

**[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)の「window長は無関係」は成り立たない。** duty 50%固定でも、gate 4,000(未読61,504)は有効に正常でgate 6,000(92,288)は無効だった。条件2で計算すると、window中のdrain帯域82 MB/sに対しring 65,536が許すwindow byte長は`65,536 ÷ (1 − 82/160)` = 約134,000 byte、gate幅で4,200 word相当である。**gate 4,000が通りgate 6,000が通らないのは条件2の予測どおりである。**

**[E055](../e055_p4_gated_buffer_source/README.ja.md)の「ring容量を倍にしても結果は変わらない」も成り立たない。** ring 64 KiB / queue 64(未読93,760)は実際には破損しており、ring 128 KiB / queue 64(未読90,752)は正常である。**ring容量を倍にしたことで正常になっていた。** ただしE055が示した「queueを浅くすると破綻する」ことは変わらない。**ringとqueueは両方が独立に効く。**

**[E049](../e049_p4_gated_window_absorption/README.ja.md)の境界も動く。** E049は破綻の起点を平均byte rate(gate 6,000の96.0 MB/sは正常、8,000の106.7 MB/sは破綻)としたが、条件2で見るとgate 5,000(未読77,632)から既にringを超えている。実測の境界はgate 4,000と5,000の間になる。ただしE049はalias のringで測っているので、この境界は未読の値からの推論であり実測ではない。**確認には倍数から外したringでの再測が必要である。**

### 検証器の教訓

[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)は、定常・周期的な100 kHz PWMではsample単位の欠落が見えないことを指摘してgray code rampへ替えた。今回はそのgray code rampが、**周期がbuffer sizeを割り切るという別の形のalias** で盲になっていた。

一般化すると、**検証用patternの周期は経路上のどのbuffer sizeも割り切ってはならない。** ring容量、chunk size、queue容量、destination容量のいずれかがpattern周期の倍数だと、その単位でのずれが見えなくなる。

triggerなしspool経路の[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)と[E042](../e042_p4_parlio_16ch_seq_verify/README.ja.md)は、pattern周期がring容量を割り切る構成(1,024 byteと2,048 byteに対しring 65,536)でありながら破綻を検出できていた。gated captureとの違いは、持続的な過負荷ではringの周回と読み出し位置の遅れが一定にならないため、上書き量がring容量の整数倍にならないことだと考えられる。**両実験の結論は変わらない**が、この違いは仮説であり切り分けていない。

## 事実・候補・未決

**事実**

1. **pattern周期4,096 byteの整数倍のring(65,536 / 61,440)では飛びが期待どおり22で階差も15 / 15一致した。倍数から外したring(63,488 / 59,392)では飛びが172と86に跳ね階差一致は0 / 15になった。** 4条件すべてqueue overflowは0である。
2. **ringは未読がring容量を超えた時点で実際に上書きされている。** これまでの「正常」判定は検証器のalias による盲点だった。
3. **`未読 > ring容量`は破綻の指標である。** E055の「指標ではない」は誤りだった。
4. 正しい条件は`条件1(平均)`と`条件2(尖頭)`の2本で、条件2の容量は`min(ring容量, queue深さ × chunk size)`である。**E048のmodelは形としては正しかった。**
5. E050の「window長は無関係」は成り立たない。duty 50%固定でもgate 4,000は正常でgate 6,000は無効である。条件2が許すwindow byte長は約134,000(gate幅4,200 word相当)で、実測の分かれ目と一致する。
6. E055の「ring容量を倍にしても変わらない」も成り立たない。ring 128 KiBにしたことで正常になっていた。**ringとqueueは両方が独立に効く。**
7. 検証用patternの周期は経路上のどのbuffer sizeも割り切ってはならない。

**候補**: gated captureのcapabilityを条件1と条件2の2本で申告し、条件2の容量を`min(ring容量, queue深さ × chunk size)`とする。今後の検証patternは周期がring容量・chunk size・queue容量・destination容量のいずれも割り切らないように選ぶ。

**未決**: **倍数から外したringでのE049・E050の再測**(境界の実測確定) / triggerなしspool経路がalias で盲にならなかった理由 / window中のdrain帯域82 MB/sの由来 / 条件2の境界をring容量付近で細かく測ること。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): gated captureの条件を2本立てへ直し、容量を`min(ring, queue × chunk)`にする。検証patternの選び方を注意として書く
- [E049](../e049_p4_gated_window_absorption/README.ja.md)・[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)・[E055](../e055_p4_gated_buffer_source/README.ja.md): 該当条件のdata検証が無効であることを追記する
- [LEDGER](../LEDGER.ja.md): E056の節
