# EspUsbHost への改修依頼

状態: **依頼済み**(2026-09-13 に送付。対象 [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) 2.8.0)

> **送付時に併せて伝えたこと**: ① 順序は **HR-2 を先、HR-1 を後**(device 側で CR-7 を外した教訓)、② **検証に要る HS 同士の直結は次に実機を触れるときまで待ち**で、現状は board 1 の HS が PC 側、③ **比較の基準は取得済み**([E088](../experiments/e088_p4_usb_ceiling_idle/README.ja.md): 8 KiB 転送・capture なしで 23.88 MB/s)、④ **書き込み前に HS device を detach する**(attach 中の chip reset は console まで巻き込む)。

> **device 側([EspUsbDevice](espusbdevice-change-requests.ja.md))は CR-1〜CR-9 が全件対応され、2.3.0 として公開された。** その過程で **host 側にしか答えられない問いが 1 つ残った**ので、こちらを出す。

## なぜ出すか — 消去法で host 側しか残っていない

**device 役の vendor bulk は 8 KiB 転送で約 24 MB/s** で頭打ちになる。これは **microframe あたり 6.02 transaction**(HS が許すのは 13、**同じ P4 が host 役で送信すると 8.89**)。

**原因の候補を片端から潰した。**

| 潰した候補 | どう潰したか |
|---|---|
| host 側 software の投げ方 | **URB を 64 KiB〜1 MiB、depth 2〜4 のどれにしても 23.5〜23.8 MB/s で動かない**([E084](../experiments/e084_p4_transfer_tuning/README.ja.md)) |
| usbip 経路 | **native(Windows 直)21.2 対 usbip 21.97 MB/s**。差が無い([E081](../experiments/e081_p4_winusb_bind/README.ja.md)) |
| capture との同居 | **capture を止めても同じ**(idle 23.88 対 同時 23.36 MB/s)([E088](../experiments/e088_p4_usb_ceiling_idle/README.ja.md)) |
| 1 転送あたりの死に時間 | 転送長を伸ばすと減るが、**8 KiB で最良、16 KiB はむしろ遅い**([E085](../experiments/e085_p4_transfer_size_model/README.ja.md) / [E088](../experiments/e088_p4_usb_ceiling_idle/README.ja.md)) |
| device 側の in-flight 転送数 | device 側で検討され、**不要と結論**([CR-7](espusbdevice-change-requests.ja.md)) |

**残るのは 2 つだけ。**

- **(A) device 側の供給限界**(DWC2 の device 側が 1 microframe に 6 packet ぶんしか出せない)
- **(B) PC の host controller が bulk IN に振る token の数**

**(A) と (B) は「訊く側」を自分で作らないと分けられない。** PC の xHCI が 1 microframe に何回 IN token を出すかは、**host 側 software では動かせない**(URB の中に何百もの transaction が入るため)。**P4 を host にして読む**のが唯一の道である。

## 注文の順序について — 転送長を先に

**device 側で同じ形の問いを解いたときの教訓を共有したい。**

こちらは当初 **「endpoint ごとに転送を 1 本しか投げていないのが原因」** と踏んで [CR-7](espusbdevice-change-requests.ja.md)(in-flight 2 本)を出した。**外れだった。** 効いていたのは **1 転送が何 packet 運ぶか**(`CFG_TUD_VENDOR_TX_EPSIZE`)で、**4096 → 8192 で +9%**、in-flight を増やす話は不要になった。

**host 側も同じ非対称がありそう**なので、**[HR-2](#hr-2-参考継続-in-の-1-転送サイズだけでも指定させてほしい)(継続 IN の 1 転送サイズを指定させる)を先に**、**[HR-1](#hr-1-bulk-in-にも-async-queue-がほしいout-にはある)(queue 深さ)を後に**することを勧める。**HR-2 のほうが小さく、効きは大きいかもしれない。**

> 現状 [E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) が 5.6 MB/s で止まったのは、**host が 512 B を 1 転送ずつ、8,192 回**受けていたため。**device 側はもう 8 KiB 単位で出している**ので、**host 側が 8 KiB 単位で受けられるようになるだけで大きく動く可能性がある。**

### 一覧

| | 内容 | 優先度 | 規模 | 直ったことの確認 |
|---|---|---|---|---|
| **[HR-2](#hr-2-参考継続-in-の-1-転送サイズだけでも指定させてほしい)** | **継続 IN の 1 転送サイズを指定させる** | **高(先に)** | **小** | [E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) 再実行。**5.6 MB/s から動くか** |
| [HR-1](#hr-1-bulk-in-にも-async-queue-がほしいout-にはある) | bulk IN の async queue(depth) | 高(後で) | **大**(API 追加) | HR-2 の上に積んで**さらに伸びるか** |
| [HR-3](#hr-3-1024-b-の-interrupt-in-endpoint-を受けられるようにしたい) | 1,024 B の periodic IN | 中 | 中(FIFO 配分) | [E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) の 1,024 B 行が埋まる(8.2 MB/s 見込み)。**device 側の [CR-8](espusbdevice-change-requests.ja.md) は対応済み** |

### 何が分かれば成功か

**P4 を host にして P4 device から bulk IN を読み、どこで頭打ちになるかを見る。**

| 結果 | 意味 |
|---|---|
| **24 MB/s 付近で飽和** | **(A) device 側の限界**。PC の xHCI は無罪で、device 役の天井が確定する |
| **24 を明確に超える**(30 MB/s 以上) | **(B) PC の host controller が律速だった**。これまでの全測定が PC 側の上限を見ていたことになる |
| 5.6 MB/s から動かない | host 側にさらに別の律速がある |

**どちらに転んでも、2 年ぶんくらいの「device 役は遅い」という思い込みに決着が付く。**

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

### 動機が戻ってきた(2026-09-13 追記その 2)

**[E084](../experiments/e084_p4_transfer_tuning/README.ja.md) の分析で、HR-1 にしか答えられない問いが立った。**

転送長 2 点から内訳を出すと、**線上の漸近 rate は 26.3 MB/s、1 転送あたりの死に時間は 30.8 us**。つまり

- **死に時間を完全に消しても 26 MB/s** で、**host 役の 36.4 MB/s には届かない**([CR-7](espusbdevice-change-requests.ja.md) を入れても説明できない)
- **26.3 MB/s は microframe あたり 6.4 transaction**(HS が許すのは 13、host 役は 8.9)。**device 役はバスの半分しか使えていない**

残る候補は **(A) device 側の供給限界** と **(B) PC の host controller が bulk IN に振る token 数**の 2 つ。**host 側 software は既に無関係と分かっている**(URB を 64 KiB〜1 MiB、depth 2〜4 のどれにしても動かない。usbip と native でも同じ)。

**(A) と (B) を分けるには「訊く側」をこちらで作るしかない** — **P4 を host にして bulk IN を async queue で回す**、まさに HR-1 である。[E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) が 5.6 MB/s で止まったのは host 側が 512 B × depth 1 でしか読めなかったためで、**あの構成では切り分けにならない**。

**「P4 を host として使うときに要る」から「device 役の天井が device 側にあるのかを言うために要る」へ、優先度が戻った。**

### 測定の動機は消えた(2026-09-13 追記)

HR-1 を立てた当初の動機は **「device 側の天井を測りたい」**だったが、**PC 側で測れてしまった**。`libusb` の async API で URB depth を振った結果は **1 = 18.64 / 2 = 22.68 / 4 = 22.69 / 8 = 22.87 MB/s**([device 側の回答](espusbdevice-change-requests.ja.md))。**depth 2 で飽和するので、約 23 MB/s は device 側の天井**である。

したがって HR-1 は **「測定のために要る」から「P4 を host として使うときに要る」**へ性格が変わった。**実需が立つまで依頼しない。**

> **この判断は上の「追記その 2」で覆った。** depth を振って分かったのは「host 側 *software* は律速ではない」ことであり、**「device 側が天井」までは言えていなかった**。バス上の token の出方は software では動かせない。

なお **P4 host 相手の 5.6 MB/s**([E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md))は、device 側が 1 転送 512 byte だった頃の値である。**device 側が 1 転送 4 KiB を送るようになったいま、同じ測定をやり直す価値がある** — host 側の 512 B × depth 1 が本当に律速なのかは、**再測定するまで分からない**。

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
