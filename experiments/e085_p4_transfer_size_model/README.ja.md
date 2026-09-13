# E085 1 転送あたりの死に時間と、線上の漸近 rate

状態: **計画 — console 復帰待ち**(2026-09-13)

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

- ESP32-P4 `esp32-p4-30eda0e31478`、**console(現在落ちており挿し直しが要る)**、HS port を usbipd で WSL へ
- host: `uv run --with libusb1 --with numpy`

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**4 点で `R` と `T` を回帰し、残差を示す。** `R` が漸近線かどうかを述べる。

## 影響

- [E084](../e084_p4_transfer_tuning/README.ja.md) の分析(2 点からの外挿)の裏取り
- [EspUsbDevice への改修依頼](../../references/espusbdevice-change-requests.ja.md) CR-7 — **`T` を消す価値がいくらかが決まる**
- [EspUsbHost への改修依頼](../../references/espusbhost-change-requests.ja.md) HR-1 — **`R` が device 側の限界かを確かめる唯一の道**
