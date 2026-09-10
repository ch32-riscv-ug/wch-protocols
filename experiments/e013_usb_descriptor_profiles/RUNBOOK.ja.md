# E013 ESP32-S3 USB profile試験手順

状態: **実行前手順**。実験計画は[README.ja.md](README.ja.md)を参照する。

⚠ E013は2026-09-10に中断した([README](README.ja.md)「中断」節)。この手順は[E062](../e062_usb_same_identity_layout_change/README.ja.md)が機材、identity、準備、build、書込み、Linux基準記録の部分を引き継ぐ。sequenceとclean state手順はE062側に書く。

## 1. 使用する機材

- この実験専用のESP32-S3 board 1枚
- native USB D−/D+に接続されたdata対応USB端子
- firmware upload用UART/USB-Serial端子
- data対応USB cable 2本。upload用とnative USB用
- Linux PC
- Windows 11 PC、またはUSB pass-throughを固定できるWindows 11 VM

既存の常設peer対は使わない。試験中は同じESP32-S3、同じUSB serial number、同じproduct stringを維持する。boardを交換するとWindowsのdevice instance条件も変わるため、A→B→A試験にならない。

書込み端子とnative USB端子が別にあるboardが扱いやすい。一つしかない場合もBOOT/RESET操作で書き換えられるが、HID-onlyを書いた後に通常のserial upload portが消えることを前提に復旧方法を先に確認する。

## 2. USB identityと安全条件

試験firmwareは次を提示する。

| 項目 | 値 |
|---|---|
| VID:PID | `1209:0001` |
| manufacturer | `Open Embedded Probe (TEST ONLY)` |
| product | `OEP USB Profile Test` |
| Profile A `bcdDevice` | `0001` |
| Profile B `bcdDevice` | `0002` |

`1209:0001`はprivate test専用であり、第三者へfirmware binaryを書き渡さない。販売、製造、通常配布には使用しない。PCに同じtest VID:PIDを使う別deviceがある場合は、ESP32-S3のserial numberを`--serial`で必ず指定する。

## 3. software準備

repositoryの`experiments`へ移動する。

```console
cd <wch-protocols>/experiments
uv sync
arduino-cli version
```

必要なPython packageは`pyusb`、`libusb-package`、`hidapi`、`pyserial`で、`uv sync`が導入する。

Firmware側の`sketch.yaml`がArduino-ESP32 3.3.11と追加index URLを固定している。`arduino-cli compile`は不足するCoreをprofile専用領域へ自動取得するため、`core update-index`や`core install`は実行しない。

Linuxでは一般userからUSB interfaceを開けるよう、実験中だけ次のudev ruleを用意する。system policyに応じて管理者が設置する。

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="1209", ATTR{idProduct}=="0001", TAG+="uaccess"
KERNEL=="hidraw*", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="0001", TAG+="uaccess"
```

rule更新後はudev ruleをreloadし、native USB cableを抜き差しする。test program自体をrootで実行しない。

WindowsではMicrosoft USBViewを用意する。Profile Bのvendor-specific interfaceはfirmwareが提示するMicrosoft OS 2.0 descriptorによってWinUSBへbindする想定であり、Zadig等で手動置換してから試験を始めない。自動bindingに失敗した事実も結果に含める。

## 4. firmwareのbuild

Linux、Windowsとも`arduino-cli`を直接実行する。Profile A/Bは独立したsketchであり、それぞれの`sketch.yaml`を自動的に使用する。

```console
arduino-cli compile e013_usb_descriptor_profiles/firmware/profile_a
arduino-cli compile e013_usb_descriptor_profiles/firmware/profile_b
```

初回だけ、profileに固定されたCoreのdownloadに時間がかかることがある。Profile A/Bの`.ino`はprofile番号だけを保持し、USB実装は共通のheader-only local libraryを使用する。

## 5. firmwareの書込み

`<UPLOAD_PORT>`にはnative USB側ではなく、書込みに使うUART/USB-Serial portを指定する。`compile --upload`により、選んだsketchをbuildしてそのまま書き込む。

Linux:

```console
arduino-cli compile --upload --port /dev/ttyUSB0 e013_usb_descriptor_profiles/firmware/profile_a
arduino-cli compile --upload --port /dev/ttyUSB0 e013_usb_descriptor_profiles/firmware/profile_b
```

Windows PowerShell:

```powershell
arduino-cli compile --upload --port COM5 e013_usb_descriptor_profiles/firmware/profile_a
arduino-cli compile --upload --port COM5 e013_usb_descriptor_profiles/firmware/profile_b
```

書込み後、native USB側を一度抜き差しする。書込み用USB-Serial portとProfile BのCDC portを取り違えない。

## 6. Linux基準記録

run directoryを一度だけ作り、以後の出力をそこへ保存する。

```console
mkdir -p _runs/E013_<UTC>_linux
```

### 6.1 Profile A初回

Profile Aを書き込み、native USBを接続する。

```console
lsusb -d 1209:0001
lsusb -v -d 1209:0001 > _runs/E013_<UTC>_linux/profile_a_initial_lsusb.txt
uv run python e013_usb_descriptor_profiles/usb_profile_test.py \
  --output _runs/E013_<UTC>_linux/profile_a_initial.json \
  inspect --expect-profile a
uv run python e013_usb_descriptor_profiles/usb_profile_test.py \
  --output _runs/E013_<UTC>_linux/profile_a_echo.json \
  test --profile a
```

### 6.2 Profile B

Profile Bを書き込み、native USBを抜き差しする。

```console
lsusb -v -d 1209:0001 > _runs/E013_<UTC>_linux/profile_b_lsusb.txt
uv run python e013_usb_descriptor_profiles/usb_profile_test.py \
  --output _runs/E013_<UTC>_linux/profile_b.json \
  inspect --expect-profile b
uv run python e013_usb_descriptor_profiles/usb_profile_test.py \
  --output _runs/E013_<UTC>_linux/profile_b_echo.json \
  test --profile b
```

CDC自動検出が曖昧な場合だけ、Profile Bが作ったportを明示する。

```console
uv run python e013_usb_descriptor_profiles/usb_profile_test.py \
  test --profile b --cdc-port /dev/ttyACM0
```

### 6.3 Profile Aへ戻す

Profile Aへ戻し、同じ確認を行う。CDC device nodeとvendor interfaceが消え、HID echoだけが成功することを確認する。

## 7. Windows profile分離試験

PowerShellまたは端末から`experiments`へ移動し、run directoryを作る。

```powershell
$Run = "_runs/E013_<UTC>_windows"
New-Item -ItemType Directory -Force $Run
```

### 7.1 Profile A初回

1. Profile Aを書き込む
2. native USBを接続する
3. USBViewでdevice descriptorとconfiguration descriptorを保存する
4. 次を実行する

```powershell
uv run python e013_usb_descriptor_profiles/usb_profile_test.py `
  --output "$Run/profile_a_initial.json" inspect --expect-profile a
uv run python e013_usb_descriptor_profiles/usb_profile_test.py `
  --output "$Run/profile_a_echo.json" test --profile a
powershell -ExecutionPolicy Bypass -File `
  e013_usb_descriptor_profiles/collect_windows.ps1 `
  -OutputPath "$Run/profile_a_pnp.json"
```

### 7.2 Profile B

1. Profile Bを書き込む
2. native USBを抜き差しする
3. Device Managerで次を確認する
   - vendor-specific childがWinUSBを使用する
   - HID childがHID driverを使用する
   - CDC childが`usbser.sys`を使用しCOM portを持つ
4. USBViewのdescriptorを保存する
5. 次を実行する

```powershell
uv run python e013_usb_descriptor_profiles/usb_profile_test.py `
  --output "$Run/profile_b.json" inspect --expect-profile b
uv run python e013_usb_descriptor_profiles/usb_profile_test.py `
  --output "$Run/profile_b_echo.json" test --profile b
powershell -ExecutionPolicy Bypass -File `
  e013_usb_descriptor_profiles/collect_windows.ps1 `
  -OutputPath "$Run/profile_b_pnp.json"
```

CDC portが複数候補になった場合は`--cdc-port COMx`を指定する。

### 7.3 Profile Aへ戻す

Profile Aを書き戻して同じ記録を行う。Device Managerを「非表示のデバイスの表示」にした状態も記録し、現在presentなdeviceと過去のdevnodeを区別する。過去devnodeがregistryに残ること自体ではなく、現在のProfile AへVendor/CDCが誤ってbindされることを失敗とする。

### 7.4 反復条件

- A→B→Aを最低3巡
- Windows再起動後
- 同じboardを別の物理USB portへ接続
- 可能ならA/Bを二台同時接続。二台のserial numberは固有にする

各段階で`collect_windows.ps1`、USBView、echo testの三つを保存する。

## 8. 判定

合格には次のすべてが必要である。

- Profile Aが`bcdDevice=0001`、HIDのみで列挙され、HID echoが通る
- Profile Bが`bcdDevice=0002`、Vendor + HID + CDCで列挙され、三つのechoが通る
- A→B→Aで現在のprofileに存在しないinterfaceが誤ってbindされない
- Windows再起動とUSB port変更後も同じ結果になる
- Linux raw descriptorが意図したinterface順、class、endpointを示す
- cache削除、driver手動置換、接続順依存を通常手順として要求しない

失敗した場合もraw記録を消さず、どの条件で失敗したかを実験READMEの結果へ追記する。
