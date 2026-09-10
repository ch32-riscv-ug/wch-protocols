# E013 USB descriptor profile分離

状態: **中断 — 前提を机上調査で反証(2026-09-10)**。問いを立て直した[E062](../e062_usb_same_identity_layout_change/README.ja.md)へ引き継ぐ

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md)

## 問い

**ESP32-S3が同一VID:PIDと同一product identityを保ったまま、`bcdDevice`だけを変更してHID-onlyとHID + vendor-specific + CDC ACM × 1を提示したとき、Windowsは二つのdescriptor profileを混線せず認識できるか。Linuxでもdescriptorと三経路の基本通信が成立するか。**

## 仮説

成立する。

- Windowsは`bcdDevice`を`REV_xxxx`付きhardware IDへ使用する
- Arduino-ESP32 3.3.11は`USB.firmwareVersion()`をdevice descriptorの`bcdDevice`へ設定する
- Arduino-ESP32のHID、USBVendor、USBCDCは同一TinyUSB deviceへ登録できる
- WebUSBを有効にするとArduino-ESP32はMicrosoft OS 2.0 descriptorを提示し、先頭のvendor-specific interfaceをWinUSBへbindできる

ただしWindowsはrevisionなしのhardware IDも生成するため、`bcdDevice`の違いだけでdriver bindingとdescriptor cacheが常に分離するとは未確認である。

## 反証条件

次のいずれかが起きたら、現在の仮説をそのまま採用しない。

1. Profile AまたはBをArduino-ESP32 3.3.11でbuildできない
2. device descriptorのVID:PIDまたは`bcdDevice`が指定値と異なる
3. Profile A→B→Aの切替後に、以前のinterface、driver、COM portが誤って残る
4. Profile BのHID、vendor-specific、CDCのいずれかが列挙されない
5. 列挙されても、対応するhost APIからopen・送受信できない
6. Windows再起動またはUSB port変更後に認識結果が変わる
7. Linuxで取得したraw descriptorが意図したprofile定義と異なる

## 方法

### Profile

| 項目 | Profile A | Profile B |
|---|---|---|
| VID:PID | `1209:0001` | `1209:0001` |
| `bcdDevice` | `0001` | `0002` |
| product | `OEP USB Profile Test` | 同一 |
| serial | ESP32-S3 chip ID由来 | 同じ物理boardでは同一 |
| interface | vendor-defined HID | USBVendor + vendor-defined HID + CDC ACM × 1 |

`1209:0001`はpid.codesがprivate test専用に予約する値である。実験室外へ配布、販売、製造するfirmwareには使用しない。

### Firmware

Profile番号だけが異なる二つの薄いsketchをbuildし、USB実装は同じheader-only local libraryから取り込む。各sketchの`sketch.yaml`がArduino-ESP32 3.3.11を固定するため、Coreの事前installは不要である。Profile BではMicrosoft OS 2.0 descriptorが指すinterface 0をUSBVendorにするため、USBVendor、HID、CDCの順に登録する。

各経路は固定payloadのechoを提供する。descriptorが見えるだけでは合格にせず、hostから送ったbyte列が同じ経路で戻ることを確認する。

### Windows

1. Profile Aを書き込み、USBViewとPowerShell snapshotを保存する
2. HID echoを実行する
3. Profile Bへ書き換え、同じnative USB portへ再接続する
4. USBViewとPowerShell snapshotを保存する
5. Vendor、HID、CDCのechoをすべて実行する
6. Profile Aへ戻し、古いVendor/CDC interfaceが残らないことを確認する
7. A→B→Aを複数回繰り返し、Windows再起動後と別USB portでも確認する
8. 可能なら二台へA/Bを別々に書き、固有serialで同時接続する

### Linux

1. Profile A/Bごとに`lsusb -v -d 1209:0001`とhost toolのJSON snapshotを保存する
2. HID echoを両profileで実行する
3. Profile BでVendorとCDC echoを実行する
4. A→B→Aでinterfaceとdevice nodeが期待どおり変わることを確認する

## 対象外

- 複数CDC。Arduino-ESP32側の対応完了後に別profile候補として扱う
- macOS。PID申請前の正式検証では必須だが、この最初の中心仮説の判定には含めない
- 正式なVID:PID。割当前なのでprivate test IDだけを使用する
- SWD、JTAG、OEP protocol本体
- USB throughputと長時間stress

## 必要な環境

- この実験専用のESP32-S3 board 1枚。既存の常設peer対は使用しない
- Arduino-ESP32 3.3.11
- firmware upload用UART/USB-Serial経路、またはBOOT操作可能なboard
- ESP32-S3 native USB D−/D+へ接続するdata cable
- Windows 11実機またはUSB pass-through可能な試験VM
- Linux host
- Windows USBView
- Python 3.10以上と`uv`

boardはGPIO19/20がnative USB D−/D+へ正しく配線され、data対応USB cableでPCへ接続できるものを使う。書込み用UART/USB-Serial端子とnative USB端子を同時に使えるboardが望ましい。D−/D+を通常GPIO配線として使用しない。

## ベンチ種別

**一時・専用機材**。既存の常設benchとは分離し、descriptor cache試験中は同じboard、cable、serial numberを維持する。

## 記録する値

| 項目 | 記録 |
|---|---|
| OS | edition、version、build、architecture |
| firmware | git revision、Arduino-ESP32 version、profile |
| USB device | VID、PID、`bcdDevice`、class、product、serial |
| interface | number、class/subclass/protocol、endpoint address/type/size |
| Windows | hardware ID、compatible ID、instance ID、driver、COM port、device path |
| Linux | `lsusb -v`、sysfs path、kernel driver、device node |
| 通信 | HID/Vendor/CDCごとのopen、送信payload、受信payload、結果 |
| sequence | 初回、A→B、B→A、再起動後、USB port変更後、同時接続 |

## 完了条件

WindowsとLinuxの全必須手順についてraw記録が残り、成功・失敗のどちらであっても次を判断できること。

- 一つのPIDと異なる`bcdDevice`で複数descriptor profileを運用できる
- 条件付きなら、必要な条件をprofile規則として列挙できる
- 成立しないなら、一PID一descriptorまたは複数PIDへ方針を戻せる

## 実行手順

詳細は[RUNBOOK.ja.md](RUNBOOK.ja.md)を参照する。最短の入口は次のとおり。

```console
cd experiments
uv sync
arduino-cli compile e013_usb_descriptor_profiles/firmware/profile_a
arduino-cli compile e013_usb_descriptor_profiles/firmware/profile_b
arduino-cli compile --upload --port <UPLOAD_PORT> e013_usb_descriptor_profiles/firmware/profile_a
uv run python e013_usb_descriptor_profiles/usb_profile_test.py inspect --expect-profile a
uv run python e013_usb_descriptor_profiles/usb_profile_test.py test --profile a
```

## 影響

- [Probe protocol実現性ゲート](../../references/probe-feasibility-gates.ja.md) Gate 2 / Gate 4
- [PID取得ロードマップ](../../references/pid-acquisition-roadmap.ja.md) Step 3
- [基本コンセプト](../../references/probe-product-concept.ja.md)の「一PID・複数profile」仮説

---

## 結果

未実行。

## 準備時の確認

2026-09-08、実機を接続せず次を確認した。これはUSB実験の結果ではない。

| 項目 | 結果 |
|---|---|
| Arduino CLI | 1.3.1 |
| Arduino-ESP32 | 3.3.11 |
| Profile A build | pass、flash 353,606 B、global 54,720 B |
| Profile B build | pass、flash 359,778 B、global 54,824 B |
| Python source | `py_compile` pass |
| Python依存 | `uv lock` / `uv sync`で解決 |
| device未接続の`inspect` | 空のdevice一覧を返し、誤って別deviceを操作しない |

未確認: firmware upload、実descriptor、interface順、Windows driver binding、HID/Vendor/CDC echo、A→B→A。

## 中断(2026-09-10)

実行前に、仮説の前提「Windowsは`bcdDevice`(`REV_`付きhardware ID)でdescriptor profileを分離できる」を一次資料とコミュニティ観測で再調査し、前提が成立しないと判断して中断した。計画欄は書き換えない。詳細は[USB descriptor変更に対するhostの挙動](../../references/usb-host-descriptor-persistence.ja.md)。

**事実**(机上調査。実機観測ではない)

1. Windowsのdevice instance IDは`USB\VID&PID\<serial>`で、`bcdDevice`は含まれない。`bcdDevice`は`REV_`付きhardware IDと、`usbflags`(MS OS 1.0 descriptorの応答cache)にだけ現れる。
2. 既存instanceが異なるinterface構成で再出現したとき、Windowsが既存devnodeとdriverを再利用してdriver選択をやり直さない観測が複数ある(OSR 2011、2017)。`bcdDevice`を上げても解消しなかった報告がある。Microsoft文書には記述がない。
3. 本実験のProfile A/Bは同一serial(MAC由来)なので、A→Bは「HID単機能のdevnodeにcompositeが再出現する」条件になり、`bcdDevice`の値に関係なく同じ結果になる見込み。仮説「`bcdDevice`が分離する」はこの設計では測れない。
4. Arduino-ESP32 3.3.11はCDCとWebUSBが同時有効だとdevice classを`0x02/0x02/0x00`へ強制し、MS OS 2.0 descriptorはWebUSB有効時のみ応答する。Profile Bは現状のfirmwareではIADと`USB\COMPOSITE`の条件を外れる。

**未決** → [E062](../e062_usb_same_identity_layout_change/README.ja.md): 同一identityで構成を変えたときのWindows 11の実挙動 / 単機能↔composite・末尾追加・`MI_nn`機能入替の各境界 / `bcdDevice`のみ変更とserialのみ変更の効果 / Arduino-ESP32でdevice classを`0xEF/0x02/0x01`に保ったままWinUSBを自動bindする実装。

**反映**: [probe-product-concept](../../references/probe-product-concept.ja.md)「USB descriptor profile」節、[probe-feasibility-gates](../../references/probe-feasibility-gates.ja.md) Gate 2 / Gate 3、[pid-acquisition-roadmap](../../references/pid-acquisition-roadmap.ja.md) Step 3 / 7、[harness-choices](../../references/harness-choices.ja.md) §2の訂正、[LEDGER](../LEDGER.ja.md) E013行。firmwareとhost toolはE062が拡張して使う。
