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
| 1. 公開条件を決める | LICENSE、PID利用方針、contribution方針 | PIDを利用できるfirmwareの範囲とFOSS license要件が読める |
| 2. 最小protocolを固定する | identity、capability discovery、基本command、extension規則、transport境界 | 二つのfirmwareとclientが同じ記述を参照して実装できる |
| 3. USB identityを固定する | descriptor profile registry、`bcdDevice`割当規則 | 申請時に使用するprofileが登録され、同じ値を別構成へ再利用しない |
| 4. Python clientを公開する | source、`pyproject.toml`、`uv`での実行手順 | clone後にbuild済み専用binaryなしで列挙と基本操作ができる |
| 5. Raspberry Pi Pico実装を公開する | firmware source、build/書込手順、license | 実機が列挙され、clientからidentity、capabilities、基本操作を確認できる |
| 6. ESP32-S3実装を公開する | firmware source、build/書込手順、license | Picoと同じclient操作が成立し、MCU非依存性を示せる |
| 7. USB profileを実証する | OS別の列挙・再接続試験記録 | 同一VID:PID候補と異なる`bcdDevice`の構成がWindowsで混線しない。Linuxでも列挙できる |
| 8. 再現可能なデモを固定する | release tag、端末transcript、接続図、既知の制限 | 第三者がREADMEの順に実行して二つのprobeを比較できる |
| 9. 申請内容をreviewする | 申請文案、source URL、license一覧、希望する利用範囲 | 複数MCU、複数profile、第三者実装によるPID利用を隠さず説明できる |
| 10. PIDを申請する | Openmoko registryへのPR | reviewerの確認を経てPIDがregistryへmergeされる |

Step 2でprotocol全体を完成させる必要はない。申請に使う二つの実装が相互運用でき、未知のserviceを追加できる最小の拡張境界があればよい。

## 申請用clientの形

申請時のclientは、GUIや配布binaryよりも、sourceを読んでそのまま実行できる端末用Python CLIを基本とする。

想定する入口は次の程度に揃える。

```console
$ uv run oep list
$ uv run oep info <device>
$ uv run oep caps <device>
$ uv run oep <basic-operation> <device>
```

command名はprotocol設計時に決める。この形の目的は、reviewerや第三者が次を短時間で確認できるようにすることである。

- 特別なIDEやinstallerを必要としない
- 実行されるclient sourceを確認できる
- 二つのprobeへ同じ操作を実行できる
- 実装していない機能も含め、capabilityの違いを比較できる
- 端末出力をそのまま動作証拠として保存できる

Python clientは全serviceの完全実装を目標にしない。申請時点では、device discovery、protocol identity、capability discoveryと、一つ以上の実操作を確認できればよい。

## 二つのreference implementationが示すもの

| 観点 | Raspberry Pi Pico | ESP32-S3 |
|---|---|---|
| 役割 | 小さく追いやすい最小実装 | 別MCU・別SDKでも成立する移植例 |
| 共通部分 | protocol identity、capability discovery、clientからの基本操作 | protocol identity、capability discovery、clientからの基本操作 |
| 異なってよい部分 | 実装するservice、USB profile、性能、pin配置 | 実装するservice、USB profile、性能、pin配置 |

両者がすべて同じ機能を持つ必要はない。むしろ機能差があっても、clientがcapabilityを確認して利用可能な範囲だけを扱えることが、このprojectのコンセプトを示す。

ただし`bcdDevice`方式を申請根拠に含めるなら、二つの実装または試験用buildを使って、少なくとも二種類のdescriptor profileを実機で検証する。

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
同じcommandで各probeの共通機能を操作
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
- Windowsで`bcdDevice`によるdescriptor profile分離を実証している
- 依存libraryを含むlicense一覧がある
- 一つのPIDを第三者の準拠実装へ利用させたいことを申請文に明記している

PIDがmergeされるまでは、仮のVID:PIDを含むfirmwareを正式配布物として扱わない。割当後に正式なVID:PIDへ置き換え、最初のreleaseを作成する。
