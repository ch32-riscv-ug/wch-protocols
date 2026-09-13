# E084 転送の形を詰める — capture と同時に降ろすときの上限

状態: **完了 — TX FIFO 32768 / 1 転送 8192 で排出 21.97 → 23.69 MB/s(+7.8%)、釣り合い点 86 → 90 Msps。host 側の URB は何をしても変わらない**(2026-09-13)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E078](../e078_p4_continuous_stream/README.ja.md)(既定の形で 21.5 MB/s、釣り合い点 86 Msps)、[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)(FIFO の深さ)、[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)(host の URB の大きさ)

## 問い

**capture と同時に降ろすとき、TX FIFO / 1 転送長 / host 側の URB をどう選ぶと排出が最大になるか。そのとき 2 channel の釣り合い点はどこまで上がるか。**

## なぜこの問いか

[E078](../e078_p4_continuous_stream/README.ja.md)は **ESP32-P4 の新既定(TX FIFO 4096 / 1 転送 4096)のまま**測って 21.5 MB/s、釣り合い点 86 Msps だった。

**ライブラリ側は単体でこれより上を測っている** — 8192/8192 で **22.81 MB/s**、32768/8192 で **23.34**(既定の 21.12 に対して +8〜10%)。ただし**それは capture が走っていない条件**である。

**排出が上がればそのまま釣り合い点が上がる**(2 channel は 1 byte = 4 sample なので、MB/s × 4 が Msps)。**21.5 → 22.8 なら 86 → 91 Msps** になる計算で、logic analyzer の実用上限が直接伸びる。

## 仮説

**単体と同じ方向に効き、同じくらい伸びる。** 8 KiB あたりで飽和し、**釣り合い点は 90 Msps 前後**になる。

**host 側の URB は既に足りている**と見ている([E079](../e079_p4_host_urb_depth/README.ja.md): depth 2 で飽和)。256 KiB × 4 本は余裕のはずだが、**capture と同時だと host 側の余裕も減る**ので確かめる。

## 反証条件

1. FIFO / 転送長を大きくしても排出が変わらない(capture との同居が別の律速を作っている)
2. 大きくすると**却って落ちる**(弾性 FIFO からの読み出しが粗くなり、harvest と競合する)
3. 釣り合い点が排出に比例して上がらない

## 方法

[E078](../e078_p4_continuous_stream/README.ja.md)の firmware を、**`write()` に渡す塊をライブラリの `writeCapacity()` から取る**よう直したものを使う(固定 4096 だと FIFO を広げても使われない)。

- **device 側**: `build_opt.h` で `CFG_TUD_VENDOR_TX_BUFSIZE` と `CFG_TUD_VENDOR_TX_EPSIZE` を振る — **4096/4096(既定)/ 8192/8192 / 16384/8192 / 32768/8192**
- **host 側**: URB の大きさ(256 KiB / 1 MiB)と depth(2 / 4)
- **飽和条件で測る**: 96 MHz(釣り合い点より上)なので、出てくる MB/s がそのまま排出の上限になる
- 各条件 3 回以上
- **いちばん速い形で釣り合い点を測り直す**(64 MiB の長い run で弾性 FIFO の占有が伸びるかを見る。[E078](../e078_p4_continuous_stream/README.ja.md) と同じ判定)

### 記録する数値

- 条件ごとの排出 MB/s、`stalls` / `waits`、短く返った URB の数
- 最良条件での釣り合い点(保つ最大 rate と積み始める rate)

## 対象外

- 64 KiB 以上の FIFO(`tu_edpt_stream_init()` が `uint16_t` で受けるため 65536 は depth 0 になる。ライブラリ側で 32768 超はビルドエラー)
- CDC / HID 経路
- 4 / 8 channel

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、HS port を usbipd で WSL へ
- [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) の working tree
- host: `uv run --with libusb1 --with numpy`

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**device 側 4 条件 × host 側 2 条件以上を測り、最良の形での釣り合い点を出す。**

## 影響

- [E078](../e078_p4_continuous_stream/README.ja.md) の釣り合い点 86 Msps
- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) の連続 streaming 上限
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md)

## 結果

### device 側の形(96 MHz で飽和させて測る、16 MiB × 3 回)

| TX FIFO / 1 転送 | MB/s | 平均 | `waits` |
|---|---|---:|---:|
| **4096 / 4096(ESP32-P4 の既定)** | 21.84 / 22.10 / 21.96 | **21.97** | 4,094 |
| 8192 / 8192 | 22.39 / 23.71 / 23.88 | 23.33(ばらつく) | 2,046 |
| 16384 / 8192 | 23.64 / 23.38 / 23.41 | 23.48 | 1,023 |
| **32768 / 8192** | **23.74 / 23.62 / 23.72** | **23.69** | 511 |

**+7.8%。** 8 KiB で大半は取れるが、**8192 は run ごとのばらつきが大きく(22.39〜23.88)、32768 は 0.12 の幅に収まる**。`waits`(= 1 転送につき 1 回 block)は FIFO に反比例して減る。

### host 側の形(FIFO 32768 / 8192、96 MHz、3 回ずつ)

| URB の大きさ × depth | MB/s |
|---|---|
| 256 KiB × 4 | 23.74 / 23.62 / 23.72 |
| 256 KiB × 2 | 23.66 / 23.62 / 23.46 |
| 1 MiB × 4 | 23.67 / 23.80 / 23.77 |
| 1 MiB × 2 | 23.48 / 23.69 / 23.81 |
| 64 KiB × 4 | 23.70 / 23.53 / 23.78 |

**どれも 23.5〜23.8 で差がない。** **64 KiB から 1 MiB まで、depth 2 でも 4 でも変わらない。** → **host 側は律速ではない。** [E079](../e079_p4_host_urb_depth/README.ja.md)(depth 2 で飽和)と整合し、**大きさにも鈍い**ことが分かった。

### 釣り合い点(FIFO 32768 / 8192)

**判定は「届いた MB/s が生成 MB/s に届いているか」と「占有が duration で伸びるか」の両方。**

| rate | 生成 | 届いた MB/s(64 MiB) | 占有 16 MiB | 占有 64 MiB | 判定 |
|---:|---:|---|---:|---:|---|
| **90 MHz** | 22.50 | **22.40 / 22.53** | 89 / 105 KB | 315 / 163 KB | **保つ** |
| 92 MHz | 23.00 | 22.65 / 22.74 | 505 / 413 KB | 1,147 / 1,006 KB | 積む |
| 94 MHz | 23.50 | 22.99 / 23.11 | — | 1,028 / 1,040 KB | 積む |
| 96 MHz | 24.00 | 23.76 / 23.74 | 250〜307 KB | 696 / 849 KB | 積む |

**90 Msps は届いた値が生成値に一致し、占有も小さいまま。92 Msps から下回る。** → **釣り合い点は 90 Msps**([E078](../e078_p4_continuous_stream/README.ja.md) の 86 から +4)。

**占有は rate に対して単調ではない**(92 / 94 より 96 のほうが小さい)。**釣り合い点より下では FIFO が枯れて ZLP が出る**ぶん効率が落ち、上では pipeline が埋まって効率が上がるためで、**占有だけを見ると判定を誤る**。短く返った URB の数も同じ向き(90 MHz で 11〜43 本、96 MHz で 53 本 ← これは別要因)なので、**「届いた MB/s 対 生成 MB/s」を主の判定にする**のがよい。

## 事実

1. **TX FIFO と 1 転送長を広げると排出が 21.97 → 23.69 MB/s(+7.8%)。** ライブラリ側の単体測定(21.12 → 23.34、+10%)と同じ方向・同じ程度で、**capture と同居していても効きは失われない**。→ **反証条件 1・2 は否定。**
2. **32768 / 8192 が最良で、いちばん安定している。** 8192 / 8192 でも平均は近いが**ばらつきが 1.5 MB/s ある**。
3. **host 側の URB は何をしても変わらない。** 64 KiB〜1 MiB、depth 2〜4 のどれでも 23.5〜23.8。**律速は device 側にある**。
4. **釣り合い点は 86 → 90 Msps。** 排出の伸び(+7.8%)がそのまま上限に乗った。→ **反証条件 3 も否定。**
5. **占有の大きさだけでは判定できない。** 釣り合い点より下では ZLP のぶん効率が落ちるので、**占有は rate に対して単調にならない**。**届いた MB/s と生成 MB/s の比を主の判定にする。**
6. **代償は internal RAM 32 KB。** 弾性 FIFO(PSRAM 8 MiB)とは別に、TX FIFO が内部 RAM を食う。

## 候補

- **`CFG_TUD_VENDOR_TX_BUFSIZE=32768` / `CFG_TUD_VENDOR_TX_EPSIZE=8192` を既定にする。** internal RAM 32 KB と引き換えに +7.8%
- **2 channel の連続 streaming は 90 Msps を上限に置く**(余裕を見るなら 88)
- **host 側は好きな形でよい。** URB の大きさも depth も効かない
- **`write()` に渡す塊は `writeCapacity()` から取る。** 固定値だと FIFO を広げても使われない

## 未決

- **8192 のばらつきの原因** `—`。16384 以上では収まる
- **internal RAM の余裕がない sketch での折り合い** `—`。8192 なら 8 KB で平均 23.33
- **4 / 8 channel での釣り合い点** `—`
- **96 Msps で短く返った URB が増える理由** `—`。ZLP とは別の要因に見える

## 影響

- [E078](../e078_p4_continuous_stream/README.ja.md) の釣り合い点 86 Msps → **90 Msps**
- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) の連続 streaming 上限
- [E082](../e082_p4_spool_then_convert/README.ja.md) の spool 速度(21.5 → 23.7 MB/s 見込み)
