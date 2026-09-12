# EspUsbDevice への改修依頼

状態: **全件対応済み**(2026-09-13。CR-1〜CR-9 すべてライブラリ側で実装・実機確認まで完了との回答。対象は 2.2.0、回答の全文は先方の `docs/CHANGE_REQUESTS.ja.md`)

> **以下の「結果」は、こちらの台(`esp32-p4-30eda0e31478`)で先方が測った値**である。**こちらの harness での追試はまだしていない**([§3.1.2](../experiments/README.ja.md))。追試するのは [E078](../experiments/e078_p4_continuous_stream/README.ja.md)(capture と同時に降ろす)だけで、それ以外は重複しないので再実行しない。

このファイルは、[E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md)〜[E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md) で ESP32-P4 の USB 2.0 HS を実測する過程で見つかった、`EspUsbDevice` 側の改修候補をまとめたもの。**すぐの対応を前提にしない**。優先度と、こちらで代替できるかを併記する。

**着手順の提案は[別紙](usb-library-change-plan.ja.md)**([EspUsbHost 側](espusbhost-change-requests.ja.md)との兼ね合いを含む)。**各項目には「直ったことをどう確かめるか」を付けた** — こちらで再実行できる実験番号である。

### 結果の要約 — 天井は **10.7 → 約 23 MB/s** に上がった

**効いていたのは in-flight 数ではなく「1 転送が何 packet 運ぶか」だった**(`CFG_TUD_VENDOR_TX_EPSIZE`、既定が bulk 1 packet)。512 byte ごとに「完了割り込み → event queue → usbd task → 再 arm」の往復が入り、**線上時間 46 us に対し往復が約 52 us**。[E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) の「1 microframe あたり 2.4 transaction」の正体がこれである。

| FIFO / 1 転送 | MB/s(4 MiB、9 回 median) |
|---|---:|
| 512 / 512(**旧既定**) | 9.83 |
| 8192 / 512 | 10.76 |
| 8192 / 2048 | 18.64 |
| **4096 / 4096(新既定)** | **21.12** |
| 8192 / 8192 | 22.81(飽和) |
| 32768 / 8192 | 23.34 |

**host 側の URB depth は 2 で飽和する**(1 = 18.64 / 2 = 22.68 / 4 = 22.69 / 8 = 22.87)。**つまり約 23 MB/s は host ではなく device 側の天井**で、[CR-7](#cr-7-endpointごとに転送を2つ以上投げられるようにしたい)(device 側 in-flight 2 本)は**不要**と結論された。→ [E079](../experiments/e079_p4_host_urb_depth/README.ja.md) はこの結果で置き換わる。

### 一覧

| | 内容 | 結果 |
|---|---|---|
| CR-1 | MS OS 2.0 の subset 構造 | **対応**。interface 1 本なら flat 162 byte、2 本以上なら subsets 178 byte を自動判定(`config.msOs20Layout` で上書き可)。**Windows 実機で対照実験済み** — flat = `CM_PROB_NONE` + `USB\MS_COMP_WINUSB` + service WinUSB、subsets 強制 = `CM_PROB_FAILED_INSTALL`。**仮説どおり構造の問題**だった |
| CR-2 | control request の観測 hook | **対応**。`onAnyControlRequest()`。vendor request は SETUP で報告するので **STALL したものも見える**。標準要求の STALL だけは見えない(`usbd.c` を触らずに塞げない) |
| CR-3 | per-speed の `endpointSize` | **対応**。FS 64 / HS 512、OTHER_SPEED_CONFIGURATION も 64 |
| CR-4 | FIFO 深さ | **対応**。class buffer を全部 `#ifndef` 化。**64 KiB が壊れた理由も判明** — `tu_edpt_stream_init()` がサイズを `uint16_t` で受けるので 65536 は depth 0(`usb_ready=1` / `mounted=0` と一致)。32768 超はビルドエラーに |
| **CR-7** | 転送を 2 つ以上 in-flight に | **不要と判明**。上の表のとおり、効くのは転送長。device 側 in-flight を 2 本にしても残りは数 % |
| CR-5 | 帯域のばらつき | **機序が判明**。`tu_edpt_stream_write_zlp_if_needed()` が「FIFO 空 かつ 直前の転送長が mps の倍数」で ZLP を送る → **512 byte 単位だと送出が途切れるたびに毎回成立**し、host の bulk read は short packet で URB が完了して再投入の往復になる。旧既定 9 run で「短く返った URB」を数えると**遅い run と完全相関**(8.33 MB/s で 47 本、10.21 MB/s で 9 本)。**FIFO 4096 以上では全 run 0 本**。加えて core 内蔵は DWC2 **slave mode**(ISR で再充填)、このライブラリは **DMA mode**(task 往復)という差もあり、CR-7 の修正で往復が 1/8 になるので実質解消 |
| CR-8 | HID の 64 B 固定 | **対応**。上限 `CFG_TUD_HID_EP_BUFSIZE - 1`(P4 で 511)、P4 既定 512。**依頼書に無かった必須修正あり** — `VENDOR_REPORT_DESCRIPTOR` の Report Count が 63 で焼き込まれており、endpoint だけ 512 にしても host は 64 byte しか読まない。instance ごとに組み立てて reportSize に追従する形へ。実測 **4.03 MB/s / 7,866 report/s / 欠落 0**、Linux の hidraw が bind |
| CR-9 | FIFO 空き待ち API | **対応**。`writeAvailable()` / `writeCapacity()` / `waitWritable()`。**実装中に TinyUSB 側のバグを 1 件発見** — `tud_vendor_tx_cb` が FIFO を次の転送へ吸い出す**前**に呼ばれるので、give の時点で空きがなく dual core だと待機側が空振りする。**修正前 1.85 → 修正後 20.94 MB/s、stalls 0**(従来は 4 MiB あたり約 25,000 回の spin) |
| CR-6 | symlink と arduino-cli | **対応**。troubleshooting に追記(`build_opt.h` の置き場所と `--clean` も) |

### こちらが間違えていた点

- **CR-8 の「512 B までなら Linux の hidraw で確認できる」は誤り**だった。**report descriptor の Report Count が 63 固定**であることを見落としており、endpoint size だけ上げても host は 64 byte しか読まない。[E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) が通っていたのは host が `EspUsbHost` の raw transfer だったため
- **CR-7 の見立て(in-flight 2 本が要る)は外れ**だった。往復の回数は合っていたが、**減らす手段は転送長**だった
- **HID の 4.03 MB/s は host 側が URB を複数 in-flight にして初めて出る**(depth 1 で約 1,100 report/s、8 以上で 7,866)。**HID は driver レスだが、host 側の投げ方は要る**

### 当初の依頼(記録として残す)

| | 内容 | 優先度 | 規模 | 直ったことの確認 |
|---|---|---|---|---|
| [CR-4](#cr-4-vendor-の-txrx-fifo-深さを-sketch-から変えたい) | vendor の TX FIFO 深さを sketch から変えたい | **高** | **小**(`#ifndef` ガード) | [E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) 再実行。既定 512 B で 8.80、8 KiB で 10.59 MB/s |
| [CR-3](#cr-3-espusbdevicevendorconfigurationdescriptor-が-per-speed-の-endpointsize-を捨てている) | per-speed の `endpointSize` を捨てている | 中 | **小**(1 行) | FS 側 `wMaxPacketSize` が 64 になる |
| [CR-2](#cr-2-control-request-を観測できる-hook-がほしい) | control request の観測 hook | **高** | 小〜中 | Windows 挿入時に MS OS 2.0 vendor request が来るか見える |
| [CR-1](#cr-1-ms-os-20-descriptor-set-の構造を単一-function-device-でも通る形にしたい) | MS OS 2.0 の subset 構造 | **高** | 中 | Windows で WinUSB が当たる(Code 28 が消える) |
| [CR-8](#cr-8-hid-の-packet-size-が-64-b-にハードで縛られている) | HID の packet size 64 B 固定 | **高** | **小**(2 か所 + ガード) | [E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) 再実行。512 B で 4.14 MB/s |
| [CR-9](#cr-9-fifo-が空くのを待てる-api-がほしいいまは-spin-するしかない) | FIFO 空き待ちの API(spin しかない) | 中 | 中 | [E078](../experiments/e078_p4_continuous_stream/README.ja.md) で harvest と競合しなくなる |
| [CR-7](#cr-7-endpointごとに転送を2つ以上投げられるようにしたい) | 転送を 2 つ以上 in-flight に | **高** | **大**(TinyUSB 内部) | PC 側 async URB で先に見立てを取る([別紙](usb-library-change-plan.ja.md)) |
| [CR-5](#cr-5-同じ送出ループで-core-内蔵-stack よりばらつきが大きい) | 帯域のばらつきが 1.65 倍 | 中 | 不明(原因未特定) | 同一条件 25 回の min–max が縮む |
| [CR-6](#cr-6-参考arduino-cli-は-symlink-した-library-dir-の-cpp-を拾わない) | (参考)symlink と arduino-cli | 低 | ドキュメント | — |

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

### 直ったことの確認

Windows に挿して **device manager の Code 28 が消え、`USB\MS_COMP_WINUSB` が compatible ID に出る**こと。[E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) の host 側を **usbip 経由ではなく Windows ネイティブ**で走らせられるようになる(いまの 8.80〜10.74 MB/s は usbip 込みの値なので、**ネイティブの値も初めて取れる**)。

### こちらでの代替

**無い。** core 内蔵 stack でも同じ構造で、どちらも sketch から変更できない。現状は **usbip で WSL へ引き込み、libusb で叩く**ことで回避している(Linux は driver 不要)。**ただしこれは開発者向けの逃げ道で、配る先には使えない。**

---

## CR-2 control request を観測できる hook がほしい

**優先度: 高**(CR-1 の切り分けに直結)

### 何が困っているか

CR-1 について、**「Windows が MS OS 2.0 の vendor request を投げていないのか、投げているが答えを捨てているのか」**が切り分けられない。

`EspUsbDeviceVendor::onControlRequest()` はあるが、**ライブラリ自身が処理した request(BOS / MS OS 2.0 / 標準 descriptor)は callback に上がってこない**ため、まさに見たいものが見えない。

### お願いしたいこと

**ライブラリが処理したものも含めて、全 control request を観測できる hook。** 例えば `EspUsbDevice::onAnyControlRequest(...)` のような、**戻り値で挙動を変えない純粋な観測用 callback**(`stage`、`bmRequestType`、`bRequest`、`wValue`、`wIndex`、`wLength`、それとライブラリが返した byte 数が分かれば十分)。

### 直ったことの確認

Windows に挿したときの log に **`bmRequestType=0xC0, bRequest=<bMS_VendorCode>, wIndex=7`** が出るかどうか。**出れば CR-1 の descriptor 構造の問題、出なければ Windows がそもそも投げていない**と確定し、**どちらに手を入れるかが決まる**。

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

**優先度: 高**(**規模が小さいのに効きが確実**。既定値の変更だけでも 8.80 → 10.59 MB/s)

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

**その後の実測でこの差はさらに重くなった。** [E076](../experiments/e076_p4_capture_hs_download/README.ja.md)で**既定の512 Bのまま実用経路(PSRAM上の4 MiB capture を降ろす)**を測ると **mean 8.80 MB/s(6.6〜10.9、15回)**。8 KiBの10.59に対して**2割近く損している**うえ、**ばらつきが1.65倍**([CR-5](#cr-5-同じ送出ループで-core-内蔵-stack よりばらつきが大きい))ある。

**送出元がPSRAMかinternal RAMかは効かない**ことも確認した(同じloopで交互に8回ずつ、internal 8.38 対 PSRAM 9.04 MB/s で分布は完全に重なる)。**つまり残っているのはFIFOと転送構造だけである。**

### 直ったことの確認

[E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md)をライブラリのコピーではなく**素のライブラリ + `build_opt.h`**で再実行する。**8 KiBで10.5 MB/s前後、`write()`が0を返す回数が4 MiBあたり3万回を切れば直っている。**

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

### 追加の根拠([E076](../experiments/e076_p4_capture_hs_download/README.ja.md))

同じ条件を 15 回回すと **6.62〜10.91 MB/s** に散らばり、**`stalls` と帯域が逆相関する**。**送出元(PSRAM / internal RAM)でも capture の有無でも動かない。** device は**ほとんどの時間 FIFO が空くのを待っている**という像が、独立した経路で再現した。

### お願いしたいこと

vendor(できれば CDC も)の送信で、**転送を 2 つ以上 in-flight にできる形**。TinyUSB の class driver に手を入れる話になるので重いのは承知している。**まず「そもそも可能か」の見立てを聞きたい。**

### 着手前にこちらで詰められること

**host 側(PC)の URB を 1 本ずつしか投げていない**のが効いている可能性がまだ残っている。`libusb` の async API で **URB を 2〜8 本 in-flight** にして同じ device を読めば、**device 側を一切変えずに「device の天井か host の投げ方か」が分かる**。**この測定を先に済ませてから CR-7 の要否を決めたい**([別紙](usb-library-change-plan.ja.md))。

### こちらでの代替

**無い**(device 側は)。上の PC 側 async 測定は代替ではなく**切り分け**である。

---

## CR-5 同じ送出ループで core 内蔵 stack よりばらつきが大きい

**優先度: 中**(原因が分かれば CR-4 と合わせて効く)

### 実測

**帯域そのものは互角である。** `build_opt.h` + `--clean` で測り直した値([E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md)、各 25 回):

| | core 内蔵 stack | **EspUsbDevice 2.2.0** |
|---|---:|---:|
| median | 9.04 MB/s | **9.03 MB/s**(互角) |
| **min–max** | **8.68 – 9.11(±2%)** | **6.79 – 10.02(±19%)** |
| `write()` が 0 を返した回数 | 33,743 | 39,746(**1.18 倍**) |

**違いは速さではなく「ばらつき」である。**

**このばらつきは別経路でも再現した。** [E076](../experiments/e076_p4_capture_hs_download/README.ja.md)で送出元を変えて交互に 8 回ずつ測ると、**internal RAM でも PSRAM でも同じ 6.62〜10.91 MB/s の幅**に散らばった。

| 送出元 | n | mean | min | max | `stalls` |
|---|---:|---:|---:|---:|---|
| internal RAM | 8 | 8.38 | 6.62 | 10.70 | 30,671〜70,318 |
| PSRAM | 8 | 9.04 | 7.43 | 10.91 | 25,413〜55,791 |

**`stalls` と帯域はきれいに逆相関する**(25,413 回で 10.91 MB/s、70,318 回で 6.62 MB/s)。**送出元でも capture の有無でも動かない**ので、**残る候補は stack 側か usbip 経路のどちらか**である。**こちらではまだ切り分けていない。**

### 直ったことの確認

同一条件 25 回の **min–max の幅**が core 内蔵並み(±数%)に縮むこと。**median が上がる必要はない。**

---

<details><summary>当初の記録(壊れたビルドフラグで測った値。参考)</summary>

⚠ **下の表は `--build-property 'build.extra_flags=...'` で platform の変数を潰したビルドで測ったもので、帯域差は上のとおり訂正されている。**

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

</details>

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

### 直ったことの確認

[E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md)をライブラリのコピーなしで再実行して **512 B で 4.1 MB/s 前後**。**1,024 B は [HR-3](espusbhost-change-requests.ja.md) が入るまで通らない**が、512 B までなら **PC を host にしても確かめられる**(Linux の hidraw)ので、**この項目だけは host 側の改修を待たずに検証できる**。

### こちらでの代替

**無い。**(測定のためにライブラリのコピーを 2 行書き換えた)

---

## CR-9 FIFO が空くのを待てる API がほしい(いまは spin するしかない)

**優先度: 中**(streaming で効く。[CR-7](#cr-7-endpointごとに転送を2つ以上投げられるようにしたい) が入れば軽くなるが消えない)

### 何が困っているか

`EspUsbDeviceVendor::write()` は **FIFO に空きが無いと 0 を返す**。送り切りたい側にできることは

```cpp
const size_t written = HsVendor.write(data + sent, want);
if (written == 0) { ++stalls; HsVendor.flush(); taskYIELD(); continue; }
```

**空きができるまで回し続ける**ことだけで、4 MiB の転送でこの spin が **2.5 万〜7 万回**走る([E076](../experiments/e076_p4_capture_hs_download/README.ja.md))。

**貯めてから送る**用途では CPU が余っているので害は小さい。**問題は capture しながら送る**とき([E078](../experiments/e078_p4_continuous_stream/README.ja.md))で、**送出 task の spin が harvest task と CPU を取り合う**。[E067](../experiments/e067_p4_usb_vs_capture_core/README.ja.md) で「capture と同居させると USB 側が 7〜16% 落ちる」と出ているのは、これが一因と見ている。

### お願いしたいこと

次のどちらか(両方あると嬉しい)。

- **`bool waitWritable(size_t bytes, uint32_t timeoutMs)`** のような、**task を block できる**待ち方(内部で semaphore を待ち、`tud_vendor_tx_cb` で give する形)
- **`size_t writeAvailable()`**(= `tud_vendor_n_write_available()`)の公開。**いくら書けるか分かれば呼ぶ側で待ち方を決められる**

### 直ったことの確認

[E078](../experiments/e078_p4_continuous_stream/README.ja.md) で、**同じ sample rate に対して `stalls` が桁で減り、harvest 側の余裕(弾性 FIFO の最大占有)が下がる**こと。

### こちらでの代替

`taskYIELD()` で回す(現状)。**優先度を下げると他が動くが、送出が遅れて FIFO が空く時間が増える**ので解にならない。

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
- [E071 送信 FIFO の深さ](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) / [E073 HID の帯域](../experiments/e073_p4_hs_hid_throughput/README.ja.md) / [E076 capture を降ろす通し](../experiments/e076_p4_capture_hs_download/README.ja.md)
- [着手順の提案](usb-library-change-plan.ja.md) — EspUsbHost 側との兼ね合い
