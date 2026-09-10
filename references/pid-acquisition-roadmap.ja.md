# Open Embedded Probe PID取得ロードマップ

状態: **計画案**。PID申請までに何を公開し、何を実証するかを管理する。

## ゴール

Open Embedded Probeに対して、OSS向けVIDを管理する団体からproject固有のPIDを一つ割り当ててもらう。

申請時には、文書だけでなく次の状態を示す。

- ESP32-S3で動作するprobe実装がある
- Raspberry Pi Picoで動作するprobe実装がある
- 同じ公開protocolを二つの実装が利用している
- sourceから実行できるPython clientがある
- clientの基本操作を端末上で確認できる
- 一つのPIDを複数のMCU、descriptor profile、準拠実装で利用する範囲が明文化されている

二つのprobeは完成品としての機能数を競うものではない。異なるMCUとUSB実装でも、共通のidentity、capability discovery、基本操作が成立することを示すreference implementationとする。

## 申請までのステップ

| Step | 成果物 | 完了条件 |
|---|---|---|
| 0. projectの入口を作る | GitHub Organization、canonical repository、短い説明 | `Open-Embedded-Probe`から仕様、実装、client、利用条件へ到達できる |
| 1. 公開条件を決める | LICENSE、PID利用方針、contribution方針 | project codeのlicenseと、PIDを利用できるfirmwareのFOSS license要件が読める |
| 2. 最小protocolを固定する | identity、capability discovery、UART/GPIO、SWD、JTAG、extension規則、transport境界 | 二つのfirmwareとclientが同じ記述を参照して実装できる |
| 3. USB identityを固定する | descriptor profile registry、profile分離手段の規則、Windows/Linux先行実験 | 同一PIDで構成が異なるprofileをWindowsが分離できる条件を判定し、Linuxでdescriptorと各interfaceの基本動作を確認する |
| 4. Python clientを公開する | source、`pyproject.toml`、`uv`での実行手順 | clone後にbuild済み専用binaryなしで列挙と基本操作ができる |
| 5. Raspberry Pi Pico実装を公開する | firmware source、build/書込手順、license | 実機が列挙され、clientからidentity、capabilities、基本操作を確認できる |
| 6. ESP32-S3実装を公開する | firmware source、build/書込手順、license | Picoと同じclient操作が成立し、MCU非依存性を示せる |
| 7. USB profileを実証する | OS別の列挙・再接続試験記録 | 同一VID:PID候補の公開profile群がWindowsで混線せず、LinuxとmacOSでも列挙・通信できる |
| 8. 再現可能なデモを固定する | release tag、端末transcript、接続図、既知の制限 | 第三者がREADMEの順に実行して二つのprobeを比較できる |
| 9. 申請内容をreviewする | 申請文案、source URL、license一覧、希望する利用範囲 | 複数MCU、複数profile、第三者実装によるPID利用を隠さず説明できる |
| 10. PIDを申請する | Openmoko registryへのPR | reviewerの確認を経てPIDがregistryへmergeされる |

Step 2でprotocol全体を完成させる必要はない。申請に使う二つの実装が相互運用でき、未知のserviceを追加できる最小の拡張境界があればよい。

現在の`wch-protocols`で行う企画と実験は、Step 3のWindows profile分離試験とLinux基本試験までとする。ESP32-S3でHID-onlyとHID + vendor-specific + CDC ACM × 1などを同一VID:PID・同一serialで切り替え、Windowsがdevnodeとdriverをどう扱うかを実測して分離手段を決定する([E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md))。当初の`bcdDevice`による分離案は、Windowsのdevice instance identityに`bcdDevice`が含まれないため2026-09-10に撤回した([調査結果](usb-host-descriptor-persistence.ja.md))。Step 7では、この先行判断を正式なreference firmwareによってWindows、Linux、macOSで再検証する。その間のprotocol、reference firmware、client実装は`Open-Embedded-Probe` organizationのcanonical repositoryで進める。詳細な終了条件は[Probe protocol実現性ゲート](probe-feasibility-gates.ja.md)のGate 4を参照する。

## 最初に実証するtarget機能

最初のtargetはCH32RV固有protocolにしない。公開情報と既存実装があり、protocol解析を行わずに実装と実機試験を始められるものを選ぶ。

### 第一段: UART経由のESP32 ROM bootloader

probeが提供する機能は、baud rate等を設定できる汎用UARTと、必要な制御線の操作とする。ESP32固有のbootloader protocolはclient側で扱い、probe firmwareの共通UART serviceへ埋め込まない。

申請用の最小実証では、probe用のESP32-S3またはPicoとは別のESP32 target boardを接続し、ROM bootloaderとの同期とchip情報の取得までを端末から行う。全flashの書込み機能を最初の必須条件にはしない。

この実証には次の利点がある。

- UART自体は実装と観測が容易である
- 安価で入手しやすいtarget boardを利用できる
- 成功結果をchip情報として端末に明示できる
- target固有protocolをclient側へ置けることを示せる
- 同じUART serviceを将来別の用途へ再利用できる

### 第二段: SWDによるdebug操作

UARTだけでは一般的なUSB-UART bridgeとの差が見えにくいため、両方のreference firmwareから別のPico targetへSWD接続する。

- bring-upではline reset、接続、DP IDCODE取得まで確認する
- 完成条件ではhalt、registerまたはmemory read、resumeまで確認する
- host側のOpenOCD bridgeを共通にし、Pico版とESP32-S3版で同じ操作を行う

最初からflash書込みやtarget別flash algorithmまで完成させる必要はない。既知のdebug interfaceを共通protocol上の独立したserviceとして追加し、既存debug toolから利用できることを示す。

### 第三段: JTAGによるdebug操作

両方のreference firmwareへJTAG serviceを追加する。

- bring-upではTAP reset、chain scan、IDCODE取得まで確認する
- 完成条件ではOpenOCDからhalt、registerまたはmemory read、resumeまで確認する
- SWDと同じhost bridgeとclient構造を再利用する

JTAGはOpenOCDなしでもdevice chainの識別、boundary scan、FPGA/CPLD設定に利用できる。ただしIDCODE取得だけではprobeの実用例として弱いため、PID申請用reference implementationではOpenOCD接続まで実証する。

第一段のUART、第二段のSWD、第三段のJTAGを申請前の実証とする。詳細な成立性と実装順序は[Arduino ESP32-S3 / Pico probe 実現性調査](arduino-probe-protocol-feasibility.ja.md)を参照する。

## 申請用clientの形

申請時のclientは、GUIや配布binaryよりも、sourceを読んでそのまま実行できる端末用Python CLIを基本とする。

想定する入口は次の程度に揃える。

```console
$ uv run oep list
$ uv run oep info <device>
$ uv run oep caps <device>
$ uv run oep esp-info <device>
$ uv run oep swd-idcode <device>
$ uv run oep jtag-scan <device>
$ uv run oep openocd-bridge <device>
```

command名はprotocol設計時に決める。この形の目的は、reviewerや第三者が次を短時間で確認できるようにすることである。

- 特別なIDEやinstallerを必要としない
- 実行されるclient sourceを確認できる
- 二つのprobeへ同じ操作を実行できる
- 実装していない機能も含め、capabilityの違いを比較できる
- 端末出力をそのまま動作証拠として保存できる

Python clientは全serviceの完全実装を目標にしない。申請時点では、device discovery、protocol identity、capability discovery、ESP32 targetのUART識別、SWD/JTAGの基本操作とOpenOCD bridgeを確認できればよい。

## 二つのreference implementationが示すもの

| 観点 | Raspberry Pi Pico | ESP32-S3 |
|---|---|---|
| 役割 | 小さく追いやすい最小実装 | 別MCU・別SDKでも成立する移植例 |
| 共通部分 | identity、capability discovery、UART/GPIO、SWD、JTAG | identity、capability discovery、UART/GPIO、SWD、JTAG |
| 異なってよい部分 | 実装するservice、USB profile、性能、pin配置 | 実装するservice、USB profile、性能、pin配置 |

UART/GPIO、SWD、JTAGは申請用reference implementationの共通baselineとする。それ以外は両者が同じ機能を持つ必要はない。機能差があっても、clientがcapabilityを確認して利用可能な範囲だけを扱えることが、このprojectのコンセプトを示す。

ただし「一PID・複数profile」を申請根拠に含めるなら、二つの実装または試験用buildを使って、少なくとも二種類のdescriptor profileを実機で検証する。

## 申請時に見せる最短の流れ

```text
repositoryをclone
    ↓
PicoまたはESP32-S3へ公開firmwareを書き込む
    ↓
uv run oep list
    ↓
uv run oep info / caps
    ↓
同じcommandで各probeからESP32 targetを識別
    ↓
SWD/JTAG IDCODEを取得
    ↓
OpenOCDからhalt / read / resume
    ↓
異なる機能構成がcapabilityとして表示される
```

この一連の流れをREADME、端末transcript、CIで生成する成果物に揃える。動画は補助資料にはなるが、再現可能な手順の代わりにはしない。

## 申請開始の判定

次をすべて満たしたら、Openmokoへの申請PRを開始する。

- `Open-Embedded-Probe`配下にcanonicalなproject入口がある
- protocol、PID利用方針、descriptor profile registryが公開されている
- Raspberry Pi PicoとESP32-S3のfirmware sourceがFOSS licenseで公開されている
- Python clientを`uv run`で実行できる
- 二つの実機について端末から同じ確認手順を再現できる
- 二つのprobeからUART経由でESP32 targetを識別できる
- 両方のprobeでSWDとJTAGのIDCODE取得を実証している
- 共通のhost bridgeを介し、OpenOCDからSWD/JTAG targetのhalt、registerまたはmemory read、resumeを実証している
- Windowsで、採用した分離手段による公開profile群の分離を実証している
- LinuxとmacOSで公開profileの列挙と各interfaceの基本通信を実証している
- 依存libraryを含むlicense一覧がある
- 一つのPIDを第三者の準拠実装へ利用させたいことを申請文に明記している

PIDがmergeされるまでは、仮のVID:PIDを含むfirmwareを正式配布物として扱わない。割当後に正式なVID:PIDへ置き換え、最初のreleaseを作成する。

## License方針

Open Embedded Probe自身が作るprotocol文書、reference firmware、Python client、schema、test vector、code exampleはMIT Licenseで統一する。帰属表示なしでの再利用より、認知度と運用の単純さを優先する。

公開前に次を確認する。

- project自身が書いたcodeと第三者由来codeを分離できること
- 依存libraryのnotice保持義務は別に残ること
- Openmoko申請用の元sourceには著作権者・年・SPDX headerを置くこと
- contributionがMIT Licenseで受け入れられることを`CONTRIBUTING`へ明記すること
- MITによるcode利用許可と、`PID-USE.md`によるVID:PID使用許可を混同しないこと

詳細は[Openmoko / pid.codes PID申請調査](oss-usb-pid-application-report.ja.md)の「Licenseの決定」を参照する。
