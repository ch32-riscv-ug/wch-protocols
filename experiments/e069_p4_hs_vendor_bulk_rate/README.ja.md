# E069 ESP32-P4 USB HS vendor bulk(WinUSB)の帯域とdata完全性

状態: **完了 — vendor bulkはusbip越しでも9.73 MB/s、CDCネイティブ最速8.08 MB/sを上回る**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E066](../e066_p4_usb_hs_tx_context/README.ja.md)(CDCで最速8.08 MB/s)、[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)(CDCで13%のpacket欠落)

## 問い

**OTG HS上のvendor bulk endpointへWinUSB経由でPSRAMのdataを流したとき、実効帯域は何MB/sか。CDCの約8 MB/sを超えるか。1回のread(URB)の大きさで変わるか。[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)のpacket欠落は起きるか。**

## なぜCDCではなくvendorを測るのか

[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)〜[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)はすべてCDCで測った。**CDCを選んだのは[E063](../e063_p4_usb_hs_enumerate/README.ja.md)が「Windowsがどう認識するか」を見る実験で、driver追加なしにCOM portが生えるCDCが最短だったから**である。その後の帯域測定でもCDCを引き継いだが、**bulk転送の帯域を測る道具としてCDCは適切ではない**。

| | CDC(`usbser`経由) | vendor bulk(WinUSB経由) |
|---|---|---|
| host applicationが決められること | **ほぼ無い**。`usbser`が自前のbuffer sizeでreadを発行する | **URBの大きさを自分で決める**。1 MiBのreadは1 URB |
| 1 URBあたりのtransaction | `usbser`任せ | host controllerが**back-to-backで埋める**(HSは1 microframeに13回まで) |
| 途中の層 | `usbser` + serial API + pyserial | WinUSB + libusb |

[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)〜[E067](../e067_p4_usb_vs_capture_core/README.ja.md)で見えた天井は**1 microframeあたり約2 transaction**(13のうち)だった。これを「deviceのturnaroundが上限」と読んだが、**`usbser`のread単位が原因である可能性を排除していない**。vendorで測れば、その読みが正しいかが決まる。

## 仮説

**超える。** device側のFIFOはCDCと同じ512 B(`CONFIG_TINYUSB_VENDOR_TX_BUFSIZE`)だが、**host側でURBを大きくすれば、1つのURBを埋める間はhost controllerが連続してtransactionを発行する**ので、URB間の隙間が減る。

**同時に、これは天井の所在を決める実験でもある。**

- vendorで大きく伸びる → **天井は`usbser`側だった**。E064〜E067の帯域の数値は「CDC経由での値」であって、**HS経路の上限ではない**
- vendorでも約8 MB/sで頭打ち → **天井はdevice側**。E064〜E067の読みは正しかった

## 反証条件

1. vendorでもCDCと同じ約8 MB/sで頭打ちになる。**天井はdevice側**で、`usbser`は無罪
2. read size(URB size)を4 KiBから1 MiBまで振っても帯域が変わらない。URBの粒度は効かない
3. **WinUSBに自動bindしない**(driverの手動導入が要る)。driverlessという前提が崩れ、[Gate 2](../../references/probe-feasibility-gates.ja.md)の「分離手段」の評価が変わる
4. **[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)と同じpacket欠落が起きる。** 欠落はCDC classの問題ではなく、より下の層(TinyUSBのFIFO、GDMA、PHY)にある
5. 欠落が起きない。**CDC固有である可能性が高まる**
6. pattern照合が途中で壊れる、または転送が完了しない

## 方法

### 構成

- **console** = USB-Serial-JTAG(FS)。設定と結果のみ
- **計測対象** = OTG HS上の**vendor interface 1本のみ**(CDCは載せない)。`USB.webUSB(true)`でBOSとMS OS 2.0 descriptor setを出し、**WinUSBへ自動bind**させる
- identityは`1209:0008`。E063〜E068(`0002`〜`0007`)とWindowsのdevice instanceを分ける
- 送出taskは**core 0にpin**([E066](../e066_p4_usb_hs_tx_context/README.ja.md)の最良条件)
- host側は**Windowsのuv script**で`pyusb` + `libusb-package`

### 型

[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)と同じ握手にする。

1. harnessがconsoleへ`C`を送り、deviceを武装させる
2. host側がvendor OUT endpointへ`G`を1 byte書く
3. deviceは`G`を受けた瞬間から4 MiBを送り、所要時間をconsoleへ出す
4. hostはIN endpointから**指定したread sizeの単位で**4 MiBを読み、時間を測る
5. **全wordをpattern照合する**(`word[i] = i`)。欠落があれば最初の食い違いoffsetを記録する

### 掃引

| 段 | 固定 | 振るもの | 回数 |
|---|---|---|---|
| A | 転送 4 MiB | read size = 4 KiB / 64 KiB / 256 KiB / 1 MiB | 各3 |
| B | Aで最速だったread size | — | **30**(欠落率を[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)の13%と比べる) |

## 対象外

- host → device方向(OUT)の帯域
- libusbのasync APIで複数URBを同時に投げる形。**まず1 URBの大きさだけを振る**。必要なら別実験
- CDCとvendorの同居
- vendor経由でのPARLIO capture同時実行(`p4-usb-vs-drain`のvendor版)
- Linux / macOSでの同じ測定
- WebUSB(ブラウザ)からの接続
- **HID経由の限界throughput**。HSのinterrupt endpointは`wMaxPacketSize`最大1,024 Bかつ1 microframeに3 transactionまで許すので、[harness-channels](../../references/harness-channels.ja.md)の「HID = 64 kB/s」はFS前提の値である。**別実験で測る**

## 必要な環境

[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)と同じ。加えて:

- Windows側で`uv`が`pyusb`と`libusb-package`を取得できること(networkが要る)
- **WinUSBへの自動bindが効くこと**。効かない場合は反証条件3として記録し、手動でのdriver導入は行わない(利用者の環境を変えないため)

## ベンチ種別

**一時・配線なし**。

## 記録する数値

| 項目 | 列 |
|---|---|
| 条件 | `read_size`、転送byte数 |
| device側 | `elapsed_us`、`written`、`stall`(FIFOが空くのを待った回数) |
| host側 | 受信byte数、経過秒、MB/s、**pattern不一致の最初のoffset**、欠落byte数 |
| Windows | vendor devnodeの`Service`(WinUSBか)、instance ID |
| 導出 | 1 microframeあたりのtransaction数(= MB/s ÷ 512 B ÷ 8,000) |

## 完了条件

1. **vendor bulkの実効帯域がMB/sで言える**
2. **CDCの8.08 MB/sを超えるか超えないかが言える** — つまり**天井がdevice側か`usbser`側かが言える**
3. **read sizeの効きが言える**
4. **[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)の欠落がvendorでも起きるかが言える**

WinUSBに自動bindしない場合は、反証条件3を記録して完了とし、帯域は未測定`—`のまま残す。

## 影響

- [E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)〜[E067](../e067_p4_usb_vs_capture_core/README.ja.md)の帯域の**解釈**(「HS経路の上限」なのか「CDC経由での値」なのか)
- [E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)のpacket欠落の**層の切り分け**
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段のdownload帯域とstreaming上限
- [harness-channels](../../references/harness-channels.ja.md) §6c「capture の帯域が要るときは Vendor 側へ逃がす」の実測での裏付けまたは否定

---

## 結果

状態: **完了 — vendor bulkはusbip越しでも9.73 MB/s。CDCのWindowsネイティブ最速8.08 MB/sを上回った。天井は`usbser`側だった**(2026-09-12)

### 計画からの逸脱(3点)

実行中に環境側の事情で方法を変えた。**数値を読むときはここを併せて読む。**

| 変えたもの | 計画 | 実際 | 理由 |
|---|---|---|---|
| **host** | Windows + WinUSB + pyusb | **WSL(usbip経由)+ libusb** | **WindowsがWinUSBにbindしなかった**(下記)。usbipは経路にoverheadを足すので、**得られた数値は下限**である |
| **開始の合図** | vendor OUT endpointへ`G` | **consoleから`C`** | **vendor OUT endpointがdataを受け付けない**(下記)。bulk INはhostがpollする方式なので、deviceが先に送り始めても取りこぼしは起きない |
| **送出元** | PSRAM 8 MiB | **internal RAM 64 KiBを繰り返し** | 当時PSRAMが検出されなかったため(**後に自分のビルドミスと判明。下記**)。USB経路の測定にPSRAMのread帯域を混ぜずに済む点ではむしろ素直なので、そのまま採用した |

### 帯域(4 MiB転送、各3回、usbip経由)

| read size(1 URB) | min | **median** | max |
|---:|---:|---:|---:|
| 4 KiB | 3.54 | **3.56** | 3.58 |
| 64 KiB | 7.35 | **7.47** | 7.60 |
| 256 KiB | 8.51 | **8.94** | 9.10 |
| **1 MiB** | 9.46 | **9.73** | 9.82 |
| 2 MiB | 9.40 | **9.70** | 9.75 |
| 4 MiB | 7.74 | **8.89** | 9.07 |

**pattern照合は18転送すべてで不一致0。**

### 事実

1. **vendor bulkはusbip越しで9.73 MB/s出る。** CDCの**Windowsネイティブ**最速([E066](../e066_p4_usb_hs_tx_context/README.ja.md)、8.08 MB/s)を**20%上回る**。usbipは経路を足す側なので、**ネイティブならこれ以上出る**。→ **反証条件1は否定された。天井はdevice側ではなく`usbser`側だった。**
2. **1 URBの大きさが効く。** 4 KiBで3.56、1 MiBで9.73 MB/sと**2.7倍**違う。CDC(COM port)ではこの量をapplicationが決められない。→ **反証条件2も否定。**
3. **1 MiBで頭打ちになり、4 MiBでは落ちる**(8.89)。大きすぎる単一URBは不利。
4. **pattern欠落は18転送で0件。** [E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)のCDCは30転送中4件(13%)だった。**同じ回数ではないので断定はしないが、CDC固有の疑いが強まった**(反証条件5寄り)。
5. 9.73 MB/s ÷ 512 B = 約19,000 transaction/s = **1 microframeあたり約2.4 transaction**。HSが許す13にはまだ遠く、**FIFOが512 B(`CONFIG_TINYUSB_VENDOR_TX_BUFSIZE`)であることの効きは残っている**。deviceの`stalls`(FIFOが空くのを待った回数)は1 MiB条件で約29,600回 = 1 packetあたり約3.6回で、**deviceはhostを待っている**。
6. **4 KiB read sizeの1回で、転送途中に`empty read`が出て転送が止まった**(offset 1,265,664)。再実行では起きていない。**observationとして残す。**

### Windowsで起きたこと — WinUSBにbindしない

**deviceは正しい。Windows側の問題である。**

| 確認したこと | 結果 |
|---|---|
| UsbTreeViewのdump | **BOSは正しく読まれている**。MS OS 2.0 platform capability(UUID `D8DD60DF-...`、`dwWindowsVersion=0x06030000`、`wTotalLength=0x00B2`、`bMS_VendorCode=0x02`)も正しい |
| `bcdUSB` | **0x0210**(BOSを読む条件を満たす) |
| **Linuxから`bmRequestType=0xC0, bRequest=2, wIndex=7`を投げた** | **178 byte返ってくる。`WINUSB` compatible IDを含む正しいdescriptor set** |
| Windowsの状態 | `Problem 28 (CM_PROB_FAILED_INSTALL)`、`Service`なし、compatible IDに`MS_COMP_WINUSB`が**付かない** |

**deviceは要求に正しく答えているのに、WindowsがWinUSBを当てない。** 試して効かなかったもの: `bDeviceClass`を`0xEF/0x02/0x01`から`0x00`へ / `bcdDevice`を`0x0100`→`0x0200`→`0x0201` / serialを`"0"`→`"E069-A"`(= **新しいdevice instance**)。

### 副産物 — E062に直接効く観測

| 変えたもの | Windowsのdevice instance |
|---|---|
| `bcdDevice`(0x0100 → 0x0200 → 0x0201) | **変わらない**(`USB\VID_1209&PID_0008\0`のまま) |
| **serial**(`"0"` → `"E069-A"`) | **新しいinstanceになる**(`USB\VID_1209&PID_0008\E069-A`) |

[E062](../e062_usb_same_identity_layout_change/README.ja.md)の仮説「`bcdDevice`はidentityに効かない / serialは効く」が、**実機で片側ずつ確認できた**。さらに**失敗したdriver判定は古いinstanceに貼り付いたまま再判定されない**という実害も観測した。

### ベンチの異常 — PSRAMが検出されなくなった(**原因判明・自分のビルドミス。解決済み**)

作業中に`psram_found=0 / psram_size=0`になり、board側の故障を疑ったが、**原因はこの実験の手動ビルドだった**。

- **`arduino-cli compile --build-property 'build.extra_flags=-DBANNER_GIT=...'`で、platformが組み立てている`build.extra_flags`を丸ごと上書きしていた。** この変数は実際には
  `-DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=0 -DARDUINO_USB_CDC_ON_BOOT=0 ...`を運んでおり、**`BOARD_HAS_PSRAM`ごと消えていた**
- さらに**arduino-cliがその条件で作った`core.a`をcacheし、以後は正しいpropertyで組んでも古いcoreが再利用された**ため、症状が残り続けた
- `~/.cache/arduino/cores`と`~/.cache/arduino/sketches`を消して再ビルドしたら、**2枚のboard両方で`psram_found=1 / psram_size=33554432`に復帰した**

**PSRAMのhardwareは両board正常である。** 切り分けの過程で、`esp_psram_init()`を手で呼ぶと`ESP_OK`で初期化できることも確認した(= 起動時の初期化だけが抜けていた)。

**規則として**: **sketch固有のビルドオプションは`build_opt.h`に置く**。`--build-property`で`build.extra_flags`を触らない。**変更を反映するには`--clean`でフルビルドする**。repoのpytest harnessは`build_config.toml`から`compiler.cpp.extra_flags` / `compiler.c.extra_flags`へ注入しており、**こちらは元から正しい** — 壊れていたのは手動ビルドだけである。

### vendor OUT endpointがdataを受け付けない

`libusb_bulk_transfer`でOUT endpoint(`0x01`)へ1 byte書くとtimeoutする。IN側は正常。**双方向のprotocolを載せるなら潰す必要がある**が、この実験の問いではないので観測として残す。

### 候補

- **帯域が要る経路はCDCではなくvendor bulkにする**(採用)。[harness-channels](../../references/harness-channels.ja.md) §6cの「capture の帯域が要るときは Vendor 側へ逃がす」が実測で裏付いた
- **host側のURBは1 MiB**(採用)。4 KiBの2.7倍、4 MiBより速い
- **Windows側はWinUSBが当たらないので、当面はusbip + libusb(Linux)で測る**(暫定)

### 未決

- **WindowsでWinUSBが当たらない理由** `—`。deviceは正しいと確定。追った範囲と残る仮説は[Windows が WinUSB を当てない — 調査記録](../../references/windows-winusb-binding.ja.md)へ分けた
- **ネイティブ(usbipなし)での帯域** `—`。上の9.73 MB/sは下限
- **usbipのoverheadの大きさ** `—`。CDCを同じusbip経路で測れば較正できる
- **[E068](../e068_p4_hs_cdc_tail_loss/README.ja.md)のpacket欠落がvendorでも起きるか** — 18転送で0件だが、**30転送以上で確かめる必要がある**
- **FIFOを512 Bより深くしたらどこまで伸びるか** `—`。coreのstackでは変えられない → **[E070](../e070_p4_hs_vendor_stack_compare/README.ja.md)で`EspUsbDevice`と比較する**
- vendor OUT endpointが受け付けない理由 `—`
- ~~PSRAMが検出されない理由~~ → **解決**(上記。自分のビルドミスで、boardは正常)

### 反映

- [LEDGER](../LEDGER.ja.md) §1にE069を採番、§3に記録を追加
- [harness-channels](../../references/harness-channels.ja.md) §6cの裏付け
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段 — download帯域の最良値をCDCの8.08からvendorの9.73以上へ
- [E062](../e062_usb_same_identity_layout_change/README.ja.md) — `bcdDevice`とserialのidentityへの効きが片側ずつ実測で付いた
