# E063 ESP32-P4 USB 2.0 OTG HS の列挙ゲート

状態: **完了 — High-Speedで列挙、consoleも同時に生きる**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E013](../e013_usb_descriptor_profiles/README.ja.md)(中断) / 根拠: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段、[harness-channels](../../references/harness-channels.ja.md) §物理IF

## 問い

**Arduino-ESP32 3.3.11でESP32-P4のUSB 2.0 OTG HS portをdeviceとして列挙でき、negotiateする速度はHSかFSか。そのときUSB-Serial-JTAG(FS)のconsole経路は生き続けるか。**

## 仮説

列挙し、**HSでnegotiateし、USB-Serial-JTAGは生き続ける**。

根拠はcore sourceで確認済みである。

1. `cores/esp32/esp32-hal-tinyusb.c`の`init_usb_hal()`はP4に対して`.otg_speed = USB_PHY_SPEED_HIGH`を設定する。
2. 同じく`tinyusb_driver_install()`はP4で`tinit.speed = TUSB_SPEED_HIGH`とし、**rhport 0ではなく1**に対して`tusb_init()`を呼ぶ。
3. P4の`soc_caps.h`は`SOC_USB_OTG_SUPPORTED=1`、`SOC_USB_SERIAL_JTAG_SUPPORTED=1`、`SOC_USB_OTG_PERIPH_NUM=2`を宣言する。OTGとSerial-JTAGは別peripheralであり、排他ではない。
4. P4用precompiled libsの`sdkconfig`は`CONFIG_TINYUSB_ENABLED=y`で、CDC / HID / MSC / VENDOR / DFU / MIDI / AUDIO / VIDEOがすべて有効である。
5. `HWCDC` classは`SOC_USB_SERIAL_JTAG_SUPPORTED`だけで compile条件が閉じており、global instanceの生成だけが`ARDUINO_USB_MODE`に依存する。したがって**USB-OTG modeのbuildでもsketchが自前で`HWCDC`を宣言すればconsoleを保持できる**。

これらはすべて「そう書いてある」の水準であり、実機でdeviceが立ち上がる証拠ではない。この実験はそこを埋める。

## 反証条件

次のいずれかが起きたら、仮説をそのまま採用しない。

1. buildが通らない(P4でUSBクラスがcompileできない)。ESP-IDF直か別coreへ分岐する
2. buildは通るがWindowsにもLinuxにもdeviceが現れない
3. 列挙するが**FS(12 Mbps)でnegotiateする**。HS前提のdownload帯域の見積りを書き直す
4. OTG HSを有効にするとUSB-Serial-JTAGのconsoleまたは書込みが失われる。P4でも単一port運用になり、[E013](../e013_usb_descriptor_profiles/README.ja.md)の制約が復活する
5. 列挙するがdataが流れない(echoが返らない)
6. firmwareの自己申告(`tud_speed_get()`)とhost側の観測が食い違う

## 方法

### identity

| 項目 | 値 | 理由 |
|---|---|---|
| VID:PID | **`1209:0002`** | pid.codesのprivate test範囲(`0x0001`–`0x0010`)。**`1209:0001`は[E062](../e062_usb_same_identity_layout_change/README.ja.md)が使う**ので、E063がWindowsのdevnode cacheを汚さないよう別の値を取る |
| manufacturer | `Open Embedded Probe (TEST ONLY)` | E013と同じ |
| product | `OEP P4 HS Enumerate Test` | |
| serial | coreがMAC由来で自動生成(`30:ED:A0:E3:14:78`) | 同じchipのUSB-Serial-JTAGも同じ文字列を名乗る。**1 chipから出る2 deviceがserialを共有する**ことの確認も兼ねる |

`1209:0002`はprivate test専用であり、第三者へfirmware binaryを渡さない。

### 構成

- **console** = USB-Serial-JTAG(FS、`303a:1001`)。sketchが自前で`HWCDC`を宣言して保持する。pytest harnessはこちらに繋ぐ
- **試験device** = OTG HS上のCDC ACM 1本。単機能構成から始める
- buildは`USBMode=default`(USB-OTG / TinyUSB)、`CDCOnBoot=default`(無効)。`CDCOnBoot`を有効にすると`Serial`がOTG側へ移り、consoleとDUTが同じportになるので使わない

### 段階

| 段 | 手順 | 見るもの |
|---|---|---|
| P1 | build → 書込み → console起動 | buildの可否、consoleが生きているか(反証条件 1・4) |
| P2 | firmwareに`tud_mounted()`と`tud_speed_get()`を問う | device側から見たmount状態と速度(反証条件 2・3) |
| P3 | Windows側のdevnodeを列挙する | instance ID、hardware ID、compatible ID、driver、service、COM番号(反証条件 2・6) |
| P4 | Windows側からCOM portを開いてechoする | dataが流れるか(反証条件 5) |
| P5 | P2をもう一度問う | rx/txのcounterが進んでいるか(反証条件 6) |
| P6 | Windows側でUsbTreeView(USB Device Tree Viewer)のdumpを取る | **host側から見た速度**、full descriptor、`usbflags` registry key(反証条件 3・6) |

Windows側の観測と操作は、WSLから`powershell.exe`を起動して行う。`usbipd`でWSLへ引き込むとWindowsの列挙結果が隠れるため、**HS portはWindowsに繋いだまま**にする。

**P6を置く理由**: Windowsはbus speedをPnP propertyとして公開しない(`Get-PnpDeviceProperty`の全keyを実機で確認し、speedに当たるものが無いことを確かめた)。USBViewもこのhostに無い。UsbTreeViewは`Device Bus Speed`とdescriptor全文を出すので、host側から速度を言えるのはこの経路である。dumpは手動で取り、この節に貼る。

### 掃引しないもの

この実験は掃引を持たない。単一構成が立ち上がるかどうかのゲートである。

## 対象外

別の問いとして分ける。

- **throughput**(`p4-hs-bulk-rate` / `p4-hs-cdc-vs-bulk` / `p4-batch-download` / `p4-stream-throughput`)。列挙が通ってから測る
- **descriptor identityの永続性**([E062](../e062_usb_same_identity_layout_change/README.ja.md))。同一PIDでinterface構成を変える試験はE062の範囲
- composite構成、CDCの本数、endpoint予算(`p4-cdc-budget-hs`)
- MS OS 2.0 descriptorとWinUSBのdriverless bind(`p4-winusb-msos2`)
- PARLIO captureとの同時動作(`p4-usb-vs-drain`)
- Linux側のdriver bind、device node、`by-id`名の詳細。P6で見るのは**速度とdescriptorだけ**で、driver層の挙動は[E062](../e062_usb_same_identity_layout_change/README.ja.md)の範囲
- macOS

## 必要な環境

- ESP32-P4 board 1枚。**このbenchの個体は`esp32-p4-30eda0e31478`**(chip rev v1.3、400 MHz、flash 16 MB)で、[E014](../e014_p4_parlio_internal_capture/README.ja.md)〜[E061](../e061_p4_drain_core_split/README.ja.md)で使った`esp32-p4-e8f60ae0aa24`とは**別個体**である。過去のP4実験の値をこの個体の値として引き継がない
- **2本のUSB線**: USB-Serial-JTAG(console / 書込み)と OTG HS(試験対象)
- console portはusbipdでWSLへattach済み。stable alias `/run/board-identify/by-id/esp32-p4-30eda0e31478`
- **OTG HS portはWindows 11に接続したまま**にする
- Arduino-ESP32 3.3.11 / P4 ESリリースのprecompiled libs
- WSLから`powershell.exe`が起動できること(確認済み)
- 外部配線、target、logic analyzerは不要

**flash sizeは16 MB**なので、既存のP4 profileの`FlashSize=32M`をそのまま使わない。PSRAMは搭載量が未確認なので`PSRAM=enabled`でbuildし、`psramFound()`の申告で確かめる。boot時にPSRAM未検出でabortする場合は`PSRAM=disabled`へ落として記録する。

## ベンチ種別

**一時・配線なし**。2 portを繋いだ状態を維持する。HS側の接続先(Windows / WSL)を変えたら記録する。

## 記録する数値

| 項目 | 記録 |
|---|---|
| build | 成否、program storage / dynamic memoryの使用量、core版 |
| chip | model、revision、flash size、`psram_found`、`psram_size`、`SOC_USB_OTG_PERIPH_NUM` |
| build option | `ARDUINO_USB_MODE`、`ARDUINO_USB_CDC_ON_BOOT` |
| device側 | `tud_mounted()`、`tud_speed_get()`の生値、VID、PID、serial、rx/txのbyte数 |
| Windows側 | present devnodeのinstance ID、hardware ID、compatible ID、driver、service、FriendlyName、status、COM番号 |
| echo | 送信payload、受信payload、往復の成否 |
| console | OTG HS有効時にconsoleが生きているか、書込みが通るか |
| UsbTreeView(P6) | `Device Bus Speed`、`bcdUSB`、device / configuration / IAD / interface / endpoint descriptor(特にbulkの`wMaxPacketSize`)、string descriptor、`usbflags` registry key |

`tud_speed_get()`の生値はTinyUSBの`tusb_speed_t`で、`0 = FULL` / `1 = LOW` / `2 = HIGH`。**数値をそのまま記録し、解釈は表で与える。**

繰り返しは3回(§7-3)。boardのre-plugを挟まず、reset 3回で取る。

## 完了条件

次に答えられたら完了とする。

1. **列挙するか / しないか**(yes/no)
2. **HSかFSか**(device側の申告とWindows側の観測の両方で)
3. **USB-Serial-JTAGのconsoleと書込みが生きているか**(yes/no)
4. **dataが流れるか**(echoの往復)

Windows側の記録が機材の都合で取れなくても、device側の1・2・3・4が埋まれば完了とし、残りは未決に書く。

## 影響

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段 — 「download経路がUARTに限られる」「P4のUSB 2.0 HS device経路は配線を変えない限り測れない」の解消。**6 Mbaud前提のdownload時間の表**はthroughput実測(別実験)で置き換える
- [harness-channels](../../references/harness-channels.ja.md) §物理IFの帯域表 — すべてFS前提(`native USB CDC ×1 = ~1 MB/s`)。HSが成立するなら前提が変わる
- [probe-feasibility-gates](../../references/probe-feasibility-gates.ja.md) Gate 2 — 試験boardの選択肢にP4が入るか
- [E062](../e062_usb_same_identity_layout_change/README.ja.md) — 書込み口と試験口が物理的に別なP4は、単一portのS3より試験boardとして扱いやすい可能性がある

**仕様のstatusは動かさない。** これはboardの能力の測定であって、protocolの検証ではない。

---

## 結果

状態: **完了 — 列挙する / High-Speed / consoleは生きる / dataは流れる**(2026-09-12)

採用run: `_runs/E063_20260912T023520Z_default` / `T023605Z` / `T023646Z`。全run一覧は`_runs/E063_20260912T*`(14件)。

### 4つの完了条件

| # | 問い | 答え | 根拠 |
|---|---|---|---|
| 1 | 列挙するか | **する** | Windowsに`USB\VID_1209&PID_0002\0`が出た。device側も`tud_mounted()=1` |
| 2 | HSかFSか | **High-Speed** | device側`tud_speed_get()=2`が10 runすべてで同値。UsbTreeViewの`Device Bus Speed : 0x02 (High-Speed)`、`bcdUSB 0x200`、全endpointの`wMaxPacketSize=0x200`(512 B) |
| 3 | consoleと書込みは生きるか | **生きる** | 同じ実行の中でbuild → 書込み → monitorがUSB-Serial-JTAG経由で通った |
| 4 | dataは流れるか | **流れる** | Windows側でCOM8を開いた3往復がすべて一致。device側counterも`rx=21 tx=36 lines=3`で整合 |

### buildと環境

| 項目 | 値 |
|---|---|
| build | 成功。program storage 390,804 B (29%)、dynamic memory 72,108 B (22%) |
| chip | ESP32-P4 rev 1.3(`ESP.getChipRevision()`=103)、2 core、400 MHz |
| flash / PSRAM | flash **16 MiB** / PSRAM **32 MiB**(`psram_found=1`、`psram_size=33554432`) |
| `SOC_USB_OTG_PERIPH_NUM` | 2 |
| build option | `ARDUINO_USB_MODE=0`、`ARDUINO_USB_CDC_ON_BOOT=0` |

### Windowsから見えたもの

| 項目 | 値 |
|---|---|
| 親 | `USB\VID_1209&PID_0002\0` / class `USB` / service **`usbccgp`** / `USB Composite Device` |
| `BusReportedDeviceDesc`(親) | `OEP P4 HS Enumerate Test` |
| 子 | `USB\VID_1209&PID_0002&MI_00\7&2B36746D&0&0000` / class `Ports` / service **`usbser`** / **COM8**(`\Device\USBSER000`) |
| `BusReportedDeviceDesc`(子) | `TinyUSB CDC` |
| compatible ID(子) | `USB\Class_02&SubClass_02&Prot_00` ほか |
| 接続位置 | `Port_#0002.Hub_#0003`、親は`USB\VID_1A40&PID_0101`(外部USB 2.0 hub)。**hubを介してもHS** |
| registry | `HKLM\SYSTEM\CurrentControlSet\Control\usbflags\120900020100`(`osvc: 00 00`)が作られた |

descriptorはIAD composite(`bDeviceClass=0xEF/0x02/0x01`)、interface 2本(CDC control + CDC data)、endpoint 3本(interrupt IN `0x85`、bulk OUT `0x03`、bulk IN `0x84`)で**3本とも`wMaxPacketSize=512`**。**driverの追加なしにCOM portが生えた。**

UsbTreeViewの全文dumpはこの節の証拠として取得済み(`Device Bus Speed`、descriptor全文、string descriptor、`usbflags`)。

### 事実

1. **Arduino-ESP32 3.3.11はESP32-P4のUSB 2.0 OTG HSをTinyUSB deviceとして立ち上げる。** core sourceの読み(`tinyusb_driver_install()`がP4でrhport 1 / `TUSB_SPEED_HIGH`)は実機の挙動と一致した。
2. **negotiateする速度はHigh-Speedである。** device側の申告とhost側の観測が一致し、endpointも512 Bで開いた。`bMaxPacketSize0`だけは64 Bである。
3. **USB-Serial-JTAGのconsoleとOTG HSのdeviceは同時に成立する。** 1つのchipから`303a:1001`(FS)と`1209:0002`(HS)の**2つのUSB deviceが同時にWindowsへ列挙される**。usbipdからもそれぞれ別busid(`3-1` / `3-2`)として見える。**書込み口を失わずにHS側の構成を変えられる。**
4. **単機能CDCのつもりの構成が、Windowsではcomposite(`usbccgp`)として扱われる。** Arduino-ESP32のUSB stackはIADを付けて`0xEF/0x02/0x01`を名乗るため、interfaceが1 functionでもcomposite扱いになり、COM portは子devnode(`&MI_00`)側に生える。
5. **P4ではUSB serial stringが`"0"`になる。** `cores/esp32/USB.cpp`は`USB_SERIAL`の既定を`CONFIG_IDF_TARGET_ESP32S3`のときだけ`"__MAC__"`とし、**それ以外のtargetでは`"0"`**とする。結果、Windowsのdevice instance IDは`USB\VID_1209&PID_0002\0`になった。**同じfirmwareを焼いた2枚目のP4は、Windows上で同一のdevice instanceを名乗る。**
6. **Windowsはbus speedをPnP propertyとして公開しない。** `Get-PnpDeviceProperty`の全keyを実機で確認したが、speedに当たるものは無い。速度を言うにはUsbTreeView(またはUSBView)のdumpが要る。
7. **Device Qualifier Descriptorの取得が`ERROR_GEN_FAILURE`になる。** HS deviceでは本来必須のdescriptorだが、Windowsはこれを許容して列挙とdata転送を続けた。
8. **interrupt IN endpointが512 B / `bInterval=1`で開いている。** HSの`bInterval=1`は125 usなので、notification endpointだけで大きなbandwidthを予約している。CDCの本数を増やすときのendpoint予算に効く。
9. `Get-PnpDeviceProperty`をkeyごとに呼ぶと1 deviceあたり数十秒かかる。**一括取得に変えて56秒 → 10秒**になった。
10. **console経路(usbip)は不安定な側である。** harnessの不具合を直した後の13回中10回pass。失敗3回の内訳は、**consoleに1行も出ないまま15秒でtimeout**が1回(usbip経由のCDCで20秒級の遅延が起きうる)、**usbipdのattachが落ちた状態での書込み失敗**が2回。**HS側の観測はこの3回のいずれにも関与していない。** timeoutを45秒に上げた後は3回連続でpassした。

### 候補

- **consoleを`HWCDC`の自前宣言で保持する型**(採用)。`USBMode=default`でもUSB-Serial-JTAGをconsoleに使えるので、DUTと監視経路を分離できる
- **Windows側の観測をWSLから`powershell.exe`で駆動する型**(採用)。`collect_windows.ps1`が列挙とechoの両方を1つのJSONで返す
- **速度とdescriptorの証拠はUsbTreeViewのdump**(採用)。PnP propertyでは取れない
- identity汚染を避けるため**実験ごとにpid.codes test範囲の別PIDを取る**(採用)。E062の`1209:0001`は使っていない

### 未決

- **throughputは未測定** `—`。HSが成立したので、`p4-hs-bulk-rate` / `p4-hs-cdc-vs-bulk` / `p4-batch-download` / `p4-stream-throughput`へ進む。[p4-logic-analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段の6 Mbaud前提の表は、実測が出るまで置き換えない
- **serial `"0"`を上書きするか。** `USB.serialNumber()`にchip UIDを入れれば個体を分けられるが、**Windowsのdevice instance IDが変わる**ので[E062](../e062_usb_same_identity_layout_change/README.ja.md)のS3条件と同じ論点になる。[ecosystem-any-hardware](../../references/ecosystem-any-hardware.ja.md) §4.5の「chip UIDをserial stringに」の方針と、coreの既定が食い違っている
- **Device Qualifier無応答の影響。** Windowsは許容したが、他のhost(macOS、Linuxの一部、USB hub chip)が同じとは限らない
- **interrupt EPの512 B / 125 us予約が、CDCを複数本にしたときのendpoint予算にどう効くか**(`p4-cdc-budget-hs`)
- **外部hubを介さない直結でも同じか。** 今回はhubを1段挟んでいる
- **usbip経由CDCの20秒級遅延の原因**。benchの再現性に直接効くが、この実験の問いではない
- **Linux側の列挙**は未取得 `—`。HS portをWSLへ引き込むとWindowsの記録が消えるので、必要になった段で分けて取る

### 反映

- [LEDGER](../LEDGER.ja.md) §1にE063を採番、§3に記録を追加
- [p4-logic-analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段の「配線を変えない限り測れない」を解消済みとして更新
- 仕様([../../protocols/](../../protocols/))のstatusは動かさない(boardの能力の測定であり、protocolの検証ではない)
