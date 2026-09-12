# EspUsbHost への改修依頼

状態: **依頼**(2026-09-12 更新。対象 [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) 2.8.0)

[E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) / [E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) で ESP32-P4 を 2 枚直結し、device → host の bulk IN / interrupt IN を測る過程で見つかったもの。**すぐの対応を前提にしない**。

**着手順の提案は[別紙](usb-library-change-plan.ja.md)。** 結論だけ先に書くと、**[EspUsbDevice 側](espusbdevice-change-requests.ja.md)を先にするのを勧める** — 理由は「device 側の改修は PC 1 台を host にして今のベンチのまま確認でき、host 側の改修は board 2 枚を直結する必要があって、**その間 PC からどちらの端も覗けなくなる**」ため。

### 一覧

| | 内容 | 優先度 | 規模 | 直ったことの確認 |
|---|---|---|---|---|
| [HR-1](#hr-1-bulk-in-にも-async-queue-がほしいout-にはある) | bulk IN の async queue | **高** | **大**(API 追加) | [E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) 再実行。5.6 MB/s を超えるか |
| [HR-3](#hr-3-1024-b-の-interrupt-in-endpoint-を受けられるようにしたい) | 1,024 B の periodic IN | 中 | 中(FIFO 配分) | [E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) の 1,024 B 行が埋まる(8.2 MB/s 見込み) |
| [HR-2](#hr-2-参考継続-in-の-1-転送サイズだけでも指定させてほしい) | (簡易版)継続 IN の転送サイズ | 中 | 小 | 同上、512 B → 8 KiB で伸びるか |

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

### 急ぐ必要はなくなった(2026-09-12 追記)

HR-1 を立てた当初の動機は **「device 側の天井を測りたい」**だったが、**それは PC 側でも測れる**ことが分かった。`libusb` の async API で **URB を複数 in-flight** にすれば、**board 2 枚を直結しなくても「device の天井か host の投げ方か」を切り分けられる**([別紙](usb-library-change-plan.ja.md))。

したがって HR-1 は **「測定のために要る」から「P4 を host として使うときに要る」**へ性格が変わった。**優先度は高いままだが、device 側の改修より後で構わない。**

### 直ったことの確認

[E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) を再実行して **5.6 MB/s を明確に超える**こと。device 側の `stalls` が **88,914 回(PC 相手の 28,844 回に対して 3 倍)**から下がることも併せて見る。

### こちらでの代替

`vendorReadSync()`(on-demand モード)で大きい buffer を指定すれば 1 転送は大きくできるかもしれないが、**同期なので in-flight は 1 のまま**で、stream 用途には向かない。未試行。

---

## HR-2 (参考)継続 IN の 1 転送サイズだけでも指定させてほしい

**優先度: 中**(HR-1 が重いときの簡易版)

queue 深さまで手を入れるのが重いなら、**`vendorOpen()` に「1 転送あたりの byte 数」だけでも足してほしい**。512 B → 8 KiB にするだけでも、OUT 側の FS の実測(512 B で 0.88 MB/s、大きい転送で 1.098 MB/s)から見て効果が期待できる。

---

## HR-3 1,024 B の interrupt IN endpoint を受けられるようにしたい

**優先度: 中**

### 症状

device 側の HID interrupt IN を **1,024 B** にすると、`vendorOpen` 相当の HID 経路で **stream が流れない**。device は 1 report 送って止まり、host 側の `onHIDVendorInput()` に何も届かない([E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md))。512 B までは問題なく **4.14 MB/s** 出る。

### 心当たり

README.ja.md に OUT 側の同じ話が書かれている。

> 512バイトを超えるinterrupt OUTエンドポイントを持つデバイス(…)はclaimに失敗して `ESP_ERR_NOT_SUPPORTED` となり、host driver が `HCD DWC: EP MPS (1024) exceeds supported limit (512)` を出力します。FIFO を再分割して領域を確保してください。

`ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT` が OUT 側の答えとして用意されている。**IN 側にも同じ配分の問題があるのではないか。**

### お願いしたいこと

- IN 側にも `ESP_USB_HOST_FIFO_LARGE_PERIODIC_*` 相当の配分が要るなら、その旨をドキュメントに一行
- 可能なら 1,024 B の periodic IN を受けられる配分オプション

**1,024 B が通れば HID は 8.2 MB/s**(= 1,024 × 8,000)になり、driver レスのまま vendor bulk に迫る。

### 直ったことの確認

[E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) の 1,024 B 行が埋まること。**device 側は [CR-8](espusbdevice-change-requests.ja.md) が要る**ので、**この項目だけは device 側と対で入れないと確かめられない**。512 B までなら device 側だけで足りる。

---

## 参考になった点(記録として)

- **`docs/usb-host-advanced.md` の帯域表が、device 側を調べるうえで一番効いた。** 「同じ P4 が host 役なら 36.4 MB/s」という 1 行があったおかげで、**device 側の 9〜10 MB/s は hardware の限界ではない**と即断でき、[E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) / [E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) の設計がそこから決まった
- **「depth 2 で張り付く」という書き方**が、device 側の [CR-7](espusbdevice-change-requests.ja.md) を立てる根拠になった
- `vendorOpen()` → `onVendorData()` は、P4 を 2 枚繋いで 10 分で streaming 測定が立ち上がるくらい素直だった

## 参照

- [E072 P4 同士を直結した device → host の bulk IN 帯域](../experiments/e072_p4_hs_device_to_host_native/README.ja.md)
- [E071 device 側 vendor bulk の天井 — 送信 FIFO の深さ](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md)
- [EspUsbDevice への改修依頼](espusbdevice-change-requests.ja.md)(device 側。CR-7 が対になる)
- [着手順の提案](usb-library-change-plan.ja.md) — device 側と host 側、どちらから手を入れるか
