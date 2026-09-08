# Probe protocol 実現性ゲート

状態: **実現性調査**。基本コンセプトとは分離し、成立条件と未確認事項を管理する。

## 現時点の判定

| ゲート | 判定 | 要点 |
|---|---|---|
| project名と公開場所 | **一部確定** | `Open Embedded Probe`とGitHub Organizationを確保。canonical repositoryは未作成 |
| MCU非依存でPIDを取得できるか | **候補あり** | Openmokoとpid.codesが候補 |
| 一つのPIDを複数hardwareで使えるか | **見込みあり・要確認** | OpenmokoはhardwareごとにPIDを取らないよう明記 |
| 第三者の準拠実装も同じPIDを使えるか | **未確認** | 割当団体へ利用範囲の確認が必要 |
| `bcdDevice`のprofile数は足りるか | **問題なし** | `0000`を予約しても9,999 profile |
| Windowsで異なるprofileが安全に共存するか | **未実証** | 実機試験が必要 |
| 現在のrepositoryでの企画終了条件 | **定義** | Windowsでprofile分離を判定し、Linuxでdescriptorと各interfaceの基本動作を確認する |

## Gate 0 — project名と公開場所

PIDはproject名、owner、source URLと結び付けて登録されるため、申請前にprojectのidentityと恒久的な公開場所を決める必要がある。

project名は **Open Embedded Probe** とし、GitHub Organizationとして[`Open-Embedded-Probe`](https://github.com/Open-Embedded-Probe)を確保した。現在の`wch-protocols`は引き続き検討場所として利用し、申請時までにcanonical repositoryを専用organizationへ公開する。

| 配置 | 評価 |
|---|---|
| protocolもprobeも現在のrepository | projectの範囲がWCH専用に見える |
| protocolだけ専用organization | identityは明確だが、申請時に実現性を示すsourceが分散する |
| protocolと最小referenceを専用organization、実用probeは現在のrepository | **推奨**。申請の自己完結性と既存開発の継続を両立できる |
| すべて専用organizationへ移動 | 構成は明快だが、既存projectとの統合まで移動する必要はない |

専用organization側は、protocol、descriptor profile、PID利用方針、適合確認と、動作する最小probe/client例を管理する。CH32向けの実用probeやapplication統合は現在のrepositoryに残し、canonical projectから参照する。

通過条件は、中立的な名称、安定したowner、canonical repository、license、最小referenceと実用実装の責任境界が確定することである。

## Gate 1 — PIDを取得できるか

MCU vendorのPID制度は特定vendorのMCUを使うことが条件になるため、本protocolには適さない。購入もしない前提では、MCUに依存しないOSS向け割当が候補になる。

### 候補

| 候補 | 条件 | このprojectとの適合 |
|---|---|---|
| **Openmoko `VID 0x1d50`** | 公開済みのFOSS firmware/software、またはopen hardware。GitHub PRで申請 | **有力**。個々の対応hardwareごとにPIDを取らず、一つのPIDを使うよう明記されている |
| **pid.codes `VID 0x1209`** | 公開repository、USB deviceのsource、認知されたOSS/OSHW license。GitHub PRで申請 | **候補**。softwareだけのprojectや複数hardwareを包含する利用は説明・確認が必要 |

Openmokoは、一つのsoftwareが対応するhardwareごとにPIDを要求せず、hardware情報はUSB descriptionへ含めるよう求めている。この方針は本protocolと近い。

一方、本projectが想定する範囲は、単一firmwareを複数boardで動かすだけでなく、第三者が独自に実装した準拠probeまで含む。この利用方法が一つのPID割当の範囲として認められるかは、どちらの候補についても事前確認が必要である。

また、これらはUSB-IFが公式に提供・endorseする割当制度ではない。USB-IF certificationやlogo使用を必要条件とする場合は別の判断が必要になる。

### 通過条件

このゲートは、次を満たした時点で通過とする。

- protocol、license、利用目的を公開する
- Raspberry Pi PicoとESP32-S3向けreference implementationが動作する
- project PIDを使用するprobe firmwareに、認知されたFOSS licenseを必須とする
- 一つのPIDで複数のMCUとdescriptor profileを扱う計画を申請時に説明する
- 第三者による準拠実装を含むPIDの利用範囲について、割当団体の了承を得る
- project専用のPIDがregistryへ登録される

候補資料:

- [Openmoko USB PID registry](https://github.com/openmoko/openmoko-usb-oui)
- [pid.codes: How to get a PID](https://pid.codes/howto/)
- [pid.codes: About](https://pid.codes/about/)

## Gate 2 — `bcdDevice`をprofile番号として使えるか

Windowsは`idVendor`、`idProduct`、`bcdDevice`からrevision付きhardware IDを生成し、`usbflags`もVID・PID・revision単位で保持する。このため、同じVID:PIDで異なるdescriptor構成を区別する値として`bcdDevice`を利用できる見込みがある。

ただしWindowsはrevisionを含まない汎用hardware IDも生成する。`bcdDevice`を変えれば常に完全分離できるとはまだ断定せず、driver bindingとdescriptor cacheを実機で確認する。

根拠:

- [Microsoft: USB device descriptors](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-device-descriptors)
- [Microsoft: Standard USB identifiers](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/standard-usb-identifiers)
- [Microsoft: USB device registry entries](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-device-specific-registry-settings)

### profile数

`bcdDevice`は16 bitだが、USB上は4桁のpacked BCDとして扱う。各桁に`0`から`9`を使えるため、有効な値は理論上`0000`から`9999`までの**10,000通り**になる。

`0000`を未割当として予約する場合、利用可能なのは**9,999 profile**である。

連番は二進数ではなくBCDとして進める。

```text
0001, 0002, ... 0009, 0010, 0011, ... 9999
```

`000A`のようにBCDでない値は割り当てない。

## Gate 3 — `bcdDevice`をどう管理するか

`bcdDevice`には機能の意味を符号化しない。単なる不透明なdescriptor profile番号として、registryから連番で割り当てる。

- hostは`bcdDevice`から機能を逆引きしない
- hostは実際のUSB descriptorから利用可能なUSB経路を判断する
- probeの機能はprotocol上のcapability discoveryで判断する
- firmware versionやMCUの種類では`bcdDevice`を変えない
- 外部に見えるdescriptor構成が変わる場合だけ、新しい値を割り当てる
- 一度公開した値は変更・再利用しない

registryは「番号から機能を調べる表」ではなく、firmware作成時と適合確認時に、出力するdescriptorに対応する番号を選ぶための管理表になる。通常のclient動作はregistryに依存しない。

最低限、各entryは次だけを持てばよい。

| 項目 | 内容 |
|---|---|
| `bcdDevice` | BCD連番 |
| profile名 | 人間向けの短い識別名 |
| descriptor定義 | 外部へ提示するUSB構成の正本 |
| 状態 | draft / active / retired |

新しい番号が必要になるdescriptor差分の境界は別途定義する必要がある。少なくともclass、interface、endpoint、HID report等、OSの列挙・binding・cacheへ影響する変更を対象にする。

### 通過条件

- HID-only構成とHID + vendor-specific + CDC ACM × 1のcomposite構成に異なる`bcdDevice`を割り当てる
- 同じVID:PIDのままWindowsへ交互・同時接続する
- descriptor、driver binding、COM port、再接続が混線しないことを確認する
- revisionなしhardware IDによる誤bindingがないことを確認する
- LinuxとmacOSでも同じ構成を確認する

この試験を通過するまで、`bcdDevice`方式は**有力な設計案**であり、確定した前提とはしない。

### 最初のWindows実験

ESP32-S3の現行USB device libraryで構成できる範囲を使い、二つの試験用profileを作る。

実験計画、firmware、host test、実行手順は[E013 USB descriptor profile分離](../experiments/e013_usb_descriptor_profiles/README.ja.md)に置く。

| 項目 | Profile A | Profile B |
|---|---|---|
| USB interface | HIDのみ | HID + vendor-specific + CDC ACM × 1 |
| VID:PID | 同一の試験値 | Profile Aと同一 |
| `bcdDevice` | `0001` | `0002` |
| product string | 同一 | 同一 |
| serial number | 同じ物理boardでは維持 | 同じ物理boardでは維持 |

複数CDCへの対応完了を待つ必要はない。この二構成だけで、interface数、class、endpoint構成が異なるprofileを一つのPIDで切り替えられるかを検証できる。

Windows 11のclean環境または試験用VMで、次を記録する。

1. Profile Aを接続し、device descriptor、hardware ID、interface、driver bindingを保存する
2. 同じESP32-S3をProfile Bへ書き換え、VID:PIDとserial numberを維持したまま再接続する
3. HID reportの送受信、CDC COM portのopenと送受信、vendor-specific interfaceのopenと転送を確認する
4. Profile Aへ戻し、誤ったinterfaceやdriver bindingが残らないことを確認する
5. AとBを複数回切り替え、Windows再起動後とUSB port変更後にも再確認する
6. 可能なら二台のboardへAとBを入れ、固有serial numberを与えて同時接続する

vendor-specific interfaceはdescriptorに現れるだけでは合格にしない。WinUSBへの自動binding、Microsoft OS descriptorの必要性、またはlibusb利用時の権限・driver条件を明記し、想定clientから実際にopenして転送できることを確認する。

Microsoftの資料では、Windowsは`bcdDevice`をrevision付きhardware IDの`REV_xxxx`へ使用する。一方、composite interfaceにはrevisionを含まないhardware IDも生成される。このため、hardware IDに違いが見えることだけではなく、interfaceごとのdriverとdevice pathが期待どおり更新されることを合格条件とする。

### OSごとの試験段階

Windowsだけでproject全体のUSB成立を宣言することはできない。試験を二段階に分ける。

| 段階 | OS | 目的 |
|---|---|---|
| 現在の企画終了gate | Windows 11 | `bcdDevice`変更時のcache、PnP identity、driver binding、COM portの分離を判定する |
| 現在の企画終了gate | Linux | raw descriptorの比較と、HID・vendor-specific・CDCの基本送受信を確認する |
| PID申請前の正式検証 | Windows 11、Linux、macOS | 公開するprofileについて列挙、再接続、同時接続、各interfaceの通信を確認する |

Linuxはraw descriptorを取得しやすく、Windowsで問題が起きたときにfirmwareのdescriptor不良とWindows固有のbinding/cache問題を切り分ける基準になる。このため現在のgateにも含める。

macOSも最終的には必須とするが、`bcdDevice`を用いる中心仮説の最初の判定を止める条件にはしない。利用できる実機があれば同時に試験し、なければcanonical repositoryへ移動後、PID申請前までに閉じる。Windows 10や複数Linux distributionは互換性を広げる追加試験とし、最初の必須matrixには含めない。

根拠:

- [Microsoft: Standard USB identifiers](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/standard-usb-identifiers)
- [Microsoft: USB composite interface collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/support-for-interface-collections)

### 失敗した場合

`bcdDevice`だけでは安定してprofileを分離できない場合、このprojectの中心仮説が一つ否定されたことになる。cache削除を通常手順にしたり、接続順へ依存させたりして通過扱いにはしない。次のいずれかへ設計を戻す。

- 一つのPIDで許可するdescriptor構成を一つに固定する
- descriptor構成ごとに別PIDを申請する
- Windowsが安定して識別できる別のprofile識別方法を調査する

## Gate 4 — 現在のrepositoryで企画を終了する条件

`wch-protocols`で企画を広げ続ける期間は、Gate 2のWindows実験結果とLinuxの基本試験結果が出るまでとする。これは、共通PIDと自由な機能構成を両立する中心仮説を、文書ではなく実機で判断するためである。

現在のrepositoryで完了させるもの:

- core conceptと非目標
- PID割当経路とlicense方針
- descriptor profileと`bcdDevice`管理案
- ESP32-S3によるProfile A/Bの最小USB firmware
- Windows上のdescriptor、hardware ID、driver binding、通信試験記録
- Linux上のraw descriptorと各interfaceの基本通信記録
- 実験結果を反映した「一PID・複数profile」の可否判断

ここではSWD/JTAGを含む完成probeを作らない。Windows gateに必要な最小firmwareとhost testだけを実験として置く。

Gate 4通過後は企画を無制限に広げず、次の作業を`Open-Embedded-Probe` organizationのcanonical repositoryへ移す。

- protocol specificationとtest vector
- header中心の共通library
- Pico / ESP32-S3 reference firmware
- Python clientとOpenOCD bridge
- SWD/JTAG/UART/GPIOの実装と実機試験
- descriptor profile registryとPID利用条件

通過条件は、Windows実験の結果が成功・失敗のどちらであっても記録され、その結果に応じてPID/profile方針を一つに決められることである。成功すること自体ではなく、中心仮説を未検証のまま次段へ持ち越さないことを企画終了条件とする。

## 推奨する判断

1. PID候補は**Openmokoを第一候補、pid.codesを第二候補**とする。
2. 申請前に「一つのPIDを複数descriptor profileと第三者実装で共有する」利用方法を説明し、可否を確認する。
3. `bcdDevice`は意味を持たないBCD連番とし、機能の逆引きには使わない。
4. descriptor profile registryはfirmware作成と適合確認に使い、通常のclient動作には使わない。
5. Windows、Linux、macOSの実機試験をPID申請前の必須ゲートにする。
6. 現在のrepositoryでの企画はWindowsのprofile分離、Linuxの基本試験と方針決定までとし、完成実装は専用organizationへ移す。
