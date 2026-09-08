# Openmoko / pid.codes PID申請調査

調査日: 2026-09-08

## 結論

MCUを固定しない本projectでも、PIDを申請できる可能性は高い。第一候補は **Openmoko (`VID 0x1d50`)**、第二候補は **pid.codes (`VID 0x1209`)** とする。

Openmokoには、専用hardwareを持たず、STM32とCH32を対象にCDC/HIDを提供するsoftware stackへ一つのPIDを割り当てた直接的な前例がある。pid.codesにも、一つのVID:PIDを多数のboardで共有するsoftware projectの前例がある。

ただし、次の利用方法まで明示的に認めた規則は確認できなかった。

- 同じPIDで複数のUSB descriptor profileを使う
- `bcdDevice`でprofileを分離する
- project本体以外が作る準拠実装にもPID使用を認める

したがって、通常のPID申請をいきなり出すのではなく、reference implementation公開後に、この3点を申請本文で説明して確認を求める必要がある。

## 比較

| 項目 | Openmoko | pid.codes |
|---|---|---|
| VID | `0x1d50` | 主に`0x1209` |
| 費用 | 無料 | 無料 |
| 申請方法 | registryへのGitHub PR | Web registry sourceへのGitHub PR |
| 公開条件 | 申請前にFOSS firmware/softwareまたはopen hardwareを公開 | 公開repositoryと認知されたOSS/OSHW license |
| software-only | 明示的に許容。実際の割当例あり | 可能だが、なぜend user個別申請でなくsoftware projectに必要か説明を求める場合がある |
| 複数hardware | hardwareごとにPIDを取らず一つを使うよう明記 | 実例はあるが、審査では公開されている対象範囲を確認される |
| 申請データ | `usb_product_ids.psv`の一行 | organization pageとPID page |
| 審査 | license、source、project範囲を手動確認 | license、source、対象device、registry形式を手動確認＋自動check |
| 今回との適合 | **高い** | **候補** |

どちらもUSB-IFが公式にendorseするPID割当制度ではない。USB-IF certificationやlogo使用を必要とする製品には、そのまま適用できるとは考えない。

protocolの利用条件とPIDの利用条件は分離する。project PIDを使用するprobe firmwareには、MIT等の認知されたFOSS licenseを必須とする。一方、PIDを使用しないtransport実装やclient applicationまで、PID割当条件だけを理由に同じlicenseへ拘束しない。
---

## 1. Openmoko

### 1.1 申請条件

Openmokoは、次のどちらかを満たすprojectへPIDを割り当てる。

- USB deviceがopen hardwareである
- device上のfirmware/softwareがFOSSである

softwareだけでも申請できるため、任意のMCUに実装できるprotocol/firmware projectと相性がよい。ただし、**申請前にFOSS licenseで公開済みであること**を繰り返し要求している。

README、LICENSE、source fileのcopyright/license表示も審査対象になる。hardwareを申請根拠に含める場合は、回路図、layout、library、製造dataまで公開状態を整える必要がある。

公式資料: [openmoko-usb-oui README](https://github.com/openmoko/openmoko-usb-oui)

### 1.2 申請手順

1. `openmoko/openmoko-usb-oui`をforkする。
2. `usb_product_ids.psv`で、申請時点の空きPIDを確認する。
3. 次の形式の一行を追加する。

```text
0x1d50 | 0x???? | [https://example.org/project Project description]
```

4. commit subjectに`request`を含める。
5. commit messageとPR本文へ次を記載する。
   - project名と短い説明
   - firmware/hardwareのlicense
   - source repositoryまたはproject page
   - registry/`lsusb`に表示する短いdescription
   - 必要なPID数と理由
6. upstreamへPRを送り、reviewで指定されたPIDや説明を修正する。

GitHubを使わない場合は、README記載のmail addressへ連絡できる。

空き番号はPR同士で競合する。番号を事前に固定せず、申請直前にregistryとopen PRを確認し、reviewで変更できる前提にする。

### 1.3 descriptionの注意

登録descriptionは`lsusb`等に表示される。READMEは、USB deviceであることは自明なので、説明中で単に「USB」を繰り返さず、deviceが何をするかを短く書くよう求めている。

本projectなら、特定MCU名ではなく、protocol product familyを表す名称にする。

```text
<Project name> extensible debug and measurement probe
```

project名はOpen Embedded Probeとした。実際の登録文字列は、申請時の実装範囲に合わせて最終決定する。

### 1.4 今回に近い前例

[XRobot/libxrの申請PR](https://github.com/openmoko/openmoko-usb-oui/pull/68)は、次の条件で`0x1d50:0x6199`を取得している。

- pure software project
- 専用hardwareなし
- STM32とCH32を対象
- CDCとHIDを実装するUSB device stack
- Apache-2.0

これは、MCUを一社へ固定しないsoftware/protocol projectでも申請可能であることを示す強い前例である。

またOpenmoko READMEは、softwareが対応する個々のhardwareごとにPIDを要求せず、**一つのPIDを使ってhardware情報をUSB descriptionへ含める**よう明記している。

### 1.5 reviewで実際に見られている点

[Amovlab Flycoreの申請PR](https://github.com/openmoko/openmoko-usb-oui/pull/79)では、reviewerが次を確認している。

- firmware-only申請であること
- 大規模repository内の各componentと第三者libraryのlicense
- project固有部分とupstream部分の境界
- fork専用PIDか、upstream後も同じidentityを維持するか
- 同時進行PRとのPID競合

単にrootへLICENSE fileを置くだけではなく、依存componentを含めたlicenseの説明と、PIDがどのproject範囲に帰属するかを明確にする必要がある。

### 1.6 運用状況

registryは現在も更新されており、2026年にも複数のmerge実績がある。一方、[XRobotのPR](https://github.com/openmoko/openmoko-usb-oui/pull/68)ではmaintainer応答停止への言及があり、申請からmergeまで約3か月かかっている。審査は有志による手動運用なので、所要時間は保証されない。

### 1.7 Openmokoへ確認する内容

申請本文で次を明記し、了承を得る。

1. PIDは一つのprobe protocol product familyへ割り当てる。
2. firmwareは複数のMCUへ移植される。
3. 同じPIDで複数のdescriptor profileを使い、`bcdDevice`で区別する。
4. 実際のprobe機能はprotocol上で列挙し、USB descriptorから決めない。
5. 第三者の準拠実装にも、projectの利用条件に従ってPID使用を認めたい。
6. PIDの再配布ではなく、一つのopen source projectに属する互換実装群として管理する。

特に5と6は既存READMEだけでは判断できないため、mergeをもって合意が得られた状態とする。

---

## 2. pid.codes

### 2.1 申請条件

pid.codesは、申請前に次を要求する。

- 公開source repositoryがある
- USB interfaceを持つdeviceの変更可能なPCB designまたはsource codeがある
- 認知されたopen source/open hardware licenseで公開されている
- repositoryにLICENSE fileがある

hardwareとsoftwareの両方をprojectに含める場合は、両方が適切なlicenseで公開されている必要がある。software-onlyの場合は、end userが個別にPIDを申請するのではなく、software projectへPIDを割り当てる理由を質問される可能性がある。

公式資料: [pid.codes: How to get a PID](https://pid.codes/howto/)

### 2.2 申請手順

1. `pidcodes/pidcodes.github.com`をforkする。
2. [VID 0x1209の一覧](https://pid.codes/1209/)で空きPIDを選ぶ。
3. project ownerのorganization entryがなければ`org/<owner>/index.md`を追加する。

```yaml
---
layout: org
title: <owner name>
site: https://example.org/
---
<owner/project organization description>
```

4. `1209/<PID>/index.md`を追加する。

```yaml
---
layout: pid
title: <device family name>
owner: <owner slug>
license: <recognized OSS/OSHW license>
site: https://example.org/project
source: https://example.org/source
---
<hardware, firmware, protocol and intended use>
```

5. PR本文でproject、hardware、firmware、license、PIDが必要な理由を説明する。
6. checkとreviewへ対応し、競合した場合は別の空きPIDへ変更する。

`0x0000–0x0fff`はtesting等のcommon task用、`0x1000–0x1fff`は元のVID owner用に予約されているため、通常申請では選ばない。

申請はPRの到着順で扱われる。同じ番号を別PRが先に取得した場合は選び直す。

### 2.3 project pageに必要な内容

`title`はDevice Managerや`dmesg`に現れる名称を意識する。`owner`はorganization entryと一致させ、`source`はUSB deviceのsourceへ直接到達できるURLにする。

本文には少なくとも次を書く。

- protocolとprobe familyの目的
- reference hardwareとfirmware
- USB class/descriptorの概要
- license
- source/build手順
- 一つのPIDを複数hardwareで利用する理由
- `bcdDevice`によるdescriptor profile管理
- PIDを利用できる実装の範囲

### 2.4 今回に近い前例

[ArduPilot `0x1209:0x5741`](https://pid.codes/1209/5741/)は、software projectが一組のVID:PIDを多数のboardで共有する前例である。

ただしArduPilotは、Windows上でsingle-endpoint CDC ACMとdual-endpoint CDC ACMを区別するため、二つのPIDを使用している。これは、USB descriptor/driver構成が異なる場合に別PIDを使った前例であり、本projectの`bcdDevice`方式が自動的に認められる根拠にはならない。

また、[pid.codes PR #1184](https://github.com/pidcodes/pidcodes.github.com/pull/1184)では、公開hardware sourceが一variant分しか確認できなかったため、登録対象をそのvariantだけに限定するようreviewされている。申請時点で存在しない実装を広く包含する説明より、動作するreference implementationと公開済みの適用範囲を示す方が通りやすい。

### 2.5 制度上の注意

pid.codes自身が、USB-IFからsupport・endorseされておらず、使用するVIDがUSB-IFのdeveloper情報でobsolete/invalidとして掲載されていると説明している。ただしpid.codesは、OSがそのVIDを拒否する兆候はないとの立場を取っている。

この点は通常の自作・OSS device利用と、USB-IF certificationを必要とする製品を分けて判断する必要がある。

公式説明: [pid.codes: About](https://pid.codes/about/)

### 2.6 運用状況

repositoryは2026年にもmergeされている。ただし2026-09-08時点でopen PRが多数あり、[PR #1184](https://github.com/pidcodes/pidcodes.github.com/pull/1184)は申請からmergeまで約5か月を要した。review待ち期間は計画に含める。

### 2.7 pid.codesへ確認する内容

Openmokoと同じ6点に加え、次を明示する。

- hardware product一機種ではなく、open protocolとreference implementationへの割当を求める理由
- end userごとのPID取得では相互運用とPID節約を実現できないこと
- descriptor profileを無制限に増やさず、project registryと適合条件で管理すること
- 申請時点で動作するprofileと、将来profile追加の扱い

---

## 3. 申請前に揃えるもの

どちらへ申請する場合も、先にproject名とcanonical repositoryを確定する。protocolのcanonical repositoryは、MCU非依存であることが伝わる専用organizationの下に置くのが望ましい。

すべてのprobe実装をそのorganizationへ移す必要はない。推奨構成は次のとおりである。

```text
dedicated organization / canonical project
  ├─ protocol specification
  ├─ USB descriptor profile registry
  ├─ PID use policy
  ├─ conformance tests
  ├─ minimal reference probe
  └─ minimal reference client

current WCH-related repository
  ├─ practical CH32 probe implementation
  └─ ch32rv / tool integration
```

専用organization側のminimal referenceは、完成品probeではなく、protocol、descriptor profile、capability discoveryが実際に動くことを示す申請用の基準実装とする。実用probeは現在のrepositoryで発展させ、canonical projectから対応実装としてlinkする。

これにより、PID申請先はcanonical repositoryだけでprojectのidentity、利用規則、実現性を確認でき、同時にWCH固有の実装詳細を汎用protocolへ持ち込まずに済む。

その上で、次を公開する。

| 必要物 | 最低限示す内容 |
|---|---|
| project identity | MCU vendorに依存しない名称と短い説明 |
| canonical repository | PID registryから恒久的に参照するsource URL |
| project README | 問題、共通PIDの目的、protocolの適用範囲 |
| license | protocol、firmware、host software、reference hardwareのlicense境界 |
| protocol draft | identity、capability discovery、extension、transportの基本 |
| descriptor profile registry | profile番号、descriptor構成、変更規則 |
| reference firmware | Raspberry Pi PicoとESP32-S3で動く二つのprobe実装 |
| client implementation | `uv run`で実行でき、列挙と一つ以上のprobe機能を端末から操作できるPython source |
| 動作記録 | build手順、USB列挙結果、実targetでの操作結果 |
| Windows検証 | 同一VID:PID・異なる`bcdDevice`でprofileが分離する証拠 |
| PID利用方針 | 誰が、どの条件でproject PIDを使用できるか |

PID利用方針には、少なくとも「PIDを使用するprobe firmwareは認知されたFOSS licenseで公開する」ことを明記する。hardwareの公開も必須にするかは、最終的に選ぶ割当団体の条件に合わせる。

申請の説得力を考えると、単一profileが動くだけでなく、次の二構成を示すのが望ましい。

- software USBによるHID-only reference
- native USBによるHID + CDCまたはvendor-specific reference

同じ仮の開発用VID:PIDと異なる`bcdDevice`でWindows検証を行い、申請後は割り当てられたVID:PIDへ置き換える。開発用IDのfirmwareは配布用releaseにしない。

## 4. 推奨する申請順序

1. **Openmokoを第一候補**として準備する。
2. reference implementationとlicense資料を公開する。
3. 一つのPIDを複数profile・複数MCU・第三者実装で共有する意図をPR本文に明記する。
4. reviewで利用範囲に合意が得られた場合のみ、そのPIDを正式releaseへ使用する。
5. Openmokoが利用範囲を認めない、または長期間進展しない場合は、申請を取り下げてからpid.codesへ切り替える。
6. 二重割当を避けるため、両方へ同時に正式申請しない。

## 5. 申請文面の骨子

```text
Request: one PID for <project name>

<project name> is an open, MCU-independent protocol for debug and
measurement probes. A conforming probe implements only the services it
needs and reports its actual capabilities through the protocol.

We request one PID for the protocol's USB product family. Implementations
may expose one of a controlled set of USB descriptor profiles. Profiles
share the same VID:PID and use distinct bcdDevice values. bcdDevice is used
only to distinguish descriptor layouts; clients inspect the descriptors and
protocol capabilities rather than deriving features from that number.

The repository contains the protocol specification, descriptor profile
registry, conformance rules, working reference firmware for Raspberry Pi
Pico and ESP32-S3, and a Python client that can be run directly from source
with uv under <licenses>.

Please confirm that the allocation may be used by conforming implementations
on different MCUs, including third-party implementations governed by the
project's published PID-use policy.
```

実際の申請では、動作済みprofile、source URL、license、USB description、希望PIDを各registryの形式に合わせて追加する。
