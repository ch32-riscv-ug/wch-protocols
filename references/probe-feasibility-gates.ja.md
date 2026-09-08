# Probe protocol 実現性ゲート

状態: **実現性調査**。基本コンセプトとは分離し、成立条件と未確認事項を管理する。

## 現時点の判定

| ゲート | 判定 | 要点 |
|---|---|---|
| project名と公開場所 | **未決定** | PID申請前に恒久的なidentityとsource URLが必要 |
| MCU非依存でPIDを取得できるか | **候補あり** | Openmokoとpid.codesが候補 |
| 一つのPIDを複数hardwareで使えるか | **見込みあり・要確認** | OpenmokoはhardwareごとにPIDを取らないよう明記 |
| 第三者の準拠実装も同じPIDを使えるか | **未確認** | 割当団体へ利用範囲の確認が必要 |
| `bcdDevice`のprofile数は足りるか | **問題なし** | `0000`を予約しても9,999 profile |
| Windowsで異なるprofileが安全に共存するか | **未実証** | 実機試験が必要 |

## Gate 0 — project名と公開場所

PIDはproject名、owner、source URLと結び付けて登録されるため、申請前にprojectのidentityと恒久的な公開場所を決める必要がある。

現在の`wch-protocols`は検討場所として利用できるが、名称がWCHに限定されて見えるため、MCU非依存protocolの公開場所には適さない。申請時までに、中立的なproject名の専用repositoryへ切り出す案を基本とする。

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
- CH32 RISC-V向けreference implementationが動作する
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

- HID-only構成とcomposite構成に異なる`bcdDevice`を割り当てる
- 同じVID:PIDのままWindowsへ交互・同時接続する
- descriptor、driver binding、COM port、再接続が混線しないことを確認する
- revisionなしhardware IDによる誤bindingがないことを確認する
- LinuxとmacOSでも同じ構成を確認する

この試験を通過するまで、`bcdDevice`方式は**有力な設計案**であり、確定した前提とはしない。

## 推奨する判断

1. PID候補は**Openmokoを第一候補、pid.codesを第二候補**とする。
2. 申請前に「一つのPIDを複数descriptor profileと第三者実装で共有する」利用方法を説明し、可否を確認する。
3. `bcdDevice`は意味を持たないBCD連番とし、機能の逆引きには使わない。
4. descriptor profile registryはfirmware作成と適合確認に使い、通常のclient動作には使わない。
5. Windows実機試験をPID申請前の必須ゲートにする。
