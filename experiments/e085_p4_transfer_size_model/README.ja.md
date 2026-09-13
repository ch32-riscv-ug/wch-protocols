# E085 1 転送あたりの死に時間と、線上の漸近 rate

状態: **完了 — `S/R + T` で表せる(残差 1.0%)。`R` = 24.64 MB/s、`T` = 21.7 us。転送長を無限に伸ばしても 24.4 MB/s で、36.4 には届かない**(2026-09-13)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E084](../e084_p4_transfer_tuning/README.ja.md)(2 点から `R` ≈ 26 MB/s、`T` ≈ 31 us を外挿)

## 問い

**1 転送の長さを変えたときの所要は `S / R + T` で表せるか。`R`(線上の漸近 rate)と `T`(1 転送あたりの死に時間)はいくらか。**

## なぜこの問いか

[E084](../e084_p4_transfer_tuning/README.ja.md)は転送長 4096 と 8192 の 2 点から

```
R = 26.34 MB/s   T = 30.8 us
```

を出し、**「死に時間を完全に消しても 26 MB/s にしかならないので、host 役の 36.4 MB/s との差は転送の arming では説明できない」**と結論した。**しかし 2 点では直線を引いただけで、模型が正しいかは検証していない。**

`R` が本当に漸近線なら、**転送長をいくら伸ばしても 26 MB/s を超えない**。もし `R` が転送長とともに上がるなら、**律速は別のところにある**。

## 仮説

**`S / R + T` で表せる。** 4 点が同じ直線(period 対 S)に乗り、`R` は 25〜27 MB/s、`T` は 30 us 前後になる。

## 反証条件

1. period が S に対して直線にならない(模型が違う)
2. `R` が転送長とともに上がる(漸近線ではない)
3. 16384 で mount しない、または落ちる

## 方法

[E084](../e084_p4_transfer_tuning/README.ja.md)の firmware をそのまま使い、**`CFG_TUD_VENDOR_TX_EPSIZE` だけを振る**。FIFO は**転送長の 2 倍以上**を確保して、FIFO 側が律速にならないようにする。

| 条件 | FIFO | 1 転送 |
|---|---:|---:|
| A | 8192 | **2048** |
| B | 8192 | **4096** |
| C | 16384 | **8192** |
| D | 32768 | **16384** |

- 96 MHz(釣り合い点より上)で飽和させ、**n=9**([E084](../e084_p4_transfer_tuning/README.ja.md) の訂正: n=3 では分散を語れない)
- period = S / rate を出し、**S に対して直線回帰**する。傾きの逆数が `R`、切片が `T`

### 記録する数値

- 条件ごとの median / min–max / stdev、period
- 回帰の `R` と `T`、残差
- `waits`(= 転送数)が S に反比例するかの確認

## 対象外

- device 側の in-flight 2 本([CR-7](../../references/espusbdevice-change-requests.ja.md))
- host を替えての切り分け([HR-1](../../references/espusbhost-change-requests.ja.md) 待ち)
- 32768 超の FIFO(ライブラリがビルドエラーにする)

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、HS port を usbipd で WSL へ
- **[EspUsbDevice 2.3.0](https://github.com/tanakamasayuki/EspUsbDevice)**(Library Manager。**src は測定に使った working tree と byte 単位で同一**)
- host: `uv run --with libusb1 --with numpy`

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**4 点で `R` と `T` を回帰し、残差を示す。** `R` が漸近線かどうかを述べる。

## 影響

- [E084](../e084_p4_transfer_tuning/README.ja.md) の分析(2 点からの外挿)の裏取り
- [EspUsbDevice への改修依頼](../../references/espusbdevice-change-requests.ja.md) CR-7 — **`T` を消す価値がいくらかが決まる**
- [EspUsbHost への改修依頼](../../references/espusbhost-change-requests.ja.md) HR-1 — **`R` が device 側の限界かを確かめる唯一の道**

## 結果

**128 MHz(釣り合い点よりはるかに上)で飽和させ、各条件 n=9。**

| FIFO / 1 転送 | median MB/s | min–max | period |
|---|---:|---|---:|
| 8192 / **2048** | 19.13 | 19.05–19.23 | 107.06 us |
| 8192 / **4096** | 21.84 | 21.64–22.02 | 187.55 us |
| 16384 / **8192** | 23.36 | 23.13–23.48 | 350.68 us |
| 32768 / **16384** | 23.81 | 23.50–24.02 | 688.11 us |

### 直線回帰

```
period(S) = S / R + T      R = 24.64 MB/s      T = 21.67 us
```

**残差は ±3.44 us、平均 period の 1.0%。** 4 点が同じ直線に乗る。

| 転送長 | 残差 |
|---:|---:|
| 2048 | +2.27 us |
| 4096 | −0.35 us |
| 8192 | −3.44 us |
| 16384 | +1.53 us |

### 外挿

| 1 転送 | 予測 |
|---:|---:|
| 32768 | 24.25 MB/s |
| 65536 | 24.44 MB/s |
| ∞ | **24.64 MB/s**(= `R`) |

## 事実

1. **模型は成立する。** 4 点が `S/R + T` の直線に残差 1.0% で乗る。→ **反証条件 1 は否定。**
2. **`R` は漸近線である。** 転送長を倍にするたびに利得は半減し、16384 → ∞ でも +3.5% しかない。→ **反証条件 2 も否定。**
3. **`R` = 24.64 MB/s は microframe あたり 6.02 transaction。** HS が許すのは 13、host 役は 8.89。**device 役はバスの半分以下しか使えていない。**
4. **[E084](../e084_p4_transfer_tuning/README.ja.md) の 2 点外挿(`R` = 26.34、`T` = 30.8)はずれていた。** `R` は 7%、`T` は 42% 過大。**結論(死に時間を消しても 36.4 には届かない)は変わらないが、数値は本実験のものを使う。**
5. **16384 は 8192 より速いが +1.9% にとどまる**(23.36 → 23.81)。**internal RAM を 32 KB 積む価値は薄い** — [E084](../e084_p4_transfer_tuning/README.ja.md) の結論(8192 が実用最良)と整合する。

### `R` は定数ではない — capture 負荷で動く

**本実験は 128 MHz で飽和させて測った。** 同じ 8192 構成でも、**capture が軽いほど排出は上がる**。

| capture rate | 生成 | 排出(実測) |
|---:|---:|---:|
| 96 MHz(釣り合い点) | 24.00 MB/s | **23.90〜24.07** |
| 110 MHz | 27.50 | 23.04〜23.23 |
| 128 MHz(本実験) | 32.00 | 23.13〜23.48 |

**PARLIO → PSRAM の harvest が重くなるぶん、USB 側が削られる。** したがって **`R` は「この capture 負荷での漸近線」**であって、素の USB 上限ではない。**素の上限を測るには capture を止めて同じ掃引をやり直す必要がある**(未実施)。

## 候補

- **1 転送は 8192 でよい。** 16384 の +1.9% に internal RAM 32 KB は見合わない
- **転送長をこれ以上伸ばしても頭打ち。** `T` を消す方向([CR-7](../../references/espusbdevice-change-requests.ja.md))の上限も 24.6 MB/s
- **36.4 MB/s との差は転送の組み立て方では埋まらない。** [HR-1](../../references/espusbhost-change-requests.ja.md) で「訊く側」を作って切り分ける

## 未決

- **capture を止めた状態での `R`** `—`。本実験は capture 同時のみ
- **`T` = 21.7 us の内訳** `—`。完了割り込み → event queue → usbd task → 再 arm のどこが効いているか
- **`R` が microframe あたり 6 transaction で止まる理由** `—`。device 側の供給か PC の token 発行か([HR-1](../../references/espusbhost-change-requests.ja.md))

## 影響

- [E084](../e084_p4_transfer_tuning/README.ja.md) の 2 点外挿 — **4 点で置き換え**
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §0 の内訳表
- [CR-7](../../references/espusbdevice-change-requests.ja.md) — **`T` を消して得られる上限が 24.6 MB/s と分かった**
