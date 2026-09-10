# E062 同一USB identityでのinterface構成変更

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E013](../e013_usb_descriptor_profiles/README.ja.md)(中断) / 根拠: [USB descriptor変更に対するhostの挙動](../../references/usb-host-descriptor-persistence.ja.md)

## 問い

**同一VID:PID・同一serialのESP32-S3が接続の間にinterface構成を変えたとき、Windows 11は次のどの切替で既存devnodeとdriverを再利用して誤ったbindingを残すか。(1)HID単機能↔composite、(2)compositeへの末尾function追加、(3)既存interface番号の機能入替。また、`bcdDevice`だけを変えた場合とserialだけを変えた場合に結果は変わるか。Linuxでは全条件で再認識されるか。**

## 仮説

Windows 11は(1)と(3)で既存devnodeを再利用してdriverを選び直さず、(2)では既存childを保ったまま新childを追加して動く。`bcdDevice`の値は結果に影響しない。serialを変えると新instanceになり、すべて再認識される。Linuxは全条件でdescriptorを読み直し、期待どおりのdriverとdevice nodeになる。

根拠は[調査結果](../../references/usb-host-descriptor-persistence.ja.md)§1.1〜1.5。Windowsのdevice instance IDに`bcdDevice`が含まれないことは一次資料で確認済み。既存devnodeの再利用はコミュニティ観測(OSR 2011、2017)で、Microsoft文書には記述がない。Windows 10では再現しなかったという報告が一件あるため、Windows 11の実挙動はこの実験で決める。

## 反証条件

次のいずれかが起きたら、現在の仮説をそのまま採用しない。

1. (1)でWindows 11が自動的にusbccgpへ切り替え、全interfaceが列挙・通信できる。devnode再利用による破綻は起きないことになり、同一PID内の自由度は仮説より大きい
2. (2)で新しいchildが列挙されない、または既存childのdriverが壊れる。「末尾追加は安全」が誤り
3. `bcdDevice`だけを変えた条件と変えない条件で結果が異なる。`bcdDevice`がidentityに効いており、E013の仮説を再検討する
4. serialを変えても旧状態が残る。instance identityの理解が誤り
5. Linuxで構成変更後にdriver bindやdevice nodeが期待と異なる
6. Linuxのraw descriptorが意図したprofile定義と異なる。firmware側の問題であり、Windowsの結果を判定に使わない

## 方法

### Profile

すべて`1209:0001`(pid.codesのprivate test用)、product string同一、`bDeviceClass`は単機能では`0x00`、compositeでは`0xEF/0x02/0x01`とする。serialは既定でESP32-S3のMAC由来。

| Profile | interface構成(番号: function) | 用途 |
|---|---|---|
| **A** | 0: vendor-defined HID | 単機能。E013のProfile Aと同じ |
| **B** | 0: vendor-specific(WinUSB) / 1: HID / 2–3: CDC ACM | composite。E013のProfile Bと同じ構成 |
| **B-rev** | Bと同一、`bcdDevice`のみ`0002` | `bcdDevice`の効果 |
| **B-ser** | Bと同一、serialに接尾辞`-P2` | serialの効果 |
| **C** | B + 4–5: 2本目のCDC ACM | 末尾追加 |
| **D** | 0: HID / 1: vendor-specific / 2–3: CDC ACM | interface 0と1の機能入替 |

Cの2本目がArduino-ESP32 3.3.11で構成できない場合は、2本目のvendor-specificまたはHIDで代替し、代替したことを記録する。末尾追加という条件は変えない。

### Sequence

各sequenceはclean stateから始める。clean stateは、試験deviceに関するdevnode(非表示を含む)をDevice Managerでuninstallし、`usbflags\12090001*`キーが存在しないことを確認した状態、またはVM snapshotの復元とする。どちらを使ったかを記録する。

| Seq | 手順 | 見るもの |
|---|---|---|
| S1 | A → B | 単機能→composite。親のdriver、child列挙、echo |
| S2 | A → B-rev | S1と同じ観測で、`bcdDevice`の差が結果を変えるか |
| S3 | A → B-ser | serialの差で新instanceになるか |
| S4 | B → C | 末尾追加。既存childの継続、新childの列挙とCOM番号 |
| S5 | B → D | interface 0/1の機能入替。各childのdriver |
| S6 | C → B | function削除。ghost devnodeとCOM番号の残り方 |
| S7 | S1とS4を、Windows再起動後と別USB portで再確認 | 永続性 |

各段階で、present/非表示を含む全devnodeのinstance ID、hardware ID、compatible ID、driver、service、COM port、`usbflags`キー、USBViewのdescriptor、HID/Vendor/CDCのecho結果を保存する。

### Linux

Profile A、B、C、Dごとに`lsusb -v`とhost toolのJSONを保存し、HID/Vendor/CDCのechoを行う。S1、S4、S5と同じ順で切り替え、`/dev/hidraw*`、`/dev/ttyACM*`、`/dev/serial/by-id/`の変化とkernel driverを記録する。

### Firmware / host tool

E013の`OepUsbDescriptorTest` library、`usb_profile_test.py`、`collect_windows.ps1`を拡張する。計画時点で分かっている必要な変更:

- profile C、Dと、`bcdDevice`・serial接尾辞のbuild-time指定
- **WebUSBを無効にしたまま`USB.usbVersion(0x0210)`でBOSを出し、MS OS 2.0 descriptor setを`USBVendor::onRequest`から返す。** Arduino-ESP32 3.3.11はCDCとWebUSBが同時有効だとdevice classを`0x02/0x02/0x00`へ強制するため、E013のProfile Bのままでは`0xEF/0x02/0x01`を保てない
- `collect_windows.ps1`に非表示devnode、`Enum\USB`キーの`Driver`/`Service`/`MatchingDeviceId`、`usbflags`キーの収集を追加
- `usb_profile_test.py`の`--expect-profile`をprofile C/Dへ拡張し、`bcdDevice`の一致は判定条件から外す(記録はする)

## 対象外

- macOS。PID申請前の正式検証で行う
- MS OS 1.0 descriptorと`usbflags\...\osvc`の挙動
- 複数台の同時接続。serialの効果はS3で単体確認する
- 正式なVID:PID、OEP protocol本体、throughput、長時間stress
- Windows 10、複数Linux distribution

## 必要な環境

E013と同じ。専用のESP32-S3 board 1枚、native USB用data cable、upload用UART/USB-Serial経路、Linux host、Windows 11実機またはUSB pass-throughを固定できる試験VM、USBView、Python 3.10以上と`uv`。

clean stateへ戻す手順が繰り返し必要になるため、**Windows 11はVM snapshotを復元できる環境が望ましい**。実機の場合はDevice Managerのuninstall手順を毎回記録する。

## ベンチ種別

**一時・専用機材**。E013と同じboard、cable、serial numberを維持する。S3だけserialを変える。

## 記録する値

| 項目 | 記録 |
|---|---|
| OS | edition、version、build、architecture |
| firmware | git revision、Arduino-ESP32 version、profile、`bcdDevice`、serial |
| clean state | 方法(uninstall / snapshot)、開始時のdevnode一覧が空であることの確認 |
| USB device | VID、PID、`bcdDevice`、device class、product、serial |
| interface | number、class/subclass/protocol、IAD、endpoint address/type/size |
| Windows | sequence、段階、present/非表示のdevnode一覧、instance ID、hardware ID、compatible ID、driver、service、`MatchingDeviceId`、COM port、`usbflags`キー |
| Linux | `lsusb -v`、sysfs path、kernel driver、device node、`by-id`名 |
| 通信 | HID/Vendor/CDCごとのopen、送信payload、受信payload、結果 |

## 完了条件

S1〜S7のraw記録が残り、(1)(2)(3)のそれぞれについてWindows 11が「再認識する / 既存devnodeを再利用して破綻する」のどちらかを言え、`bcdDevice`とserialの効果についてyes/noが言えること。Linuxで全profileのraw descriptorとechoが記録されていること。

いずれかのsequenceが機材の都合で実行できなくても、実行できた範囲で上記の表が埋まれば完了とし、残りは未決に書く。

## 影響

- [Probe protocol実現性ゲート](../../references/probe-feasibility-gates.ja.md) Gate 2の「分離手段の候補」から一つを選ぶ根拠。Gate 4の企画終了条件
- [基本コンセプト](../../references/probe-product-concept.ja.md)の「USB descriptor profile」節
- [PID取得ロードマップ](../../references/pid-acquisition-roadmap.ja.md) Step 3
- [harness-choices](../../references/harness-choices.ja.md) §2「CDCの個数は後から変えられるか」の表を実測で置き換える
- [USB descriptor変更に対するhostの挙動](../../references/usb-host-descriptor-persistence.ja.md) §1.5の「要実測」

## 実行手順

E013の[RUNBOOK.ja.md](../e013_usb_descriptor_profiles/RUNBOOK.ja.md)を基にする。機材、identity、software準備、build、書込み、Linux基準記録はそのまま使う。sequenceとclean state手順、拡張したprofileの分は、firmware / host tool実装時にこのdirectoryの`RUNBOOK.ja.md`へ書く(未作成)。

---

## 結果

未実行。
