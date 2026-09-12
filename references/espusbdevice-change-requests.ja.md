# EspUsbDevice への改修依頼

状態: **依頼**(2026-09-12 時点。対象 [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) 2.2.0)

このファイルは、[E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) / [E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md) で ESP32-P4 の USB 2.0 HS を実測する過程で見つかった、`EspUsbDevice` 側の改修候補をまとめたもの。**すぐの対応を前提にしない**。優先度と、こちらで代替できるかを併記する。

計測環境: ESP32-P4 rev 1.3(`esp32-p4-30eda0e31478` / `...f5` の 2 枚)、Arduino-ESP32 3.3.11、`EspUsbDevice` 2.2.0(Library Manager 経由)、host は WSL + usbip + libusb。

---

## CR-1 MS OS 2.0 descriptor set の構造を単一 function device でも通る形にしたい

**優先度: 高**(これが解けないと Windows で driverless にならない)

### 症状

vendor bulk 1 interface だけの device を Windows 11 に挿すと、**`CM_PROB_FAILED_INSTALL`(Code 28)で driver が当たらない**。`USB\MS_COMP_WINUSB` が compatible ID に現れない。

### 確認済みの事実

- **device 側は正しく答えている。** Linux から `bmRequestType=0xC0, bRequest=0x02, wValue=0, wIndex=7` を投げると **178 byte**が返り、中に `WINUSB` の compatible ID がある(core 内蔵 stack / `EspUsbDevice` の**両方**で同じ 178 byte)
- **Windows は BOS を読んでいる。** UsbTreeView の dump に MS OS 2.0 platform capability が出る(UUID `D8DD60DF-4589-4CC7-9CD2-659D9E648A9F`、`dwWindowsVersion=0x06030000`、`wTotalLength=0x00B2`、`bMS_VendorCode=0x02`)
- **`bcdUSB=0x0210`**、BOS 57 byte / 2 capability
- **Windows 側に driver install を阻む要因は無い。** `DeviceInstall\Restrictions` ポリシー無し、`SearchOrderConfig=1`、**driver store に `winusb_generic_device.inf`(libwdi、`USB\MS_COMP_WINUSB` 用)が存在する**
- **`pnputil /remove-device` で devnode を消して再列挙させても同じ**(新しい `FirstInstallDate` が付いた上で再び Code 28)。`setupapi.dev.log` には install の節が**1 つも書かれない**

### 疑っている点

`EspUsbDevice` が返す MS OS 2.0 descriptor set は、TinyUSB の WebUSB サンプル由来と思われる次の入れ子になっている(core 内蔵 stack と同一)。

```
Set header (0x0A)
  Configuration subset header (0x08)
    Function subset header (0x08, bFirstInterface = 0)
      Compatible ID feature (0x14, "WINUSB")
      Registry property feature (0x84, DeviceInterfaceGUIDs)
```

MS OS 2.0 の **Configuration / Function subset は composite device の「function」に適用するための入れ子**であり、**単一 interface の非 composite device では compatible ID を Set header の直下に置く**のが素直な形になる。**現在の構造だと Windows が compatible ID を function に結び付けられず、device に適用されていない**、というのが今の仮説である。

### お願いしたいこと

**subset を挟むかどうかを選べるようにしたい。** 例えば次のいずれか。

- `EspUsbDeviceConfig` に `msOs20UseSubsets`(既定 true = 現状維持)のようなフラグを足す
- interface が 1 本だけのときは自動的に subset を省く
- MS OS 2.0 descriptor set 全体を sketch から差し替えられる hook を出す

### こちらでの代替

**無い。** core 内蔵 stack でも同じ構造で、どちらも sketch から変更できない。現状は **usbip で WSL へ引き込み、libusb で叩く**ことで回避している(Linux は driver 不要)。

---

## CR-2 control request を観測できる hook がほしい

**優先度: 高**(CR-1 の切り分けに直結)

### 何が困っているか

CR-1 について、**「Windows が MS OS 2.0 の vendor request を投げていないのか、投げているが答えを捨てているのか」**が切り分けられない。

`EspUsbDeviceVendor::onControlRequest()` はあるが、**ライブラリ自身が処理した request(BOS / MS OS 2.0 / 標準 descriptor)は callback に上がってこない**ため、まさに見たいものが見えない。

### お願いしたいこと

**ライブラリが処理したものも含めて、全 control request を観測できる hook。** 例えば `EspUsbDevice::onAnyControlRequest(...)` のような、**戻り値で挙動を変えない純粋な観測用 callback**(`stage`、`bmRequestType`、`bRequest`、`wValue`、`wIndex`、`wLength`、それとライブラリが返した byte 数が分かれば十分)。

### こちらでの代替

USBPcap で bus を取れば分かるが、**管理者権限が要る**うえ、HS のフルレートでは取りこぼす。device 側から見えるのが一番確実。

---

## CR-3 `EspUsbDeviceVendor::configurationDescriptor()` が per-speed の `endpointSize` を捨てている

**優先度: 中**(spec 違反。実害は FS で出る)

### 症状

HS の P4 で `EspUsbDeviceVendor UsbVendor(device, 512);` として使うと、**OTHER_SPEED_CONFIGURATION(= full speed 側)の bulk endpoint にも `wMaxPacketSize=512` と書かれる**。USB 2.0 の full speed では bulk の上限は **64 byte** なので、この値は不正である。

実測(Linux から `GET_DESCRIPTOR(OTHER_SPEED_CONFIGURATION)`):

```
09 07 20 00 01 01 00 c0 fa
09 04 00 00 02 ff 00 00 00
07 05 01 02 00 02 00      <- bulk OUT, wMaxPacketSize = 0x0200 = 512
07 05 81 02 00 02 00      <- bulk IN,  wMaxPacketSize = 0x0200 = 512
```

### 原因

`src/EspUsbDevice.cpp` の `EspUsbDeviceVendor::configurationDescriptor()` が、

```cpp
uint16_t EspUsbDeviceVendor::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber,
                                                     uint8_t endpointNumber, uint16_t endpointSize)
{
  (void)endpointSize;          // ← 渡された per-speed の値を捨てている
  ...
  static_cast<uint8_t>(endpointSize_ & 0xff), ...   // ← constructor の値を両方の速度に使う
```

基底の `configurationDescriptorForSpeed()` は `highSpeed ? 512 : 64` を渡してくれているので、**その値を使えば直る**。

### 補足

`EspUsbDevice` が DEVICE_QUALIFIER と OTHER_SPEED_CONFIGURATION に**答えること自体は core 内蔵 stack に対する明確な優位点**である(core 内蔵は両方 STALL する)。だからこそ中身も合っていてほしい。

### こちらでの代替

`endpointSize` を既定の 64 のままにすれば FS は正しくなるが、**HS の帯域が出なくなる**ので選べない。

---

## CR-4 vendor の TX/RX FIFO 深さを sketch から変えたい

**優先度: 中**(帯域の伸びしろがここにしか無い)

### 何が困っているか

P4 の HS で vendor bulk の実効帯域を測ると、**1 microframe あたり約 2.4 transaction** しか出ていない(HS は 13 まで許す)。device 側の `write()` が FIFO 空き待ちで 0 を返す回数(stall)は **4 MiB あたり 3〜6 万回**で、**device が host を待っているのではなく FIFO が浅くて回っていない**形に見える。

`src/internal/EspUsbTinyUsbConfig.h` は

```c
#define CFG_TUD_VENDOR_RX_BUFSIZE 512
#define CFG_TUD_VENDOR_TX_BUFSIZE 512
```

で、**bulk 1 packet 分**しかない。

### お願いしたいこと

**FIFO 深さを sketch 側から指定できるようにしたい。** `build_opt.h` の `-DCFG_TUD_VENDOR_TX_BUFSIZE=...` で上書きできる形でも、`EspUsbDeviceVendor` の constructor 引数でも構わない。

**これは `EspUsbDevice` の最大の強みが効く場所**でもある — core 内蔵 stack は precompiled libs の `sdkconfig` に焼かれていて**変えようが無い**。ここが可変なら、[E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md) で付いた帯域差(後述)は逆転しうる。

### 実測([E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md))

ライブラリをコピーして`CFG_TUD_VENDOR_TX_BUFSIZE`だけ差し替え、深さを振った。

| TX FIFO | median MB/s | `write()`が0を返した回数(median) |
|---:|---:|---:|
| **512 B**(現状) | 9.03 | 39,746 |
| **8 KiB** | **10.59(+17%)** | 28,844 |
| 16 KiB | 10.33 | 29,444 |
| 32 KiB | 10.39 | 29,672 |
| 64 KiB | **動作せず**(`usb_ready=1`だが`mounted=0`) | — |

**8 KiBで飽和する。** ばらつきも6.79–10.02 → 10.27–10.76と大きく縮むので、**既定を8 KiBにするだけで体感は変わる**。16 KiB以上は無意味、**64 KiBは壊れる**(P4のHS portのhardware FIFOは4 KB = 1,024 lineなので、その辺と衝突している可能性)。

**ただしこれでは足りない。** 同じP4が**host役では36.4 MB/s**([EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) `docs/usb-host-advanced.md`、async **queue depth 2**、8 KB転送)出るので、device役の10.7 MB/sは**その約30%**にとどまる。→ CR-7

### こちらでの代替

**無い。**(測定のためにライブラリのコピーを1行だけ書き換えた)

---

## CR-7 endpointごとに転送を2つ以上投げられるようにしたい

**優先度: 高**(CR-4 より効くはず)

### 根拠

`EspUsbHost` 側の実測が答えを持っている。

> | HS | 13 transactions × 512 B per microframe ≈ 53 MB/s | **36.4 MB/s**(ESP32-P4, **async queue depth 2**, 8 KB transfers) |

full-speed 側でも **「depth 2 あれば転送サイズに関係なく上限(1.098 MB/s = FS 上限の 90%)に張り付く」**と書かれている。**同時に投げる転送を 1 → 2 にすることが、host 側では決定的だった。**

device 側は TinyUSB の class driver が **endpoint ごとに 1 転送ずつしか投げない**構造で、完了 callback で次を詰める。[E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) で FIFO を深くしても `write()` の spin が 1 packet あたり約 3.5 回で下げ止まったのは、これで説明が付く。

### お願いしたいこと

vendor(できれば CDC も)の送信で、**転送を 2 つ以上 in-flight にできる形**。TinyUSB の class driver に手を入れる話になるので重いのは承知している。**まず「そもそも可能か」の見立てを聞きたい。**

### こちらでの代替

**無い。**

---

## CR-5 同じ送出ループで core 内蔵 stack より FIFO 待ちが多く、ばらつきが大きい

**優先度: 中**(原因が分かれば CR-4 と合わせて効く)

### 実測([E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md))

⚠ **下の表は壊れたビルドフラグで測ったもので、帯域差は訂正されている。** `build_opt.h` + `--clean` で測り直すと **median は 9.04 対 9.03 MB/s でほぼ同じ**、`stalls` の比も **1.9 倍ではなく約 1.18 倍**(33,743 対 39,746)だった。**残る差は「ばらつきの大きさ」**で、core 内蔵が 8.68–9.11、EspUsbDevice が 6.79–10.02(いずれも 25 回)。以下は当初の記録として残す。

条件を完全に揃え(vendor 1 本 / endpoint 512 B / 送出 task を core 0 に pin / 送出元は internal RAM 64 KiB / 4 MiB 転送 / host の read size 1 MiB / usbip 経由)、**15 転送ずつ背中合わせ**で測った。

| | core 内蔵 stack | **EspUsbDevice 2.2.0** |
|---|---:|---:|
| host 実効 median | 9.41 MB/s | **7.57 MB/s** |
| host 実効 min–max | 7.90 – 9.70(±9%) | **5.83 – 10.01(±27%)** |
| device 側 median | 9.28 MB/s | 7.36 MB/s |
| **`write()` が 0 を返した回数(median)** | 30,606 | **57,980** |
| pattern 不一致 | 0/15 | 0/15 |
| flash | 392,098 B | **374,242 B** |

**最大値だけ見ると `EspUsbDevice` の 10.01 MB/s が core 内蔵の 9.70 を上回る**ので、速く出せる瞬間はある。持続しないのと、ばらつきが大きいのが違い。

### 見立て

送出ループも FIFO の深さも同じなので、**差は「FIFO をどれだけ速く掃き出すか」**にある。TinyUSB task の構成、`flush` を投げる契機、`tud_vendor_n_write_available()` の更新タイミングあたりが候補。

### お願いしたいこと

原因の心当たりがあれば教えてほしい。こちらでも CR-2 の hook があれば追える。

---

## CR-8 HID の packet size が 64 B にハードで縛られている

**優先度: 高**(HS の HID の帯域が 8 分の 1 になっている)

### 症状

`EspUsbDeviceHidVendor` は HS でも **64 B の interrupt endpoint しか作れない**。HS の interrupt は `wMaxPacketSize` 最大 1,024 B、周期 125 us なので、**64 B に縛ると 0.512 MB/s が上限**になる。

縛っているのは 2 か所。

```cpp
// EspUsbDeviceHidVendor::begin()
return reportSize_ > 0 && reportSize_ <= 63;

// EspUsbDeviceHidVendor::configurationDescriptor()
if (mps > 64) { mps = 64; }
```

さらに `src/internal/EspUsbTinyUsbConfig.h` の

```c
#define CFG_TUD_HID_EP_BUFSIZE 64
```

も `#ifndef` ガードが無く、`build_opt.h` から上書きできない。

### 実測([E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md))

上の 2 か所を `CFG_TUD_HID_EP_BUFSIZE` 基準に緩め、その値を可変にして測った(P4 を 2 枚直結、host は `EspUsbHost` 2.8.0)。

| `wMaxPacketSize` | report/s | **MB/s** |
|---:|---:|---:|
| **64 B**(現状) | 8,046 | **0.517** |
| 128 B | 8,078 | **1.034** |
| **512 B** | 8,077 | **4.136** |
| 1,024 B | — | 動作せず(host 側の periodic FIFO 配分が疑わしい。[HR-3](espusbhost-change-requests.ja.md)) |

**`packet size × 8,000/s` にそのまま比例する。** 512 B にできれば **8 倍**で、vendor bulk(10.74 MB/s)の約 40% まで届く。

### なぜ効くか

**HID は driver を当てる必要が無い唯一の class** で、いま Windows で詰まっている [WinUSB の問題](windows-winusb-binding.ja.md)が丸ごと無関係になる。しかも interrupt endpoint は**帯域が予約される**ので、bulk のように他の traffic に押されない。**「HID は帯域不足」という一般的な理解は full speed 前提**で、HS では成立しない。

### お願いしたいこと

- `CFG_TUD_HID_EP_BUFSIZE` に `#ifndef` ガードを付けて `build_opt.h` から上げられるようにする
- `EspUsbDeviceHidVendor::begin()` と `configurationDescriptor()` の 64 固定を `CFG_TUD_HID_EP_BUFSIZE` 基準にする
- 可能なら **HS のとき既定を 512 B** にする(1,024 B は host 側の事情で通らないことがある)

### こちらでの代替

**無い。**(測定のためにライブラリのコピーを 2 行書き換えた)

---

## CR-6 (参考)arduino-cli は symlink した library dir の `.cpp` を拾わない

**優先度: 低**(ライブラリの不具合ではない。ドキュメント向け)

`sketch.yaml` の `libraries: - dir: ./EspUsbDevice` を **symlink** にすると、**header は見つかるのに `.cpp` がコンパイルされず**、`EspUsbDeviceVendor::write` などが undefined reference になる。実パスまたは **Library Manager 経由(`- EspUsbDevice (2.2.0)`)なら問題ない**。

同じ罠を踏む人がいそうなので、README か docs に一行あると親切。

---

## 良かった点(記録として)

- **DEVICE_QUALIFIER と OTHER_SPEED_CONFIGURATION に答える。** core 内蔵 stack は**両方 STALL する**(UsbTreeView が `ERROR_GEN_FAILURE` を出すのはこれ)。HS device としての正しさは明確に上
- **flash が約 18 KB 小さい**
- **data 完全性に問題なし。** 15 転送で pattern 不一致 0。CDC 経路で見つかった[転送途中の packet 欠落](../experiments/e068_p4_hs_cdc_tail_loss/README.ja.md)(30 転送中 4 件)は、vendor では両 stack とも再現しなかった
- **`tusb_config.h` を自前で持っている**こと自体が、CR-4 のような調整を可能にする唯一の道

## 参照

- [E069 vendor bulk の帯域と data 完全性](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md)
- [E070 core 内蔵 stack と EspUsbDevice の比較](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md)
- [E068 CDC の転送途中 packet 欠落](../experiments/e068_p4_hs_cdc_tail_loss/README.ja.md)
- [E063 OTG HS の列挙](../experiments/e063_p4_usb_hs_enumerate/README.ja.md)
