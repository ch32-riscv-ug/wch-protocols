# EspUsbDevice への改修依頼

状態: **全件対応済み**(2026-09-13。CR-1〜CR-9 すべてライブラリ側で実装・実機確認まで完了との回答。対象は 2.2.0、回答の全文は先方の `docs/CHANGE_REQUESTS.ja.md`)

> **以下の「結果」は、こちらの台(`esp32-p4-30eda0e31478`)で先方が測った値**である([§3.1.2](../experiments/README.ja.md))。
>
> **こちらで追試したもの**:
>
> | | こちらでの確認 |
> |---|---|
> | **CR-1**(MS OS 2.0 の flat) | **[E081](../experiments/e081_p4_winusb_bind/README.ja.md) で対照実験。flat = `Status OK` / `Service WinUSB`、subsets = Code 28。** 汚れた台でも新しい serial なら当たる |
> | **CR-4 / CR-7**(FIFO と転送長) | **[E084](../experiments/e084_p4_transfer_tuning/README.ja.md) で capture 同居のまま 21.99 → 23.97 MB/s(n=9)。** 8192/8192 が最良で、32768 は劣る |
> | **CR-9**(`waitWritable()`) | **[E078](../experiments/e078_p4_continuous_stream/README.ja.md) で `stalls` 全条件 0、1 転送 1 block。** 効果は単体の MB/s ではなく **capture との同居**に出た |
> | CR-2 / CR-3 / CR-5 / CR-6 / CR-8 | **未追試**(先方の測定と重複するため)

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

### 初の実使用(2026-09-13、[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) 側の測定)

**こちらは `GET_DESCRIPTOR(OTHER_SPEED_CONFIGURATION)` を読んで 64 になっていることを確認しただけ**で、**実際に FS で列挙させて使ったことがなかった**。

先方が P4 の HS 物理ポートを `HCFG.FSLSSUPP` で **full-speed 専用に強制**して同じ device を読んだところ:

```
HCFG=0x00000204 FSLSSUPP=1
DEVICE address=1 speed=full vid=303a pid=4019
RESULT mode=fs_only speed=full mbps=1.204 bad=0
```

**FS 理論上限 1.216 MB/s の 99%**、同じケーブルの HS 24.45 MB/s の 1/20。**256 KiB が `bad=0` で通った。**

**device が FS 側 descriptor に 512 を書いていたら、FS バスが運べない packet size を宣言することになるので、ここは通らない。** → **CR-3 の修正が実際に効いている傍証**である。

> **ただし `wMaxPacketSize` の数値そのものはまだ読んでいない。** 先方が `in_mps` を print する版を用意したが、その実行中に host 役の board がバスから消えて止まった。**断定はその値を見てから。**

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

**ただしこれでは足りない。** 同じP4が**host役では38.2 MB/s**([EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) `docs/usb-host-advanced.md`、async **queue depth 2**、8 KB転送)出るので、device役の10.7 MB/sは**その約30%**にとどまる。→ CR-7

### こちらでの代替

**無い。**(測定のためにライブラリのコピーを1行だけ書き換えた)

---

## CR-7 endpointごとに転送を2つ以上投げられるようにしたい

**優先度: 高**(CR-4 より効くはず)

### 根拠

`EspUsbHost` 側の実測が答えを持っている。

> | HS | 13 transactions × 512 B per microframe ≈ 53 MB/s | **38.2 MB/s**(ESP32-P4, **async queue depth 2**, 8 KB transfers) |

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

### 機序は speed をまたいで成立する(2026-09-13 追記)

**ZLP が host 側の転送を終端する**という CR-5 の機序は、**host 側から見ると「`shortTransfers` がほぼ毎回立ち、`bytes / completed` が要求サイズではなく device の FIFO サイズに張り付く」**という形で出る([E089](../experiments/e089_p4_host_in_queue/README.ja.md))。

**P4 同士の high speed と、[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) 側が回した S3 同士の full speed で、同じ形が出た。** **別のチップ・別の速度・別のリグで再現する**ので、**特定の環境の癖ではなく TinyUSB device の一般的な挙動**である。

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


## 追加依頼 CR-10〜CR-13（2026-09-15）

状態: **EspUsbDevice 2.4.0（2026-09-15リリース）に全件入った**: `writeDirect()` / `onTxComplete()` / `onRxData()` / `directWriteSupported()` / `lastDirectError()`（CR-10〜12）、bulk IN TX FIFO 2 packetの自動有効（CR-13、両build既定）。有効化は`-DCFG_TUD_VENDOR_TXRX_BUFFERED=0`＋`--clean`。同梱TinyUSBは無改変。**直ったことの確認（[E116](../experiments/e116_p4_usb_in_ceiling_release/README.ja.md)、2.4.0をLibrary Managerからpin、2026-09-16）: `writeDirect()`＋`onTxComplete()`内armで27,136 byte transferが45.6〜45.8 MB/s（366 Mbps）×3、65,024で47.3、8,192で39.3、short 0・pattern_bad 0・arm_failures 0、`direct_supported=1`。独自patch版E110（49.3）より7%低いが、CR-10〜13の意図どおり動く。** **stream data path一式は[E117](../experiments/e117_p4_stream_release_api/README.ja.md)で同じpin版に移植して確認: four 60 / five 60の約4.7分soak欠損0、16 ch hold 40 / 50、8-bit 60〜100、2 ch 160 M、any / edge / 配線順不同のbyte一致、全runで`direct_supported=1 last_direct_error=None arm_failures=0`。** 先方（EspUsbDevice側session）の2.4.0での対照: `EspUsbBulkInBuffering` Auto（既定）46.42 / 46.32 MB/s（27,136 / 65,024）対 Single（1 packet）26.97 / 27.04＝**+72%**で、CR-13の自動割り当ては効いている側。`esp_cache_msync(C2M)`の所要は8,192で2.40 µs、27,136で3.25 µs、65,024で3.60 µs。−7.4%の原因はFIFOでもcacheでもなく未特定。先方が一度挙げた「GHWCFG3の896 wordsに対し割り当て合計2,048 wordsで過剰割り当て」は、列挙前（`dcd_edpt_open`前）にレジスタを読んでいた測定ミスとして撤回済み。**CR-13の割り当ての実体（2026-09-16、2.4.0 pin、mount後・ストリーム中に読んだ値。mount後とストリーム中で同一）**: GRXFSIZ **304 words**（`calc_device_grxfsiz(512,16)`＝13＋1＋2×(128＋1)＋2×16）、DIEPTXF0（EP0 IN）start 976 / depth **16**、DIEPTXF1（EP1 IN）start 720 / depth **256 words＝512 byte packet 2個分**（`fifo2=0x0002`と整合）。合計576 / 使える992 words（`dfifo_top`＝1,024−2×16）。`ghwcfg3.dfifo_depth`＝896は「DFIFO深さ−EP_LOC_CNT」で896＋128＝1,024＝TinyUSBの`otg_dfifo_depth`。前回の「DIEPTXF1は512 words＝4 packet」も割り当て前のゴミ値で、**実体は2 packet。E110 patch（`fifo_size *= 2`）と2.4.0の自動割り当ては同じ2 packet**なので、−7.4%の原因からFIFOは根拠つきで外れ、E110の「2と4に差なし」の行の再測も不要になった。それまでの経過: 依頼はEspUsbDevice側sessionへ2026-09-15に送付（[usb-library-feedback.ja.md](usb-library-feedback.ja.md) 送付記録）。**取り込まれてreleaseされるまで、これらのpatchを前提にした数値（E108〜E115）は参考値で、製品の目安には使わない**（持ち主の方針）。

[E108](../experiments/e108_p4_zero_copy_stream/README.ja.md)で、vendor bulk INのcopyを全部外すと同じusbipd/WSL直結でUSB-onlyが209→247 Mbps、送出側coreのtask負荷が8-bit 60 Msps streamで57〜66%→7%になった。使ったのは出荷版2.3.0への一時patch（[espusbdevice-e108.patch](../experiments/e108_p4_zero_copy_stream/espusbdevice-e108.patch)、E097 / E101 / E102の合成＋bufsize上限撤廃）で、公開APIにするなら次の3点になる。

| | 内容 | 直ったことの確認 |
|---|---|---|
| CR-10 | **zero-copy TX**: `CFG_TUD_VENDOR_TXRX_BUFFERED=0`のとき、呼び出し側のbuffer（cache line整列、長さ≤65,535）を`usbd_edpt_xfer()`へ直接渡すwrite。完了までbufferの所有権は呼び出し側にあることをAPIで明示する | E108の`EP` probeで64 MBが247 Mbps前後、`arm_failures=0` |
| CR-11 | **TX完了callback**: 完了byte数を渡すhook（usbd task context）。callback内から次のbufferを投入できること | E108のarm ring chainが1 transfer分の隙間なく続く（完了2,359回で欠損0） |
| CR-12 | **direct RX callback**: non-buffered時に受信bufferと長さを渡すhook。現在の`onRx(size)`はbuffered専用 | 16 byte commandがmailbox経由で受かる |

いずれもE104までの改修（CR-1〜9）と独立で、buffered既定の挙動は変えない。

| **CR-13** | **bulk IN endpointのDWC2 TX FIFOを2 packet分にする。** TinyUSBの`dfifo_alloc()`は既定で`packet_size/4` words（1 packet）しか割り当てず、`tud_configure()`の`bm_double_buffered`にそのendpoint bitを立てると2倍になる。P4 HSのDFIFO（1,024 words）はRX 304＋EP0 16＋EPInfo 32を除いて672 words空いており、bulk IN 1本なら5 packetまで入る | [E110](../experiments/e110_p4_usb_in_ceiling/README.ja.md): 同じhost・firmwareで29.7→**49.3 MB/s（理論の93%）**、Windows native 27.7→47.1 MB/s。4 packetは2と同値。buffered送信のままでも効くかは未測（E090は当時25.6 MB/sのcopy律速で差が出なかった） |

CR-13は他より効果が大きく、既存APIの設定だけで済む。zero-copy（CR-10）と合わせるとvendor bulk INは約49 MB/sになる。

### CR-10の設計論点（2026-09-15、EspUsbDevice側sessionとの検討で判明）

- **patchなしで同じ経路は作れる**: `usbd_edpt_claim()` / `usbd_edpt_xfer()`（`device/usbd_pvt.h`）はライブラリが既に同梱・使用しており（CCID、AppDriver）、完了のroutingはendpoint→class driverなので、ライブラリ内の`tud_vendor_tx_cb`から完了callbackを出し、`writeDirect(buf, len)`でclaim＋xferすれば`vendor_device.c`に触らずE108と同じ経路になる（同梱TinyUSBのbyte-for-byte不変条件を保てる）。
- **ただしbuffered既定のままだとZLPが出る**: v0.21.0 `vendord_xfer_cb`のIN分岐（`#if CFG_TUD_VENDOR_TXRX_BUFFERED`内）は`tud_vendor_tx_cb`のあと`tu_edpt_stream_write_zlp_if_needed()`を呼び、FIFOが空で完了長がmpsの非ゼロ倍数（27,136は512の倍数）なら、`stream_claim`が取れる限りZLPをarmする。完了callbackの中で次をarmすればclaimが先に通ってZLPは出ないが、pipelineが空（codecがUSBより遅い通常運用）だと毎stage ZLPが出て、host側では1 MiB URBがshort完了する（E106で踏んだusbipd errorの形）。non-buffered（`CFG_TUD_VENDOR_TXRX_BUFFERED=0`、E108〜E115の構成）では`#else`側で`tud_vendor_tx_cb`だけになりZLP経路は存在しない（全runで`short=0`）。
- したがってCR-10の形は、(1) direct writeはnon-bufferedを要求する（build時条件でAPIが弾く）、または (2) vendor interfaceをAppDriver側に持って自前のxfer_cbにする、のどちらか。契約: 64 byte整列・整列長・DMA可能なinternal RAM、書いたcoreで`esp_cache_msync(C2M)`、完了callback（usbd task context）まで所有権は呼び出し側、buffered `write()`との併用禁止、callback内では次のarmだけ行う。満たせない呼び出しは従来のepbuf copy経路へ。
- patchなし2.3.0では sketch が`tud_vendor_tx_cb`を受けられないため、「callback内でarm」の検証はライブラリにhook（またはwriteDirect＋完了callback）が入ってからでないとE108と同じ経路にならない。実装判断（maintainer）待ち。
- 先方の整理（同日）: 選択肢を **A: direct writeは`CFG_TUD_VENDOR_TXRX_BUFFERED=0`を要求** / **B: vendor interfaceをAppDriver側に持ちIN側のxfer_cbを自前にする** の2択として先方docs（`docs/CHANGE_REQUESTS.ja.md`）に記録。Aは`EspUsbDeviceVendor`の`available()` / `read()` / `flush()` / `writeAvailable()` / `waitWritable()`がbuild flagで消え、S2/S3で有効にすると64 byte clampに落ちる副作用が残る。Bはdescriptor生成が既にライブラリ側にあるので自前で要るのはopen / reset / control_xfer_cb / xfer_cbで、CCIDの前例がある。長期的にはBが筋。順序は「使い捨ての試作（Aの簡易形）をworking treeに入れて測り、数字が出てから公開APIの形を決める」。実装するかはmaintainer判断で、判断待ち。試作ができて測る段になるまで板（E115）はそのまま。
- **訂正（同日、先方の実測）: 「buffered＋direct writeだとZLPが出る」は実機で再現しなかった。** 先方がライブラリ側に試作（`EspUsbDeviceVendor::writeDirect()`＋`onTxComplete()`、TinyUSB無改変）を入れ、ESP32-S3 native USB（full speed、mps 64）をPC直結、libusbで数えた結果: non-buffered / buffered、arm位置が完了callback / 別task / 単発（再armなし）、writeDirect 4096 B / stockの`write()` 64 Bのいずれも **host側short 0、device側zerolen（`onTxComplete`が0 byteを報告）0**、corrupt 0、seq gap 0。stockのbuffered writeでもZLPが出ないので、writeDirect固有ではなくこの構成ではZLP分岐が発火していない（`4096 & 63 == 0`で条件は揃って見える。機構は未解明）。**したがってbuffered＋directを落とす根拠にZLPは使えず、buffered既定のまま`writeDirect()`を足す案（既存API面が変わらない）が生きる。** 未検証の範囲: P4 high speed（mps 512、27,136 byte stage）。E106で踏んだshort packetの件は「53 byte transferが毎回short packetで終わる」形の実話で、ZLPではない。P4で測る段では host側shortsとdevice側zerolenを並べて取り、出ればA、出なければbufferedのままで足りる、という切り分けにする。
- **再訂正（同日、P4 HSの実測）: ZLPはP4 high speedでは実在する。** buffered＋別taskからのwriteDirect＋stage 27,136 byteで、hostが`SHORT len=0`を受けたあとtimeout、deviceは`blocks=2 bytes=27136 armfail=1 zerolen=1`。完了直後にclassがZLPをarmしてclaimを握り、別taskの`writeDirect()`が`armfail`で弾かれて**ストリームが停止**する（レイテンシではなく停止）。同じ構成でarmを完了callbackの中に置くと6,081 blocks / shorts 0 / zerolen 0 / 27.5 MB/sで完走。S3 full speedで出なかったのはP4 HSと挙動が違うため。なお先方のS3の表の「non-buffered」行は、`build_opt.h`変更後に`--clean`を付けずlibraryが古いflagのままbuildされていたため無効（arm位置の軸はsketch側なので有効）。P4のnon-bufferedは`--clean`付きで取り直し中で未確定。**buffered＋directを採るなら「armは完了callbackの中でだけ」が契約になる**（別taskからのarmは停止する）。
- **P4 HS実測（同日、`--clean`付き、先方）**: buffered＋完了callback arm＋27,136: 5,757 blocks / shorts 0 / zerolen 0 / 24.43 MB/s。buffered＋別task arm: 1 block目でshort 1・zerolen 1・**停止**。non-buffered＋完了callback: 6,161 / 0 / 0 / 27.86。non-buffered＋別task: 5,921 / 0 / 0 / 26.78。buffered＋callback＋8,192: 16.68。対照のbuffered stock `write()` 512 B: 20,200 blocks / **shorts 8,183 / zerolen 8,183**（mpsちょうどのwriteごとにZLP）。読みはpyusb同期なので絶対値はhost律速（E110の1 MiB URB×8では49 MB/s）。
- **仕様案（先方）: Aを採る。** `static constexpr bool directWriteSupported()`（`CFG_TUD_VENDOR_TXRX_BUFFERED == 0`でtrue）、`bool writeDirect(const void*, size_t)`（≤65,535）、`onTxComplete(std::function<void(size_t)>)`、`onRxData(std::function<void(const uint8_t*, size_t)>)`（F3、non-buffered専用）。bufferedでは`writeDirect()`はfalseで何もしない（契約違反で停止する経路を公開APIに載せない）。有効化は`-DCFG_TUD_VENDOR_TXRX_BUFFERED=0`＋`--clean`必須。non-bufferedで失うもの: `available()` / `read()`は0、`flush()`はno-op、`write()`はepbuf copy経路（clamp、1本in flight）で`writeDirect()`と混ぜない。S2/S3では64 byte clampになるので既定はbufferedのまま。Bは保留（A で数字と使い勝手を見てから）。
- **先方の実装（同日）: 4点すべて取り込み済み。** 契約は「先頭64 byte整列・DMA可能・長さ≤65,535・完了まで所有権」（**2026-09-16訂正: 「別coreで書いたらC2M」は契約から外れた。P4のL1データキャッシュは共有でTinyUSBのarm時cleanが両coreの書き込みを書き戻す。先方commit `4e9330a`、E118。ただしこの訂正は先方working treeのみで、Library Managerの2.4.0のヘッダはまだ旧記述のまま。patch releaseを出すかは持ち主判断で、出れば先方から連絡**）で長さの整列は不要（ヘッダに明記、端数長27,000 byteも測定に含む）。理由別error `EspUsbDeviceVendorDirectError {None, NotSupported, NotMounted, BadArgument, NotAligned, NotDmaCapable, Busy, TransferFailed}`、`lastDirectError()` / `lastDirectErrorName()`（`Busy`はin flight中の二重arm＝backpressure、契約違反はendpointに触る前に弾く）。arm位置は制限せずドキュメントのみ。`onTxComplete` / `onRxData`はusbd task context・非blocking、`onRxData`のbufferはcallback中のみ有効と明記。CR-13はnon-bufferedでも既定で有効（endpoint宣言から計算、起動行に`bulkInDoubleBuffered()`のbitmapを出す）。non-bufferedで失うものと`--clean`必須・「sizeが変わらなければ効いていない」もヘッダに記載。P4のRAMは136,728→131,872 byte（FIFOが消えたぶん）。`host_probe.py --no-command`用は`-DDIRECT_PURE_PATTERN=1`（`bytes(range(256))`周期・連番なし・mount後自動送出）。正規実装そのものをP4で測定中。**当方がworking tree版で取る`host_probe.py`の数値は予備測定で、正式な数値は正式リリース後の版をpinして取り直す（持ち主の指示、2026-09-15）。**
- **予備測定（当方、2026-09-15、working tree版pattern build、`host_probe.py --no-command`、1 MiB URB×depth 8、usbipd/WSL、256 MiB×2、ログ `_runs/_runs/CR10_prelim_20260915T192519JST_p4_direct_workingtree/probe.log`）**: 起動行 `P4D_STARTED buffered=0 direct=1 fifo2=0x0002 stage=27136 armfromtask=0 stock=0 pure=1`。

| stage | host MB/s（2回） | short | pattern_bad | device `P4D_STAT` |
|---|---|---|---|---|
| 27,136 | **22.76 / 23.18** | 0 | 0 | blocks 19,784 / armfail 0 / zerolen 0 |
| 65,024 | 23.07 / 23.65 | 0 | 0 | blocks 8,256 / armfail 0 / zerolen 0 |
| 8,192 | 21.20 / 21.80 | 0 | 0 | blocks 65,536 / armfail 0 / zerolen 0 |

  正しさは全構成で通ったが、速度は独自patch版のE110（同じhost経路・同じURB構成で27,136: 49.3、65,024: 49.0〜49.2、8,192: 41.2 MB/s）の**半分以下**で、先方のpyusb同期読み（22.78 / 23.60）とも一致する＝host側の読み方に依らずdeviceが約23 MB/sで律速している。transfer長3点から`t = a + b×bytes`を引くとa≈45 µs、漸近24 MB/s（5.6 packet/µframe）で、per-transferのoverheadでなくper-byteの上限。E110の1 packet zero-copyは29.7 MB/s（7.3 packet/µframe）だったので、それより遅い。疑うべき点: (1) DWC2がDMA modeで動いているか（E109の方法: `GAHBCFG.DMAEn`と`GINTMSK.RXFLVL`を読む。2.3.0 releaseはDMAだった）、(2) 2 packet FIFOが実際にdcdの割り当てに反映されているか（`fifo2=0x0002`はlibrary側の意図。実体は`DIEPTXF1`の上位16 bit＝FIFO深さwords。2 packetなら256）、(3) `writeDirect()`内の追加処理（27 KBのmsyncやチェック）は数十µs程度で、この差の説明にはならない。**正式な数値ではない**（リリース後のpin版で取り直す）。
- **切り分け結果（同日）**: 先方がレジスタを読み、`gahbcfg=0x00000027 dmaen=1 rxflvl=0 ghwcfg2_arch=2`（DMA mode、E109と同一）、`dieptxf1` 上位16 bit＝**512 words＝4 packet分**で、仮説(1)(2)はいずれも否定。原因は**測定sketchがstageごとに27 KBをusbd task上で埋めていたこと**（E102はprecomputed）。事前計算に変えた先方のpyusb読みは27,136: 22.78→27.40、65,024: 23.60→30.44 MB/s。当方が同じprecomputed build（`-DDIRECT_NO_REFILL=1`）を1 MiB URB×depth 8で読み直した結果（予備測定、ログ `_runs/CR10_prelim_20260915T192519JST_p4_direct_workingtree/probe.log`）:

| stage | host MB/s（2回） | short | pattern_bad | 参考: E110（独自patch版） |
|---|---|---|---|---|
| 27,136 | **46.64 / 46.40** | 0 | 0 | 49.3 |
| 65,024 | **48.33 / 48.32** | 0 | 0 | 49.0〜49.2 |
| 8,192 | **41.44 / 42.86** | 0 | 0 | 41.2〜41.3 |

  **正規実装（同梱TinyUSB無改変、`writeDirect()`＋`onTxComplete()`内arm、TX FIFO自動2 packet以上）は独自patch版E110と同じ天井に届く**（差は3〜6%、run間のばらつきと同程度）。知見: usbd task上（`onTxComplete`の中）でdataを作ってからarmすると約半分に落ちる。bufferは別taskで先に用意し、callbackでは簿記とarmだけにする（E108のarm ringの形）。先方docsにも実測値つきで記載予定。**正式な数値ではない**（リリース後のpin版で取り直す）。
- **当方の回答（利用側）**: (1) 契約違反は**チェックしてfalse**（黙ってcopyに落とさない）。ただし**長さの64 byte整列は要求しないこと**——最終stageの端数とstatus行（数百〜1,300 byte）は整列長でなく、DMAに要るのは先頭整列（4 byte、cacheの都合で64 byte推奨）と呼び出し側のC2M msyncだけ。falseの理由が分かるようcounterかenumを。(2) `writeDirect()`を**callback内からに制限しない**——E108のarm ringは最初のarmと、pipelineが空いたあとの再armをusb task（task context）から行う。non-bufferedならZLP経路がないので制限は要らず、ドキュメントで「完了callback内で次をarmすると隙間が出ない、task再armでも27 KiB以上なら差なし（E110 §3）」と書けばよい。(3) 追加で欲しいもの: in flight中の`writeDirect()`はfalse（二重armの検出）、完了callbackはusbd task context・非blocking・完了byte数、`onRxData`のbufferの寿命（callback中のみ）を明記、CR-13（TX FIFO 2 packet）はnon-buffered buildでも既定で効くこと。
- こちら（E108〜E115）のnon-bufferedは実在する: 各実験は別sketch dir（別build dir）でflagを最初から持ち、E097 direct RX hookが動いていた（bufferedのlibraryならhookは呼ばれずcommandが届かない）ことで裏が取れる。
- 先方の試作はライブラリ側に入って動作（S3 peer構成、EspUsbHostがhostのend-to-endで、呼び出し側bufferがそのまま届き順序も崩れず、完了callbackが連続armを維持）。buffer契約は実装コメントに転記済み。設計上の気づき: `EspUsbDevice.h`はTinyUSB configをsketchに露出していないので、sketchは自分がbuffered / non-bufferedのどちらのbuildか判定できない。Aを採るならライブラリがモードを公開する必要がある。



## 追加依頼 CR-14〜CR-16（2026-09-16、MS OS 2.0 の仕様突き合わせ）

状態: **CR-14 / CR-15はEspUsbDevice 2.5.0でリリース済み（2026-09-16実装・Windows実機検証、リリースは2026-09-17までに確認）。** `EspUsbDeviceConfig::deviceInterfaceGuid`（未指定なら従来のGUIDを維持＝後方互換）と`msOs20VendorRevision`（既定はdescriptor setからの自動導出、`microsoftOs20VendorRevision()`で実際の値を読める）。**CR-16は先方の判断で保留**（前半の`bDeviceClass`条件は、このライブラリでは`bDeviceClass`が0x00か0xEF/0x02/0x01にしかならず（`EspUsbDevice.cpp` 1593〜1595行と1868〜1870行、当方で確認）、sketchから変える手段も`bNumConfigurations > 1`も無いため到達しないコードになる。`bDeviceClass`を設定可能にする要望が出たときに判定ごと入れる）。**CCGP descriptorは先方が実装済み**（`config.msOs20CcgpDevice`、既定オフ）。単一vendor interfaceで親にusbccgpが載り、子`&MI_00`にWINUSBが当たり、`DeviceInterfaceGUIDs`は**子だけに付いて親に付かない**ことを実測。**用途は当初言った「親の残骸＝幽霊デバイスを防ぐ」ではない**（残骸は列挙されず不活性であることが判明し、当方の主張は撤回した）。**トポロジを最初から固定して、後からfunctionを足してもGUIDの登録先が動かないようにする**のが実際の用途。採用の判断材料はusbccgpを挟んだbulk帯域（当方で再測。現在の366 Mbpsは非composite値）だけで、優先度は低い。 送付は2026-09-16（[usb-library-feedback.ja.md](usb-library-feedback.ja.md) 送付記録）。起票は[Windows が WinUSB を当てない](windows-winusb-binding.ja.md) §仕様で裏を取った から。

**CR-14の検証結果（先方、identity固定でGUIDとrevisionだけ変えた4変種。全変種`STATUS OK` / `SERVICE WINUSB`、instanceは`USB\VID_303A&PID_4080\GUID-TEST-1`のまま）**: A（GUID `{A1A1…}`、revision 21192自動導出）→ そのGUIDを保持。B（`{B2B2…}`、563自動導出）→ 更新。**C（`{C3C3…}`、revisionを563に固定＝対照）→ `{B2B2…}`のまま更新されず。** C′（`{C3C3…}`、12898自動導出）→ 更新。**対照Cがあるので「revisionが効いた」と「毎回読み直している」を区別できている。** revisionは自動導出（descriptor setから）を採用。2.4.0にrevision descriptorが無かったのは実害のある欠落だったと確認された。

いずれも実機での不具合報告ではなく、**MS OS 2.0 の仕様書本文と現在の実装の差**である。CR-1（flat / subsets の自動判定）は正しく動いており、それを前提に残る差を挙げる。

根拠にした一次資料は [Microsoft OS 2.0 Descriptors Specification](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification)（本文は `MS_OS_2_0_desc.docx`）と [Enumeration of USB composite devices](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/enumeration-of-the-composite-parent-device)。読んだ実装は `src/EspUsbDevice.cpp` の `buildWebUsbDescriptors()` と `microsoftOs20SubsetLayout()`。

### CR-14 `MS_OS_20_FEATURE_VENDOR_REVISION`（0x08）を出してほしい

**優先度: 高**（descriptor を変えた firmware を配ったときに効く）

仕様の該当箇所。

> The Microsoft OS 2.0 vendor revision descriptor is used to indicate the revision of registry property and other MSOS descriptors. If this value changes between enumerations the registry property descriptors will be updated in registry during that enumeration. **You must always change this value if you are adding/modifying any registry property or other MSOS descriptors.**
>
> The vendor revision descriptor must be applied at the device scope for a non-composite device or for MSOS descriptors that apply to the device scope of a composite device. Additionally, for a composite device, the vendor revision descriptor must be provided in every function subset and may be updated independently per-function.

現在の `buildWebUsbDescriptors()` は set header / configuration subset / function subset / compatible ID / registry property の 5 種類だけを出す。vendor revision がないと、**`DeviceInterfaceGUIDs` を変えた firmware に差し替えても、Windows が registry の値を更新する契機がない**（すでに一度列挙した PC 上で）。6 byte の feature descriptor で、flat なら set header 直下、subsets なら各 function subset 内に置く。

お願いしたいこと。

- `MS_OS_20_FEATURE_VENDOR_REVISION`（wLength 6、wDescriptorType 0x08、`VendorRevision` ≥ 1）を出す。
- 値は `EspUsbDeviceConfig` から指定できるようにし、既定は 1。**registry property の中身（GUID など）が変わったら利用側が上げる**、という運用をヘッダに書く。
- ライブラリ側で descriptor set の内容から自動で導出する（例: 出力 byte 列の CRC16 の下位 15 bit ＋ 1）案もある。利用側が上げ忘れても正しく更新されるので、こちらの方が事故は少ない。どちらを採るかは実装側の判断で構わない。

### CR-15 `DeviceInterfaceGUIDs` を設定できるようにしてほしい

**優先度: 中**

`EspUsbDevice.cpp` の registry property 生成は GUID を直書きしている。

```cpp
offset += putUtf16Le(&set[offset], "{975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}", true);
```

このため **EspUsbDevice で作った vendor interface はすべて同じ device interface GUID を名乗る**。binding には影響しない（compatible ID だけで決まることは M1〜M4 で確認済み）が、host アプリが `SetupDiGetClassDevs` でこの GUID を列挙すると、自分の製品でない EspUsbDevice 製品まで拾う。製品ごとに別の GUID を名乗れるのが本来の使い方である。

お願いしたいこと。

- `EspUsbDeviceConfig` に GUID 文字列（`{...}` 形式、38 文字）を受ける項目を足し、未指定なら現在の値を既定として保つ（後方互換）。
- 形式が違う文字列は列挙を壊すので、`begin()` で弾いて `lastError()` に出す。
- CR-14 と組み合わせ、**GUID を変えたら vendor revision も変わる**ようにする。

### CR-16 自動判定の穴（`bDeviceClass` が composite の条件を満たさない複数 interface）と `MS_OS_20_FEATURE_CCGP_DEVICE`（0x07）

**優先度: 中**（いまの構成では踏まないが、DFU を足すときに踏む）

`microsoftOs20SubsetLayout()` は `configDescriptor_[4] > 1`（`bNumInterfaces`）だけで subsets を選ぶ。しかし Windows が `USB\COMPOSITE` を付けて usbccgp を載せる条件は 3 つある。

> - The device class field of the device descriptor (**bDeviceClass**) must contain a value of zero, or the class (**bDeviceClass**), subclass (**bDeviceSubClass**), and protocol (**bDeviceProtocol**) fields of the device descriptor must have the values 0xEF, 0x02 and 0x01 respectively
> - The device must have multiple interfaces.
> - The device must have a single configuration.

つまり `bDeviceClass = 0xFF`（vendor-specific）で interface 2 本の構成では usbccgp が載らず、**subsets を出すと function subset の指す先がなくなって両方の interface が Code 28 になる**。これは E081 で単一 interface に起きたのと同じ現象である。

お願いしたいこと。

- 自動判定を `bNumInterfaces > 1` **かつ**（`bDeviceClass == 0` または `0xEF/0x02/0x01`）にする。条件を満たさないのに interface が複数ある構成は、そもそも Windows では各 interface に driver が当たらないので、`begin()` で警告を出すのが親切。
- 併せて `MS_OS_20_FEATURE_CCGP_DEVICE`（wLength 4、wDescriptorType 0x07）を出せるようにしてほしい。仕様は "the device should be treated as a composite device by Windows regardless of the number of interfaces, configuration, or class, subclass, and protocol codes, the device reports" と書いており、**これを出せば interface 1 本でも usbccgp が載って function subset が効く**。E081 で見た「単一 interface に subsets は届かない」のもう一方の解でもある。既定では出さない（現在の flat が正しく動いているため）。

### こちらでの確認予定

CR-14 が入ったら、こちらの P4 で「**同一 VID:PID・同一 serial のまま registry property を変えて、Windows 側の `Device Parameters` が更新されるか**」を測る。これは [E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md)（未実行）の一部で、いまの firmware は serial を固定して devnode を使い回しているため、この条件は一度も通していない。

### 2.5.0での取り込み確認（2026-09-17、当方）

先方のCHANGELOGで確認した。**CR-14（vendor revision）とCR-15（GUID設定可能化）は2.5.0に入った。**

| 依頼 | 2.5.0での実装 |
|---|---|
| CR-14 | `MS_OS_20_FEATURE_VENDOR_REVISION`を出すようになった。既定はdescriptor setからの**自動導出**（当方が提案した「利用側の上げ忘れ事故が消える」案）。`EspUsbDeviceConfig::msOs20VendorRevision`で上書きでき、`microsoftOs20VendorRevision()`が実際に適用された値を返す |
| CR-15 | `EspUsbDeviceConfig::deviceInterfaceGuid`。**未指定なら従来のGUIDのまま**なので、既存のhost側は壊れない |
| CR-16 | 保留のまま（`bDeviceClass`がライブラリ内で0x00か0xEF/0x02/0x01にしかならず到達しない。CCGP descriptorも未実装） |

**当方の正式値（2.4.0 pin、[E116](../experiments/e116_p4_usb_in_ceiling_release/README.ja.md)〜[E118](../experiments/e118_p4_generic_fast_path/README.ja.md)）は有効なままである。** 2.5.0の変更はUVC class、isochronous FIFOの検査、HID登録順のbug fix、`deviceVersion`（`bcdDevice`）、CR-14 / CR-15で、**`writeDirect()`とvendor bulkのdata pathには手が入っていない**。再測の必要はない。2.5.0へpinし直すかは、DFUやGUIDの設定が要るようになってからでよい。
