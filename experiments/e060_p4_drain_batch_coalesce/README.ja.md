# E060 ESP32-P4 chunkのまとめ取りでdrainは上がるか

状態: **完了 — どちらも効かない。固定costはISRが支配**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E059](../e059_p4_drain_breakdown/README.ja.md)・[E058](../e058_p4_window_drain_vs_rate/README.ja.md)・[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)

## 問い

**1 chunkあたり3.6〜5.2 usの固定costは、`xQueueReceive`をまとめて呼ぶことと、連続したchunkを1回のmemcpyへまとめることで下がるか。それぞれの寄与はどれだけか。**

## 仮説

[E059](../e059_p4_drain_breakdown/README.ja.md)で、window中のdrain低下はmemcpy帯域(107 MB/s一定)ではなく1 chunkあたりの固定costによると分かった。costの中身はISR本体、`xQueueReceive`、loop本体、memcpy呼び出しである。ISR本体はdriver側なので触れないが、残りはtask側で削れる。

削る手が二つある。

- **まとめ取り** — `xQueueReceive`をtimeout 0で回して、すでにqueueに溜まっているchunkを一度に取り出す。1 chunkごとにblocking呼び出しをする現在の形より呼び出し回数が減る
- **memcpyのまとめ** — [E055](../e055_p4_gated_buffer_source/README.ja.md)で、driverはringを周回せず線形に歩き、chunkのoffsetが4,032 byte刻みで単調に増えることが分かっている。**つまり連続して届くchunkはring上でも連続している。** 隣接するchunkを1回のmemcpyへまとめれば、memcpy呼び出しの回数が減る

二つを分離するために3条件で測る。

| # | まとめ取り | memcpyのまとめ | 分かること |
|---:|---|---|---|
| 1 | しない | しない | [E059](../e059_p4_drain_breakdown/README.ja.md)の再現(基準) |
| 2 | **する** | しない | `xQueueReceive`削減の寄与 |
| 3 | **する** | **する** | memcpyまとめの追加寄与 |

sample rateは160 MHz、window byte長は96,000に固定する。[E059](../e059_p4_drain_breakdown/README.ja.md)の基準ではdrainが82.3 MB/s、memcpy占有率が77%だった。固定costがすべて消えればdrainはmemcpy帯域107 MB/sへ近づく。

memcpyをまとめるとcopy 1回のsizeが大きくなる。[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)はDMA無しのcopyを4 KiBで181、16 KiBで182.7、64 KiBで138.6 MB/sと測っており、**大きくしすぎると帯域が落ちる**。まとめた結果のsizeとmemcpy帯域も記録して、この効果を切り分ける。

未読の会計は基準と揃える。**`consumed_bytes`はmemcpyが済んだ分だけ進める** — queueから取り出しただけのchunkはring上にまだ残っており上書きの危険があるので、取り出した時点で消費済みとして数えてはならない。

## 反証条件

- 3条件でdrainが変わらない(固定costがtask側の形に依存しない)
- まとめ取りやまとめmemcpyでdrainが下がる
- memcpy帯域がまとめた分だけ落ちて、呼び出し削減の利得を打ち消す
- どれかの条件で飛びの急増やqueue overflowが出る
- ISR側の未読最大が基準と大きく変わり比較にならない

## 方法

[E059](../e059_p4_drain_breakdown/README.ja.md)の構成から回収loopの形だけを変える。captureの設定は変えない。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`。`max_recv_size`と受信sizeは62,720
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは6,000 wordで先頭3,000だけbit 7をhigh(duty 50%、window 96,000 byte)
- queue深さ64、回収は100 ms、destinationは4 MiB PSRAM、未読はISR内で標本化
- 回収loop:
  - まとめ取りは、最初の1個をtimeout 5 msで待ってから、timeout 0でqueueが空になるまで(最大64個)取り出す
  - memcpyのまとめは、取り出したchunkのうち`data + length`が次の`data`と一致するものを連続とみなして1回のmemcpyにする
  - `consumed_bytes`はmemcpyが済んだ分だけ進める
- memcpyの累積時間・累積byte・**呼び出し回数**、1 batchの最大chunk数を記録する

APIの不成立も結果として最後まで記録する。

## 対象外

回収を別coreへ移す構成、chunk sizeの変更(SoC定義で4,032固定)、queueを介さずdescriptorを直接見る方式、まとめsizeの上限掃引、sample rateとwindow長の掃引、ISR本体の実行時間の直接測定。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、まとめ取りとmemcpyまとめの有無、各API結果、memcpyの累積時間・累積byte・呼び出し回数、そこから求めたmemcpy帯域と平均copy size、1 batchの最大chunk数、dequeue回数、ISR側とtask側の未読最大、そこから逆算したdrain、queue overflow、飛びの総数と期待境界数、階差一致数、回収byte、経過us。

## 完了条件

3条件のdrainとmemcpy帯域を記録し、`xQueueReceive`削減とmemcpyまとめのそれぞれの寄与を確定する。改善しない場合は固定costがtask側の形に依存しないことを確定して完了とする。

## 影響

改善するなら、gated captureのdrainは実装の書き方で上げられることになり、条件2の`drain(rate)`は実装ごとの値になる。改善しないなら固定costはISR本体が支配しており、残る手は回収を別coreへ移すことだけになる。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のdrain表に実装依存性が付く。

## 結果

実施日: 2026-09-09

採用run: `_runs/E060_20260909T115516Z_default/test_drain_batch_coalesce/dut.log`

| まとめ取り | memcpyまとめ | 1 batchの最大 | memcpy呼び出し | 平均copy size | memcpy時間 | memcpy帯域 | ISR側の未読最大 | 飛び(期待43) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| しない | しない | 1 | 1,070 | 3,920 B | 39,078 us | 107.3 MB/s | **54,656** | 44 |
| **する** | しない | **12** | 1,070 | 3,920 B | 38,912 us | 107.8 MB/s | **54,656** | 44 |
| する | **する** | 12 | **277** | **15,142 B** | 38,535 us | 108.8 MB/s | **70,784** | 44 |

3条件すべて正常に取れた(飛び44対期待43、階差15 / 15、queue overflow 0)。

**`xQueueReceive`のまとめ取りは効果がゼロだった。** 1 batchの最大が1から12へ増えているのでまとめ取り自体は働いているが、memcpy時間は39,078から38,912 us(0.4%)、ISR側の未読最大は54,656で**完全に同一**である。queue呼び出しは1 chunkあたりcostの意味のある部分ではない。

**memcpyのまとめもdrainを改善しなかった。** 呼び出しは1,070から277へ3.9倍減り、平均copy sizeは3,920から15,142 byteになったが、memcpy時間は38,912から38,535 us(1%)しか減らず、帯域は107.8から108.8 MB/sである。[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)がDMA無しで4 KiBを181、16 KiBを182.7 MB/sと測っているのに対し、DMAが動いている状態ではcopy sizeを4倍にしても1%しか変わらない。**memcpy側の律速はDMAとの競合であり、呼び出し回数ではない。**

**memcpyの累積時間は3条件でほぼ一定(39,078 / 38,912 / 38,535 us)である。** queue呼び出し回数もmemcpy呼び出し回数も、実際のcostに寄与していない。

**memcpyまとめでは未読の測定値が悪化した(54,656 → 70,784)が、dataは正常である。** これは測定側の性質による。`consumed_bytes`はrunのmemcpyが済んだ時点でしか進まないので、48 KBのrunを448 us かけてcopyしている間は`callback_bytes`だけが増える。**実際にringで危険なのはmemcpyの読み出し位置より後ろだけなのに、run単位の公表では最大1 run分(48 KB)遅れて見える。** 未読70,784 byteは`ceil(70,784 ÷ 4,032)` = 18 chunkでringの完全chunk 15個を超えるが、dataは1 byteも失われていない。つまりこの条件では測定値が過大である。

## 判定

**1 chunkあたりの固定costはtask側の書き方では下がらない。** queue呼び出しのまとめ取りは無効果、memcpyのまとめは1%しか効かない。したがって[E059](../e059_p4_drain_breakdown/README.ja.md)が測った3.6〜5.2 us/chunkは**driver側のISRが支配している**と結論できる。task側で残る手は回収を別coreへ移すことだけである。

副次的に、**未読の測定は`consumed_bytes`がmemcpyの読み出し位置に密着していることを前提にしている**ことが分かった。per-chunk copyならこの前提は1 chunk以内で成り立つが、まとめてcopyすると最大1 run分ずれて過大に出る。条件2の判定に未読の実測値を使う場合、**per-chunk copyでなければ意味を持たない**。[E057](../e057_p4_gated_ring_boundary/README.ja.md)が「判定は計算で行う」と結論した理由がここでも効く。

実装への帰結。

- **まとめ取りもmemcpyのまとめも入れない。** 効果がなく、後者は未読の測定を狂わせる
- **per-chunk copyを維持する。** `consumed_bytes`が読み出し位置に密着し、未読の実測が意味を持つ
- **drainを上げたいなら回収を別coreへ移す。** ISRが走るcoreとmemcpyするcoreを分ける。これは未実測
- memcpy側は改善の余地がない。DMAが動いている限り107〜109 MB/sで、copy sizeにも依存しない

## 事実・候補・未決

**事実**

1. 3条件すべて正常に取れた(飛び44対期待43、階差15 / 15、queue overflow 0)。
2. **`xQueueReceive`のまとめ取りは効果ゼロ。** 1 batchの最大が1から12へ増えてもmemcpy時間は0.4%しか変わらず、**ISR側の未読最大は54,656で完全に同一**である。
3. **memcpyのまとめも効かない。** 呼び出しが1,070から277へ3.9倍減り平均copy sizeが3,920から15,142 byteになっても、memcpy時間は1%減、帯域は107.8から108.8 MB/sである。DMA無しの[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)では4 KiBと16 KiBで181と182.7 MB/sなので、**DMAが動いている状態ではcopy sizeが効かない。**
4. memcpyの累積時間は3条件でほぼ一定(39,078 / 38,912 / 38,535 us)である。
5. **memcpyまとめでは未読の測定値が54,656から70,784へ悪化したが、dataは正常だった。** `consumed_bytes`がrun単位でしか進まないため最大1 run分(48 KB)遅れて見える。未読70,784は18 chunk相当でringの完全chunk 15個を超えるのに破綻していないので、この条件では測定値が過大である。
6. したがって[E059](../e059_p4_drain_breakdown/README.ja.md)の3.6〜5.2 us/chunkはdriver側のISRが支配しており、task側の書き方では下がらない。

**候補**: まとめ取りもmemcpyのまとめも入れず、per-chunk copyを維持する。未読の実測を条件2の判定に使うのはper-chunk copyのときだけにする。drainを上げる残りの手は回収を別coreへ移すこと。

**未決**: **回収を別coreへ移した場合の効果**(task側で残る唯一の手) / ISR本体の実行時間の直接測定 / 80〜100 MHzでmemcpy帯域が飽和する理由 / triggerなしspool経路でも同じ結論になるか。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): drainの改善方向を「task側の書き方では下がらない、残るのはcore分離」に直し、per-chunk copyを維持する理由を書く
- [LEDGER](../LEDGER.ja.md): E060の節
