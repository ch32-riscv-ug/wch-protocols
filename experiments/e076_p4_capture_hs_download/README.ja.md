# E076 capture を OTG HS の vendor bulk で降ろす通し

状態: **完了 — 4 MiB が平均0.48秒(8.80 MB/s)で降り、sample 精度は保たれる。console 経路の12倍。PSRAM 読み出しは律速ではない**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E074](../e074_p4_2ch_capture_to_sr/README.ja.md)(capture と `.sr`)、[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)(vendor bulk 10.74 MB/s)、[E067](../e067_p4_usb_vs_capture_core/README.ja.md)(capture と USB の同居) / まとめ: [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §7

## 問い

**capture した data を OTG HS の vendor bulk で降ろすと、4 MiB の download は何秒になり、sample は落ちずに `.sr` まで通るか。**

## なぜこの問いか

[E074](../e074_p4_2ch_capture_to_sr/README.ja.md)は capture から `.sr` までを通したが、**download は console(FS CDC、0.72 MB/s)だった** — HS port が2枚目の board へ配線されていたため。**4 MiB に5.8秒**かかる。

[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)は同じ board の vendor bulk で **10.74 MB/s** を測っている。**この2つを同じ firmware の中で繋ぐと 0.39 秒**になるはず、というのが[まとめ §7](../../references/p4-usb-hs-summary.ja.md)の筆頭項目である。**配線が戻ったので通す。**

繋ぐこと自体に見るべき点がある。E071 は PSRAM を読まずに固定 pattern を送っていた。**今回の送出元は PSRAM 上の 4 MiB** で、[E067](../e067_p4_usb_vs_capture_core/README.ja.md)が「capture と USB を同居させると USB 側が7〜16%落ちる」と測っている経路を、**送出だけ**が使う形になる。

## 仮説

**通る。4 MiB が 0.4 秒前後(9〜11 MB/s)で降り、sample 精度は E074 と同じ(周期 1600 が min = mean = max)。**

## 反証条件

1. download が 5 MB/s を割る(PSRAM 読み出しが vendor bulk の律速になる)
2. byte が落ちる、または順序が崩れる(`.sr` の周期が一致しない)
3. capture 直後の download で device 側が stall し続ける、あるいは転送が完了しない

## 方法

[E074](../e074_p4_2ch_capture_to_sr/README.ja.md)の firmware の `run_dump()` だけを差し替える。**capture 経路は一切変えない。**

- **制御は console(USB-Serial-JTAG)、data は OTG HS vendor bulk**。2経路に分けるので、command の往復が download 時間に混ざらない
- 送出 task は **core 0 に pin**([E066](../e066_p4_usb_hs_tx_context/README.ja.md): core だけが効き、優先度は効かない。`ARDUINO_RUNNING_CORE`=1 の反対側)
- USB stack は **EspUsbDevice 2.2.0**([E070](../e070_p4_hs_vendor_stack_compare/README.ja.md))、TX FIFO は既定の 512 B
- identity は **`1209:0008` / serial `E069-A`** に固定する。usbipd の bind は VID:PID と device instance に紐づくので、変えると管理者権限の bind をやり直すことになる
- host は WSL 側で libusb、**1 MiB ごとの read**([E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md): URB の大きさが 2.7 倍効く)
- 判定は E074 と同じ**立ち上がり edge の間隔**。`min = mean = max = rate ÷ 100 kHz` なら欠落なし
- 3回以上繰り返す([§7-3](../README.ja.md))

### 記録する数値

- download の所要時間と MB/s(host 実測 / device 実測の両方)
- device 側の `stalls`(write が 0 を返した回数)
- capture の `overflow` / `timeout`
- channel ごとの周期 min / mean / max と duty
- 比較対象として **E074 の console 経路の同条件**(4 MiB)

## 対象外

- 連続 streaming(capture しながら降ろす。釣り合い点の測定は別実験)
- TX FIFO を深くした状態での再測([CR-4](../../references/espusbdevice-change-requests.ja.md))
- Windows 側から直接読む([WinUSB が当たらない](../../references/windows-winusb-binding.ja.md))
- PulseView から IP 経由で取る([連携メモ](../../references/pulseview-integration.ja.md) §2)
- trigger、外部信号、3 channel 以上

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、**OTG HS port を PC 側へ配線**、console と HS の両方を usbipd で WSL へ
- `EspUsbDevice (2.2.0)`(Library Manager)
- host: `uv run` の pyusb / pyserial

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**4 MiB の download を3回以上測り、MB/s と sample 精度を記録する。** console 経路(E074)との比を出す。

## 影響

- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §5「残る律速は download だけ」の表 — **見積もりを実測に置き換える**
- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) — download 経路の実測値

## 結果

### capture → download → `.sr`(4 MiB、2 channel、160 Msps)

| 回 | download 秒 | host MB/s | device MB/s | device `stalls` | sample 精度 |
|---:|---:|---:|---:|---:|---|
| 1 | 0.516 | 8.14 | 7.96 | 47,439 | **OK** |
| 2 | 0.530 | 7.91 | 7.75 | 49,502 | **OK** |
| 3 | 0.452 | 9.28 | 9.11 | 36,668 | **OK** |
| 4 | 0.416 | **10.08** | 9.89 | 30,018 | **OK** |
| 5 | 0.453 | 9.27 | 9.27 | 35,117 | **OK** |
| 6 | 0.579 | 7.25 | 7.10 | 57,875 | **OK** |
| 7(`.sr` 出力) | 0.433 | 9.69 | — | — | **OK** |
| **代表** | **0.483(0.416〜0.579)** | **8.80(7.25〜10.08)** | | | **7/7** |

capture 側は全回 `overflow=0` / `timeout=0`、capture 所要は 105.0 ms(= 16,777,216 sample ÷ 160 MHz)。周期は両 channel とも **min = mean = max = 1600**、duty は D0 25.00% / D1 50.00%。

`sigrok-cli -i ... --show` が読み戻す:

```
Samplerate: 160000000
Channels: 2 (D0, D1)
Logic unitsize: 1
Logic sample count: 16777216
```

### console 経路との比([E074](../e074_p4_2ch_capture_to_sr/README.ja.md)、同じ board・同じ capture)

| download 経路 | 4 MiB | 比 |
|---|---:|---:|
| console(USB-Serial-JTAG、0.72 MB/s) | **5.8 秒** | 1.0 |
| **OTG HS vendor bulk** | **0.48 秒** | **12 倍** |

### 送出元は PSRAM か internal RAM か(同じ loop、交互に8回ずつ)

反証条件1を潰すための対照。`B <bytes> <source>` で送出元だけを差し替える。

| 送出元 | n | mean | median | min | max | `stalls` |
|---|---:|---:|---:|---:|---:|---|
| internal RAM(128 KiB を繰り返す) | 8 | 8.38 | 7.71 | 6.62 | 10.70 | 30,671〜70,318 |
| **PSRAM**(4 MiB の capture) | 8 | **9.04** | 8.95 | 7.43 | 10.91 | 25,413〜55,791 |

**分布は完全に重なり、PSRAM の方がわずかに速い。** → **反証条件1は否定された。**

## 事実

1. **capture から `.sr` までが OTG HS の vendor bulk で通る。** 4 MiB × 7回すべてで sample 精度(周期 1600 が min = mean = max)、byte 欠落 0、sigrok が読み戻す。→ **反証条件2は否定。**
2. **4 MiB の download は平均 0.48 秒(8.80 MB/s)。** [まとめ §5](../../references/p4-usb-hs-summary.ja.md) の見積もり 0.39 秒(10.74 MB/s)より遅いが、**console 経路の5.8秒に対して12倍**で、[E074](../e074_p4_2ch_capture_to_sr/README.ja.md)の残していた律速は解けた。
3. **PSRAM からの読み出しは律速ではない。** internal RAM を送出元にしても mean 8.38 対 9.04 MB/s で**PSRAM の方が速い**。**どちらも同じ 6.6〜10.9 MB/s のばらつきの中にいる。**
4. **ばらつきは run 単位で 1.65 倍ある**(6.62〜10.91 MB/s)。**`stalls` が rate と綺麗に逆相関する**(25,413回で10.91 MB/s、70,318回で6.62 MB/s)。device は「FIFO が空くのを待っている」時間の長短で決まっており、**待たされ方そのものが run ごとに変わっている。**
5. **capture は download と干渉しない。** capture と送出は時間的に分かれているので当然だが、`overflow=0` が全回で確認できた。
6. **device 側実測と host 側実測の差は2%以内**(9.27 対 9.27、7.96 対 8.14)。**usbip と libusb の取り分は小さい。**

### ばらつきの出どころ

[E070](../e070_p4_hs_vendor_stack_compare/README.ja.md)が同じ stack で **6.79〜10.02 MB/s** のばらつきを記録し、core 内蔵 stack では **8.68〜9.11** と狭かった。今回の 6.62〜10.91 は EspUsbDevice 側の値とよく一致する。**送出元(PSRAM / internal)でも、capture の有無でも動かない**ので、**stack か usbip 経路のどちらかに由来する**と見ている。**本実験では切り分けていない。**

## 候補

- **capture の download は OTG HS vendor bulk で行う。** 4 MiB が 0.48 秒。console 経路は制御用に残す
- **送出元を PSRAM に置くことに帯域上の不利はない。** capture buffer を internal RAM へ移す動機はない
- **1回の測定で帯域を語らない。** 同一条件で 1.65 倍ばらつく

## 未決

- **ばらつきの出どころ** `—`。stack 側か usbip 経路か。core 内蔵 stack で同じ A/B を回せば切り分く
- **見積もり 10.74 MB/s との差** `—`。[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)は TX FIFO 8 KiB での値で、本実験は既定の 512 B。**FIFO を深くした状態での再測は未実施**([CR-4](../../references/espusbdevice-change-requests.ja.md))
- **連続 streaming** `—`。capture しながら降ろす釣り合い点
- **native(usbip なし)での帯域** `—`。[WinUSB が当たらない](../../references/windows-winusb-binding.ja.md)ため測れていない

## 影響

- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §5「残る律速は download だけ」— **見積もり 0.39 秒を実測 0.48 秒に置き換える**。§7 の筆頭項目が消える
- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) — download 経路の実測値が入る
- [E070](../e070_p4_hs_vendor_stack_compare/README.ja.md) — **ばらつきの広さが再現した**(6.6〜10.9 対 6.79〜10.02)
