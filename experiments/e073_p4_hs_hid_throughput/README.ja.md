# E073 ESP32-P4 USB HS HIDの限界throughput

状態: **完了 — 既定(64 B)で0.52 MB/s。endpointを512 Bにすると4.14 MB/s。「HID = 64 kB/s」はFSの値だった**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E072](../e072_p4_hs_device_to_host_native/README.ja.md)(2枚直結のベンチ)

## 問い

**USB 2.0 HSのinterrupt endpoint(HID)でdevice → hostへ流せる実効帯域は何MB/sか。endpointのpacket sizeでどう変わるか。**

## なぜこの問いか

[harness-channels](../../references/harness-channels.ja.md) §USBクラス8種の得失は **HIDを「interrupt 64 B/1 ms = 64 kB/s」**とし、capture用途には**「✗ 帯域不足」**と判定している。これは**full speedの値**である。

**HSではinterrupt endpointの周期は125 us(microframeごと)、`wMaxPacketSize`は最大1,024 B**で、さらにhigh-bandwidthなら1 microframeに3 transactionまで許される。**理論上は 1,024 × 3 × 8,000 = 約24.6 MB/s**まである。**HIDはdriverレスで全OSが素で扱える唯一のclass**なので、ここが本当に足りないのかは判定の分かれ目になる。

## 仮説

**64 kB/sよりはるかに速い。** 少なくとも 64 B × 8,000 = 512 kB/s は出るはずで、packet sizeを上げれば比例して伸びる。

## 反証条件

1. 64 kB/s級しか出ない。FSと同じ周期で動いている
2. packet sizeを上げても変わらない
3. packet sizeを上げると列挙・mountしない
4. data欠落が出る

## 方法

[E072](../e072_p4_hs_device_to_host_native/README.ja.md)と同じ**2枚直結**のベンチを使う(PCは経路に居ない)。

- **board 1**(`...78`)= device。`EspUsbDevice` 2.2.0 の `EspUsbDeviceHidVendor`。`sendInput()` を20,000回叩き、成功数と所要時間をconsoleへ出す
- **board 2**(`...f5`)= host。`EspUsbHost` 2.8.0 の `onHIDVendorInput()` でbyte数・report数・最大report長・時間を数える
- **OTG HS port同士を直結**
- `HID_REPORT_BYTES` と `CFG_TUD_HID_EP_BUFSIZE` を振る

### ライブラリ側の細工(再現手順)

`EspUsbDevice` 2.2.0 は**HIDのpacket sizeを2か所でハードに64 Bへ縛っている**。既定のままでは64 Bより上を測れないので、**コピーを作って2行だけ外した**。

```cpp
// src/EspUsbDevice.cpp — EspUsbDeviceHidVendor::begin()
return reportSize_ > 0 && reportSize_ <= 63;                        // ← 元
return reportSize_ > 0 && reportSize_ <= (CFG_TUD_HID_EP_BUFSIZE - 1);  // ← 測定用

// src/EspUsbDevice.cpp — EspUsbDeviceHidVendor::configurationDescriptor()
if (mps > 64) { mps = 64; }                                          // ← 元
if (mps > CFG_TUD_HID_EP_BUFSIZE) { mps = CFG_TUD_HID_EP_BUFSIZE; }  // ← 測定用

// src/internal/EspUsbTinyUsbConfig.h
#define CFG_TUD_HID_EP_BUFSIZE 64        // ← 元(`#ifndef`ガード無し)
```

**この3点をライブラリ本体で可変にしてほしい、というのが[CR-8](../../references/espusbdevice-change-requests.ja.md)。**

## 対象外

- host → device(interrupt OUT)方向
- high-bandwidth transaction(1 microframeに2〜3回)の利用
- boot protocol互換のHID(keyboard / mouse)。**vendor-defined HIDのみ**
- PC(Windows / Linux)をhostにした場合の値

## 結果

| `wMaxPacketSize` | `begin()` | report/s | 受信byte | span_us | **MB/s** |
|---:|:--:|---:|---:|---:|---:|
| **64 B**(ライブラリ既定) | ok | 8,046 | 6,656 | 12,873 | **0.517** |
| 128 B | ok | 8,078 | 13,440 | 12,998 | **1.034** |
| **512 B** | ok | 8,077 | 53,760 | 12,999 | **4.136** |
| 1,024 B | ok | — | — | — | **動作せず**(deviceは1 report送って停止、hostにburstが届かない) |

**report/s はどの条件でも約8,000** で、これは**HSのmicroframe周期(125 us)ちょうど1回**に当たる。

## 事実

1. **既定の64 B endpointで0.517 MB/s出る。** [harness-channels](../../references/harness-channels.ja.md)の「HID = 64 kB/s」は**full speedの値**で、**HSでは約8倍**になる。**反証条件1は否定。**
2. **帯域はpacket sizeにきれいに比例する。** 64 → 128 → 512 Bで0.517 → 1.034 → 4.136 MB/s。**`packet size × 8,000/s`がそのまま出ている**(64 B×8,000 = 0.512、512 B×8,000 = 4.096 MB/s)。**反証条件2も否定。**
3. **1 microframeあたり1 transactionしか出ていない。** high-bandwidth(最大3回)は使われていない。**使えれば理論上あと3倍**ある。
4. **1,024 Bは動かない。** `device.begin()`は通る(`usb_ready=1`)が、deviceは1 report送って止まり、hostにburstが届かない。**`EspUsbHost`側のperiodic FIFO配分が疑わしい** — ライブラリのREADMEが「512バイトを超えるinterrupt OUTエンドポイントはclaimに失敗し`HCD DWC: EP MPS (1024) exceeds supported limit (512)`が出る。FIFOを再分割して領域を確保せよ」と書いており(`ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT`)、IN側にも同じ事情がありそうである。
5. **`EspUsbDevice`はHIDのpacket sizeを2か所でハードに64 Bへ縛っている**(`begin()`の`reportSize_ <= 63`と`configurationDescriptor()`の`mps > 64`)。**外さないと64 Bより上は測れない。**
6. `sendInput()`は送信可能でないとき即座に`false`を返す。20,000回中約105回しか成功しないが、**成功したものは正確に1 microframeに1回のペースで出ている**ので、帯域の測定としては有効である。

## 他のclassとの比較(このベンチでの実測)

| class | 経路 | 実測 | 備考 |
|---|---|---:|---|
| **vendor bulk** | device → PC(usbip + libusb、1 MiB URB) | **10.74 MB/s** | [E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)。TX FIFO 8 KiB |
| vendor bulk | device → P4 host(継続IN 512 B) | 5.6 MB/s | [E072](../e072_p4_hs_device_to_host_native/README.ja.md) |
| **HID(512 B)** | device → P4 host | **4.14 MB/s** | 本実験。**driverレス** |
| HID(64 B、既定) | device → P4 host | 0.52 MB/s | 本実験 |
| CDC | device → PC(Windows native) | 8.08 MB/s | [E066](../e066_p4_usb_hs_tx_context/README.ja.md)。**転送途中のpacket欠落あり**([E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)) |

**HIDは512 Bにできれば vendor bulk の約40%まで届く。** しかも**driver当てが要らない**(Windowsで詰まっている[WinUSBの問題](../../references/windows-winusb-binding.ja.md)が無関係になる)うえ、**interrupt endpointは帯域が予約される**ので、bulkのように他のtrafficに押されない。

## 候補

- **HIDを捨てない。** 「HIDは帯域不足」という判定は**FS前提の値に基づいていた**。HSなら512 Bで4.14 MB/s出る
- **driverレスを優先するならHID(512 B)、生帯域を優先するならvendor bulk**
- 2 channel / 4 sample per byteのPARLIO captureなら、**HID 512 Bでも約16.5 Msps相当**のstreamingになる

## 未決

- **1,024 Bが動かない理由** `—`。hostのperiodic FIFO配分か、deviceか。`ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT`相当のIN側設定があるかを含めて[HR-3](../../references/espusbhost-change-requests.ja.md)
- **high-bandwidth transaction(1 microframeに2〜3回)を使えるか** `—`。使えれば理論上あと3倍
- **PCをhostにしたときの値** `—`。いまは2枚直結の配線なので未測定
- HID OUT方向 `—`
- **複数のHID interfaceを並べたら合算されるか** `—`

## 影響

- [harness-channels](../../references/harness-channels.ja.md) §USBクラス8種の得失 — **「HID = 64 kB/s、capture には帯域不足」はFS前提**。HSでは0.52〜4.14 MB/sで、**判定を書き直す必要がある**
- [EspUsbDeviceへの改修依頼](../../references/espusbdevice-change-requests.ja.md) CR-8(HIDのpacket size制限)
- [EspUsbHostへの改修依頼](../../references/espusbhost-change-requests.ja.md) HR-3(1,024 Bのperiodic IN)
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段 — driverレスなdownload経路の選択肢
