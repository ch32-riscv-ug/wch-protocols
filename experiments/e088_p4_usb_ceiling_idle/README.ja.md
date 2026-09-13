# E088 capture を止めた素の USB 上限

状態: **完了 — capture の有無で天井は動かない(どちらも 8 KiB 転送で約 23.9 MB/s)。模型は 8 KiB を超えると崩れ、16 KiB はむしろ遅い**(2026-09-13)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E085](../e085_p4_transfer_size_model/README.ja.md)(capture 同時で `R` = 24.64 MB/s)、[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)(同じ送出 firmware の原型)

## 問い

**capture を止めると、vendor bulk の天井はどこまで上がるか。[E085](../e085_p4_transfer_size_model/README.ja.md) の `R` = 24.64 MB/s は「その capture 負荷での漸近線」だったが、素の値はいくらか。**

## なぜこの問いか

[E085](../e085_p4_transfer_size_model/README.ja.md)は転送長 4 点から `period(S) = S/R + T` を当てはめ、**`R` = 24.64 MB/s(microframe あたり 6.02 transaction)**を出した。**ただし全点が capture 同時**で、**capture rate を上げると排出が下がる**ことも同時に観測した(96 MHz で 23.9、110 MHz で 23.1 MB/s)。

**したがって `R` は「素の USB 上限」ではない。** host 役の 36.4 MB/s との差を論じるには、**capture を止めた値**が要る。

## 仮説

**上がる。** capture が PSRAM と DMA を占める分が消えるので、**25〜27 MB/s** くらいまで伸びる。

## 反証条件

1. 変わらない(capture 負荷は天井に関係なかった)
2. 下がる
3. 模型(`S/R + T`)が capture の有無で形を変える

## 方法

**[E085](../e085_p4_transfer_size_model/README.ja.md) と同じものを、負荷だけ外して測る。**

- firmware は [E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md) 系(**internal RAM の 64 KiB pattern を繰り返し送るだけ**)。**PARLIO も harvest task も PSRAM も無い**
- 送出ループは [E084](../e084_p4_transfer_tuning/README.ja.md) / [E086](../e086_p4_8ch_stream/README.ja.md) と同じ **`waitWritable()` + FIFO 容量ちょうど**。`tx_chunk` は `writeCapacity()` から取る
- ライブラリは **[EspUsbDevice 2.3.0](https://github.com/tanakamasayuki/EspUsbDevice)**、host は URB 256 KiB × depth 4
- 転送長を [E085](../e085_p4_transfer_size_model/README.ja.md) と同じ 4 点(2048 / 4096 / 8192 / 16384)で振り、**各 n=9**

## 対象外

- capture 側の最適化
- CDC / HID 経路
- host を替えての切り分け([HR-1](../../references/espusbhost-change-requests.ja.md) 待ち)

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、HS port を usbipd で WSL へ、EspUsbDevice 2.3.0
- host: `uv run --with libusb1`

## ベンチ種別

board(単体、外部配線なし)

## 結果

4 MiB 転送、各 n=9。

| FIFO / 1 転送 | **idle(capture なし)** | 参考: [E085](../e085_p4_transfer_size_model/README.ja.md)(capture 同時) | 差 |
|---|---:|---:|---:|
| 8192 / 2048 | 18.65(18.43〜18.81) | 19.13 | **−0.48** |
| 8192 / 4096 | 22.34(22.09〜22.49) | 21.84 | +0.50 |
| **16384 / 8192** | **23.88**(23.28〜24.09) | 23.36 | **+0.52** |
| 32768 / 16384 | **23.40**(23.00〜23.83) | 23.81 | −0.41 |

**差は ±0.52 MB/s で、run 間の幅(0.38〜0.84)の内側**である。

### 模型は 8 KiB を超えると崩れる

2048〜8192 の 3 点で当てはめると

| | `R` | `T` | 16384 の予測 | 16384 の実測 |
|---|---:|---:|---:|---:|
| idle | 26.24 MB/s | 29.96 us | 25.04 MB/s | **23.40** |
| capture 同時 | 25.20 MB/s | 25.49 us | 24.25 MB/s | 23.81 |

**どちらも 16384 を 1.2〜1.6 MB/s 過大に予測する。** つまり **`S/R + T` は 8 KiB あたりまでの近似**であって、**`R` は本当の漸近線ではない**。実際 **idle では 16384 が 8192 より遅い**(23.40 対 23.88)。

## 事実

1. **capture の有無で天井は動かない。** 8 KiB 転送で **idle 23.88、capture 同時 23.36 MB/s**。差は run 間のばらつきの内側。→ **反証条件 1 が成立し、仮説は否定された。**
2. **[E085](../e085_p4_transfer_size_model/README.ja.md) の `R` = 24.64 MB/s は「素の上限」でもあった。** capture 負荷を外す理由で 36.4 MB/s との差を説明することはできない。
3. **8192 が最適で、16384 は遅くなる。** **capture の有無に関わらず**そうなので、[E084](../e084_p4_transfer_tuning/README.ja.md) の「8192 が実用最良」は負荷の産物ではない。
4. **`S/R + T` は 8 KiB までの近似。** 16 KiB を 1.2〜1.6 MB/s 過大に予測する。**`R` を「無限に伸ばしたときの値」として引用してはいけない。**
5. **[E084](../e084_p4_transfer_tuning/README.ja.md) で見た「capture 負荷で排出が下がる」(96 MHz 23.9 → 110 MHz 23.1)は残る**が、**3% 程度の効果**であって天井を決めてはいない。

## 候補

- **vendor bulk の実力は 8 KiB 転送で約 24 MB/s**。capture を止めても増えない
- **転送長は 8192。** それ以上は逆効果
- **36.4 MB/s との差は device 側の USB 経路そのもの**にある。capture でも転送長でも host software でもない → [HR-1](../../references/espusbhost-change-requests.ja.md)

## 未決

- **16 KiB で遅くなる理由** `—`。FIFO 32 KiB 側の問題か、転送長そのものか
- **microframe あたり 6 transaction で止まる理由** `—`。device 側の供給か PC の token 発行か([HR-1](../../references/espusbhost-change-requests.ja.md))
- **CDC / HID でも同じ天井か** `—`

## 影響

- [E085](../e085_p4_transfer_size_model/README.ja.md) の未決「capture を止めた `R`」— **解決。同じだった**
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §0 の内訳
- [HR-1](../../references/espusbhost-change-requests.ja.md) — **切り分けの必要性がさらに強まった**
