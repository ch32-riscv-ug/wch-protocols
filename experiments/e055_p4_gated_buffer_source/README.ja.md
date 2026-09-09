# E055 ESP32-P4 gated captureの緩衝はringかqueueか

状態: **完了 — 緩衝はqueue。ringは線形に歩く**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)・[E049](../e049_p4_gated_window_absorption/README.ja.md)・[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)

## 問い

**gated captureで実際に緩衝として働いているのは64 KiBのinternal ringか、64 entryのchunk queueか。またdriverはringを本当に周回して使っているのか。**

## 仮説

[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)のtriggerなしspool経路では、ring上の未読byte数がring容量を超えるとdataが壊れた。ところが[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)のgated captureでは、未読が92,288 byte(ring容量65,536の1.4倍)や106,880 byte(1.6倍)に達してもdataは1 byteも失われなかった。**「ring容量を超えたら壊れる」がgatedでは成り立たない。**

chunk queueは64 entryで、1 chunkが約4,032 byteなので約258 KiB分のpointerを保持できる。[E049](../e049_p4_gated_window_absorption/README.ja.md)で破綻した条件では未読が442,624 byteでこれを超え、queue overflowが51件出ていた。**queueが実際の緩衝である**なら、queueを浅くすれば未読が92 KBに達するE050の条件でも破綻するはずである。

もう一つ確かめる。未読92 KBが64 KiBのringに収まらないのにdataが正常なのは、driverがringを単純な周回buffer として使っていない可能性がある。callbackが渡す`chunk.data`のringからのoffsetを記録すれば、周回しているのか別の構造なのかが直接見える。

case:

| # | ring容量 | queue深さ | queueのpointer容量 | 予測 |
|---:|---:|---:|---:|---|
| 1 | 64 KiB | 64 | 約258 KiB | 正常(E050の再現) |
| 2 | 64 KiB | **8** | 約32 KiB | **queueが緩衝なら破綻** |
| 3 | **128 KiB** | 64 | 約258 KiB | ringが効かないなら case 1 と同じ |
| 4 | **128 KiB** | **8** | 約32 KiB | case 2 と同じなら ring は無関係 |

case 2が破綻しcase 1・3が正常なら、緩衝はqueueである。case 3でringを倍にしても未読の最大値が変わらないなら、ringは律速でない。

## 反証条件

- case 2とcase 4が正常(queueは緩衝でない)
- case 1が破綻する(E050が再現しない)
- ring容量を倍にすると未読の最大値やdataの正しさが変わる(ringも効いている)
- chunk offsetがringの範囲を超える、または周回しない

## 方法

[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)の3者共有構成でgateとrateを固定し、ring容量とqueue深さだけを変える。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`。`max_recv_size`と受信sizeはcaseのring容量に合わせる
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは12,000 wordで先頭6,000だけbit 7をhigh(duty 50%、平均80 MB/s)
- 回収は100 msの固定window、destinationは4 MiB PSRAM
- ringはcaseごとにinternal RAMへ確保し、queueもcaseごとに作り直す
- 回収loopで`chunk.data - ring_buffer`を先頭16個記録し、全体の最大offsetも取る
- RMTは動かしたままにする(non-DMA、`mem_block_symbols` 48)。本実験の問いではないので記録のみ

平均byte rateは80 MB/sで持続spool帯域の約98 MB/sを下回るので、[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)の条件どおりqueueとringが足りていれば正常なはずである。

APIの不成立も結果として最後まで記録する。

## 対象外

dutyとgate幅の掃引、sample rateの掃引、triggerなしspool経路での同じ確認、RMTの設定、chunk sizeの変更、`indirect_mount`との比較。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、ring容量、queue深さ、各API結果、PARLIO callback数・dequeue数、queue overflow、未読最大、ring超過判定、chunk offsetの先頭16個と最大値、bit 7が0のsample数、飛びの数と階差一致数、回収byte、回収rate、経過us。

## 完了条件

queueを浅くしたときに破綻するか、ringを倍にしたときに挙動が変わるかを確定する。chunk offsetの分布からdriverがringをどう使っているかを記録する。

## 影響

緩衝がqueueだと確定すれば、gated captureの設計値は「queue深さ × chunk size > 1 windowの未読最大」になる。ringは`max_recv_size`としての意味しか持たない。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のdrop判定の記述を、ring容量基準からqueue基準へ直せる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E055_20260909T095625Z_default/test_gated_buffer_source/dut.log`

| ring容量 | queue深さ | queue overflow | 飛び(期待22) | 階差一致 | 未読最大 | 最大chunk offset |
|---:|---:|---:|---:|---:|---:|---:|
| 64 KiB | 64 | **0** | 22 | **15 / 15** | 93,760 | 62,976 |
| 64 KiB | **8** | **502** | **483** | **0 / 15** | 1,925,632 | 62,976 |
| 128 KiB | 64 | **0** | 22 | **15 / 15** | 90,752 | **128,000** |
| 128 KiB | **8** | **448** | **465** | **0 / 15** | 1,803,200 | 128,000 |

**緩衝はchunk queueである。** queueを64から8へ浅くすると、ring容量が64 KiBでも128 KiBでも破綻した(overflow 502 / 448件、飛びが期待22に対し483 / 465、階差一致0 / 15)。**ring容量を倍にしても結果は変わらない。**

**ringは周回bufferではなく`max_recv_size`まで線形に歩いている。** chunk offsetは0, 4032, 8064, ... と4,032 byte刻みで単調に増え、最大offsetはring 64 KiBで62,976、128 KiBで**128,000**だった。ring容量を倍にすればchunkが届く範囲もそのまま倍になる。

**`未読 > ring容量`は破綻の指標ではない。** ring 64 KiBのcase 1は未読93,760 byteでring容量を超えているのにdataは正常で、ring 128 KiBのcase 3は未読90,752 byteでring容量以内である。未読の値は両者でほぼ同じ(93,760と90,752)で、ring容量ではなくwindowの過負荷で決まっている。

## 判定

**gated captureの緩衝はqueue深さ × chunk sizeであり、ring容量ではない。** [E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)のtriggerなしspool経路ではring容量が指標として機能したが、gated captureでは機能しない。

これで[E048](../e048_p4_gated_rate_ceiling/README.ja.md)が立てた過負荷modelが、**容量をringからqueueへ差し替えた形で復活する。**

```
条件1(平均) duty × sample rate × bytes/sample < 持続spool帯域(約98 MB/s)
条件2(尖頭) queue深さ × chunk size > window byte長 × (1 − window中のdrain帯域 ÷ sample rate)
```

条件2を実測で確かめる。window 192,000 byte、sample rate 160 MHz、window中のdrain帯域約82 MB/s([E049](../e049_p4_gated_window_absorption/README.ja.md))とすると尖頭の未読は192,000 × (1 − 82/160) = 93,600 byteで、実測93,760とほぼ一致する。必要なqueue深さは93,760 ÷ 4,032 = 23.3、つまり**24 entry以上**である。case 1の64は足り、case 2の8は足りない。

これが[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)で「window長は無関係」に見えた理由でもある。queue 64 entryは258,048 byte分あり、160 MHzでは`258,048 ÷ 0.4875` = 約529,000 byteのwindowまで許容する。E050が試した最大は224,000 byteなので、条件2には遠く届いていなかった。

またE050で試した範囲では条件2が効かないので、window長の上限は「無い」のではなく**queue深さで決まる**が正確である。E050の結論(dutyだけで決まる)は、queueが十分深い前提での話に限定される。

[E049](../e049_p4_gated_window_absorption/README.ja.md)のgate 8,000の破綻も整合する。dutyが66.7%で平均106.7 MB/sが持続spool帯域を超えるので条件1が破れ、未読が際限なく増えて442,624 byteとなり258,048 byteのqueueを超えた。破綻の起点は条件1である。

**残る謎を明記する。** ring 64 KiBのcase 1では未読93,760 byteが64 KiBのringに収まらないのにdataが正常だった。chunk offsetは線形に歩いてringの端まで行くので、次のtransactionで先頭から書き直すはずであり、その時点で未読の古いchunkは上書きされるように見える。にもかかわらず飛びの数と階差は完全に正しい。`trans_queue_depth`が1なのでtransactionの終端でDMAが一旦止まる、ISRが再armするまでの間に taskが追いつく、といった機序が考えられるが、本実験では切り分けていない。

## 事実・候補・未決

**事実**

1. **queueを64から8へ浅くすると、ring容量64 KiBでも128 KiBでも破綻した。** overflow 502 / 448件、飛びが期待22に対し483 / 465、階差一致0 / 15である。
2. **ring容量を64 KiBから128 KiBへ倍にしても、queue 64なら両方正常でqueue 8なら両方破綻した。** ring容量は結果を変えない。
3. **ringは周回bufferではなく線形に歩いている。** chunk offsetは4,032 byte刻みで単調増加し、最大offsetはring 64 KiBで62,976、128 KiBで128,000だった。
4. **`未読 > ring容量`は破綻の指標ではない。** case 1は未読93,760でring容量65,536を超えているのに正常、case 3は未読90,752でring容量131,072以内である。未読の値はring容量ではなくwindowの過負荷で決まる。
5. 尖頭の未読は`window byte長 × (1 − window中のdrain ÷ sample rate)`で説明できる。192,000 × (1 − 82/160) = 93,600に対し実測93,760である。必要queue深さは24 entry以上と計算できる。
6. ring 64 KiBで未読がring容量を超えてもdataが正常だった機序は未解明である。

**候補**: gated captureの成立条件を2本立てで申告する。条件1は`duty × sample rate × bytes/sample < 持続spool帯域`、条件2は`queue深さ × chunk size > window byte長 × (1 − drain ÷ sample rate)`。drop判定はring容量ではなく`未読 > queue深さ × chunk size`で行う。ringは`max_recv_size`としての意味しか持たない。

**未決**: ring容量を超える未読でdataが正常な機序(`trans_queue_depth`とtransaction終端の挙動) / 条件2の境界をqueue深さ24付近で実測すること / triggerなしspool経路でも同じくqueueが緩衝なのか(E036はring容量で説明できていたが偶然の一致かもしれない) / chunk sizeが4,032固定である根拠。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): drop判定をring容量基準からqueue基準へ直し、gated captureの条件を2本立てにする
- [E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md): window長の上限が「無い」のではなくqueue深さで決まることを追記する
- [LEDGER](../LEDGER.ja.md): E055の節
