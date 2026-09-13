# E089 P4 を host にして bulk IN を読む — 天井は device 側か PC 側か

状態: **準備完了 — 配線待ち**(2026-09-13。両側とも P4 でビルド確認済み。**HS 同士の直結が要る**)

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

- **ESP32-P4 2 枚の OTG HS 同士を直結**(board 2 = host `...f5` / board 1 = device `...78`)。**現在 board 1 の HS は PC 側なので、ケーブルの差し替えが要る**
- 両方の console は usbipd で WSL へ
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
- **止まっているのは配線だけ**

## 影響

- [EspUsbHost への改修依頼](../../references/espusbhost-change-requests.ja.md) HR-1 / HR-2 の合否
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §0 の「天井の内訳」
- [HR-3](../../references/espusbhost-change-requests.ja.md) — 先方は「interrupt IN にも同じ改修が要る可能性が高いが、HR-1 の実測を見てから」としている
