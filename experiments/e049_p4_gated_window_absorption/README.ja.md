# E049 ESP32-P4 gated captureの吸収限界は1 windowの過負荷で決まるか

状態: **完了 — modelは反証。境界は平均byte rateで決まる**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E048](../e048_p4_gated_rate_ceiling/README.ja.md)・[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)

## 問い

**gated captureが成立する条件は、E048で立てたmodel `window byte長 × (1 − 持続spool帯域 ÷ sample rate) < ring容量` で決まるか。window長を伸ばして予測どおりの位置で破綻するか。**

## 仮説

[E048](../e048_p4_gated_rate_ceiling/README.ja.md)で、gated captureが内部clock源の上限160 MHzまで成立した。gate区間内の瞬間byte rateはsample rateそのものでcopy段の約98 MB/sを上回っているのに成立するのは、ringがwindow単位の過負荷を吸収しgapで空になるためだった。そこから次のmodelを立てた。

```
1 windowの過負荷 = window byte長 × (1 − 持続spool帯域 ÷ sample rate)
成立条件 = 過負荷 < ring容量
```

sample rate 160 MHz、持続spool帯域98 MB/s、ring 64 KiBで解くと、window byte長の上限は約169 KBになる。E048で使った最大windowは65,408 byteで過負荷25.3 KBだったので余裕があった。

windowを伸ばしていけば予測位置で破綻するはずである。data_width 8、sample rate 160 MHz、TX 5 MHz(run長32)なので`window byte長 = gate幅 × 32`である。

| gate幅(word) | window byte長 | 予測過負荷 | 予測 |
|---:|---:|---:|---|
| 2,044 | 65,408 | 25.3 KB | 成立(E048と同一) |
| 4,000 | 128,000 | 49.6 KB | 成立 |
| 5,000 | 160,000 | 62.0 KB | 境界(ring 64 KiB直下) |
| 6,000 | 192,000 | 74.4 KB | **破綻** |
| 8,000 | 256,000 | 99.2 KB | **破綻** |

**5,000と6,000の間に境界が現れる**というのがmodelの予測である。

信号源は1 loopに1 windowだけ置く。gapは4,000 word固定で、RMT symbolのduration上限(32,767 tick)に収まるようloop長をgate幅 + 4,000 wordにする。gap 4,000 wordは160 MHz換算で128,000 sample分、実時間800 usで、過負荷62 KBをspool 98 MB/sで吐き出すのに要する0.63 msより長い。

captureされるのはloopの先頭からgate幅までなので、windowごとにgray7 rampが0から再開する。window境界のindex差は`4,001`で128の剰余が33なので、rampの飛びとして必ず現れる。window内部は連続なので、**飛びの数がwindow境界の数と一致し、階差がwindow byte長と一致する限りdropは無い**。ringが溢れればwindow内部にも飛びが出て階差が崩れる。

## 反証条件

- 予測が破綻とした条件(gate幅6,000 / 8,000)でring未読がring容量を超えず飛びの階差も崩れない
- 予測が成立とした条件(2,044 / 4,000)でring未読がring容量を超える
- 境界が5,000と6,000の間に来ない
- bit 7が0のsampleが出る

## 方法

[E048](../e048_p4_gated_rate_ceiling/README.ja.md)の3者共有構成をそのまま使い、sample rateを160 MHzに固定してgate幅だけを掃引する。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 9、`resolution_hz` = 20,000,000、`en_partial_rx=true`、buffer 32 symbol
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは`gate幅 + 4,000 word`で、先頭`gate幅`だけbit 7をhighにする。bit 0〜6は`gray7(index & 0x7F)`
- 回収は100 msの固定window、destinationは4 MiB PSRAM
- gate幅掃引: 2,044 / 4,000 / 5,000 / 6,000 / 8,000 word

drop判定は[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)と同じring未読byte数の最大値である。あわせて飛びの階差がwindow byte長と一致するかを見る。

APIの不成立も結果として最後まで記録する。

## 対象外

ring容量を変えたときの線形性、sample rateの掃引(160 MHz固定)、gapを変えたときの挙動、data_width 16、長時間の持続確認、RMTのduration検証(本実験の問いではないので記録のみ)。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

gate幅、loop長、window byte長、予測過負荷、各API結果、ring未読最大とring超過判定、queue overflow、bit 7が0だったsample数、飛びの総数と先頭16 offset、階差がwindow byte長と一致した数、回収byte、回収rate、RMT callback数とduration、経過us。

## 完了条件

各gate幅について成立・破綻とring未読量を記録し、境界がmodelの予測位置(5,000〜6,000 wordの間)に来るかを確定する。来ない場合は実測の境界とmodelのずれを記録して完了とする。

## 影響

modelが当たれば、gated captureのcapabilityを「dutyがいくら」ではなく「最大window長がいくらまで」という形で申告でき、ring容量から計算できるようになる。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のgate行に上限式の裏付けが付く。外れれば律速が別にあることになる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E049_20260909T090339Z_default/test_gated_window_absorption/dut.log`

| gate幅 | window byte | 予測過負荷 | duty | 平均byte rate | ring未読最大 | ring超過 | queue overflow | 飛び | 期待境界数 | 階差一致 |
|---:|---:|---:|---:|---:|---:|:-:|---:|---:|---:|---:|
| 2,044 | 65,408 | 25,345 | 33.8% | 54.1 MB/s | 33,280 | — | 0 | 64 | 64.1 | 15 / 15 |
| 4,000 | 128,000 | 49,600 | 50.0% | 80.0 MB/s | 61,504 | — | 0 | 33 | 32.8 | 15 / 15 |
| 5,000 | 160,000 | 62,000 | 55.6% | 88.9 MB/s | **77,632** | **超過** | 0 | 26 | 26.2 | 15 / 15 |
| 6,000 | 192,000 | 74,400 | 60.0% | 96.0 MB/s | **92,288** | **超過** | 0 | 22 | 21.8 | 15 / 15 |
| 8,000 | 256,000 | 99,200 | 66.7% | 106.7 MB/s | 442,624 | 超過 | **51** | **93** | 16.4 | **4 / 15** |

**E048のmodelは反証された。** modelはring容量65,536を過負荷が超える条件、つまりgate幅5,000以上で破綻すると予測した。しかし5,000と6,000はring未読がring容量を超えているのに**dataは完全に正常**である。飛びの数は期待するwindow境界数と一致し(26対26.2、22対21.8)、先頭15個の階差はすべてwindow byte長と厳密に一致した。ringの未読量がring容量を超えることは、この構成ではdata喪失の指標になっていない。

**実測の境界はgate 6,000と8,000の間にあり、平均byte rateが持続spool帯域を横切る点と一致する。**

| gate幅 | duty × sample rate | 対 98 MB/s | 結果 |
|---:|---:|---|---|
| 2,044 | 54.1 MB/s | 下 | 正常 |
| 4,000 | 80.0 MB/s | 下 | 正常 |
| 5,000 | 88.9 MB/s | 下 | 正常 |
| 6,000 | 96.0 MB/s | **すぐ下** | 正常 |
| 8,000 | 106.7 MB/s | **上** | **破綻** |

gate 8,000だけがqueue overflow 51件を出し、飛びが93個(期待16.4個)へ跳ね、階差の一致が4 / 15へ落ちた。実際にsampleを失っているのはこの条件だけである。

**測り方の交絡を明記する。** gapを4,000 word固定にしたので、gate幅を伸ばすとdutyも一緒に上がってしまった(33.8% → 66.7%)。したがってこの掃引ではwindow長と平均byte rateを分離できていない。5点すべてを説明できるのは平均byte rateのmodelだけで、window長のmodelは5,000と6,000で外れる。分離するにはgapをwindowに比例させてdutyを固定した掃引が要る。

副次的な観測を二つ残す。

- **gate 8,000でRMTがhighを記録しなかった。** high durationは8,000 × 4 = 32,000 tickで、`signal_range_max_ns` 1.6 msの32,000 tickと同じである。RMTはこれをidleと見て受信を打ち切った。hardwareの限界ではなく設定の問題で、`signal_range_max_ns`は想定する最長levelより確実に大きく取る必要がある
- gate 2,044と5,000でlow durationに16,000〜16,064 tickの64 tick幅のばらつきが出た。4,000と6,000は16,000固定だった。原因は切り分けていない

またring未読量とwindow byte長の比から、window区間中の実効drain帯域は約82〜83 MB/sと計算できる。E036が測った持続値98 MB/sより低い。gate中はDMAが160 MB/sでringへ書き込みながらCPUが同じringから読むため、競合が持続状態より厳しくなると考えられる。

## 判定

**gated captureの成立条件は`duty × sample rate < 持続spool帯域(約98 MB/s)`である。E048で立てた1 windowの過負荷modelは支持されない。**

E048の結論のうち「gated captureは160 MHzまで成立する」は変わらない。E048のdutyは27.7%で平均44.3 MB/sだったので、この条件を十分に満たしていた。変わるのは機構の説明である。

- **誤り**: gateは持続的な過負荷をringが吸収できるburstへ変換し、ring容量が上限を決める
- **正しい**: gateは**平均byte rateを持続spool帯域の下へ下げる**。持続spool帯域そのものは変わっていない。sample rateが98 MHzを超えられるのは、間引きによってbyte rateが下がるからである

したがってcapabilityは次の形で申告する。

```
sample rate ≤ 160 MHz(内部clock源の上限)
かつ duty × sample rate × (bytes/sample) < 約98 MB/s
```

8 channel(1 byte/sample)なら、duty 50%で最大196 MHz相当まで許容されるが実際はclock源の160 MHzで止まる。duty 60%では163 MHz相当なのでやはり160 MHzが取れる。**duty 61%以上では160 MHzが取れなくなる**、というのが実用上の境界になる。

ring容量については、未読量がring容量を超えてもdataが正常だった事実が残る。queueは64 entry × 約4,032 byte ≒ 258 KiBで、破綻したgate 8,000の未読最大442,624 byteはこれを超えている。ringとqueueのどちらが実際の緩衝なのかは本実験では決まらない。

## 事実・候補・未決

**事実**

1. gate幅2,044 / 4,000 / 5,000 / 6,000は正常だった。飛びの数が期待境界数と一致し、先頭15個の階差はすべてwindow byte長と厳密に一致した。
2. **gate 5,000と6,000ではring未読最大が77,632 / 92,288 byteでring容量65,536を超えたが、dataは正常だった。** ring未読 > ring容量はこの構成でdata喪失の指標になっていない。
3. gate 8,000だけが破綻した。queue overflow 51件、飛び93個(期待16.4個)、階差一致4 / 15である。
4. **実測の境界は平均byte rate(duty × sample rate)が約98 MB/sを横切る点と一致した。** 6,000で96.0 MB/sが正常、8,000で106.7 MB/sが破綻である。
5. gapを固定したためgate幅とdutyが一緒に動いており、window長と平均byte rateを分離できていない。5点すべてを説明できるのは平均byte rateのmodelだけである。
6. gate 8,000でRMTがhighを記録しなかったのは、high duration 32,000 tickが`signal_range_max_ns`の32,000 tickに達してidle扱いになったためである。設定の問題である。
7. window区間中の実効drain帯域はring未読量から約82〜83 MB/sと計算でき、E036の持続値98 MB/sより低い。

**候補**: gated captureのcapabilityを`sample rate ≤ 160 MHz`かつ`duty × sample rate × bytes/sample < 約98 MB/s`として申告する。E048のwindow過負荷modelは採用しない。`signal_range_max_ns`は想定最長levelより十分大きく取る。

**未決**: gapをwindowに比例させてdutyを固定した掃引(window長と平均rateの分離) / ringとqueueのどちらが実際の緩衝なのか / window区間中のdrain帯域が持続値より低い理由 / low durationの64 tickばらつき / duty 61%付近での160 MHz可否の実測 / destinationを大きくした長時間持続。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): gate行の成立条件をwindow過負荷式から`duty × sample rate < 持続spool帯域`へ直す
- [E048](../e048_p4_gated_rate_ceiling/README.ja.md): 機構の説明が反証されたことを追記する
- [LEDGER](../LEDGER.ja.md): E049の節

## 追記 — E050による確定(2026-09-09)

本レポートは書き換えない。[E050](../e050_p4_gated_window_at_fixed_duty/README.ja.md)がgapをwindowに比例させてdutyを50%に固定し、window byte長を32,000から224,000まで振った結果、次が確定した。

- **本実験で残した交絡は解けた。境界はwindow長ではなくdutyによるものだった。** duty固定なら window byte長224,000(ring容量の3.4倍)でもdataは正常で、ring未読は106,880 byteに達していた。成立条件は`duty × sample rate × bytes/sample < 約98 MB/s`の一本である。window長に上限は無い。
- **gate 8,000でRMTがhighを記録しなかった理由の解釈は成り立たない。** 本レポートは`signal_range_max_ns`(32,000 tick)への到達と説明したが、E050ではhigh 24,000 tickのgate 6,000でも同じくcallbackが0回だった。共通しているのはloop周期が2.4 ms以上であることで、1.6 ms以下では取得できている。原因は未解明である。

## 追記 — E051で見つかったmsync非整列(2026-09-09)

本レポートは書き換えない。[E051](../e051_p4_rmt_partial_threshold/README.ja.md)で、`build_pattern`が`loop_words`だけを`esp_cache_msync`しており、64 byteのcache line境界に載らないためerror logが出ていたことが分かった。本実験でも同じerrorが出ていた。

ただし**本実験のdataは検証を通っており結論は変わらない**。この環境ではflushが失敗してもCPUの書き込みはDMAから見えていたことになる。E051ではbuffer全体をsyncする形へ直している。
