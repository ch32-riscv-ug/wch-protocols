# Windows が WinUSB を当てない — 調査記録

状態: **未解決**(2026-09-12。切り分けはかなり進んだが最後の 1 手が残っている)

[E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) で ESP32-P4 の vendor bulk endpoint を Windows 11 から driverless に使おうとして詰まった件の記録。**device 側は正しいと確定している**ので、Windows 側の話としてここに分ける。

## 症状

VID:PID `1209:0008` の vendor-specific interface 1 本の device を挿すと、

```
status  = Error
problem = 28 (CM_PROB_FAILED_INSTALL)
service = (なし)
compatible IDs に USB\MS_COMP_WINUSB が付かない
```

## 確定していること

### device 側は正しい

| 確認 | 結果 |
|---|---|
| `bcdUSB` | **0x0210**(BOS を読む条件を満たす) |
| BOS descriptor | **57 byte / 2 capability**。UsbTreeView の dump で確認 |
| MS OS 2.0 platform capability | UUID `D8DD60DF-4589-4CC7-9CD2-659D9E648A9F`、`dwWindowsVersion=0x06030000`、`wTotalLength=0x00B2`、`bMS_VendorCode=0x02`、`bAltEnumCode=0x00` — **すべて正しい** |
| **MS OS 2.0 descriptor set 本体** | Linux から `bmRequestType=0xC0, bRequest=0x02, wValue=0, wIndex=7` を投げると **178 byte 返る。`WINUSB` の compatible ID を含む** |
| 同上(stack を変えて) | Arduino-ESP32 core 内蔵 stack と `EspUsbDevice` 2.2.0 で**同じ 178 byte** |

### Windows 側に一般的な阻害要因は無い

| 確認 | 結果 |
|---|---|
| `HKLM\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions` | **キー自体が存在しない**(制限ポリシー無し) |
| `DriverSearching\SearchOrderConfig` | `1`(Windows Update も検索する) |
| driver store の WinUSB | **ある**。`winusb_generic_device.inf`(libwdi、`USB\MS_COMP_WINUSB` 用)、他に SEGGER / Raspberry Pi の WinUSB INF も |

**`MS_COMP_WINUSB` さえ付けば当たる driver は揃っている。** 付かないことが唯一の原因である。

### 失敗は device instance に焼き付く

```
DEVPKEY_Device_InstallState = 2   (FailedInstall)
DEVPKEY_Device_ConfigFlags  = 64  = CONFIGFLAG_FAILEDINSTALL
```

これが立つと **以後その instance では driver 判定をやり直さない**。descriptor を直しても、`bcdDevice` を変えても効かない理由がこれ。

**instance ID を変える条件**(実測):

| 変えたもの | device instance |
|---|---|
| `bcdDevice`(0x0100 → 0x0200 → 0x0201) | **変わらない**(`USB\VID_1209&PID_0008\0` のまま) |
| **serial string** | **新しい instance になる**(`...\E069-A`) |

→ [E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md) の仮説「`bcdDevice` は identity に効かない / serial は効く」が片側ずつ裏付いた。

### 効かなかった対処

| 試したこと | 結果 |
|---|---|
| `bDeviceClass` を `0xEF/0x02/0x01` → `0x00` | 変わらず |
| `bcdDevice` を変える | 変わらず(instance が同じなので当然) |
| **serial を変えて新 instance にする** | **変わらず**(新 instance でも即 Code 28) |
| **`pnputil /remove-device` で devnode を消して再列挙** | **変わらず**(新しい `FirstInstallDate` が付いた上で再び Code 28) |
| **composite 化**(vendor + CDC、vendor が interface 0) | **変わらず**。`usbccgp` すら載らない |
| USB stack を `EspUsbDevice` に替える | 変わらず |

**`setupapi.dev.log` には、これらの再列挙に対する install の節が 1 つも書かれない。** 削除(`Delete Device`)は記録されるので、ログ自体は生きている。つまり **Windows は driver 検索を走らせずに Code 28 を付けている**。

## 残っている仮説

### H1: MS OS 2.0 descriptor set の入れ子構造(本命)

両 stack が返す 178 byte は次の形で、TinyUSB の WebUSB サンプル由来と思われる。

```
Set header (0x0A)
  Configuration subset header (0x08)
    Function subset header (0x08, bFirstInterface = 0)
      Compatible ID feature (0x14, "WINUSB")
      Registry property feature (0x84, DeviceInterfaceGUIDs)
```

**Configuration / Function subset は composite device の function に compatible ID を結び付けるための入れ子**である。単一 interface の非 composite device では **Set header の直下に compatible ID を置く**のが素直で、現在の構造では Windows が device に結び付けられていない可能性がある。

**composite 化しても直らなかった**ことは H1 に不利に見えるが、その試験では `usbccgp` 自体が載らなかったので、**composite として扱われる前段で落ちている**可能性が残る(= H1 の検証になっていない)。

### H2: Windows が vendor request を投げていない

BOS を読んでも `bRequest=0x02, wIndex=7` を投げていないなら、descriptor の中身は無関係になる。

### H1 と H2 を分ける手段

1. **device 側で全 control request を観測する。** `EspUsbDevice` に観測用 hook を足してもらう([改修依頼 CR-2](espusbdevice-change-requests.ja.md))。**これが一番確実で安全**
2. **USBPcap で bus を取る。** この PC に導入済み。ただし**管理者権限が要る**うえ、HS のフルレートでは取りこぼす(列挙は低レートなので実用にはなる)
3. **MS OS 2.0 descriptor set を subset 無しで出す。** [改修依頼 CR-1](espusbdevice-change-requests.ja.md)

## 当面どうするか

| 手 | 権限 | 状態 |
|---|---|---|
| **usbip で WSL へ引き込み、libusb で叩く** | bind に管理者 1 回 | **採用中**。[E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) / [E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md) はこれで測れている。ただし usbip の overhead が乗る(値は下限になる) |
| Zadig で WinUSB を手動割当 | 管理者 | 未実施。**確実だが「driverless」ではなくなる**ので、製品として配る形の検証にはならない |
| CDC に載せる | 不要 | driverless だが**帯域が出ない**([E066](../experiments/e066_p4_usb_hs_tx_context/README.ja.md) 8.08 対 [E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) 9.73 MB/s)し、**転送途中の packet 欠落**がある([E068](../experiments/e068_p4_hs_cdc_tail_loss/README.ja.md)、30 転送中 4 件) |

## usbipd を使ううえでの注意(実測)

- `usbipd bind` は **VID:PID と device instance に紐づく**。**PID や serial を変えるたびに管理者権限の bind が要る**
- したがって **usbip で測る実験は USB identity を固定する**
- `attach` は bind 済みなら管理者不要

## 影響する doc

- [probe-feasibility-gates](probe-feasibility-gates.ja.md) Gate 2 — 「driverless で vendor bulk」が Windows で成立するかは**まだ言えない**
- [harness-channels](harness-channels.ja.md) §6c — 「帯域が要るときは Vendor 側へ逃がす」は**帯域としては正しい**が、**Windows の driver 当ての問題が別に残る**
- [E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md) — instance ID の決まり方について実測が付いた
