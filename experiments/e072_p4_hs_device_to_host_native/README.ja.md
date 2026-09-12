# E072 ESP32-P4 同士を直結した device → host のbulk IN帯域

状態: **完了 — 5.6 MB/s。ただしこれはhost側(EspUsbHostの継続IN)の上限で、deviceの天井ではない**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)(usbip経由で10.74 MB/s)

## 問い

**PCを経路から外し、ESP32-P4を2枚HS port同士で直結して device → host の bulk IN を測ると何MB/sか。usbip経由の10.74 MB/sと比べてどうか。**

## なぜこの問いか

[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)〜[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)の測定はすべて **WSL + usbipd + Windows** を経由しており、**usbipのoverheadがどれだけ乗っているか分からなかった**。同じP4が**host役では36.4 MB/s**([EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) `docs/usb-host-advanced.md`)出るので、**PCを外せばdeviceの本当の天井が見える**はずだった。

## 仮説

**usbipを外せば10.74 MB/sより速くなる。** 経路が短くなるだけなので。

## 反証条件

1. 直結の方が遅い。**host側(EspUsbHost)が律速している**ことになる
2. 列挙しない / HSにならない
3. data化けが出る

## 方法

- **board 1**(`esp32-p4-30eda0e31478`)= device。[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)と同じfirmware(EspUsbDevice 2.2.0、vendor 1 interface、endpoint 512 B、**TX FIFO 8 KiB**、送出taskはcore 0)
- **board 2**(`esp32-p4-30eda0e314f5`)= host。`EspUsbHost` 2.8.0 で `vendorOpen()`(既定の継続読み)し、`onVendorData()` でbyte数と時刻を数える
- **2枚のOTG HS portをUSBケーブルで直結**。PCは両者のUSB-Serial-JTAG(console)にしか繋がっていない
- 4 MiBを5回。deviceのconsoleへ`C`を送って開始し、hostが300 ms無音になったらburstとして報告する

## 結果

```
CONNECT addr=1 vid=1209 pid=0008 speed=2      <- USB_SPEED_HIGH
VENDOROPEN ok
```

| run | bytes | **chunks** | **max_chunk** | span_us | **MB/s** | device側 stalls |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 4,194,304 | 8,192 | 512 | 744,647 | **5.633** | 88,914 |
| 1 | 4,194,304 | 8,192 | 512 | 748,356 | **5.605** | 88,033 |
| 2 | 4,194,304 | 8,192 | 512 | 755,108 | **5.555** | 89,719 |
| 3 | 4,194,304 | 8,192 | 512 | 746,655 | **5.617** | 89,701 |
| 4 | 4,194,304 | 8,192 | 512 | 744,748 | **5.632** | 88,694 |

**5回とも4 MiB全部が届き、欠落は無い。** device側の`written`も毎回4,194,304。

## 事実

1. **直結の方が遅い。5.6 MB/s で、usbip経由の10.74 MB/sの約半分。反証条件1が成立した。**
2. **原因はhost側の読み方である。** `chunks=8192`、`max_chunk=512` — つまり**hostは512 Bずつ8,192回に分けて受けている**。4 MiB ÷ 512 B = 8,192 と一致する。
3. **`EspUsbHost`の継続IN(`ESP_USB_HOST_VENDOR_READ_CONTINUOUS`)は、endpointの`wMaxPacketSize`ぶんの転送を1つずつ投げる。** `src/EspUsbHost.cpp` の `device->usbVendorInPacketSize = foundIn ? inEndpoint.maxPacketSize : 0;` がそれで、**転送サイズもqueue深さも設定できない**。OUT側には `vendorWriteQueueBegin(depth, bufferBytes, ...)` があるのに、**IN側には対応するものが無い**。
4. **device側の`stalls`は88,914**(usbip + PC hostのときは28,844)。**deviceはhostを待っている**。つまりdeviceはまだ余裕がある。
5. HSで繋がり(`speed=2`)、data欠落は無い。

## 天井の現在地

| 経路 | hostの読み単位 | 実測 |
|---|---|---:|
| **P4 device → P4 host**(EspUsbHost 継続IN) | **512 B × depth 1** | **5.6 MB/s** |
| P4 device → Windows PC(usbip + libusb) | 1 MiB URB × depth 1 | **10.74 MB/s** |
| P4 host → device(EspUsbHost async queue) | 8 KB × **depth 2** | **36.4 MB/s**(ライブラリ側の実測) |
| HS bulkの理論上限 | — | 53 MB/s |

**ここまでの測定はどれも「hostの読み方」か「deviceのFIFO」で頭打ちになっており、deviceの本当の天井にはまだ届いていない。** 分かっているのは **device側は少なくとも10.74 MB/s出せて、そのとき`write()`のspinはまだ28,844回ある**ということだけである。

## 候補

- **PCを外しても速くなるとは限らない。** 経路の長さより**hostの読み単位**の方が効く
- **device側の天井を測るには、hostが「大きい転送を複数in-flightで」読む必要がある。** いま手元にその組み合わせは無い

## 未決

- **`EspUsbHost`のIN側にasync queueが入ったらいくつ出るか** `—`。**これが本命**([EspUsbHostへの改修依頼](../../references/espusbhost-change-requests.ja.md) HR-1)
- **deviceの本当の天井** `—`。上が解けるまで測れない
- usbipのoverheadの大きさ `—`。**直結が遅かったので、逆に「usbipは思ったほど悪くない」ことが分かった**(1 MiB URBのPC hostの方が512 BのP4 hostより速い)
- host側を`vendorReadSync()`(on-demand)にすると転送サイズを指定できるか `—`

## 影響

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段 — **download帯域の最良は依然10.74 MB/s(usbip経由)**
- [EspUsbHostへの改修依頼](../../references/espusbhost-change-requests.ja.md) HR-1
- [E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)の未決「usbipのoverheadはどれだけか」に部分的な答え
