# Probe protocol 実現性ゲート

状態: **実現性調査**。基本コンセプトとは分離し、成立条件と未確認事項を管理する。

## 現時点の判定

| ゲート | 判定 | 要点 |
|---|---|---|
| project名と公開場所 | **一部確定** | `Open Embedded Probe`とGitHub Organizationを確保。canonical repositoryは未作成 |
| MCU非依存でPIDを取得できるか | **候補あり** | Openmokoとpid.codesが候補 |
| 一つのPIDを複数hardwareで使えるか | **見込みあり・要確認** | OpenmokoはhardwareごとにPIDを取らないよう明記 |
| 第三者の準拠実装も同じPIDを使えるか | **未確認** | 割当団体へ利用範囲の確認が必要 |
| `bcdDevice`でprofileを分離できるか | **否定** | Windowsのdevice instance identityに`bcdDevice`は含まれない。一次資料とコミュニティ観測で否定([調査結果](usb-host-descriptor-persistence.ja.md)) |
| 同一PIDで異なるprofileがWindowsで安全に共存するか | **未実証** | 分離手段の候補はPID、serial、interface番号の固定。[E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md)で判定する |
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

## Gate 2 — 同一PIDで異なるdescriptor profileを共存させられるか

当初は、Windowsが`idVendor`、`idProduct`、`bcdDevice`からrevision付きhardware IDを生成することを根拠に、`bcdDevice`をprofile分離の鍵にする案を立てていた。2026-09-10の再調査でこの根拠は否定された。詳細は[USB descriptor変更に対するhostの挙動](usb-host-descriptor-persistence.ja.md)にある。

一次資料で確認した事実:

- Windowsのdevice instance IDは`USB\VID_xxxx&PID_yyyy\<serial>`(serialがなければport由来)で、composite childは`USB\VID_xxxx&PID_yyyy&MI_nn\...&00nn`である。`bcdDevice`はhardware ID(`REV_rrrr`)にだけ現れ、instance IDには入らない
- WindowsはEnumキーの有無で「以前installされたdeviceか」を判定し、新規のdeviceだけをゼロから構成する
- `usbflags\VVVVPPPPRRRR`が`bcdDevice`単位なのは、MS OS 1.0 string descriptorの応答cache(`osvc`)である。MS OS 2.0にはこの単位のcacheはない

コミュニティ観測(Microsoft文書には記述がない):

- 同じinstanceが異なるinterface構成で再出現すると、既存devnodeとdriverが再利用され、driver選択はやり直されない。単機能→compositeでusbccgpが載らず、uninstallだけが解決した報告がある。`bcdDevice`を変えても解消しなかった報告がある
- TinyUSB、Teensy、ArduPilotは「interfaceの組合せごとに別PID」を採用している
- Windows 10では再現しなかったという報告もあり、Windows 11の実挙動は実測が必要である

したがって、同一PIDで複数profileを共存させる鍵は`bcdDevice`ではなく、**device instance identity(PID、serial)とinterface番号(`MI_nn`)** である。

### 分離手段の候補

| 手段 | Windowsでの効き方 | 代償 |
|---|---|---|
| **interface番号と機能の対応を固定し、末尾追加だけ許す** | 既存childのdevnodeが同じ機能を保つので、再利用されても害にならない見込み。親は常にcomposite(usbccgp)にする | 単機能profile(low-speed HIDなど)を同じPIDの同一個体で切り替えられない。削除したfunctionはghost devnodeとCOM番号予約を残す |
| **profileごとにPIDを分ける** | 別device IDなので完全に分離する。developerの通例 | PIDを複数申請する。割当団体への説明が増える |
| **serialにprofile識別を含める** | 別instanceになりdriver選択をやり直す | 同じ個体が別deviceに見える。COM番号やper-device設定が引き継がれない |
| `bcdDevice`を分ける | **効かない**。MS OS 1.0 descriptorを使う場合の`osvc` cache分離のみ | — |

LinuxとmacOSには永続cacheがなく、どの手段でも再認識される。制約は命名(`/dev/serial/by-id`の`-ifNN`、`/dev/cu.usbmodem<serial><if>`)とModemManagerのAT probe(CDC `bInterfaceProtocol` 1〜6)である。

### 通過条件

- 上記の手段のうち採用するものを、E062の実測結果に基づいて一つ決める
- 採用した手段で、単機能相当のprofileとcomposite、compositeへの末尾追加が、Windows 11で混線せずに列挙・通信できる
- Linuxでraw descriptorと各interfaceの基本通信が成立する
- cache削除、driver手動置換、接続順依存を通常手順として要求しない

### 実験

[E013](../experiments/e013_usb_descriptor_profiles/README.ja.md)は`bcdDevice`による分離を問いにしていたため中断し、問いを立て直した[E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md)へ引き継いだ。E062は同一VID:PID・同一serialで、単機能↔composite、末尾追加、interface番号の機能入替、`bcdDevice`のみ変更、serialのみ変更の各条件を測る。firmwareとhost toolはE013のものを拡張する。

### 失敗した場合

どの手段でも同一PID内でprofileを安定して分離できない場合、または末尾追加だけでは必要なprofileを表現できない場合は、descriptor構成ごとに別PIDを申請する方針へ戻す。cache削除を通常手順にしたり、接続順へ依存させたりして通過扱いにはしない。

## Gate 3 — descriptor profileをどう管理するか

profileは外部に見えるUSB構成の定義であり、識別子はregistryの番号である。`bcdDevice`はこの番号を運ぶ場所として使わない。firmware版として通常どおり使い、descriptor構成が変わってもそれだけで変える必要はない。

- hostはUSB descriptorの値から機能を逆引きしない
- hostは実際のUSB descriptorから利用可能なUSB経路を判断する
- probeの機能はprotocol上のcapability discoveryで判断する
- 外部に見えるdescriptor構成が変わる場合だけ、新しいprofileを起こす
- 一度公開したprofileのinterface番号と機能の対応は変更しない
- 一度公開した番号は変更・再利用しない

registryは「番号から機能を調べる表」ではなく、firmware作成時と適合確認時に、出力するdescriptorがどのprofileに対応するかを確認する管理表である。通常のclient動作はregistryに依存しない。

最低限、各entryは次を持つ。

| 項目 | 内容 |
|---|---|
| profile番号 | 連番 |
| profile名 | 人間向けの短い識別名 |
| descriptor定義 | interface番号ごとのfunction、class、endpoint、IAD、HID reportの正本 |
| 分離手段 | Gate 2で採用した手段に基づく、このprofileが使うPIDまたはserial規則 |
| 状態 | draft / active / retired |

新しいprofileが必要になるdescriptor差分の境界は別途定義する。少なくともclass、interface番号、endpoint、HID reportなど、OSの列挙・binding・永続化へ影響する変更を対象にする。

### OSごとの試験段階

Windowsだけでproject全体のUSB成立を宣言することはできない。試験を二段階に分ける。

| 段階 | OS | 目的 |
|---|---|---|
| 現在の企画終了gate | Windows 11 | 同一identityで構成を変えたときのdevnode再利用、driver binding、COM portの挙動を判定し、分離手段を決める |
| 現在の企画終了gate | Linux | raw descriptorの比較と、HID・vendor-specific・CDCの基本送受信を確認する |
| PID申請前の正式検証 | Windows 11、Linux、macOS | 公開するprofileについて列挙、再接続、同時接続、各interfaceの通信を確認する |

Linuxはraw descriptorを取得しやすく、Windowsで問題が起きたときにfirmwareのdescriptor不良とWindows固有のbinding問題を切り分ける基準になる。このため現在のgateにも含める。

macOSも最終的には必須とするが、最初の判定を止める条件にはしない。利用できる実機があれば同時に試験し、なければcanonical repositoryへ移動後、PID申請前までに閉じる。Windows 10や複数Linux distributionは互換性を広げる追加試験とし、最初の必須matrixには含めない。

根拠:

- [USB descriptor変更に対するhostの挙動](usb-host-descriptor-persistence.ja.md)(一次資料の一覧を含む)
- [Microsoft: Standard USB identifiers](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/standard-usb-identifiers)
- [Microsoft: Instance IDs](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/instance-ids)
- [Microsoft: USB device registry entries](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-device-specific-registry-settings)
- [Microsoft: USB composite interface collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/support-for-interface-collections)

## Gate 4 — 現在のrepositoryで企画を終了する条件

`wch-protocols`で企画を広げ続ける期間は、Gate 2のWindows実験結果とLinuxの基本試験結果が出るまでとする。これは、共通PIDと自由な機能構成を両立する中心仮説を、文書ではなく実機で判断するためである。

現在のrepositoryで完了させるもの:

- core conceptと非目標
- PID割当経路とlicense方針
- descriptor profileと分離手段の管理案
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
3. `bcdDevice`はprofile分離の手段にしない。firmware版として通常どおり使い、hostは機能の逆引きに使わない。
4. descriptor profile registryはfirmware作成と適合確認に使い、通常のclient動作には使わない。
5. Windows、Linux、macOSの実機試験をPID申請前の必須ゲートにする。
6. 現在のrepositoryでの企画はWindowsのprofile分離、Linuxの基本試験と方針決定までとし、完成実装は専用organizationへ移す。
