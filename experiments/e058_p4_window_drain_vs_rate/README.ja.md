# E058 ESP32-P4 window中のdrain帯域はrateに依存するか

状態: **完了 — drainはrate依存。未読には2 chunkの床がある**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E057](../e057_p4_gated_ring_boundary/README.ja.md)・[E049](../e049_p4_gated_window_absorption/README.ja.md)・[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)

## 問い

**window長を固定してsample rateを変えると、window中のdrain帯域は一定か、rateとともに下がるか。**

## 仮説

条件2の計算に使っているwindow中のdrain帯域約82 MB/sは、[E049](../e049_p4_gated_window_absorption/README.ja.md)の未読とwindow長の比から逆算した値で、直接測っていない。[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)が測った持続spool帯域98 MB/sより低い理由も分かっていない。

考えられるのは、window中はDMAがsample rateそのもの(160 MHzなら160 MB/s)でringへ書き込みながら、CPUが同じringから読み出しているため競合が持続状態より厳しくなることである。この見方が正しければ、**DMAの書き込みrateが下がればdrainは持続値へ近づく**、つまりdrainはsample rateの関数になる。

window byte長を96,000に固定し、sample rateだけを変える。TX rateは5 MHz固定なのでrun長は`rate ÷ 5 MHz`で変わり、gate幅を`96,000 ÷ run長`に取ればwindow byte長が揃う。

| sample rate | run長 | gate幅 | loop長 | 平均byte rate(duty 50%) |
|---:|---:|---:|---:|---:|
| 80 MHz | 16 | 6,000 | 12,000 | 40 MB/s |
| 100 MHz | 20 | 4,800 | 9,600 | 50 MB/s |
| 120 MHz | 24 | 4,000 | 8,000 | 60 MB/s |
| 160 MHz | 32 | 3,000 | 6,000 | 80 MB/s |

平均byte rateはすべて持続spool帯域98 MB/sを下回るので条件1は満たされる。尖頭未読は最大でも46,800 byte(12 chunk)で、ringの完全chunk 15個を下回るので条件2も満たされる。**4条件すべて正常に取れる見込みなので、未読の測定がdata喪失に汚されない。**

drainは`drain = sample rate × (1 − 尖頭未読 ÷ window byte長)`で逆算できる。drainが一定なら尖頭未読は96,000 × (1 − 82 ÷ rate)に従い、rateごとに約0 / 17,280 / 30,400 / 46,800 byteになる。rateが上がるほどdrainが下がるなら、高いrateで予測より大きく出る。

**測り方を一点改める。** [E057](../e057_p4_gated_ring_boundary/README.ja.md)で、`dequeue`ごとに1回だけ標本化する現在の方法では尖頭を過小に見ることが分かった(破綻しているのに容量を下回る値が出た)。未読が増えるのはISRがchunkを通知する瞬間だけなので、**ISR内で未読を計算して最大値を取れば真の尖頭が取れる**。task側の値と両方記録して、過小評価の量も測る。

ring容量は62,720にする。patternのsample周期は`128 word × run長`でrateごとに2,048 / 2,560 / 3,072 / 4,096 byteと変わるが、62,720はこのどれの倍数でもないので[E056](../e056_p4_ring_period_alias/README.ja.md)のalias を避けられる。

## 反証条件

- 逆算したdrainがrateによらず一定(競合の見方が外れ)
- 尖頭未読がwindow byte長を超える、または負になる
- ISR側とtask側の未読最大が一致しない(標本化の改善が効いていない)
- どれかの条件でqueue overflowや飛びの急増が出る(正常に取れていない)
- ring 62,720やgate 4,800がAPIに拒否される

## 方法

[E057](../e057_p4_gated_ring_boundary/README.ja.md)の構成からsample rateとgate幅を対で振り、pattern bufferを広げる。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`。`max_recv_size`と受信sizeは**62,720**
- TX: data_width 8、GPIO 2〜9、**5 MHz固定**。1 loopは`gate幅 × 2`で先頭`gate幅`だけbit 7をhigh(duty 50%)。bit 0〜6は`gray7(index & 0x7F)`
- pattern bufferは32,768 word(gate 12,000のloop 24,000を収めるため)
- queue深さは64固定、回収は100 msの固定window、destinationは4 MiB PSRAM
- **ISR内で`callback_bytes − consumed_bytes`の最大値を取る。** taskが書く`consumed_bytes`は32 bit整列の単一storeなのでISRから読める
- case: (80 MHz, gate 6,000) / (100 MHz, 4,800) / (120 MHz, 4,000) / (160 MHz, 3,000)

期待するwindow境界数は`4,194,304 ÷ 96,000` ≒ 43である。

APIの不成立も結果として最後まで記録する。

## 対象外

dutyの掃引、window長の掃引、ring容量とqueue深さの掃引、drainの内訳(DMA writeとCPU readのどちらが効いているかの分離)、triggerなしspool経路での同じ測定、data_width 16。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

sample rate、run長、gate幅、loop長、window byte長、pattern周期、ring剰余、各API結果、ISR側とtask側の未読最大、queue overflow、飛びの総数と期待境界数、階差一致数、逆算したdrain、回収byte、回収rate、経過us。

## 完了条件

4つのrateについて尖頭未読からdrainを逆算し、一定かrate依存かを確定する。ISR側とtask側の未読最大の差も記録する。

## 影響

drainがrate依存なら、条件2の計算は`drain(rate)`の形にする必要があり、単一の82 MB/sという定数は使えない。一定なら82 MB/sを定数として確定できる。あわせて未読の正しい測り方が決まるので、[E057](../e057_p4_gated_ring_boundary/README.ja.md)が残した「実測は尖頭を過小に見る」という制約が外れる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E058_20260909T105237Z_default/test_window_drain_vs_rate/dut.log`

| sample rate | run長 | gate幅 | ISR側の未読最大 | task側の未読最大 | 差 | 82 MB/s一定なら | 飛び(期待43) | 階差一致 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 80 MHz | 16 | 6,000 | **8,064** | 4,032 | 4,032 | 0 | 42 | 15 / 15 |
| 100 MHz | 20 | 4,800 | **8,064** | 4,032 | 4,032 | 17,280 | 44 | 15 / 15 |
| 120 MHz | 24 | 4,000 | **27,328** | 23,296 | 4,032 | 30,400 | 44 | 15 / 15 |
| 160 MHz | 32 | 3,000 | **52,416** | 48,384 | 4,032 | 46,800 | 44 | 15 / 15 |

**4条件すべて正常に取れた。** 飛びは42〜44で期待境界数43と一致し、階差も15 / 15、queue overflowは0である。したがって未読の測定はdata喪失に汚されていない。

**task側の標本化はちょうど1 chunk分だけ尖頭を見落としていた。** 差は4条件すべてで正確に4,032 byteである。[E057](../e057_p4_gated_ring_boundary/README.ja.md)が指摘した過小評価はこの1 chunkで、ISR内で標本化すれば取れる。

**未読には約8,064 byte(2 chunk)の床がある。** 80 MHzは平均40 MB/s・瞬間80 MB/sでdrainを下回るので過負荷が生じないが、それでも未読最大は8,064である。これはchunkが通知される段とqueueに載る段のpipeline分で、過負荷とは別に常に乗る。

**drainは一定ではない。** 床を引いた増分から逆算する。

| sample rate | 増分(ISR尖頭 − 8,064) | window所要時間 | 不足分 | 逆算したdrain |
|---:|---:|---:|---:|---:|
| 80 MHz | 0 | 1.20 ms | — | ≥ 80 MB/s |
| 100 MHz | 0 | 0.96 ms | — | **≥ 100 MB/s** |
| 120 MHz | 19,264 | 0.80 ms | 24.1 MB/s | **95.9 MB/s** |
| 160 MHz | 44,352 | 0.60 ms | 73.9 MB/s | **86.1 MB/s** |

**sample rateが上がるほどdrainは下がる。** 100 MHz以下では100 MB/s以上あり、120 MHzで95.9、160 MHzで86.1になる。window中はDMAがsample rateでringへ書きながらCPUが同じringから読むので、DMAの書き込みrateが上がるほどCPUの読み出しが圧迫される、という見方と整合する。[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)が測った持続spool帯域98 MB/sは、DMAの書き込みが98 MB/s程度だった状態の値なので、この曲線上の一点として辻褄が合う。

## 判定

**尖頭未読は床とrate依存のdrainで表せる。**

```
尖頭未読 = 8,064 + window byte長 × (1 − drain(rate) ÷ rate)
drain(rate) ≈ 160 MHzで86 MB/s、120 MHzで96 MB/s、100 MHz以下では100 MB/s以上
```

この形の当てはまりを確かめる。

| 条件 | window byte | 予測尖頭 | 実測 |
|---|---:|---:|---:|
| 本実験 120 MHz | 96,000 | 27,344 | **27,328** |
| 本実験 160 MHz | 96,000 | 52,464 | **52,416** |
| [E057](../e057_p4_gated_ring_boundary/README.ja.md) gate 3,000 @160 MHz | 96,000 | 52,464 | 47,360(task側)+ 4,032 = 51,392 |
| E057 gate 4,000 @160 MHz | 128,000 | 67,264 | 59,456(task側)+ 4,032 = 63,488 |

本実験の2条件は**48 byte以内**で一致する。E057はtask側の値しか記録していないので1 chunk足しても差が残るが、桁は合っている。

条件2をこの形で書き直すと次になる。

```
必要chunk数 = ceil((8,064 + window byte長 × (1 − drain(rate) ÷ rate)) ÷ chunk size)
成立条件    = 必要chunk数 ≤ min(floor(ring容量 ÷ chunk size), queue深さ)
```

[E057](../e057_p4_gated_ring_boundary/README.ja.md)の境界をこの形で照合すると、gate 3,000は52,464 → 14 chunk ≤ 15で正常、gate 4,000は67,264 → 17 chunk > 15で破綻となり、**実測どおりである**。

**これまで使ってきた「drain 82 MB/s・床なし」という近似は、160 MHzでは尖頭を1〜2 chunk小さく見積もる。** E057の境界判定では偶然どちらの形でも同じ結論になったが、安全側ではないので**床とrate依存のdrainを使う形へ差し替える**。

## 事実・候補・未決

**事実**

1. 4条件すべて正常に取れた(飛び42〜44対期待43、階差15 / 15、queue overflow 0)。未読の測定はdata喪失に汚されていない。
2. **task側の標本化はちょうど1 chunk(4,032 byte)分だけ尖頭を見落としていた。** 差は4条件すべてで正確に4,032である。ISR内で標本化すれば取れる。
3. **未読には約8,064 byte(2 chunk)の床がある。** 過負荷が生じない80 MHzでもこの値が出る。chunk通知とqueue投入のpipeline分である。
4. **drainはrate依存で、rateが上がるほど下がる。** 床を引いた増分から逆算すると100 MHz以下で100 MB/s以上、120 MHzで95.9、160 MHzで86.1 MB/sである。
5. **尖頭未読は`8,064 + window byte長 × (1 − drain(rate) ÷ rate)`で表せる。** 本実験の120 MHzと160 MHzは予測27,344 / 52,464に対し実測27,328 / 52,416で、48 byte以内で一致する。
6. E036の持続spool帯域98 MB/sは、DMAの書き込みが98 MB/s程度だった状態の値としてこの曲線上の一点に収まる。
7. 「drain 82 MB/s・床なし」という近似は160 MHzで尖頭を1〜2 chunk小さく見積もる。安全側ではない。

**候補**: 条件2を`ceil((8,064 + window × (1 − drain(rate) ÷ rate)) ÷ chunk) ≤ min(floor(ring ÷ chunk), queue深さ)`とし、`drain(rate)`を160 MHzで86・120 MHzで96・100 MHz以下で100 MB/sとして表に持つ。未読の測定はISR内で行う。

**未決**: drainのrate依存の内訳(DMA writeとCPU readのどちらが圧迫されているかの分離) / 100 MHz以下でのdrainの上限値(過負荷が生じないため測れていない) / 床8,064 byteが`trans_queue_depth`やchunk sizeでどう変わるか / chunk sizeが4,032固定である根拠 / triggerなしspool経路にも同じ床とdrain曲線が当てはまるか。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): 条件2を床とrate依存drainの形へ差し替え、drainの表を載せる
- [E057](../e057_p4_gated_ring_boundary/README.ja.md): task側の未読が1 chunk分過小だったことと、ISR側で測れることを追記する
- [LEDGER](../LEDGER.ja.md): E058の節

## 追記 — chunk sizeの根拠(2026-09-09、実験不要)

未決に挙げた「chunk sizeが4,032固定である根拠」は、SoC定義から確定するので実験は要らない。

`hal/dma_types.h`に`DMA_DESCRIPTOR_BUFFER_MAX_SIZE`が4,095(descriptorのsize fieldが12 bit)、64 byte整列版の`DMA_DESCRIPTOR_BUFFER_MAX_SIZE_64B_ALIGNED`が`4095 − 63` = **4,032**と定義されている。P4のinternal RAMのcache line整列要件は64 byteなので([E016](../e016_p4_parlio_psram_direct/README.ja.md))、driverはこの値でtransactionを刻む。

したがって**ring容量は4,032の整数倍で取るのが無駄がない**。62,720は15.55倍なので末尾2,240 byteが使えていない。
