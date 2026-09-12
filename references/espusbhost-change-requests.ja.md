# EspUsbHost への改修依頼

状態: **依頼**(2026-09-12 時点。対象 [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) 2.8.0)

[E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) で ESP32-P4 を 2 枚直結し、device → host の bulk IN を測る過程で見つかったもの。**すぐの対応を前提にしない**。

計測環境: ESP32-P4 rev 1.3 × 2 枚(`esp32-p4-30eda0e31478` = device / `...f5` = host)、OTG HS port 同士を直結、Arduino-ESP32 3.3.11、`EspUsbHost` 2.8.0、device 側は `EspUsbDevice` 2.2.0。

---

## HR-1 bulk IN にも async queue がほしい(OUT にはある)

**優先度: 高**

### 症状

`vendorOpen()` の既定(`ESP_USB_HOST_VENDOR_READ_CONTINUOUS`)で device からの bulk IN stream を受けると、**HS でも 5.6 MB/s で頭打ちになる**。

`onVendorData()` が受け取る chunk を数えると、4 MiB の転送に対して

```
chunks = 8192   max_chunk = 512
```

つまり **endpoint の `wMaxPacketSize`(512 B)ぶんを 1 転送ずつ、8,192 回**受けている。

### 原因と思われる箇所

`src/EspUsbHost.cpp`

```cpp
device->usbVendorInPacketSize = foundIn ? inEndpoint.maxPacketSize : 0;
```

継続 IN の転送サイズが **endpoint の最大パケット長に固定**されており、`vendorOpen()` にも `EspUsbHostConfig` にも**転送サイズ・queue 深さを指定する口が無い**。

OUT 側には既に

```cpp
bool vendorWriteQueueBegin(size_t depth, size_t bufferBytes, ...);
```

があり、ライブラリ自身の計測が **depth 2 で劇的に変わる**と記録している。

> | HS | 13 transactions × 512 B per microframe ≈ 53 MB/s | **36.4 MB/s**(ESP32-P4, async queue depth 2, 8 KB transfers) |
>
> — `docs/usb-host-advanced.md`

full-speed 側も「**depth 2 あれば転送サイズに関係なく上限(1.098 MB/s = FS 上限の 90%)に張り付く**」「同期の `vendorWrite()` は 512 byte で 0.88 MB/s まで落ちる」と書かれている。**IN 側は、その改善が入る前の OUT 側と同じ状態に見える。**

### お願いしたいこと

**IN 側にも OUT と同じ形の async queue**。例えば

```cpp
bool vendorReadQueueBegin(size_t depth, size_t bufferBytes, uint8_t address = ...);
```

のように、**転送サイズと in-flight 数を指定できる**もの。`onVendorData()` はそのまま使えると嬉しい。

### なぜ困るか

**device 側の天井が測れない。** [E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) で `EspUsbDevice` の送信 FIFO を 8 KiB にすると、**PC(usbip + libusb、1 MiB URB)相手には 10.74 MB/s** 出る。ところが **P4 host 相手だと 5.6 MB/s** に落ちる。device 側の `write()` の spin は PC 相手で 28,844 回、P4 host 相手で **88,914 回** — **device は host を待っている**。

つまり現状、

| 経路 | host の読み単位 | 実測 |
|---|---|---:|
| P4 device → **P4 host**(継続 IN) | **512 B × depth 1** | **5.6 MB/s** |
| P4 device → PC(usbip + libusb) | 1 MiB URB × depth 1 | 10.74 MB/s |
| P4 host → device(**async queue**) | 8 KB × **depth 2** | **36.4 MB/s** |

となっていて、**「P4 同士で HS の実力を測る」ことがまだできない**。HR-1 が入れば、PC を一切介さずに device 側の天井を出せる。

### こちらでの代替

`vendorReadSync()`(on-demand モード)で大きい buffer を指定すれば 1 転送は大きくできるかもしれないが、**同期なので in-flight は 1 のまま**で、stream 用途には向かない。未試行。

---

## HR-2 (参考)継続 IN の 1 転送サイズだけでも指定させてほしい

**優先度: 中**(HR-1 が重いときの簡易版)

queue 深さまで手を入れるのが重いなら、**`vendorOpen()` に「1 転送あたりの byte 数」だけでも足してほしい**。512 B → 8 KiB にするだけでも、OUT 側の FS の実測(512 B で 0.88 MB/s、大きい転送で 1.098 MB/s)から見て効果が期待できる。

---

## 参考になった点(記録として)

- **`docs/usb-host-advanced.md` の帯域表が、device 側を調べるうえで一番効いた。** 「同じ P4 が host 役なら 36.4 MB/s」という 1 行があったおかげで、**device 側の 9〜10 MB/s は hardware の限界ではない**と即断でき、[E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) / [E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) の設計がそこから決まった
- **「depth 2 で張り付く」という書き方**が、device 側の [CR-7](espusbdevice-change-requests.ja.md) を立てる根拠になった
- `vendorOpen()` → `onVendorData()` は、P4 を 2 枚繋いで 10 分で streaming 測定が立ち上がるくらい素直だった

## 参照

- [E072 P4 同士を直結した device → host の bulk IN 帯域](../experiments/e072_p4_hs_device_to_host_native/README.ja.md)
- [E071 device 側 vendor bulk の天井 — 送信 FIFO の深さ](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md)
- [EspUsbDevice への改修依頼](espusbdevice-change-requests.ja.md)(device 側。CR-7 が対になる)
