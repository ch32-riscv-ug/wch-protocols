# E089 P4 を host にして bulk IN を読む — 天井は device 側か PC 側か

状態: **完了 — P4 host が 24.45 MB/s。PC(23.88)と同じ水準に届いた。約 24 MB/s は device 側の天井で確定**(2026-09-13)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 依頼: [EspUsbHost への改修依頼](../../references/espusbhost-change-requests.ja.md) HR-2 / HR-1 / 先行: [E072](../e072_p4_hs_device_to_host_native/README.ja.md)(5.6 MB/s で止まった)、[E088](../e088_p4_usb_ceiling_idle/README.ja.md)(PC 相手の基準値)

## 問い

**device 役の約 24 MB/s は device 側の限界か、PC の host controller が bulk IN に振る token 数の限界か。**

## なぜこの問いか

[E084](../e084_p4_transfer_tuning/README.ja.md)〜[E088](../e088_p4_usb_ceiling_idle/README.ja.md)で候補を潰した結果、**残るのはこの 2 つだけ**になった。

| 潰した候補 | どう潰したか |
|---|---|
| host 側 software の投げ方 | URB 64 KiB〜1 MiB × depth 2〜4 で不動([E084](../e084_p4_transfer_tuning/README.ja.md)) |
| usbip 経路 | native 21.2 対 usbip 21.97 MB/s([E081](../e081_p4_winusb_bind/README.ja.md)) |
| capture との同居 | 止めても同じ([E088](../e088_p4_usb_ceiling_idle/README.ja.md)) |
| 転送長 | 8 KiB が最良、16 KiB は逆に遅い([E085](../e085_p4_transfer_size_model/README.ja.md) / [E088](../e088_p4_usb_ceiling_idle/README.ja.md)) |
| device 側の in-flight | device 側で不要と結論([CR-7](../../references/espusbdevice-change-requests.ja.md)) |

**PC の xHCI が 1 microframe に何回 IN token を出すかは host 側 software では動かせない**(URB の中に何百もの transaction が入る)。**訊く側を自分で作る**しかない。

**[HR-2](../../references/espusbhost-change-requests.ja.md) と [HR-1](../../references/espusbhost-change-requests.ja.md) が実装された**ので、それができるようになった。

## 仮説

**24 MB/s 付近で飽和する(= device 側の限界)。**

[E088](../e088_p4_usb_ceiling_idle/README.ja.md)で **capture を止めても天井が動かなかった**ので、device 側に構造的な上限があると見ている。**ただし 36.4 MB/s(P4 が host として *送信* する側)が出ている以上、外れる目もある。**

## 反証条件

1. **24 MB/s を明確に超える**(30 MB/s 以上)→ **PC の controller がこれまでの全測定を縛っていた**
2. **5.6 MB/s から動かない** → host 側にさらに別の律速がある
3. 転送長を変えても depth を変えても動かない

## 方法

**board 2 を host、board 1 を device にして HS 同士を直結する**([E072](../e072_p4_hs_device_to_host_native/README.ja.md) と同じ配線)。

- **device 側**([`device/`](device/)): [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) の `tests/peer/usb_vendor_read/peer_device` を P4 profile で。**`'S' + 長さ 4 byte(LE)` を受けると 0..255 の ramp を送る**ので、**host 側で欠落と順序を byte 単位で検証できる**。`build_opt.h` で **TX FIFO / 1 転送 = 8192**([E084](../e084_p4_transfer_tuning/README.ja.md) の最良)
- **host 側**([`host/`](host/)): 同ライブラリの `tests/manual/vendor_bulk_in_throughput`。**continuous を基準に、depth {1, 2, 4} × 転送長 {512, 2048, 8192, 16384, 32768} を各 1 MiB**
- ライブラリは **working tree**(`/home/mt/dev/EspUsbHost`、未リリース)

### 2 軸を分けて振る

[CR-7](../../references/espusbdevice-change-requests.ja.md) の教訓([E084](../e084_p4_transfer_tuning/README.ja.md):効いたのは in-flight 数ではなく転送長)に合わせ、**転送長と depth を独立に振れる**形になっている。

- `vendorReadQueueBegin(1, size)` = 転送長だけ(in-flight 1 本、ただし callback 内で再 submit)
- `vendorReadQueueBegin(depth, size)` = in-flight を増やす

### 記録する数値

host 側が `vendorReadStats()` で出すもの:

| 値 | 読み方 |
|---|---|
| MB/s | 素の帯域 |
| **`bytes / completed`(per_transfer)** | **device が 1 転送に実際に詰めた量** |
| **`starved`** | **完了時点で他に 1 本も飛んでいなかった回数。** depth 1 では全件になる |
| `shortTransfers` | short packet で終端した回数(= FIFO が枯れた徴候) |
| errors / resubmitFailures | 異常 |

**判定の型**(先方の整理に従う):

- **24 MB/s 付近で飽和 + `starved` ≈ 0 + per_transfer が要求どおり** → **host は訊き続けているのに device が出せていない = (A) device 側の限界**
- **per_transfer が要求に届かないのに `starved` が伸びる** → **host 側にまだ残りがある**

## 対象外

- interrupt IN / HID([HR-3](../../references/espusbhost-change-requests.ja.md) は HR-1 の結果を見てから)
- device 側の追加改修
- PC 経由の測定([E088](../e088_p4_usb_ceiling_idle/README.ja.md) が基準値)

## 必要な環境

**配線([E072](../e072_p4_hs_device_to_host_native/README.ja.md) と同一。あちらで実証済みの構成)**

| 役 | board | console(J3 / Type-C) | OTG HS(J4 / Type-A) |
|---|---|---|---|
| **device** | **`esp32-p4-30eda0e31478`**(board 1) | **PC へ**(現状のまま) | **`...f5` の J4 へ** ← **現在 PC に繋がっているので差し替える** |
| **host** | **`esp32-p4-30eda0e314f5`**(board 2) | **PC へ** | **`...78` の J4 へ** |

- **`...f5` は現在 board-identify に出ていない(外れている)。挿し直しが要る**
- **J4 同士を USB ケーブルで直結**する。WT9932P4-TINY の J4 は **Type-A**(ユーザーガイド: 「ESP32-P4 acts as a USB Host and can supply up to 500mA」)なので **A-A ケーブル**になる。**[E072](../e072_p4_hs_device_to_host_native/README.ja.md) がこの構成で動いている**
- **PC は両者の J3(console)にしか繋がらない**
- **代替ボードは使わない**: `esp32-p4-e8f60ae0aa24` は別個体(flash 32 MiB、E014〜E061 の台)で EspUsbDevice 側のテストが使用中、`esp32-p4-80f1b2d0b261` は素性不明(ttyUSB 経由)
- EspUsbDevice 2.3.0(Library Manager)/ EspUsbHost working tree

## ベンチ種別

peer(board 2 枚、要配線)

## 完了条件

**continuous 基準に対し、depth {1,2,4} × 転送長 5 種を測り、飽和点と `starved` / per_transfer を並べる。** 仮説(24 MB/s 付近)が当たるか外れるかを述べる。

## 比較の基準(取得済み)

| 経路 | 実測 |
|---|---:|
| **device → PC**(usbip、8 KiB 転送、capture なし) | **23.88 MB/s**([E088](../e088_p4_usb_ceiling_idle/README.ja.md)) |
| device → PC(native Windows) | 21.2 MB/s([E081](../e081_p4_winusb_bind/README.ja.md)) |
| **device → P4 host**(改修前、512 B × depth 1) | **5.6 MB/s**([E072](../e072_p4_hs_device_to_host_native/README.ja.md)) |
| (参考)P4 host → device(**送信**、async queue depth 2) | 36.4 MB/s(EspUsbHost `docs/usb-host-advanced.md`) |

## 状態

- **両側とも P4 でビルド確認済み**(device 377,694 B / host 545,078 B)
- **先方も実機未検証**(S3 peer ボードが外れているとのこと)。**この実験が HR-2 / HR-1 の初回検証になる**
- **止まっているのは配線だけ** — **`...f5` を挿し、その J4 と board 1 の J4 を繋ぐ**
- **S3 の 2 枚(`d0cf1358fd94` / `d0cf1359101c`)には触らない**。EspUsbHost 側の full test が使用中

## 影響

- [EspUsbHost への改修依頼](../../references/espusbhost-change-requests.ja.md) HR-1 / HR-2 の合否
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §0 の「天井の内訳」
- [HR-3](../../references/espusbhost-change-requests.ja.md) — 先方は「interrupt IN にも同じ改修が要る可能性が高いが、HR-1 の実測を見てから」としている

## 結果

[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) working tree、1 MiB / 条件。`mbps` は MiB/s なので **MB/s に直した列**を足した。

| mode | depth | 1 転送 | MB/s | per_transfer | short | **starved** |
|---|---:|---:|---:|---:|---:|---:|
| **continuous(改修前の形)** | 1 | 512 | **6.10** | — | 0 | 0 |
| queue | 1 | 512 | 6.59 | 512 | 1 | 2049 |
| queue | 1 | 2048 | 12.05 | 2044 | 1 | 513 |
| queue | 1 | 8192 | 14.02 | 4096 | 128 | 256 |
| queue | 1 | 16384 | 14.57 | 8192 | 86 | 128 |
| **queue(HR-2 のみ)** | **1** | **32768** | **14.77** | 16132 | 65 | 65 |
| queue | 2 | 2048 | 18.74 | 2044 | 1 | 0 |
| queue | 2 | 32768 | 21.99 | 12053 | 87 | 0 |
| queue | 4 | 8192 | 22.74 | 4877 | 87 | 0 |
| queue | 4 | 16384 | 24.19 | 8192 | 82 | 0 |
| **queue(HR-2 + HR-1)** | **4** | **32768** | **24.45** | 9620 | 109 | 0 |

**`bad=0`**(ramp の位相)**、`errors=0`** が全条件。

| | MB/s | 倍率 |
|---|---:|---:|
| continuous(= [E072](../e072_p4_hs_device_to_host_native/README.ja.md) の形) | 6.10 | 1.00 |
| **HR-2 だけ**(depth 1、32 KiB) | **14.77** | **2.42** |
| **HR-2 + HR-1**(depth 4、32 KiB) | **24.45** | **4.01** |
| (参考)同じ device を **PC** が読む([E088](../e088_p4_usb_ceiling_idle/README.ja.md)) | 23.88 | — |

## 事実

1. **P4 host が 24.45 MB/s に到達し、PC(23.88 MB/s)と同じ水準に並んだ。** **まったく別の host controller 2 つが同じ天井で止まる**ので、**約 24 MB/s は device 側の限界**である。→ **仮説どおり (A)。反証条件 1(24 を明確に超える)は成立しなかった。**
2. **HR-2(転送長)だけで 2.42 倍**(6.10 → 14.77 MB/s)。**HR-1(depth)を足して 4.01 倍**(24.45)。**両方要る。**
3. **順序の見立ては当たり、かつ片方だけでは足りなかった。** device 側([CR-7](../../references/espusbdevice-change-requests.ja.md))では転送長だけで済んだが、**host 側は depth も効く**。理由は非対称で、**device は「送る物がある限り詰める」が、host は「訊かないと来ない」**ため、折り返しの隙間が直接失われる。
4. **`starved` が判定に効いた。** depth 1 では**全転送が starved**(定義上そうなる)、depth 2 以上で **0**。**depth を上げると starved が消え、そのぶん伸びる**という関係がそのまま見えた。
5. **`per_transfer` は device 側の転送長で頭打ちになる。** 要求 16384 に対し **ちょうど 8192**、要求 32768 に対し 9620〜16132。**device の TX FIFO / 1 転送が 8192** なので、**device が 8 KiB 出すたびに FIFO が一瞬空になり ZLP が転送を終端する**([CR-5](../../references/espusbdevice-change-requests.ja.md) の機序)。**short が毎回立つのは異常ではなく、device 側の転送境界が見えているだけ**である。

### 最初の測定は device 側の sketch が律速していた(§7-6)

**1 回目の掃引では depth を上げるほど遅くなった**(depth 1/32768 で 14.25 MB/s、depth 2 以上で 8.2〜8.5 MB/s、per_transfer が 540 前後、short が全転送)。

**原因は host ではなく、参照用の device sketch** だった。[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) の `tests/peer/usb_vendor_read/peer_device` は

- **`CHUNK = 512` 固定**で `write()` に渡す — **`CFG_TUD_VENDOR_TX_BUFSIZE` を 8192 にしても 512 しか渡らない**
- **512 B ごとに `flush()`** — **1 packet で 1 転送**になる

**これはこちらが [E084](../e084_p4_transfer_tuning/README.ja.md) で踏んだのと同じ罠**である(「`write()` に渡す塊を定数で持つと FIFO を広げても効かない」)。**塊を `writeCapacity()` から取り、`flush()` を stream の最後だけに変えた**ところ、上の結果になった。

**教訓: 相手側の測定用 firmware も測定対象である。** device が 512 B ずつしか出さなければ、host 側をどう改修しても「depth を上げると悪化する」という誤った像が出る。

## 候補

- **P4 を host にして bulk IN を流すなら `vendorReadQueueBegin(4, 32768)`**(または depth 2 / 16 KiB 以上)
- **約 24 MB/s は device 側の天井。** host を替えても越えない
- **device 側の 1 転送長が host 側の per_transfer を決める。** 両側を揃えて考える

## 未決

- **device 側の 24 MB/s(microframe あたり 6 transaction)の正体** `—`。**host を替えても越えないことは確定した**ので、残るのは device 側の DWC2 / TinyUSB の構造
- **depth 8 以上** `—`。4 で頭打ちに見えるが未測定
- **device の 1 転送長を 16 KiB 以上にしたときの per_transfer** `—`。[E088](../e088_p4_usb_ceiling_idle/README.ja.md) では 16 KiB は遅かった
- **[HR-3](../../references/espusbhost-change-requests.ja.md)(1,024 B periodic IN)** `—`。先方は HR-1 の結果を見てからとしている。**HR-1 は効いた**ので、interrupt IN 側にも同じ改修を入れる根拠ができた

## 影響

- [EspUsbHost への改修依頼](../../references/espusbhost-change-requests.ja.md) — **HR-2 / HR-1 とも合格。両方要る**
- [E072](../e072_p4_hs_device_to_host_native/README.ja.md) の 5.6 MB/s — **4 倍になった**
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §0「天井の内訳」— **(A) で確定**
- [HR-3](../../references/espusbhost-change-requests.ja.md) — 着手の根拠ができた
