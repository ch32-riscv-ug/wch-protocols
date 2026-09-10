# USB descriptor変更に対するhostの挙動 — Windows / Linux / macOS

状態: **調査結果**(2026-09-10)。一次資料で確認した事実、コミュニティ観測、そこからの推論を分けて記す。実機で測っていないものは「要実測」と明記する。

上位文書: [probe-product-concept](probe-product-concept.ja.md) / [probe-feasibility-gates](probe-feasibility-gates.ja.md) Gate 2 / 訂正対象: [harness-choices](harness-choices.ja.md) §2

## 問い

同じ`idVendor`、`idProduct`、`iSerialNumber`のまま、firmware書換でconfiguration descriptor(interface構成)が変わったとき、各OSは何を記憶し、何を再認識するか。`bcdDevice`はそこに関与するか。

repositoryの当初の結論は「Windowsは認識をcacheするので`bcdDevice`でprofileを分ける必要がある」だった。この結論は前半だけが正しい。

## 要約

| OS | 永続化されるもの | 単位 | `bcdDevice`の関与 | 構成変更後の再認識 |
|---|---|---|---|---|
| **Windows** | devnode(`Enum\USB\...`)と結ばれたdriver、`usbflags`、COM番号 | **VID:PID + serial**(親)、**+ interface番号 `MI_nn`**(子) | **instance identityに含まれない**。`REV_`付きhardware IDと、MS OS 1.0 descriptorの応答cacheにだけ現れる | 既存devnodeが再利用される。親のdriverが変わる切替(単機能↔composite)と、同じ`MI_nn`の機能変更で破綻する観測がある。末尾追加は見込みあり(要実測) |
| **Linux** | なし(静的なquirk表とudev hwdbのみ) | — | driver `id_table`の範囲matchとhwdb modaliasに使えるが静的 | 毎回descriptorを読み直し、その場でdriverをmatchする |
| **macOS** | なし(I/O Registryはメモリ上) | — | matching keyの一つだが静的なpersonality用 | 接続ごとにmatchingする |

「標準classだけなら再認識されるか」への答え: LinuxとmacOSは**はい**。Windowsは**devnodeに結ばれたdriverが変わらない範囲でだけ**。classが標準かどうかは関係なく、devnodeの再利用で決まる。

## 1. Windows

### 1.1 identity(一次資料で確認)

- USB hub driverはdevice descriptorからdevice ID `USB\VID_xxxx&PID_yyyy`を作る。hardware IDは`USB\VID_xxxx&PID_yyyy&REV_rrrr`と`USB\VID_xxxx&PID_yyyy`の二つ
- instance IDはserial number、なければport由来の文字列。device instance IDは`USB\VID_xxxx&PID_yyyy\<serial>`または`USB\VID_xxxx&PID_yyyy\5&109d12e&0&1`のような形になる。Microsoftの例示にも`REV`は含まれない
- compositeの子(usbccgp)は`USB\VID_xxxx&PID_yyyy&MI_nn`をdevice IDとし、instance pathは`USB\VID_xxxx&PID_yyyy&MI_nn\<生成prefix>&00nn`になる。`nn`は`bInterfaceNumber`(IADがあれば`bFirstInterface`)
- 同じVID:PID:`bcdDevice`の個体を区別するには`iSerialNumber`が必要、とMicrosoft自身が書いている

コミュニティ観測: OSR NTDEVでTim Robertsが「試したが、`bcdDevice`はPnP identifierの一部ではない」と明言している。

### 1.2 何が永続化され、いつ再利用されるか

一次資料:

- PnP managerは`Enum\<enumerator>\<deviceID>`キーの有無で「このdeviceが以前installされたか」を判定し、新規のdeviceだけをゼロから構成する。キーには`HardwareID`、`CompatibleIDs`、`Driver`、`Service`が保存される
- 既存instanceが**異なる**hardware ID / compatible IDで再出現したときの挙動は、Microsoftの文書に記述がない

コミュニティ観測(Microsoft文書なし):

- 2011年、OSR: 1 interfaceで列挙された後、firmware downloadで2 interfaceになるdevice。「usbccgpが先に載らず、vendor driverがそのまま載る。唯一の解決はDevice Managerでdriverをuninstallすること」。`bcdDevice`変更は試したが効かなかった
- 2017年、OSR(Red Hat): HID collectionを変えても以前のdevnodeが再利用され、黄色い感嘆符になる。「HID levelのcacheではなく、同じinstance pathのPDOを見たらdevnodeを作り直さないというPnP managerの設計」。修正はreport descriptorのhashを子instance IDへ含めること。**ただし本人が「Windows 10では再現しなかった。他者からは報告がある」と書いている**
- TinyUSBのexampleは「interfaceの組合せごとにPIDを一意にせよ。PCは初回にdriverを保存するので、同じVID:PIDで別interfaceを出すとsystem errorになりうる」とcommentし、組合せをbitmapにしたPIDを使う。Teensy(USB Typeごとに別PID)、ArduPilot(composite用に別PID)も同じ慣行
- PJRC forumで作者は「`bcdDevice`を上げればWindowsは再読込する」と勧めているが、同じthreadに「上げても直らず、Device Managerでuninstallして直った」という反例がある

推論: Windows 10/11では再現しないという報告が一件あるため、**Windows 11での挙動は実測が必要**。ただしdeveloperの慣行は一致して「別PID」であり、`bcdDevice`が効いた報告はない。

### 1.3 `bcdDevice`が実際に効く場所(一次資料で確認)

| 場所 | 内容 | 影響 |
|---|---|---|
| hardware ID `REV_rrrr` | INFのmatch候補。`REV`なしの汎用IDも同時に生成される | **新規devnodeの**driver選択にだけ効く。既存devnodeには効かない |
| `usbflags\VVVVPPPPRRRR\osvc` | MS OS **1.0** string descriptor(index 0xEE)への応答を初回列挙で記録し、二度と問い直さない | MS OS 1.0 descriptorを使う場合、対応を追加したら`bcdDevice`を上げる必要がある(MicrosoftはContainerID追加時にこれを要求している) |
| MS OS **2.0**(BOS経由) | 仕様書の「cache」はすべて1.0の0xEE問合せに関する記述。registry propertyは初回列挙で書き込まれ、更新は**VendorRevision descriptor**で指示する | `bcdDevice`への依存は書かれていない。1.0の`osvc`とは別 |

Arduino-ESP32はMS OS 2.0を使うので、`osvc` cacheは本projectの経路に関与しない。

### 1.4 IAD、usbser、COM番号、HID(一次資料で確認)

- IADを解釈させるには、device descriptorが`bDeviceClass=0xEF`、`bDeviceSubClass=0x02`、`bDeviceProtocol=0x01`でなければならない。「これがないとIADを検出せず、interfaceを正しくgroupしない」
- compatible ID `USB\COMPOSITE`が生成される条件は、device classが0または`EF/02/01`、複数interface、単一configuration。この条件を外れると親device全体にclass driver(たとえば`USB\Class_02`でusbser)が結ばれうる
- IADがない場合、usbccgpは各interfaceを別PDOにする。CDCのunion descriptorによるgroupingはvendor INFでusbccgpを明示的に構成しないと使われない
- usbser(Windows 10以降)は`USB\Class_02&SubClass_02&Prot_01`と`USB\Class_02&SubClass_02`にmatchする
- COM番号は`MI_nn` child instanceの`Device Parameters\PortName`に保持される。interface番号が変わると別instanceになり別COM番号が振られる。ComDBは外したdeviceの番号も予約し続ける
- HIDClassはtop-level collectionごとにPDOを作り、IDは`HID\VID&PID&REV&MI&Colxx`とusage page/usageで構成される。report descriptor自体を保存するcacheは文書上存在しない。上記Red Hatの観測もdevnode再利用で説明されている

### 1.5 導かれる境界(推論。E062で要実測)

| 切替 | 親devnode | 子devnode | 見込み |
|---|---|---|---|
| **HID単機能 → composite**(E013のA→B) | hidusbが結ばれた親にusbccgpが必要になる | — | **破綻**(2011年の観測と同型) |
| composite内で**末尾にfunction追加** | usbccgpのまま | 既存`MI_nn`は同じ機能、新childが増える | **動く見込み** |
| **既存`MI_nn`の機能を変える**(例: MI_00をVendor→HID) | 不変 | 旧driverを持つchildが再利用される | **破綻**(2017年の観測と同型) |
| compositeで**function削除** | 不変 | 消えたchildはghost devnodeになり、COM番号予約が残る | 動く見込み。掃除は残る |
| **`bcdDevice`だけ変更** | 同じinstance path | 同じ | **効果なし** |
| **serial変更** | 新instance | 新instance | 動く。個体識別と設定の継続を失う |
| **PID変更** | 新device ID | 新device ID | 動く。PIDを消費する |

### 1.6 Arduino-ESP32 3.3.11に固有の事項(ローカルsourceで確認)

`cores/esp32/USB.cpp`と`cores/esp32/esp32-hal-tinyusb.c`(Arduino-ESP32 3.3.11)。

- serial numberの既定は`__MAC__`で、efuse MACの12桁hexになる。同じboardでは全profileで同一
- `USB.webUSB(true)`は`bcdUSB`を`0x0210`へ引き上げる。BOS descriptorは常に返すが、MS OS 2.0 descriptor setへの応答は`WEBUSB_ENABLED`のときだけ行う
- **CDCが有効かつWebUSBが有効だと、device classを`0x02/0x02/0x00`へ強制する**(「Windows 10 will not recognize the CDC device if WebUSB is enabled and USB Class is not 2」というcomment)。この状態では`EF/02/01`を外れ、IADと`USB\COMPOSITE`の条件を満たさない
- MS OS 2.0 descriptor setはfunction subsetの`bFirstInterface`が0で固定されている。WinUSBを結ぶvendor interfaceはinterface 0に置く必要がある
- `tinyusb_vendor_control_request_cb`は弱いsymbolで、`USBVendor`が上書きして`onRequest`へ渡す。WebUSBを無効にしたまま`USB.usbVersion(0x0210)`を設定し、MS OS 2.0 descriptor setをsketch側の`onRequest`から返せばdevice classを保てる見込み(要確認)
- TinyUSBのCDC templateはcontrol interfaceの`bInterfaceProtocol`を`0x00`にしている(ModemManagerの対象外。§2.2)

E013のProfile Bはこの強制変更の対象で、そのままではWindowsで親device全体にusbserが結ばれる危険がある。

## 2. Linux(kernel v6.12、systemd、ModemManagerのsourceで確認)

### 2.1 永続cacheはない

- `usb_new_device()` → `usb_enumerate_device()`が毎回descriptorを読み、`usb_get_configuration()`がconfiguration descriptor全体を取得する
- generic driverがconfigurationを選択してinterfaceを登録し、interface driverはその場で`id_table`にmatchする。`bcdDevice`は範囲matchに使える
- **device classが`0xFF`だと、`USB_INTERFACE_INFO`によるinterface class matchは行われない**。class driver(cdc-acm、usbhid)を使うprofileでは`bDeviceClass`を`0x00`か`0xEF`にする
- VID:PIDで引く表は`drivers/usb/core/quirks.c`、`drivers/hid/hid-quirks.c`、udev hwdb(`usb:vXXXXpYYYYdZZZZ...`の`d`が`bcdDevice`)で、いずれも静的。新規のVID:PIDには関係しない
- reset/resume時にはkernelが記憶しているdescriptorと比較し、異なれば「device firmware changed」として再列挙する。物理的な抜き差しなしのfirmware切替も新deviceとして扱われる

### 2.2 CDC ACM

- cdc-acmはcontrol interface(class 02、subclass 02、protocol 0または1〜6)にbindする。IADや`bDeviceClass`は参照しない
- functionごとにunion descriptorが必要。なければ3 endpointの単一interface、またはcall management descriptorで代替し、どれもなければ`-ENODEV`
- `ttyACM<n>`の番号は最小の空き番号で、接続順に依存する。安定名は`/dev/serial/by-id/<bus>-<vendor>_<model>_<serial>-if<NN>`で、`NN`はcontrol interface番号。**profileでinterface配置が変わると接尾辞が変わる**
- ModemManagerはcdc-acmのportについて、class 2/subclass 2/protocol 1〜6以外を禁止し、protocol 1〜6でも単一ttyACMだけのdeviceは他portがなければ許可しない。TinyUSBのprotocol `0x00`は対象外。明示的に外すudev tagは`ID_MM_DEVICE_IGNORE=1`

### 2.3 vendor-specific、HID、権限

- class `0xFF`のinterfaceにはVID:PID指定なしでbindする標準driverがない。libusbから直接claimできる
- usbhidはclass 03の全interfaceにbindし、hid-genericが他のHID driverに取られなかったdeviceを受けてhidrawを含めて接続する
- systemdは汎用のhidraw / USB向け`uaccess` ruleを持たない。一般userからのhidraw / libusbアクセスにはVID:PID指定のudev ruleが必要

### 2.4 構成変更時の注意

- ttyACM番号は不安定。`by-id`を使い、`-ifNN`がprofileで変わることをclient側で吸収する
- udevは`remove`でsymlinkを消すため、旧構成のsymlinkは残らない
- 同一serialで構成を変えたときにLinuxが誤認識する報告は見つからなかった

## 3. macOS(Apple文書、Apple公開source、libusbで確認)

### 3.1 永続cacheはない

- I/O Registryは「ディスクに保存されず、起動間で保持されない。起動ごとに構築されメモリに置かれる」とAppleの文書に明記されている
- 接続ごとにdevice descriptorから`IOUSBHostDevice`が作られ、matchingが走る。configurationが選ばれるとinterfaceごとに`IOUSBHostInterface`が作られ、再びmatchingが走る
- matching keyには`idVendor`、`idProduct`、`bcdDevice`、`bConfigurationValue`、`bInterfaceNumber`、`bInterfaceClass/SubClass/Protocol`、`bDeviceClass`等が使え、`idVendor + idProduct + bcdDevice`が最高score。これは静的なdriver personalityの話で、cacheではない
- 起動時に別driverが先にmatchした場合、より良いmatchでも後から奪えない(Apple QA1076)。同一接続中の話で、接続をまたぐ永続化ではない
- descriptorのcacheに関するmacOSの報告は見つからなかった

### 3.2 CDC ACM

- device nodeは`/dev/cu.usbmodem<serial><interface番号>`。serialにASCII英数字以外があるとserialを捨てて位置由来の名前になる
- 同じ接尾辞が既に使われていると、ttyのunit番号で置き換える(`IOSerialBSDClient::getUniqueTTYSuffix`)。同一serialの2台や、接尾辞が衝突する複数functionは名前が不定になる
- composite中のCDC ACMはIADが必要(OS X 10.7以降で対応。Microchipのreadme)。`bDeviceClass`の要件についてはAppleの明文は見つからなかった

### 3.3 HIDとvendor-specific

- HIDとCDCのinterfaceはAppleのdriverが掴む。libusbはkernel driverのないinterfaceにはroot不要で使えるが、**interface単位のdetachはできない**。device全体のcaptureにはentitlement(`com.apple.vm.device-access`)かroot権限が必要。vendor-specific interfaceだけがlibusbで自由に使える経路になる
- Input Monitoring権限は「systemが直接理解するdevice(keyboard、mouseなど)」に要求される(Apple DTSの発言)。vendor-defined usage pageが免除されるという正式文書は見つからなかった

## 4. 設計への含意

1. **`bcdDevice`はprofile分離の手段にならない。** Windowsのinstance identityに含まれず、効くのはMS OS 1.0 descriptorの`osvc` cacheだけ。firmware版として通常どおり使えばよい
2. **Windowsで効くのはPID、serial、interface番号の3つ。** 同一PIDに留まるなら「interface番号と機能の対応を固定し、末尾追加だけ許し、親は常にcompositeにする」が候補。単機能profileを同じPIDへ入れる場合は別個体としてしか共存できない
3. **標準classだけでも回避できない。** 問題はclass別のcacheではなくdevnodeの再利用なので、HID/CDCだけの構成でも単機能↔compositeや`MI_nn`の機能変更は同じ結果になる
4. **LinuxとmacOSの制約は命名と権限。** `by-id`の`-ifNN`、`cu.usbmodem<serial><if>`、ModemManagerのprotocol条件、hidraw/libusbのudev rule、macOSでのinterface単位detach不可
5. **Arduino-ESP32 3.3.11でcompositeにvendor interfaceとCDCを同居させるなら、WebUSBを使わずにMS OS 2.0を返す実装が要る。** そうしないとdevice classが`02/02/00`になる
6. Windows 11の実挙動(特に末尾追加と、Windows 10以降で挙動が変わったという示唆)は[E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md)で測る

## 根拠

### Windows

- [Microsoft: Standard USB identifiers](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/standard-usb-identifiers)
- [Microsoft: Instance IDs](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/instance-ids)
- [Microsoft: Enumeration of USB composite devices](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/enumeration-of-the-composite-parent-device)
- [Microsoft: USB FAQ(serial numberとPDO)](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-faq--introductory-level)
- [Microsoft: USB device registry entries(`usbflags`、instance IDの例)](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-device-specific-registry-settings)
- [Microsoft: Adding a PnP device to a running system](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/adding-a-pnp-device-to-a-running-system)
- [Microsoft: How Windows selects a driver package](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/how-windows-selects-a-driver-for-a-device)
- [Microsoft: Microsoft OS descriptors(`osvc`)](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors)
- [Microsoft: USB ContainerIDs(`bcdDevice`増加の要求)](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-containerids-in-windows)
- [Microsoft OS 2.0 Descriptors Specification(docx)](https://download.microsoft.com/download/3/5/6/3563ED4A-F318-4B66-A181-AB1D8F6FD42D/MS_OS_2_0_desc.docx)
- [Microsoft: USB Interface Association Descriptor](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-interface-association-descriptor)
- [Microsoft: Support for interface collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/support-for-interface-collections)
- [Microsoft: Enumeration of interfaces not grouped in collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/enumeration-of-interfaces-not-grouped-in-collections)
- [Microsoft: USB driver installation based on compatible IDs(usbser)](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-driver-installation-based-on-compatible-ids)
- [Microsoft: External naming of COM ports](https://learn.microsoft.com/en-us/previous-versions/windows/drivers/serports/external-naming-of-com-ports)
- [Microsoft: COM port database](https://learn.microsoft.com/en-us/previous-versions/windows/drivers/serports/com-port-database)
- [Microsoft: HID top-level collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/top-level-collections)
- [Microsoft: HIDClass hardware IDs](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/hidclass-hardware-ids-for-top-level-collections)
- [Microsoft: composite child instance IDの実例(Azure Virtual Desktop USB redirection)](https://learn.microsoft.com/en-us/azure/virtual-desktop/redirection-configure-usb)
- [OSR NTDEV: USB Composite Device Troubles(2011)](https://community.osr.com/t/usb-composite-device-troubles/41415)
- [OSR NTDEV: HID report descriptor caching / reenumerating HID PDOs(2017)](https://community.osr.com/t/hid-report-descriptor-caching-reenumerating-hid-pdos/53717)
- [virtio-win: vioinput child instance IDにreport descriptorのhashを使う修正](https://github.com/virtio-win/kvm-guest-drivers-windows/commit/a2943d499bfb8ad91f191062506fe927ba624f6b)
- [TinyUSB example `usb_descriptors.c`(PID per interface combination)](https://github.com/raspberrypi/tinyusb/blob/pico/examples/device/webusb_serial/src/usb_descriptors.c)
- [PJRC cores `usb_desc.h`](https://github.com/PaulStoffregen/cores/blob/master/teensy4/usb_desc.h) / [PJRC forum: many axis joystick p.6](https://forum.pjrc.com/index.php?threads/many-axis-joystick.23681/page-6)
- [ArduPilot: USB IDs](https://ardupilot.org/dev/docs/USB-IDs.html)
- [libwdi: WCID devices(`usbflags`キーは削除されない)](https://github.com/pbatard/libwdi/wiki/WCID-Devices)

### Linux

- [linux v6.12 `drivers/usb/core/hub.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/core/hub.c) / [`config.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/core/config.c) / [`generic.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/core/generic.c) / [`driver.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/core/driver.c) / [`quirks.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/core/quirks.c) / [`sysfs.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/core/sysfs.c)
- [linux `Documentation/driver-api/usb/persist.rst`](https://github.com/torvalds/linux/blob/v6.12/Documentation/driver-api/usb/persist.rst)
- [linux `drivers/usb/class/cdc-acm.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/class/cdc-acm.c)
- [linux `drivers/hid/usbhid/hid-core.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/hid/usbhid/hid-core.c) / [`hid-generic.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/hid/hid-generic.c) / [`hid-quirks.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/hid/hid-quirks.c)
- [systemd `man/hwdb.xml`](https://github.com/systemd/systemd/blob/main/man/hwdb.xml) / [`udev-builtin-usb_id.c`](https://github.com/systemd/systemd/blob/main/src/udev/udev-builtin-usb_id.c) / [`60-serial.rules`](https://github.com/systemd/systemd/blob/main/rules.d/60-serial.rules) / [`70-uaccess.rules.in`](https://github.com/systemd/systemd/blob/main/rules.d/70-uaccess.rules.in)
- [ModemManager `src/mm-filter.c`](https://gitlab.freedesktop.org/mobile-broadband/ModemManager/-/blob/main/src/mm-filter.c) / [ModemManager overview(`ID_MM_DEVICE_IGNORE`)](https://gitlab.freedesktop.org/mobile-broadband/ModemManager/-/blob/main/docs/reference/api/ModemManager-overview.xml)
- [hidapi `udev/69-hid.rules`](https://github.com/libusb/hidapi/blob/master/udev/69-hid.rules)

### macOS

- [Apple QA1076: USB driver matching](https://developer.apple.com/library/archive/qa/qa1076/_index.html)
- [Apple: IOUSBHostMatchingPropertyKey](https://developer.apple.com/documentation/iousbhost/iousbhostmatchingpropertykey)
- [Apple: I/O Kit Fundamentals — The I/O Registry](https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/IOKitFundamentals/TheRegistry/TheRegistry.html)
- [Apple: USB Device Interface Guide — USB overview](https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/USBBook/USBOverview/USBOverview.html)
- [apple-oss-distributions IOSerialFamily `IOSerialBSDClient.cpp`](https://github.com/apple-oss-distributions/IOSerialFamily/blob/main/IOSerialFamily.kmodproj/IOSerialBSDClient.cpp)
- [esp-idf issue #18516(`cu.usbmodem`命名とserial文字種)](https://github.com/espressif/esp-idf/issues/18516)
- [Microchip MCP2200/MCP2221 CDC Mac readme(IADは10.7以降)](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2200_MCP2221_CDC_Mac_Readme.txt)
- [libusb FAQ(macOSのkernel driver detach)](https://github.com/libusb/libusb/wiki/FAQ) / [libusb PR #911](https://github.com/libusb/libusb/pull/911)
- [Apple: `com.apple.vm.device-access` entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.vm.device-access)
- [Apple Developer Forums: Input Monitoringの範囲(DTS)](https://developer.apple.com/forums/thread/804793)

### ローカルsource

- Arduino-ESP32 3.3.11 `cores/esp32/USB.cpp`、`cores/esp32/esp32-hal-tinyusb.c`、`libraries/USB/src/USBVendor.cpp`
- TinyUSB(Arduino-ESP32 3.3.11同梱)`src/device/usbd.h`の`TUD_CDC_DESCRIPTOR`
