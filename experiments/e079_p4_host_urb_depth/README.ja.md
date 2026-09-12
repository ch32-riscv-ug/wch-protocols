# E079 PC 側の URB を複数 in-flight にすると帯域は伸びるか

状態: **計画 — 機材待ち**(2026-09-12。board 1 / board 2 の console(USB-Serial-JTAG)が usbip 越しに応答しなくなっており、**物理的な挿し直しが要る**)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)(URB の**大きさ**が 2.7 倍効く)、[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)(FIFO 8 KiB で 10.74 MB/s)、[E076](../e076_p4_capture_hs_download/README.ja.md)(実用経路で 8.80 MB/s、1.65 倍のばらつき) / 設計: [改修の着手順](../../references/usb-library-change-plan.ja.md)

## 問い

**host 側(PC)が bulk IN の URB を複数同時に投げると、device を一切変えずに帯域は伸びるか。**

## なぜこの問いか

**改修の着手順を決めるのに要る。**

[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)以降の host 側 script はすべて `libusb` の**同期 API** で、**URB を 1 本ずつしか投げていない**。1 本が完了してから次を投げるので、**その隙間は bus が空く**。

device 側の[CR-7](../../references/espusbdevice-change-requests.ja.md)(転送を 2 つ以上 in-flight に)は **TinyUSB の class driver に手を入れる重い改修**である。着手する前に、**同じ「in-flight を増やす」を host 側で試せば、天井がどちらにあるか分かる**。

[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) の実測が「**depth 2 で張り付く**」と言っているのは host 側の話なので、**PC 側でも同じことが起きる可能性が高い**。

## 仮説

**伸びる。10.7 MB/s(= [E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md) の FIFO 8 KiB での値)を超える。**

いまの 8.80 MB/s ÷ 512 B ÷ 8,000 = **1 microframe あたり約 2.1 transaction**(HS は 13 まで許す)。**URB の隙間で bus が空いているなら、in-flight を増やすだけで詰まる。**

## 反証条件

1. **depth を上げても変わらない** → 天井は device 側。CR-4 / CR-7 の優先度が上がる
2. **depth 2 で頭打ちになり、それ以上増やしても伸びない** → host 側は解決、残りは device 側
3. **ばらつき(6.6〜10.9 MB/s)が depth を上げても残る** → ばらつきの出どころは usbip か device 側で、URB の投げ方ではない

## 方法

`python-libusb1`(`usb1`)の async API で **URB を depth 本 in-flight** に保ち、同じ device の同じ endpoint を読む。**device 側の firmware は変えない。**

- 送出側: [E076](../e076_p4_capture_hs_download/README.ja.md) の `B <bytes> <source>`(capture を挟まない純粋な送出)を使う。E078 の firmware なら `S <bytes> <rate>`
- depth を振る: **1 / 2 / 4 / 8**
- URB 1 本の大きさ: **256 KiB**([E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)で 1 MiB まで効くと分かっているので、depth の効果と混ざらない大きさに固定する)
- 各条件 **5 回以上**。[E076](../e076_p4_capture_hs_download/README.ja.md)で同一条件が 1.65 倍ばらつくので、**回数を取らないと差が見えない**
- **同期 API(いまの読み方)も同じ条件で測り、直接比較する**

### 記録する数値

- depth ごとの MB/s(mean / median / min / max)、device 側の `stalls`
- **1 microframe あたりの transaction 数**(= MB/s ÷ 512 B ÷ 8,000)
- 同期 API との比

## 対象外

- device 側の改修([CR-7](../../references/espusbdevice-change-requests.ja.md))
- usbip を外した native 測定([Windows で WinUSB が当たらない](../../references/windows-winusb-binding.ja.md)ため不可)
- P4 同士の直結([HR-1](../../references/espusbhost-change-requests.ja.md))
- capture との同居([E078](../e078_p4_continuous_stream/README.ja.md))

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、**console が生きていること**(現在ここで止まっている)
- HS port を usbipd で WSL へ
- host: `uv run --with libusb1`

## ベンチ種別

board(単体)

## 完了条件

**depth 1 / 2 / 4 / 8 の帯域を各 5 回以上測り、同期 API と並べる。** 伸びる/伸びないを[着手順](../../references/usb-library-change-plan.ja.md)へ反映する。

## 影響

- [改修の着手順](../../references/usb-library-change-plan.ja.md) — **CR-7 と HR-1 の優先度がこの結果で決まる**
- [EspUsbDevice への改修依頼](../../references/espusbdevice-change-requests.ja.md) CR-7
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §1 — 「host の 1 URB の大きさ」に「本数」が加わる
