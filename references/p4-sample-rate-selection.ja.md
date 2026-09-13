# ESP32-P4 ロジアナの sample rate — 何を出せて、何を選ばせるか

状態: **reference**(2026-09-13。[E086](../experiments/e086_p4_8ch_stream/README.ja.md) / [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) / [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) / [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) の実測から)

PulseView などへ「選べる sample rate」を出すとき、**値は 3 つの事情で決まる** — clock で作れるか、線に載るか、client の driver が受け付けるか。

## 1. clock で作れる rate — 1 MHz 刻みでも 10 MHz 刻みでも全部出る

**source は `PLL_F160M` = 160 MHz、分周器は integer(1〜256)+ numerator / denominator の分数分周**(`parlio_ll_rx_set_clock_div()`)。**160 ÷ 整数に限られない。**

実測(周期 = rate ÷ 100 kHz がちょうど出るかで判定):

| 掃引した rate | 結果 |
|---|---|
| **1 / 2 / 3 / 4 / 5 / 6 / 7 / 8 / 9 MHz** | **全部ちょうど**(周期 10 / 20 / … / 90) |
| **10 / 20 / 30 / 40 / 50 / 60 / 70 / 80 / 90 / 100 MHz** | **全部ちょうど**(周期 100 / 200 / … / 1000) |
| 86 / 88 / 90 / 92 / 94 / 96 / 98 MHz | ちょうど([E084](../experiments/e084_p4_transfer_tuning/README.ja.md)) |
| 160 MHz | ちょうど([E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) / [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md)) |

**上限は源の 160 MHz、下限は integer 256 分周で約 625 kHz**(分数分周でさらに細かく)。

> **希望どおりの刻みが作れる** — 一桁は 1 MHz 刻み、それ以上は 10 MHz 刻み。**clock 側の制約は無い。**

## 2. 線に載る rate — byte rate 23 MB/s が天井

**packing は `1 byte = 8 ÷ channel 数 sample`** なので、**線上の byte rate = `rate × channel 数 ÷ 8`**。

| channel | **継ぎ目なく流せる最大 rate** | = byte rate | 出典 |
|---:|---:|---:|---|
| **8ch** | **23 Msps**(余裕を見て 20) | 23.0 MB/s | [E086](../experiments/e086_p4_8ch_stream/README.ja.md) |
| **4ch** | **46 Msps**(48 は際どい) | 23.0 MB/s | 同上 |
| **2ch** | **96 Msps** | 24.0 MB/s | [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) |
| 1ch | (byte rate では 184 Msps 相当だが)**160 Msps で clock 頭打ち** | 20.0 MB/s | 未測定 |

**上限は sample rate ではなく byte rate で決まる**(8 / 4 / 2ch が同じ byte rate で頭打ちになる)。**見積もりは `rate × channel ÷ 8 ≤ 23 MB/s`。**

### batch なら別の天井

**PSRAM へ貯めてから降ろす**なら USB は関係ない。律速は **PARLIO の持続 spool 帯域(約 98 MB/s)**と **PSRAM の容量**。

| channel | batch の上限 | 出典 |
|---:|---|---|
| 1 / 2 / 4ch | **160 Msps**(clock 源の上限) | [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) / [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) |
| 8ch | **96 Msps**(1 MiB で sample 精度)。160 Msps は burst 窓のぶんだけ | [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) |

**継ぎ目を許すかどうかで上限が 2〜4 倍違う。**

## 3. client が受け付ける rate — driver ごとに違う

**「選べる rate」は演じる device の driver が決める。** ここが target によって変わる部分である。

| 経路 | client 側の list | 効く制約 |
|---|---|---|
| **BeagleLogic の TCP を演じる**([E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md) / [E080](../experiments/e080_p4_pulseview_gapless/README.ja.md)) | **10 Hz〜100 MHz**(libsigrok の `beaglelogic` driver) | **100 MHz を超える rate は要求できない。** 160 Msps は出せない |
| `.sr` を書く([E082](../experiments/e082_p4_spool_then_convert/README.ja.md)) | **制約なし**(metadata に書くだけ) | 160 Msps も書ける |
| (参考)fx2lafw | 20 kHz〜24 MHz の離散 list | 置き換える相手の値 |

**BeagleLogic の sample unit は 1 byte(8 channel)か 2 byte(最大 14 channel)** なので、**8 channel はそのまま 1 byte/sample に載る**。[E086](../experiments/e086_p4_8ch_stream/README.ja.md) のとおり **PARLIO の packed がそのまま sigrok の形式**になり、host 側の展開が要らない。

## 4. したがって、何を出させるか

**channel 数が決まると上限が決まる**ので、**server は channel 数に応じて list を変える**のがよい。

| channel | 出してよい list(streaming) | 上限の根拠 |
|---:|---|---|
| **8ch** | 1 / 2 / 3 / 4 / 5 / 6 / 7 / 8 / 9 / 10 / **20** Msps | 23 Msps([E086](../experiments/e086_p4_8ch_stream/README.ja.md)) |
| **4ch** | … / 10 / 20 / 30 / 40 Msps | 46 Msps |
| **2ch** | … / 10 / 20 / … / 80 / 90 Msps | 96 Msps |

**余裕を 1 割見て切りのよい値で止める**(8ch なら 23 ではなく 20)。理由は 2 つ。

- **排出は一定ではない** — capture 負荷が上がると下がる([E085](../experiments/e085_p4_transfer_size_model/README.ja.md))
- **97 / 99 MHz のように突発的に滞る rate がある**([E084](../experiments/e084_p4_transfer_tuning/README.ja.md)、未特定)

**batch なら別 list**(8ch は 96 まで、それ以外は 100 まで = driver の上限)。

## 5. pin は自由

**`data_gpio_nums[lane]` に lane ごとの任意 GPIO を渡せる**(GPIO matrix 経由)。**連番である必要も昇順である必要もない** — `9,2,7,4,12,6,20,8` で 8 lane すべて正しく取れることを[E086](../experiments/e086_p4_8ch_stream/README.ja.md)で確認した。**どの物理 pin が使えるかは board 側の事情**(flash / PSRAM / USB が占有する pin)であって PARLIO の制約ではない。

## FX2(fx2lafw)との比較

| | fx2lafw | **ESP32-P4** |
|---|---|---|
| channel | 8 | **8**(最大 8。16 は pin が足りない) |
| 公称 | 24 Msps | **23 Msps**(継ぎ目なし streaming) |
| 実用 | 16 Msps 程度 | **20 Msps を常用**([E086](../experiments/e086_p4_8ch_stream/README.ja.md)) |
| 少ない channel で | 変わらず | **2ch なら 96 Msps** |
| batch | 無し | **160 Msps / 16 Mi sample**([E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md)) |
| host 側 | 専用 driver(sigrok 同梱) | **stock の PulseView が driver 追加なし**([E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md)) |

**公称 24 Msps には 1 Msps 届かないが、実用域(16〜20 Msps)では置き換えになる。**

## 参照

- [E086](../experiments/e086_p4_8ch_stream/README.ja.md) — 8 / 4 channel の連続 streaming 上限、pin の自由度
- [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) — 2 channel の上限と転送の形
- [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) / [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) — batch の上限と幅ごとの sample 精度
- [PulseView / sigrok 連携](pulseview-integration.ja.md) — 経路と protocol
- [capture の圧縮](capture-compression.ja.md) — 帯域を減らす方向の選択肢
