# E050 ESP32-P4 dutyを固定してwindow長だけを振る

状態: **完了 — window長は無関係。条件は平均byte rateのみ**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E049](../e049_p4_gated_window_absorption/README.ja.md)・[E048](../e048_p4_gated_rate_ceiling/README.ja.md)

## 問い

**dutyを50%に固定してwindow長だけを振ったとき、window長はgated captureの成立に影響するか。**

## 仮説

[E049](../e049_p4_gated_window_absorption/README.ja.md)はgapを固定したためgate幅とdutyが一緒に動き、window長と平均byte rateを分離できなかった。5点すべてを説明できたのは平均byte rateのmodelだけだったが、window長のmodelを積極的に否定する条件は測れていない。

gapをwindowと同じ長さにすればdutyは常に50%になり、平均byte rateはsample rateの半分に固定される。sample rate 160 MHzなら80 MB/sで、持続spool帯域の約98 MB/sを十分に下回る。この状態でwindow長だけを振れば、二つのmodelは**反対の予測**をする。

| gate幅(word) | window byte | E048のmodel(過負荷 対 ring 65,536) | E049のmodel(平均80 MB/s 対 98) |
|---:|---:|---|---|
| 1,000 | 32,000 | 12,400 → 成立 | 成立 |
| 2,000 | 64,000 | 24,800 → 成立 | 成立 |
| 4,000 | 128,000 | 49,600 → 成立 | 成立 |
| 6,000 | 192,000 | 74,400 → **破綻** | 成立 |
| 7,000 | 224,000 | 86,800 → **破綻** | 成立 |

6,000と7,000が正常ならwindow長のmodelは完全に否定され、成立条件は平均byte rateだけで決まる。破綻すればwindow長も独立に効いていることになり、E049の境界はdutyとwindow長の両方が同時に閾値へ達した偶然だったことになる。

`signal_range_max_ns`はE049で得た教訓に従い、想定最長level(7,000 word = 28,000 tick)より十分大きい32,000 tickのままにする。

## 反証条件

- 6,000 / 7,000で破綻する(window長も独立に効いている)
- 1,000 / 2,000のような短いwindowで破綻する
- dutyが50%から外れる
- 飛びの階差がwindow byte長と一致しない
- bit 7が0のsampleが出る

## 方法

[E049](../e049_p4_gated_window_absorption/README.ja.md)の構成からgapの決め方だけを変える。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 9、`resolution_hz` = 20,000,000、`signal_range_max_ns` = 1,600,000、`en_partial_rx=true`、buffer 32 symbol
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは`gate幅 × 2`で、先頭`gate幅`だけbit 7をhigh。bit 0〜6は`gray7(index & 0x7F)`
- 回収は100 msの固定window、destinationは4 MiB PSRAM
- gate幅掃引: 1,000 / 2,000 / 4,000 / 6,000 / 7,000 word(dutyは全条件50%)

drop判定はring未読byte数の最大値とqueue overflow、および飛びの階差がwindow byte長と一致するかである。

APIの不成立も結果として最後まで記録する。

## 対象外

dutyの掃引(50%固定)、sample rateの掃引(160 MHz固定)、ring容量を変えた確認、data_width 16、長時間の持続、ringとqueueのどちらが緩衝かの切り分け。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

gate幅、loop長、window byte長、duty、E048 modelの予測過負荷、各API結果、ring未読最大とring超過判定、queue overflow、bit 7が0だったsample数、飛びの総数と先頭16 offset、階差がwindow byte長と一致した数、回収byte、回収rate、RMTのduration、経過us。

## 完了条件

dutyを固定した状態で全window長について成立・破綻を記録し、window長が独立に効くか否かを確定する。

## 影響

window長が効かないなら、gated captureのcapabilityは`duty × sample rate × bytes/sample < 約98 MB/s`の一本で申告でき、window長に上限を設ける必要がなくなる。効くなら両方の条件を並べる必要がある。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のgate行の条件式が確定する。

## 結果

実施日: 2026-09-09

採用run: `_runs/E050_20260909T090830Z_default/test_gated_window_at_fixed_duty/dut.log`

| gate幅 | window byte | E048予測過負荷 | duty | ring未読最大 | ring超過 | queue overflow | 飛び | 期待境界数 | 階差一致 | RMT |
|---:|---:|---:|---:|---:|:-:|---:|---:|---:|---:|---|
| 1,000 | 32,000 | 12,400 | 50% | 18,688 | — | 0 | 131 | 131.1 | 15 / 15 | 9 callback |
| 2,000 | 64,000 | 24,800 | 50% | 33,280 | — | 0 | 66 | 65.5 | 15 / 15 | 4 callback |
| 4,000 | 128,000 | 49,600 | 50% | 61,504 | — | 0 | 33 | 32.8 | 15 / 15 | 1 callback |
| 6,000 | 192,000 | 74,400 | 50% | **92,288** | **超過** | 0 | 22 | 21.8 | **15 / 15** | **0 callback** |
| 7,000 | 224,000 | 86,800 | 50% | **106,880** | **超過** | 0 | 18 | 18.7 | **15 / 15** | **0 callback** |

**5条件すべてでPARLIO側のdataが正常だった。** 飛びの数は期待するwindow境界数と一致し(131対131.1、66対65.5、33対32.8、22対21.8、18対18.7)、先頭15個の階差はすべてwindow byte長と厳密に一致した。queue overflowは0、bit 7が0のsampleも0件である。

**E048のwindow過負荷modelは完全に否定された。** modelはgate 6,000(過負荷74,400)と7,000(86,800)がring容量65,536を超えるので破綻すると予測した。実際にはring未読最大が92,288 / 106,880 byteとring容量の1.4〜1.6倍に達したのに、dataは1 byteも失われていない。window byte長224,000はring容量の3.4倍である。

**E049の平均byte rate modelが確定した。** dutyを50%に固定した結果、平均byte rateは160 MHz × 0.5 = 80 MB/sで全条件同一であり、持続spool帯域の約98 MB/sを下回る。window長を7倍(32,000 → 224,000 byte)に振っても成立し続けた。したがって**window長はgated captureの成立に影響しない。**

E049で観測された境界(gate 6,000正常 / 8,000破綻)は、window長ではなくdutyの上昇(60% → 66.7%)によるものだったと確定する。

**RMTについては新しい未決が出た。** loop周期が2.4 ms以上のcase(gate 6,000と7,000)でRMTのcallbackが0回になり、symbolを取得できなかった。1.6 ms以下(gate 1,000 / 2,000 / 4,000)では取得できている。E049のgate 8,000も loop周期2.4 msで同じく0だった。

E049ではこれをhigh duration 32,000 tickが`signal_range_max_ns`の32,000 tickに達したためと解釈したが、**本実験のgate 6,000はhigh 24,000 tickで閾値を大きく下回っており、その説明では成り立たない。** loop周期が約2 msを超えるとRMTがsymbolを返さなくなる、という観測だけが残る。原因は本実験では切り分けていない。

## 判定

**gated captureの成立条件は平均byte rateだけで決まる。window長は影響しない。**

```
sample rate ≤ 160 MHz(内部clock源)
かつ duty × sample rate × bytes/sample < 約98 MB/s(持続spool帯域)
```

window長に上限を設ける必要はない。ring未読量がring容量を超えることもdata喪失の指標にならない。ring 64 KiBに対して未読106,880 byteでdataが正常だったので、実際の緩衝はringだけではなく、chunk queue(64 entry × 約4,032 byte ≒ 258 KiB)を含めた経路全体で見る必要がある。E049の破綻条件では未読442,624 byteがこの258 KiBを超えており、queue overflowが51件出ていた。**ringとqueueのどちらが律速かはまだ決まっていないが、少なくとも「ring容量を超えたら壊れる」ではない。**

capabilityの申告はこれで一本化できる。8 channel(1 byte/sample)なら、

| duty | 許容sample rate | 実際に取れるrate |
|---:|---:|---:|
| 25% | 392 MHz相当 | 160 MHz(clock源) |
| 50% | 196 MHz相当 | 160 MHz |
| 60% | 163 MHz相当 | 160 MHz |
| 61%以上 | 160 MHz未満 | duty次第 |

**duty 61%までは常に160 MHzが取れる。** それ以上ではdutyから逆算する。

RMTのloop周期依存は、qualificationの実装に直接効く制約になりうる。gate周期が2 ms以上の信号ではwindow timestampが取れない可能性があり、次に切り分ける必要がある。

## 事実・候補・未決

**事実**

1. dutyを50%に固定してwindow byte長を32,000から224,000まで7倍に振り、**5条件すべてでPARLIO側のdataが正常だった。** 飛びの数は期待境界数と一致、先頭15個の階差はすべてwindow byte長と厳密に一致、queue overflow 0、bit 7が0のsampleは0件。
2. **E048のwindow過負荷modelは否定された。** gate 6,000 / 7,000でring未読が92,288 / 106,880 byte(ring容量の1.4〜1.6倍)に達してもdataは正常だった。window byte長224,000はring容量の3.4倍である。
3. **E049の平均byte rate modelが確定した。** duty 50%で平均80 MB/sは持続spool帯域98 MB/sを下回り、window長に関係なく成立した。E049の境界はwindow長ではなくdutyの上昇によるものだった。
4. ring未読量がring容量を超えてもdataが正常なので、実際の緩衝はring単体ではない。E049の破綻条件では未読442,624 byteがchunk queueの容量(約258 KiB)を超えqueue overflowが51件出ていた。
5. **loop周期が2.4 ms以上のcase(gate 6,000 / 7,000、およびE049のgate 8,000)でRMTのcallbackが0回になった。** 1.6 ms以下では取得できている。E049で`signal_range_max_ns`到達と解釈したが、本実験のgate 6,000はhigh 24,000 tickで閾値32,000を大きく下回るため、その説明は成り立たない。

**候補**: gated captureのcapabilityを`sample rate ≤ 160 MHz`かつ`duty × sample rate × bytes/sample < 約98 MB/s`の一本で申告する。window長の上限は設けない。8 channelならduty 61%までは常に160 MHzが取れる。

**未決**: RMTがloop周期2 ms以上でsymbolを返さない原因(qualificationの実装に直接効く) / ringとqueueのどちらが律速か / duty 61%付近の実測 / window中のdrain帯域が持続値より低い理由 / destinationを大きくした長時間持続 / data_width 16でのgated rate。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): gate行の条件を平均byte rateの一本に確定し、window長の上限を外す。RMTのloop周期依存を未測定項目に追加する
- [E049](../e049_p4_gated_window_absorption/README.ja.md): 境界がdutyによるものと確定したこと、RMT dropoutの解釈が成り立たないことを追記する
- [LEDGER](../LEDGER.ja.md): E050の節

## 追記 — E051で見つかったmsync非整列(2026-09-09)

本レポートは書き換えない。[E051](../e051_p4_rmt_partial_threshold/README.ja.md)で、`build_pattern`が`loop_words`だけを`esp_cache_msync`しており、64 byteのcache line境界に載らないためerror logが出ていたことが分かった。本実験でも同じerrorが出ていた。

ただし**本実験のdataは検証を通っており結論は変わらない**。この環境ではflushが失敗してもCPUの書き込みはDMAから見えていたことになる。E051ではbuffer全体をsyncする形へ直している。

## 追記 — E055による限定(2026-09-09)

本レポートは書き換えない。[E055](../e055_p4_gated_buffer_source/README.ja.md)がring容量とqueue深さを独立に振った結果、**緩衝はring容量ではなくchunk queueの深さ × chunk size**だと分かった。

したがって本レポートの「window長に上限は無い」は、**queueが十分深い前提での話**に限定される。正確には条件が2本ある。

```
条件1(平均) duty × sample rate × bytes/sample < 持続spool帯域(約98 MB/s)
条件2(尖頭) queue深さ × chunk size > window byte長 × (1 − window中のdrain ÷ sample rate)
```

queue 64 entry(258,048 byte)では160 MHzで約529,000 byteのwindowまで条件2を満たす。本実験が試した最大は224,000 byteなので条件2に遠く届いておらず、そのためwindow長が効かなく見えた。queueを8 entryへ浅くすると、本実験と同じ192,000 byteのwindowでも破綻する。
