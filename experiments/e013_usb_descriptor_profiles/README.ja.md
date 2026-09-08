# E013 USB descriptor profile分離

状態: **計画**

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

同じsketchを`OEP_USB_TEST_PROFILE=1`または`2`でbuildする。Profile BではMicrosoft OS 2.0 descriptorが指すinterface 0をUSBVendorにするため、USBVendor、HID、CDCの順に登録する。

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
uv run python e013_usb_descriptor_profiles/firmware_tool.py build --profile all
uv run python e013_usb_descriptor_profiles/firmware_tool.py upload --profile a --port <UPLOAD_PORT>
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
