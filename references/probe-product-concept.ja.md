# 共通 PID を共有できる拡張可能な probe protocol

状態: **基本コンセプト**。具体的な仕様や実装を決定する文書ではない。

## コンセプト

> **必要な機能だけを選んで作った自作 probe と、さまざまなclient applicationを、共通の USB VID:PID と共通 protocolによって相互運用できるようにする。**

完成品として一種類の万能 probe を作るのではない。異なる MCU、回路、機能構成で作られた「自分専用の probe」が、共通の入口を利用できる基盤を作る。

## 解決する問題

自作 probe の多くは、USBで利用するときに開発用やsample用のVID:PIDを流用しており、他のdeviceと衝突する可能性がある。

さらに、機能構成によってUSB descriptorが変わると、従来の運用では構成ごとに別のPIDが必要になる。たとえばCH32V003のsoftware USBではHIDのみを公開し、CH32X035ではHIDにCDCとvendor-specific interfaceを追加する場合、同じprobe protocolであっても別製品としてPIDを分けることになる。これでは機能の組合せが増えるほどPIDも必要になり、自作probeの自由な構成を支えられない。

probeごとにprotocolも異なるため、probeとapplicationの組合せごとに専用対応が必要になる問題もある。

このprotocolは、次の二つを同時に解決する。

- descriptorや機能構成が異なる自作probeが、正規の共通VID:PIDを利用できる
- 機能構成が異なるprobeと異なるclient applicationを、共通protocolで相互接続できる

protocolの利用とproject PIDの利用は分けて扱う。共通PIDを使用するprobe firmwareは、PID割当元の条件に従い、MIT等の認知されたFOSS licenseで公開されていることを必須とする。PIDを使用しないserial/IP実装やclient applicationには、この条件を自動的には課さない。

## 固定する入口と、自由に選べる機能

共通protocolに参加するprobeとclient applicationは、どちらもすべての機能を持つ必要はない。probeはDMI、SWD、JTAG、UART、GPIO、電源制御、logic captureなどから必要な機能を選んで実装し、protocol上で申告する。client applicationも必要な機能だけに対応し、双方が対応する範囲で相互運用する。

機能は独立して追加できる構造とし、現在存在しないdebugやmeasurementのprotocolにも、共通部分を変更せず対応できるようにする。

共通化するのはprobeのidentity、機能の発見方法、通信の基本規則である。個別機能の内容や組合せは固定しない。

## USB descriptor profile

Windows等はUSB descriptorとdriver bindingをdevice identityに関連付けて保持する。このため、同じVID:PIDと`bcdDevice`で異なるdescriptorを返す構成は安全に共存できない。

本protocolでは、`bcdDevice`を **USB descriptor profileの識別子**として利用する。descriptor構成が異なる場合はprofileを分け、同じ構成では同じprofileを使う。firmware versionや実際の機能はUSB descriptorではなくprotocol上で取得する。

profileは外部に見えるUSB interfaceの構成だけを定義する。候補には次のような違いがある。

| 候補 | 利点 | 欠点 |
|---|---|---|
| **vendor-defined HID** | 標準driverで利用しやすく、low-speed MCUにも載せられる | 大容量・高速転送には向かない |
| **CDC ACM** | serial portとして扱え、既存toolを利用しやすい | port管理とframingが必要。low-speedでは使えない |
| **vendor-specific** | bulk等を使った高速で自由度の高い転送が可能 | driver bindingや権限への対応が必要 |
| **composite** | 複数のinterfaceを組み合わせられる | descriptor、MCU資源、OS対応が複雑になる |

どのclassと組合せを標準profileとして用意するかは、このコンセプトでは決めない。

profileはprobeの機能や通信経路の優先順位を意味しない。複数の経路がある場合、clientは利用可能な経路を選択できる。たとえばvendor-specific interfaceを権限上利用できなければ、HIDへfallbackできる構成を許す。

このdescriptor profileが、共通PIDを共有するために設ける最初の制約となる。必要な外形が既存profileにない場合は、新しいprofileを共通仕様へ追加する。

## USBに限定しない

共通protocolはUSB専用にしない。serialやIPでも同じ機能modelとmessageを利用できるようにする。

USB、serial、IPの違いはtransport側で吸収し、client applicationはtransportに依存しない共通のprotocol modelでprobeを操作する。application自体は同一である必要はない。

transportごとの速度や制約は異なってよい。共通にするのは機能の意味であり、物理interfaceの特性ではない。

## シンプルさと拡張性

protocolのcoreは、接続、識別、機能発見、要求と応答など、すべてのprobeに必要な最小部分に絞る。

個別のdebug、I/O、capture機能はcoreから分離して追加する。新しい機能を増やすために、既存機能やtransportを変更する必要がない構造とする。

> **小さな共通coreに、必要な機能とtransportを組み合わせられることを、このprotocolのシンプルさと拡張性とする。**

## このprotocolの価値

- 自作 probe が開発用PIDの流用から脱却できる
- 必要な機能だけを持つprobeを自由に作れる
- 異なるprobeと異なるclient applicationを相互運用できる
- USB、serial、IPの間でapplication logicを再利用できる
- 将来の未知のprobe機能を追加できる

中心的な価値は、**自作probeの自由度を維持したまま、USB identityとhost ecosystemを共有できること**にある。

## 仕様と初期実装の範囲

protocolの考え方は特定のMCUやtarget protocolに限定しない。SWD、JTAG、未知の将来protocolやUSB以外のtransportを追加できるものとする。

最初の実装では、公開情報が揃ったUART、JTAG、SWD等を用いて、共通protocolとUSB descriptor profileが実際に成立することを示す。独自解析を必要とするtarget protocolへの対応は、その後の実用実装として追加する。その実績をもって、OSSプロジェクト向けのVIDを管理する団体から、このプロジェクト専用のPID割当を受ける。
