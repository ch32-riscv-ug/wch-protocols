# E081 Windows が WinUSB を当てるか — 改修後の対照実験

状態: **完了 — flat なら当たる、subsets なら当たらない。汚れた台でも直った。native は 21.2 MB/s で usbip 経由と同じ**(2026-09-13)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)(Code 28 で詰まった)、[E070](../e070_p4_hs_vendor_stack_compare/README.ja.md)(**stack を替えても同じ**)、[E062](../e062_usb_same_identity_layout_change/README.ja.md)(device instance と driver 判定の貼り付き) / 調査記録: [Windows が WinUSB を当てない](../../references/windows-winusb-binding.ja.md)

## 問い

**MS OS 2.0 descriptor set を flat にすると、Windows 11 は vendor bulk device に WinUSB を当てるか。subset 構造のままなら当たらないままか。**

## なぜこの問いか

[調査記録](../../references/windows-winusb-binding.ja.md)は「**device 側は正しい**(178 byte を返す)のに Windows が driver を当てない(Code 28)」で止まり、**残る仮説は 2 つ**だった。

1. **MS OS 2.0 の subset 構造**が単一 interface の device に合っていない
2. Windows がそもそも vendor request を投げていない

[CR-1](../../references/espusbdevice-change-requests.ja.md) としてライブラリへ出したところ、**仮説 1 が正しく、既に修正された**との回答を得た(interface 1 本なら flat 162 byte、2 本以上なら subsets 178 byte を自動判定)。**先方は Windows 実機で対照実験まで済ませている。**

**それでもこちらで測る。** 理由は 3 つ。

- **[§3.1.2](../README.ja.md) の原則** — 他所の測定結果をこちらの記録の根拠にしない
- **こちらの Windows は状態が違う。** [E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) 以来、**失敗した driver 判定が device instance に貼り付いている**([E062](../e062_usb_same_identity_layout_change/README.ja.md)、`ConfigFlags=0x40`)。**その汚れた台で直るのかを確かめる価値がある**
- **usbip を外した native の帯域**がまだ一度も取れていない。WinUSB が当たれば**初めて測れる**

## 仮説

**flat なら当たる。subsets なら当たらない。**

## 反証条件

1. flat でも Code 28 のまま(構造以外の原因が残っている)
2. subsets でも当たる(構造は無関係だった)
3. **どちらも当たらない** — こちらの Windows 側の汚れ(`ConfigFlags`、driver store)が効いている

## 方法

**同じ firmware で `config.msOs20Layout` だけを変えた 2 本**を、**それぞれ別の serial** で焼く。

| build | `msOs20Layout` | serial | 期待 |
|---|---|---|---|
| **A** | `AUTO`(= 単一 interface なので **flat**) | **`E081-A`** | 当たる |
| **B** | `SUBSETS`(強制) | **`E081-B`** | 当たらない |

**serial を分けるのが要点である。** Windows は device instance を **VID + PID + serial** で識別し、**一度失敗した driver 判定はその instance に貼り付いて再評価されない**([E062](../e062_usb_same_identity_layout_change/README.ja.md))。**使い回すと descriptor を直しても過去の失敗が返ってくる。** 既存の `E069-A` は Code 28 で汚れているので使わない。

- **HS port は Windows 側に置いたまま**にする(usbipd で WSL へ attach しない)。attach すると Windows からは見えなくなる
- 確認は [E063](../e063_p4_usb_hs_enumerate/README.ja.md) の [`collect_windows.ps1`](../e063_p4_usb_hs_enumerate/collect_windows.ps1) を使う
- **書き込みの前に HS device を detach する**([E078](../e078_p4_continuous_stream/README.ja.md) で踏んだ。attach 中の reset は死んだ vhci entry を残し、serial port まで巻き込む)

### 記録する数値

- `status` / `problem`(`CM_PROB_*`)/ `service` / **compatible ID に `USB\MS_COMP_WINUSB` が出るか**
- device 側が返す MS OS 2.0 descriptor の **長さ**(flat 162 / subsets 178)
- 当たった場合: **usbip を外した native の帯域**(WinUSB + libusb、Windows 上の `uv`)

## 対象外

- WebUSB(browser からの接続)
- 複合 device(HID + vendor など)での判定
- driver store の掃除(`pnputil`)— **汚れたままで直るか**が見どころなので、あえて触らない
- Linux 側の挙動([E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md) で driver 不要と確認済み)

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、**OTG HS port を Windows 側に置く**
- [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) の working tree(release 前。commit を記録する)
- Windows 11 + `powershell.exe`、native 帯域を測るなら Windows 側の `uv`

## ベンチ種別

board(単体、外部配線なし)

## 完了条件

**A と B の両方を焼き、Windows での判定を並べる。** A が当たれば [調査記録](../../references/windows-winusb-binding.ja.md) を解決として閉じ、native の帯域を測る。

## 影響

- [Windows が WinUSB を当てない](../../references/windows-winusb-binding.ja.md) — **未解決のまま残っている**
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §3
- [EspUsbDevice への改修依頼](../../references/espusbdevice-change-requests.ja.md) CR-1 の確認

## 結果

ライブラリは working tree(`lib=2.3.0`)。**同じ board・同じ firmware で、`msOs20Layout` と serial だけが違う 2 本。**

| build | `msOs20Layout` | descriptor 長 | `subsets` | serial | **Windows の判定** |
|---|---|---:|---:|---|---|
| **A** | `AUTO`(→ flat) | **162** | 0 | `E081-A` | **`Status=OK`** / `Service=WinUSB` / **`USB\MS_COMP_WINUSB` あり** / `DeviceDesc=WinUSB Generic Device` |
| **B** | `SUBSETS` | 178 | 1 | `E081-B` | `Status=Error` / **`CM_PROB_FAILED_INSTALL`(Code 28)** / `Service=None` / **compatible ID なし** |

A の instance ID は `USB\VID_1209&PID_0008\E081-A`、compatible ID は

```
USB\MS_COMP_WINUSB
USB\COMPAT_VID_1209&Class_FF&SubClass_00&Prot_00
...
```

**device 側の `mounted` も一致する** — A は 1(Windows が configure した)、B は 0(driver が付かないので configure されない)。

### usbip を外した native の帯域

WinUSB が当たったので、**初めて usbip を経由しない測定ができた**。Windows 上の `uv` + pyusb(libusb backend)で 4 MiB を 1 MiB ずつ読む。

| | MB/s |
|---|---:|
| **native(Windows 直、host 実測)** | **21.21 / 20.97 / 21.20** |
| native(device 自身の時計) | 21.47 / 21.24 / 21.40(195〜197 ms) |
| 参考: usbip 経由([E078](../e078_p4_continuous_stream/README.ja.md) 飽和時) | 21.4〜22.4 |

**usbip の取り分は測定できる大きさではない。** [E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md) 以来「usbip 込みなので下限である」と断ってきたが、**その但し書きは実質不要だった**。

## 事実

1. **MS OS 2.0 を flat にすると Windows は WinUSB を当てる。** `Status=OK`、`Service=WinUSB`、`USB\MS_COMP_WINUSB` が compatible ID に出る。
2. **subsets のままだと当たらない。** `CM_PROB_FAILED_INSTALL`(Code 28)で、[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md) の症状と同一。**反証条件 2 は否定された。**
3. **原因は構造だけだった。** 同じ board・同じ firmware・同じ byte 列生成器で、**分岐は layout flag 1 つ**。[調査記録](../../references/windows-winusb-binding.ja.md)の仮説 1 が正しく、仮説 2(Windows が vendor request を投げていない)は不要になった。
4. **汚れた台でも直る。** この Windows には [E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)〜[E070](../e070_p4_hs_vendor_stack_compare/README.ja.md) の失敗した driver 判定が残っているが、**新しい serial を使えば関係ない**。**反証条件 3 も否定。**
5. **serial を変えるのは必須である。** Windows は instance を VID + PID + serial で識別し、**失敗した判定はその instance に貼り付く**([E062](../e062_usb_same_identity_layout_change/README.ja.md))。`E069-A` のまま試していたら、直った firmware でも Code 28 が返っていたはずである。
6. **native は 21.2 MB/s で、usbip 経由と差がない。** これまでの全測定の「usbip 込みなので下限」という但し書きは外せる。

### 方法の誤り(2 件。[§7-6](../README.ja.md) に従い残す)

**(a) host 側の計測が 28.58 MB/s を出した。** 最初の block の完了時点で時計を始めながら、**その block の byte 数を数に入れていた**。転送が始まるまでの待ち時間が分母から落ちるので速く出る。**先頭 block を数からも時間からも外す**よう直した。

さらに、直した直後は **0.46 MB/s** になった。**長さ 0 の packet が読みを即座に完了させる**ので、そこで時計が始まり、**trigger を送る前の 9 秒が分母に入っていた**。**空の block を開始点として扱わない**ようにして解決。**device 自身の時計(21.2〜21.5 MB/s)と一致するまで直した。**

**(b) Windows の `uv` に WSL 側の venv を壊された。** cwd が WSL のプロジェクト内のまま Windows の `uv run` を呼んだところ、**`experiments/.venv/pyvenv.cfg` が Windows の CPython を指すよう書き換えられ**、`Lib/` と `Scripts/` が足された。WSL 側の python が `Failed to import encodings module` で起動しなくなる。`rm -rf .venv && uv sync` で復旧。**Windows 側の `uv` は、必ず Windows 側の作業ディレクトリで実行すること。**

## 候補

- **単一 interface の vendor device は flat で出す**(ライブラリの `AUTO` が既にそうする)
- **WinUSB の検証は毎回新しい serial で行う。** 使い回すと過去の判定が返る
- **Windows 直結で 21.2 MB/s 出る。** driver 追加は要らない(WinUSB は Windows 標準)
- **usbip 経由の測定値をそのまま信じてよい**

## 未決

- **読み size を変えた native の掃引** `—`。1 MiB でしか測っていない
- **WebUSB(browser)からの接続** `—`
- **複合 device(HID + vendor など)での layout** `—`。interface が 2 本以上なら subsets になるが未確認
- **リリース版での再確認** `—`。本実験は working tree(`lib=2.3.0`)

## 影響

- [Windows が WinUSB を当てない](../../references/windows-winusb-binding.ja.md) — **解決。閉じる**
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §3 と、全測定の「usbip 込みなので下限」の但し書き
- [EspUsbDevice への改修依頼](../../references/espusbdevice-change-requests.ja.md) CR-1 — **こちらの台でも確認**
