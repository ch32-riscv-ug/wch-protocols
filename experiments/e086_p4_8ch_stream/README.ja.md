# E086 8 channel の連続 streaming — FX2 ロジアナの置き換えになるか

状態: **完了 — 8ch は 23 Msps まで継ぎ目なく流せる。20 Msps なら余裕。24 Msps(FX2 の公称)は積む**(2026-09-13)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E084](../e084_p4_transfer_tuning/README.ja.md)(2ch で 96 Msps)、[E075](../e075_p4_width_sample_accuracy/README.ja.md)(幅ごとの sample 精度)、[E083](../e083_p4_attach_order/README.ja.md)(準備順序)

## 問い

**8 channel で継ぎ目なく流せる sample rate の上限はいくらか。FX2(fx2lafw、8ch 公称 24 Msps)の置き換えになるか。**

## なぜこの問いか

[E078](../e078_p4_continuous_stream/README.ja.md)〜[E085](../e085_p4_transfer_size_model/README.ja.md)は**すべて 2 channel**で測ってきた。**実用の的は 8 channel** — 広く使われている FX2 系ロジアナがその形で、**公称 24 Msps、実用は 16 Msps 程度**と言われる。

**線に載る byte rate は `rate × lanes ÷ 8`** なので、2ch の 96 Msps(24 MB/s)がそのまま効くなら **8ch では 24 Msps** のはずである。**が、2ch でしか測っていない推測を答えにはできない。**

**8 channel には固有の利点もある** — PARLIO が **1 sample = 1 byte、bit n = lane n** で詰めるので、**それが sigrok の `unitsize=1` の形式そのもの**になる。[E077](../e077_p4_pulseview_over_ip/README.ja.md)/[E080](../e080_p4_pulseview_gapless/README.ja.md)で必要だった **host 側の 4 倍展開が要らない**。

## 仮説

**byte rate で決まるので 8ch では 24 Msps 前後。** 20 Msps は余裕を持って通る。

## 反証条件

1. byte rate が同じでも channel 数で上限が変わる
2. 20 Msps が通らない
3. 8ch では sample 精度が出ない([E075](../e075_p4_width_sample_accuracy/README.ja.md) は 96 MHz まで確認済みだが streaming ではない)

## 方法

[E084](../e084_p4_transfer_tuning/README.ja.md)の firmware を **`LANE_COUNT` で幅を選べる**ようにしたもの。TX FIFO / 1 転送は **8192**([E084](../e084_p4_transfer_tuning/README.ja.md) の最良)。

- 信号源は LEDC PWM 100 kHz、**lane ごとに別の duty**(32 / 64 / 96 / 128 / 160 / 176 / 192 / 208)。取り違えと死んだ lane を同時に検出する
- 判定は [E084](../e084_p4_transfer_tuning/README.ja.md) と同じ **届いた MB/s 対 生成 MB/s** と **弾性 FIFO の占有が duration で伸びるか**(64 MiB)、加えて**全 lane の立ち上がり周期**
- **4 channel も測る**(2ch と 8ch の間を埋める)

## 対象外

- 16 channel(`PARLIO_PINS` が 8 本)
- trigger、外部信号
- batch 捕捉([E074](../e074_p4_2ch_capture_to_sr/README.ja.md) / [E075](../e075_p4_width_sample_accuracy/README.ja.md))

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、HS port を usbipd で WSL へ、**[EspUsbDevice 2.3.0](https://github.com/tanakamasayuki/EspUsbDevice)**
- host: `uv run --with libusb1 --with numpy`

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**8ch で保つ最大 rate と積み始める rate を、周期判定つきで挟む。** 4ch も同様に。

## 結果

### 8 channel(1 sample = 1 byte、生成 MB/s = rate Msps)

| rate | 生成 | 届いた MB/s | 占有(64 MiB) | 周期 | 判定 |
|---:|---:|---|---:|---|---|
| 16 Msps | 16.0 | 16.04 / 16.04 / 16.04 | 23〜36 KB | 160/160 | **保つ** |
| **20 Msps** | **20.0** | **20.01 / 20.03 / 20.03** | **46〜189 KB** | **200/200** | **保つ** |
| 22 Msps | 22.0 | 22.04〜22.06 | 33〜73 KB | 220/220 | **保つ** |
| **23 Msps** | **23.0** | **23.03 / 23.04 / 23.04** | **36〜43 KB** | **230/230** | **保つ** |
| 24 Msps | 24.0 | 23.17 / 23.24 / 23.49 | **1.67〜1.95 MB** | 240/240 | **積む** |

### 4 channel(2 sample = 1 byte、生成 MB/s = rate ÷ 2)

| rate | 生成 | 届いた MB/s | 占有(64 MiB) | 周期 | 判定 |
|---:|---:|---|---:|---|---|
| 40 Msps | 20.0 | 20.05 / 20.05 | 26〜28 KB | 400/400 | **保つ** |
| **46 Msps** | **23.0** | **23.03 / 23.06** | **36〜64 KB** | **460/460** | **保つ** |
| 48 Msps | 24.0 | 23.87 / 23.94 | 186〜363 KB | 480/480 | **際どい** |
| 50 Msps | 25.0 | 23.78 / 23.91 | **2.78〜2.91 MB** | 500/500 | **積む** |

### 幅をまたぐと byte rate で揃う

| 幅 | 保てる最大 rate | **= 線上の byte rate** |
|---:|---:|---:|
| 8ch | **23 Msps** | 23.0 MB/s |
| 4ch | **46 Msps**(48 は際どい) | 23.0(24.0)MB/s |
| 2ch | **96 Msps**([E084](../e084_p4_transfer_tuning/README.ja.md)) | 24.0 MB/s |

**上限は sample rate ではなく byte rate で決まる。** → **反証条件 1 は否定。**

## 事実

1. **8ch は 23 Msps まで継ぎ目なく流せる。** 64 MiB(2.8 秒)で占有 36〜43 KB、全 lane の周期が完全一致。**20 Msps なら占有 46〜189 KB でさらに余裕。** → **反証条件 2・3 も否定。**
2. **24 Msps は積む。** 届いた値が生成値に 0.5〜0.8 MB/s 届かず、占有が 1.7〜2.0 MB まで伸びる。**FX2 の公称 24 Msps にはわずかに足りない。**
3. **上限は byte rate(23〜24 MB/s)で決まり、channel 数には依らない。** 8 / 4 / 2ch が同じ byte rate で頭打ちになる。
4. **8ch では host 側の展開が要らない。** PARLIO の packed(1 sample = 1 byte、bit n = lane n)が **sigrok の `unitsize=1` そのもの**なので、[E080](../e080_p4_pulseview_gapless/README.ja.md)の numpy 展開も [E082](../e082_p4_spool_then_convert/README.ja.md)の変換も**素通し**にできる。**8ch は 2ch より host 側が軽い。**
5. **FX2 との比較**: FX2 は 8ch 公称 24 Msps・実用 16 Msps 程度。**P4 は 8ch で 20〜23 Msps を継ぎ目なく保つ**ので、**実用域では置き換えになる**。公称値には 1 Msps 届かない。

### 方法の誤り(§7-6)

**4ch の初回測定で周期が `0/501` のように壊れた。** firmware ではなく **host 側 `drain_sweep.py` の `unpack()` が 8ch と 2ch しか扱えず、4ch を 2 bit/sample として展開していた**。`lanes` から `per_byte` と mask を導く形に直したところ、同じ capture が 400/400 になった。**「幅を変えたら host 側の展開も変わる」を見落とすと、device 側の不具合に見える。**

### pin は自由に選べる

`PARLIO_PINS` を **`9,2,7,4,12,6,20,8`**(順序バラバラ、飛び飛び、元の 2〜9 の外の GPIO 12 / 20 を含む)にして測った。

```
D0=12.50%  D1=25.00%  D2=37.50%  D3=50.00%  D4=62.50%  D5=69.00%  D6=75.00%  D7=81.50%
20 Msps: 19.97 MB/s  overflow 0  period 200/200  OK
```

**8 lane すべてが正しく取れ、duty は lane 順どおりに並ぶ。** driver の API が `data_gpio_nums[lane]` に **lane ごとの任意 GPIO** を取る形(GPIO matrix 経由)なので、**連番である必要も、昇順である必要もない**。

> D5 が 69.00%(期待 68.75%)、D7 が 81.50%(期待 81.25%)なのは **LEDC 側の duty 量子化**で、capture の取り違えではない。lane ごとに duty が違うので、入れ替わっていれば一目で分かる。

**どの物理 pin が使えるかは board 側の事情**(flash / PSRAM / USB が占有する pin)であって、**PARLIO の制約ではない**。

## 候補

- **FX2 の置き換えとしては 8ch 20 Msps を常用、23 Msps を上限に置く**
- **8ch を既定の形にする。** host 側が素通しになり、sigrok の形式と一致する
- **幅を変えたら「byte rate = rate × lanes ÷ 8 が 23 MB/s 以下か」で見積もれる**
- **pin は空いているところへ自由に割り当てられる。** 連番・昇順である必要はない

## 未決

- **24 Msps を通す方法** `—`。byte rate の天井([E085](../e085_p4_transfer_size_model/README.ja.md) の `R` = 24.64 MB/s)を上げる必要があり、[HR-1](../../references/espusbhost-change-requests.ja.md) の切り分け待ち
- **16 channel** `—`。`PARLIO_PINS` が 8 本
- **外部信号での確認** `—`。信号源は内部 PWM
- **1 channel** `—`。byte rate では 184 Msps 相当だが clock 源が 160 MHz で頭打ち

## 影響

- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) — 幅ごとの連続 streaming 上限
- [sample rate の選び方](../../references/p4-sample-rate-selection.ja.md) — 何を出せるか
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §0
