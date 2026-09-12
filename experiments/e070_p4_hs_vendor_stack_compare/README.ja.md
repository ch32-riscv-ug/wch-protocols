# E070 ESP32-P4 USB HS vendor bulk — core内蔵stackと EspUsbDevice の比較

状態: **完了 — 帯域はcore内蔵が速く安定(9.41 対 7.57 MB/s)、descriptor準拠はEspUsbDeviceが上**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)(core内蔵stackで9.73 MB/s)

## 問い

**同じvendor bulk構成を、Arduino-ESP32 core内蔵のUSB stackと[EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) 2.2.0の 両方で作ったとき、帯域・data完全性・descriptorの正しさはどう違うか。**

## なぜ比べるか

[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)で、core内蔵stackのvendor bulkは**usbip越しで9.73 MB/s**まで出た。ただし**1 microframeあたり約2.4 transaction**(HSが許す13のうち)で、**送出FIFOが512 B**であることの効きが残っている。core内蔵stackではこの値は**precompiled libsの`sdkconfig`に焼かれていて動かせない**。

`EspUsbDevice`はv2で**TinyUSBを自前buildし、自分の`tusb_config.h`を持つ**。つまり**FIFOやendpoint sizeを動かせる側にいる**。加えて、[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)と[E063](../e063_p4_usb_hs_enumerate/README.ja.md)の両方で**core内蔵stackはDevice Qualifier descriptorに答えていない**(UsbTreeViewが`ERROR_GEN_FAILURE`)。HS deviceでは本来必須で、`EspUsbDevice`は`otherSpeedConfigurationDescriptor()`を持つ。

したがって比べる価値があるのは次の3点である。

1. **帯域** — 同じ条件で速いのはどちらか
2. **data完全性** — [E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)で見たpacket欠落が出るか
3. **descriptorの正しさ** — Device Qualifierに答えるか、WinUSBにbindするか

## 仮説

**帯域は同等かEspUsbDeviceがやや速い。descriptorの正しさはEspUsbDeviceが上。**

既定の`tusb_config.h`はどちらも`VENDOR_TX_BUFSIZE=512`なので、**同じ設定なら帯域も同程度になるはず**である。差が出るとすれば、初期化・task構成・descriptor生成の違いによる。**FIFOを深くできることの効きは、この実験では測らない**(既定同士を比べる。深くする効果は次の問い)。

## 反証条件

1. 帯域が大きく違う(±20%超)。**既定の設定が同じでも実装差が効いている**ことになり、どこが効いたかを追う必要がある
2. `EspUsbDevice`でHSにならない、または列挙しない
3. `EspUsbDevice`でpacket欠落が出る。**[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)の欠落はstack実装ではなくより下の層**にある
4. core内蔵stackでも実はDevice Qualifierに答えている(測り方の誤り)
5. **どちらもWindowsでWinUSBにbindしない**。[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)のWindows側の問題はstackと無関係と確定する
6. `EspUsbDevice`ではvendor OUT endpointがdataを受け付ける。[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)のOUT不通はcore内蔵stack固有

## 方法

### そろえるもの

[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)と**同じ条件**にする。

- vendor interface 1本のみ、OTG HS、endpoint 512 B
- 送出task = **core 0にpin**、優先度5
- 送出元 = **internal RAM 64 KiB**を繰り返し(PSRAMは使わない。この個体では検出されない)
- 転送 4 MiB、**consoleから`C`で開始**
- host = WSL(usbip経由)+ libusb、**read size 1 MiB**([E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)の最良)
- pattern = 32-bit word index(周期64 KiB)、**全word照合**

### 変えるもの

| | A: core内蔵 | B: EspUsbDevice 2.2.0 |
|---|---|---|
| stack | `USB.h` + `USBVendor.h`(Arduino-ESP32 3.3.11) | `EspUsbDevice.h` |
| PID | `1209:0008`([E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)と同じ) | **`1209:0009`**(Windowsのdevice instanceを分ける) |
| serial | `E069-A` | `E070-B` |

**Aは[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)の実測値をそのまま使う**(同じfirmware、同じ条件)。Bを新たに測って並べる。

### 回数

各20転送。[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)の欠落率13%なら、20転送で1件も出ない確率は約6%なので、**欠落の有無はこの回数で判断できる**。

### descriptorの確認

両方について、Linux側で次を取る。

- `lsusb -v`(Device Qualifier、BOS、endpoint)
- MS OS 2.0 control request(`0xC0, 2, 0, 7`)の応答
- Windows側の`Service`と`Problem`(HS portをWindowsへ戻せる場合のみ)

## 対象外

- **FIFOを512 Bより深くしたときの効果**。既定同士の比較に絞る。深さの効果は別の問い(`p4-hs-vendor-fifo-depth`)
- CDC / HID / MSCなど他のclassでの比較
- host → device(OUT)方向の帯域。**OUTが通るかどうかだけ**は見る
- WindowsネイティブでのWinUSB経由の測定(現状bindしないため不可)
- 消費flash / RAMの比較

## 必要な環境

[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)と同じ。加えて:

- `EspUsbDevice` 2.2.0(`~/dev/EspUsbDevice`。`.env`の`TEST_ESPUSBDEVICE_DIR`で受け取り、`sketch.yaml`の`libraries: dir:`へ渡す)
- HS portがWSLへattachされていること(`usbipd bind`は管理者が一度実行済み)

## ベンチ種別

**一時・配線なし**。

## 記録する数値

| 項目 | 列 |
|---|---|
| 条件 | stack(A/B)、read size、転送byte数 |
| device側 | `elapsed_us`、`written`、`stalls` |
| host側 | 受信byte数、MB/s、pattern不一致offset、error |
| descriptor | Device Qualifierの応答、BOSの有無、MS OS 2.0応答のbyte数 |
| 導出 | 1 microframeあたりのtransaction数、AとBの比 |

## 完了条件

1. **AとBの帯域が同じ条件で並び、比が言える**
2. **Bでpacket欠落が出るか出ないかが20転送で言える**
3. **Device Qualifierに答えるかがA/Bそれぞれで言える**
4. **「どちらを使うべきか」を、上の3点に基づいて一文で言える**

## 影響

- P4でUSBを使う実験すべての土台(どちらのstackを標準にするか)
- [E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)のpacket欠落の層の切り分け
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段のdownload帯域

---

## 結果

状態: **完了 — 帯域はcore内蔵が速く安定(9.41 対 7.57 MB/s)。descriptorの正しさはEspUsbDeviceが上(Device Qualifierに答える)。data完全性は両方とも問題なし**(2026-09-12)

### 計画からの逸脱

| 変えたもの | 計画 | 実際 | 理由 |
|---|---|---|---|
| Bのidentity | `1209:0009` / serial `E070-B` | **`1209:0008`** / serial `E070-B` | **usbipdの`bind`はVID:PID(とinstance)に紐づく**ので、PIDを変えるたびに管理者権限でのbindが要る。PIDは固定した |
| ライブラリの取り込み | `.env`のパス | **Library Manager(`EspUsbDevice (2.2.0)`)** | arduino-cliは**symlinkされたlibrary dirの`.cpp`を拾わない**。Library Manager経由ならベンチ固有のパスがsketchに入らず、versionも固定できる |
| 比較の取り方 | A/Bを並べて1回 | **Bを15転送 → Aを焼き戻して15転送**(連続) | usbip経路の時間変動を避けるため、**背中合わせで測った** |

### 帯域(read size 1 MiB、4 MiB転送、各15回、usbip経由)

| | **A: core内蔵** | **B: EspUsbDevice 2.2.0** |
|---|---:|---:|
| host median | **9.41 MB/s** | 7.57 MB/s |
| host min–max | **7.90 – 9.70** | 5.83 – 10.01 |
| device median | 9.28 MB/s | 7.36 MB/s |
| `stalls` median(FIFO待ちの回数) | **30,606** | 57,980 |
| 転送の失敗 | 0/15 | 0/15 |
| **pattern不一致** | **0/15** | **0/15** |

read sizeを振った段(各3回)では、Bは4 KiBで1.91 MB/s、64 KiBで6.53、256 KiBで7.89、1 MiBで7.01、2 MiBで7.49。**Aと同じく大きいURBほど速いが、どの点でもAより遅く、ばらつきが大きい。**

### descriptorの正しさ

| 要求 | A: core内蔵 | B: EspUsbDevice |
|---|---|---|
| **DEVICE_QUALIFIER**(HS deviceでは必須) | **STALL(答えない)** | **10 byte返す** |
| **OTHER_SPEED_CONFIGURATION** | **STALL** | **32 byte返す** |
| BOS | 57 byte、2 capability | 57 byte、2 capability |
| MS OS 2.0(`0xC0, 2, 0, 7`) | 178 byte、`WINUSB`入り | 178 byte、`WINUSB`入り |

### 事実

1. **帯域はcore内蔵stackの方が速く、安定している。** 背中合わせの15転送ずつでmedian 9.41対7.57 MB/s(**+24%**)。ばらつきもAが7.90–9.70(±9%)、Bが5.83–10.01(**±27%**)。
2. **BはFIFO待ちが約1.9倍多い**(stalls median 57,980対30,606)。同じ送出loop・同じ512 B FIFOなので、**差はstackがFIFOを掃き出す速さにある**。
3. **Bの最大値10.01 MB/sはAの最大9.70を上回る。** 速く出せる瞬間はあるが**持続しない**。
4. **descriptorの正しさはBが上。** **core内蔵stackはDEVICE_QUALIFIERにもOTHER_SPEED_CONFIGURATIONにも答えない**(STALL)。USB 2.0仕様ではHS対応deviceに必須で、[E063](../e063_p4_usb_hs_enumerate/README.ja.md)・[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)でUsbTreeViewが`ERROR_GEN_FAILURE`を出していたのはこれである。
5. **ただしBのOTHER_SPEED_CONFIGURATIONは中身が正しくない。** full-speed側のbulk endpointに`wMaxPacketSize=512`と書いている(FSの上限は64)。`EspUsbDeviceVendor::configurationDescriptor()`が**per-speedで渡される`endpointSize`を`(void)`で捨て、constructorの`endpointSize_`を両方の速度に使う**ため。→ **ライブラリ側の指摘事項**
6. **data完全性はどちらも問題なし。** 15転送ずつで不一致0。[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)のCDCは30転送中4件(13%)だったので、**あの欠落はCDC class側の問題である可能性がさらに強まった**(反証条件3は不成立)。
7. **flashはBの方が小さい。** 374,242 B(28%)対392,098 B(29%)で**約18 KB少ない**。
8. **MS OS 2.0はどちらも正しく答えるのに、Windowsはどちらでもbindしない。** → **反証条件5が成立。[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)のWindows側の問題はstackと無関係**である。

### どちらを使うべきか(完了条件4)

**帯域が要る経路は今のところcore内蔵stackが速い。ただしspec準拠とFIFO等の可変性はEspUsbDeviceが上で、伸びしろもそちらにある。**

- **いまP4で帯域を測る/出すなら core内蔵**(9.41 MB/s、ばらつき±9%)
- **HS deviceとして正しく振る舞わせたいなら EspUsbDevice**(Device Qualifierに答える。core内蔵は答えない)
- **EspUsbDeviceは`tusb_config.h`を自前で持つので、512 B FIFOを深くできる唯一の道**。[E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)の「1 microframeあたり約2.4 transaction」という天井を超えるには**この可変性が要る**。**現状の差(−24%)はFIFOを深くした効果で逆転しうる**

### 未決

- **BのFIFO待ちが多い理由** `—`。stackの送出経路(task構成、flushの契機)の差を追えば、Bの帯域はAに追いつく可能性がある
- **FIFOを512 Bより深くしたときの効果** `—`(`p4-hs-vendor-fifo-depth`)。**Bでしか試せない。次の問い**
- **Bのばらつきが±27%と大きい理由** `—`
- usbipを介さないネイティブでの両者の値 `—`
- vendor OUT endpoint:Aでは受け付けなかった。**Bでは未確認**
- **HID経由のthroughput** `—`(別実験)

### ライブラリへの指摘(EspUsbDevice 2.2.0)

1. **`EspUsbDeviceVendor::configurationDescriptor()`がper-speedの`endpointSize`を無視する**(`(void)endpointSize;`)。結果、other-speed(FS)のconfigurationにも512 Bと書かれ、**FSのbulk上限64 Bを超える**。HS/FS両対応を謳うなら、速度に応じて丸める必要がある
2. **同じ送出loopでFIFO待ちがcore内蔵の約1.9倍**。帯域差の主因に見える
3. arduino-cliは**symlinkされたlibrary dirの`.cpp`を拾わない**(headerは見つかるがlink時にundefined reference)。Library Manager経由なら問題ない

### 反映

- [LEDGER](../LEDGER.ja.md) §1にE070を採番、§3に記録を追加
- [E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)のpacket欠落 — **vendorでは2つのstackとも0/15**で、CDC固有の疑いが強まった
- [E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)のWindows WinUSB問題 — **stackと無関係**と確定
