# E075 ESP32-P4 channel幅ごとのsample単位精度

状態: **完了 — 1 / 2 / 4 channelは160 Mspsでsample精度。8 channelは持続spool帯域の内側なら正確で、超えるとburst窓のぶんだけ**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E074](../e074_p4_2ch_capture_to_sr/README.ja.md)(2 channelでの周期判定)、[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)(dutyでは欠落を検出できない)

## 問い

**PARLIOのchannel幅1 / 4 / 8で、どのsample rateまでsample単位の欠落なしに取れるか。**

## なぜこの問いか

[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)の限界matrixは、**1 / 2 / 4 channel行に「160 MHz成立」と書きつつ※を付けて**「検証はdutyとedge数によるもので、**sample単位の欠落は検出できない**」と断っている。[E074](../e074_p4_2ch_capture_to_sr/README.ja.md)が**周期による判定**で2 channel行を埋めたので、**同じ方法で1 / 4 / 8 channelも埋める**。

## 仮説

**1 / 4 channelは160 Mspsで正確。8 channelは持続spool帯域(約98 MB/s)を超えると落ちる。**

packing後のbyte rateは `rate × channel ÷ 8`。160 MHzなら 1ch = 20、2ch = 40、4ch = 80、**8ch = 160 MB/s** で、**8 channelだけが持続spool帯域を超える**。

## 反証条件

1. 1 / 4 channelが160 Mspsで落ちる
2. 8 channelが160 Mspsでも落ちない(持続帯域の理解が誤り)
3. 落ちても`overflow`が0のまま(計数が欠落を捉えられない)

## 方法

[E074](../e074_p4_2ch_capture_to_sr/README.ja.md)と同じ形で、**`LANE_COUNT`を`build_opt.h`から与えて**幅を振る。

- 信号源はLEDC PWM 100 kHz、**lane別に異なるduty**(12.5 / 25 / 37.5 / 50 / 62.5 / 68.75 / 75 / 81.25%)。channelの取り違えも同時に検出できる
- 判定は**立ち上がりedgeの間隔**。`min = mean = max = rate ÷ 100 kHz`なら欠落なし
- `overflow`(ISRがqueueへ積めなかった回数)も併せて記録する

## 対象外

- 16 channel(`PARLIO_PINS`が8本しか持たない)
- trigger、pre/post、circular ring
- 外部信号(信号源は内部PWM)
- 持続spool帯域そのものの再測定([E021](../e021_p4_parlio_psram_spool/README.ja.md)/[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)の値を使う)

## 結果

### 幅 × rate(131,072 packed byte)

| channel | rate | byte rate | overflow | **周期 min / mean / max** | 判定 |
|---:|---:|---:|---:|---|---|
| **1** | 160 MHz | 20 MB/s | 0 | **1600 / 1600.00 / 1600** | **sample精度** |
| **2** | 160 MHz | 40 MB/s | 0 | 1600 / 1600.00 / 1600 | **sample精度**([E074](../e074_p4_2ch_capture_to_sr/README.ja.md)) |
| **4** | 80 MHz | 40 MB/s | 0 | **800 / 800.00 / 800** | **sample精度** |
| **4** | 160 MHz | 80 MB/s | 0 | **1600 / 1600.00 / 1600**(3回) | **sample精度** |
| **8** | 80 MHz | 80 MB/s | 0 | **800 / 800.00 / 800** | **sample精度** |
| **8** | 96 MHz | 96 MB/s | 0 | 960 / 960.00 / 960(3回) | **sample精度** |
| 8 | 120 MHz | 120 MB/s | 0 | 1200 / 1200.00 / 1200 | sample精度(**burst窓の内側**) |
| 8 | 160 MHz | 160 MB/s | 0 | 1600 / 1600.00 / 1600 | sample精度(**burst窓の内側**) |

### 深さを増やす(1,048,576 packed byte)

| channel | rate | byte rate | overflow | 周期 | 判定 |
|---:|---:|---:|---:|---|---|
| **8** | 80 MHz | 80 MB/s | **0** | **800 / 800.00 / 800** | **sample精度** |
| **8** | **160 MHz** | **160 MB/s** | **131** | 162 / 1567 / 2368 | **欠落** |

## 事実

1. **1 channelは160 Mspsでsample精度。** 周期1600 sampleがmin = mean = maxで一致。**限界matrixの※が外せる。**
2. **4 channelも160 Mspsでsample精度**(3回とも)。同じく※が外せる。
3. **8 channelは持続spool帯域の内側なら深さを増やしてもsample精度。** 80 MB/s・1 MiBでoverflow 0、周期完全一致。
4. **8 channelを160 MHz(160 MB/s)で1 MiB取ると落ちる。** `overflow=131`、周期が162〜2368にばらける。**持続spool帯域(約98 MB/s)を超えた分は、ringが吸収できる間しか保たない**という[限界matrix](../../references/p4-logic-analyzer-investigation.ja.md)のburst深度モデルどおり。**反証条件2は否定された。**
5. **短い捕捉(131 KiB)なら8 channel × 160 MHzでも通る。** ring 64 KiBに対し、超過分 (160−98) MB/s を吸収できる窓の内側に収まっているため。**「rate上限」を単一の値で言えない**という[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)の指摘がそのまま出た。
6. **`overflow`は欠落を正しく捉える。** 落ちた条件では131、それ以外は全条件で0。**反証条件3は否定。**

### 経路の異常 — 間欠的に1 channelが定数0になる

[§7-6](../README.ja.md)に従い観測として残す。

掃引の初回に、**`overflow=0`のまま特定の1 channelだけがduty 0.00%(edge 0)になる**ことが3回あった(4ch@160 MHzでD3、8ch@96 MHzでD5、8ch@104 MHzでも周期異常)。**いずれも同じ条件を再実行すると再現せず、3回連続でsample精度になる。**

- **`overflow`は0のまま**なので、capture経路のdropとは別の現象である
- **死ぬchannelは毎回違う**(D3 / D5)ので、特定のGPIOの問題ではない
- 疑わしいのは**test firmware側の準備順序** — `create_receiver()`(PARLIOのGPIO matrix入力を張る)が`configure_pwm()`(LEDC出力をattachする)より先に走っており、`ledcAttach`がそのpinのmatrix設定を踏むことがある、という筋。[E015](../e015_p4_parlio_routing_order/README.ja.md)が「LEDC出力とPARLIO RX入力は同一GPIOで共存する」を確認しているが、**attachの順序までは見ていない**

**capture経路そのものの問題ではない**と見ているが、**未特定**である。

> **2026-09-13 追記: [E083](../e083_p4_attach_order/README.ja.md) で特定した。** **疑っていた準備順序が原因だった** — `create_receiver()` を先に走らせると **cold boot の 29%(24 回中 7 回)**で 1 channel が死に、`configure_pwm()` を先にすると **30 回で 0 件**。`overflow` は常に 0 で、capture 経路の drop ではないという見立ても正しかった。**再現するのは hard reset 直後の 1 回だけ**(同一 boot 内 200 trial で 0 件、`esp_restart()` 10 回でも 0 件)で、「掃引の初回にだけ出る」「再実行すると再現しない」はこれで説明が付く。

## 限界matrixへの反映

| channel | 従来 | **本実験後** |
|---:|---|---|
| 1 | 160 MHz成立 ※(sample単位未検証) | **160 MHz sample精度確認** |
| 2 | 160 MHz成立 ※ | **160 MHz sample精度確認**([E074](../e074_p4_2ch_capture_to_sr/README.ja.md)) |
| 4 | 160 MHz成立 ※ | **160 MHz sample精度確認** |
| 8 | 104 MHz(1 Mi burst)/ 持続98 MB/s | **96 MHz(96 MB/s)まで1 MiBでsample精度**。160 MHzは131 KiBまで(burst窓) |

## 候補

- **1 / 2 / 4 channelは160 Msps(内部clock源の上限)まで使ってよい。** sample精度の裏が付いた
- **8 channelは96 MHz(96 MB/s)を実用上限に置く。** 持続spool帯域のすぐ内側
- **`overflow`を信用してよい。** 落ちた条件では必ず非0だった

## 未決

- ~~**間欠的に1 channelが定数0になる現象**~~ → **[E083](../e083_p4_attach_order/README.ja.md) で解決**(準備順序が原因)
- **16 channel** `—`。`PARLIO_PINS`が8本しかない
- 8 channelのburst窓の正確な境界 `—`。131 KiBは通り1 MiBは落ちたが、その間は未測定
- 外部信号での同じ検証 `—`

## 影響

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) 限界matrix — **1 / 2 / 4 channel行の※を外せる**。8 channel行に「96 MHzまで1 MiBでsample精度」を足せる
- [E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)の「rate上限は単一の値にならない」— **8 channelで再現した**
