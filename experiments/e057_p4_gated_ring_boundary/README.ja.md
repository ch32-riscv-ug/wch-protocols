# E057 ESP32-P4 条件2の境界をalias から外したringで実測する

状態: **完了 — 境界はgate 3,000と4,000の間。容量は完全chunk数**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E056](../e056_p4_ring_period_alias/README.ja.md)・[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)・[E049](../e049_p4_gated_window_absorption/README.ja.md)

## 問い

**検証用patternのalias から外したringで、gated captureの条件2の境界はどこにあるか。**

```
条件2 window byte長 × (1 − window中のdrain帯域 ÷ sample rate) < min(ring容量, queue深さ × chunk size)
```

## 仮説

[E056](../e056_p4_ring_period_alias/README.ja.md)で、これまで使ってきたring容量65,536と131,072が検証用gray code rampのsample周期4,096 byteの整数倍であり、ringの上書きが検証器に見えていなかったことが分かった。そのため[E049](../e049_p4_gated_window_absorption/README.ja.md)と[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)の一部条件のdata検証は無効で、条件2の境界は未読の値からの推論に留まっている。

ring容量を63,488(周期の15.5倍)にすれば上書きは飛びとして現れる。E056でこのring容量が実際に破損を露出させることは確認済みである。

dutyを50%に固定し(gapをwindowと同じ長さにする)、gate幅だけを振る。sample rate 160 MHz、TX 5 MHzでrun長32、data_width 8なので`window byte長 = gate幅 × 32`である。window中のdrain帯域は[E049](../e049_p4_gated_window_absorption/README.ja.md)の未読とwindow長の比から求めた約82 MB/sを使う。

| gate幅 | window byte | 予測尖頭未読(× 0.4875) | ring 63,488との比較 | 予測 |
|---:|---:|---:|---|---|
| 1,000 | 32,000 | 15,600 | 下 | 正常 |
| 2,000 | 64,000 | 31,200 | 下 | 正常 |
| 3,000 | 96,000 | 46,800 | 下 | 正常 |
| 4,000 | 128,000 | 62,400 | **すぐ下** | 正常 |
| 5,000 | 160,000 | 78,000 | **上** | **破綻** |

**境界はgate 4,000と5,000の間**というのが条件2の予測である。dutyは全条件50%なので平均byte rateは80 MB/sで、条件1(持続spool帯域約98 MB/s)は全条件で満たされる。queueは64 entry(258,048 byte)固定なので、queue側の容量も全条件で足りる。**したがってringだけが効く条件になる。**

## 反証条件

- gate 5,000が正常(条件2の容量にringが入らない)
- gate 4,000以下で破綻する(境界が予測より手前)
- 実測の尖頭未読が予測から大きく外れる
- queue overflowが出る(queueが律速になり切り分けにならない)
- ring 63,488がAPIに拒否される

## 方法

[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)の構成からring容量だけを変え、gate幅の掃引範囲を境界付近へ寄せる。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`。`max_recv_size`と受信sizeは**63,488**
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは`gate幅 × 2`で先頭`gate幅`だけbit 7をhigh(duty 50%)。bit 0〜6は`gray7(index & 0x7F)`
- queue深さは64固定、回収は100 msの固定window、destinationは4 MiB PSRAM
- gate幅掃引: 1,000 / 2,000 / 3,000 / 4,000 / 5,000
- 予測尖頭未読はwindow中のdrain帯域82 MB/sで計算して記録する

期待するwindow境界数は`回収byte ÷ window byte長`で、4,194,304に対し131 / 65 / 43 / 32 / 26である。飛びがこれを大きく超えれば破損である。

APIの不成立も結果として最後まで記録する。

## 対象外

queue深さの掃引(E055で測済み)、sample rateとdutyの掃引、ring容量の細かい掃引、window中のdrain帯域の由来、triggerなしspool経路での同じ確認。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

gate幅、loop長、window byte長、予測尖頭未読、各API結果、PARLIO callback数・dequeue数、queue overflow、未読最大、最大chunk offset、bit 7が0のsample数、飛びの総数と期待境界数、階差一致数、回収byte、回収rate、経過us。

## 完了条件

条件2の境界がgate 4,000と5,000の間にあるかを確定する。外れる場合は実測の境界と予測のずれを記録して完了とする。

## 影響

境界が予測どおりなら、条件2は`window byte長 × (1 − drain ÷ rate) < min(ring容量, queue深さ × chunk size)`として実測で裏付けられ、gated captureのcapabilityが計算で申告できるようになる。ずれるなら、window中のdrain帯域の見積り(82 MB/s)か容量の取り方を見直すことになる。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)の条件2に実測の裏付けが付く。

## 結果

実施日: 2026-09-09

採用run: `_runs/E057_20260909T102539Z_default/test_gated_ring_boundary/dut.log`

| gate幅 | window byte | 予測尖頭未読 | 実測未読最大 | ring超過flag | 飛び | 期待境界数 | 階差一致 | 判定 |
|---:|---:|---:|---:|:-:|---:|---:|---:|---|
| 1,000 | 32,000 | 15,600 | 19,136 | — | 131 | 131 | 15 / 15 | 正常 |
| 2,000 | 64,000 | 31,200 | 32,256 | — | 66 | 65 | 15 / 15 | 正常 |
| 3,000 | 96,000 | 46,800 | 47,360 | — | 44 | 43 | 15 / 15 | 正常 |
| 4,000 | 128,000 | 62,400 | 59,456 | — | **47** | 32 | **10 / 15** | **破綻** |
| 5,000 | 160,000 | 78,000 | 75,584 | **超過** | **79** | 26 | **1 / 15** | **破綻** |

**境界はgate 3,000と4,000の間だった。** 計画の予測(4,000と5,000の間)より1段手前である。queue overflowは全条件で0なので、ringだけが効く条件になっている。

**alias 対策は効いている。** gate 1,000〜3,000では飛びの数が期待境界数と一致し(131 / 66 / 44 対 131 / 65 / 43、差は先頭の部分windowの分)、階差も15 / 15一致した。[E056](../e056_p4_ring_period_alias/README.ja.md)のalias 構成では見えなかった破綻が、gate 4,000と5,000で飛びの急増(47と79、期待32と26)と階差の不一致(10 / 15と1 / 15)として現れている。

**予測が1段外れた理由は容量の取り方だった。** 計画はring容量63,488をそのまま容量として使った。しかしchunk sizeは4,032 byteなので、ringに入る完全なchunkは`floor(63,488 ÷ 4,032)` = 15個、つまり**60,480 byteである**。末尾の3,008 byteには完全なdescriptorが載らない。

| gate幅 | 予測尖頭未読 | 必要chunk数 | ring 63,488の完全chunk数 | 予測 | 実測 |
|---:|---:|---:|---:|---|---|
| 3,000 | 46,800 | 12 | 15 | 正常 | **正常** |
| 4,000 | 62,400 | **16** | 15 | **破綻** | **破綻** |
| 5,000 | 78,000 | 20 | 15 | 破綻 | 破綻 |

**chunk数で数えると境界は実測どおりgate 3,000と4,000の間になる。**

**実測の未読最大は真の尖頭を過小に見ている。** gate 4,000は破綻しているのに未読最大59,456で、完全chunk容量60,480すら下回っている。未読は`dequeue`ごとに1回しか標本化していないので、その間の尖頭を取り逃す。**破綻の判定には実測の未読ではなく、window長から計算した予測尖頭を使うべきである。** `未読 > ring容量`というflagはgate 5,000でしか立たず、gate 4,000の破綻を見逃した。

## 判定

**条件2は、容量を完全chunk数で数えた形で実測と一致する。**

```
必要chunk数 = ceil(window byte長 × (1 − window中のdrain帯域 ÷ sample rate) ÷ chunk size)
成立条件    = 必要chunk数 ≤ min(floor(ring容量 ÷ chunk size), queue深さ)
```

chunk sizeは4,032 byte、window中のdrain帯域は約82 MB/sである。この形で先行実験の全条件を照合すると次のようになる。

| 実験・条件 | window byte | 必要chunk | ring完全chunk | queue深さ | 予測 | 実測 |
|---|---:|---:|---:|---:|---|---|
| 本実験 gate 3,000 | 96,000 | 12 | 15 | 64 | 正常 | **正常** |
| 本実験 gate 4,000 | 128,000 | 16 | 15 | 64 | 破綻 | **破綻** |
| 本実験 gate 5,000 | 160,000 | 20 | 15 | 64 | 破綻 | **破綻** |
| [E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md) gate 4,000 | 128,000 | 16 | 16 | 64 | 正常(境界上) | **正常** |
| [E055](../e055_p4_gated_buffer_source/README.ja.md) ring 64 KiB / queue 64 | 192,000 | 24 | 16 | 64 | 破綻 | **破綻**([E056](../e056_p4_ring_period_alias/README.ja.md)で確認) |
| E055 ring 128 KiB / queue 64 | 192,000 | 24 | 32 | 64 | 正常 | **正常** |
| E055 queue 8の2条件 | 192,000 | 24 | 16 / 32 | 8 | 破綻 | **破綻** |
| [E048](../e048_p4_gated_rate_ceiling/README.ja.md) 160 MHz | 65,408 | 8 | 16 | 64 | 正常 | **正常** |

**8条件すべてが一致する。** ringとqueueはどちらも「完全chunkがいくつ入るか」で効き、`min()`を取る形が正しい。E050のgate 4,000が境界上(必要16、ring完全chunk 16)で正常だったので、条件は`≤`である。

実装への帰結。

- **ring容量はchunk sizeの整数倍で取る。** 端数は使えないので無駄になる
- **drop判定は実測の未読ではなくwindow長からの計算で行う。** 実測の未読は標本化のため過小に出る
- 現在の構成(chunk 4,032 byte、drain 82 MB/s、160 MHz)では、`min(ring完全chunk数, queue深さ)`が必要chunk数を満たすようにringとqueueを決める

## 記録した不具合(修正済み)

最初のrunは、この実験のhost側判定に3つの誤りがあって失敗した。firmwareの計測は完走している。

1. `ring_mod_period`のfieldを追加したのに、host側のunpackが旧E050の並び(field 1つ分ずれ)のままだった
2. metricsの取り出し範囲も1つずれていた
3. 削除した定数`SPOOL_MBPS`を参照する行が残っていた

あわせてfirmware側で、[E049](../e049_p4_gated_window_absorption/README.ja.md)から引き継いだ`build_pattern`の部分msyncがcache line境界に載らずerror logを出していたのを、buffer全体をsyncする形へ直した([E051](../e051_p4_rmt_partial_threshold/README.ja.md)で見つけた同じ不具合)。採用runではcache errorは0件である。失敗runは`_runs/E057_20260909T102331Z_default/`等に残した。

## 事実・候補・未決

**事実**

1. **境界はgate 3,000(正常)と4,000(破綻)の間だった。** 計画の予測(4,000と5,000の間)より1段手前である。queue overflowは全条件0で、ringだけが効く条件になっていた。
2. alias から外したring 63,488で、gate 1,000〜3,000は飛びが期待境界数と一致し階差も15 / 15、gate 4,000と5,000は飛びが47と79(期待32と26)へ急増し階差一致が10 / 15と1 / 15へ落ちた。
3. **予測が1段外れたのは容量の取り方が原因だった。** chunk 4,032 byteに対しring 63,488に入る完全chunkは15個(60,480 byte)で、末尾3,008 byteは使えない。必要chunk数で数えると境界は実測どおりになる。
4. **実測の未読最大は真の尖頭を過小に見ている。** gate 4,000は破綻しているのに未読最大59,456で完全chunk容量60,480を下回る。`未読 > ring容量`のflagはgate 5,000でしか立たず、gate 4,000の破綻を見逃した。
5. **条件2の最終形は`ceil(window byte長 × (1 − drain ÷ rate) ÷ chunk size) ≤ min(floor(ring容量 ÷ chunk size), queue深さ)`である。** 本実験・E048・E050・E055の8条件すべてがこの形で一致する。E050のgate 4,000が境界上で正常だったので条件は`≤`である。
6. host側判定の誤り3件とfirmwareのmsync非整列1件を修正した。firmwareの計測値は最初のrunから変わっていない。

**候補**: gated captureのcapabilityを`必要chunk数 ≤ min(floor(ring ÷ chunk), queue深さ)`で申告する。ring容量はchunk sizeの整数倍で取る。drop判定は実測の未読ではなくwindow長からの計算で行う。

**未決**: window中のdrain帯域82 MB/sの由来 / chunk sizeが4,032固定である根拠 / 実測の未読を尖頭まで捉える標本化方法 / triggerなしspool経路にも同じchunk数の形が当てはまるか / duty 61%付近での条件1の実測。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): 条件2を完全chunk数の形へ直し、8条件の照合表を載せる
- [LEDGER](../LEDGER.ja.md): E057の節

## 追記 — E058による標本化の改善(2026-09-09)

本レポートは書き換えない。[E058](../e058_p4_window_drain_vs_rate/README.ja.md)がISR内で未読を標本化した結果、**task側の標本化はちょうど1 chunk(4,032 byte)分だけ尖頭を見落としている**ことが分かった。未読が増えるのはISRがchunkを通知する瞬間だけなので、そこで測れば真の尖頭が取れる。本レポートの未読最大に4,032を足したものが実際の値に近い。

またE058は尖頭の形も精密化した。

```
尖頭未読 = 8,064 + window byte長 × (1 − drain(rate) ÷ rate)
drain(rate) ≈ 160 MHzで86 MB/s、120 MHzで96 MB/s、100 MHz以下で100 MB/s以上
```

床8,064 byte(2 chunk)はchunk通知とqueue投入のpipeline分で、過負荷とは別に常に乗る。この形で本レポートの境界を照合すると、gate 3,000は14 chunk ≤ 15で正常、gate 4,000は17 chunk > 15で破綻となり実測どおりである。本レポートが使った「drain 82 MB/s・床なし」は結論としては同じだが、160 MHzでは尖頭を1〜2 chunk小さく見積もるので安全側ではない。
