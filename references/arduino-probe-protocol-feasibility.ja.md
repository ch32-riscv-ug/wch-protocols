# Arduino ESP32-S3 / Pico probe 実現性調査

状態: **技術調査**。Open Embedded Probeの最初のreference implementationで、どのtarget protocolを実証できるかを判断するための資料。protocol仕様を確定する文書ではない。

## 結論

Arduino環境のESP32-S3とRaspberry Pi Pico（RP2040）の両方で、**UART、GPIO/reset、SWD、JTAGを実現できる**。このうち、最初のdebug protocolにはSWDが最も適している。

- **SWDを先に実装する。** Pico側にはRaspberry Pi公式debugprobeというPIO実装と実機例があり、ESP32側にも既存のSWD host実装がある。別のPicoをtargetにすれば、両probeで同じDP IDCODE取得を試せる。
- **JTAGを次に実装する。** 電気的には入出力線が分かれていてSWDより扱いやすいが、Pico用Arduinoでそのまま使える公式probe実装はない。TAP resetとIDCODE取得をbring-up条件、OpenOCDからのhalt・register/memory read・resumeをreference実装の完成条件とする。
- **UARTとGPIO/resetを先に共通化する。** ESP32 ROM bootloaderとの同期やchip情報取得をclient側で行えば、低リスクな最初のend-to-end試験になる。
- **Arduino APIだけで性能を共通化しない。** 共通化するのはserviceとcommandの意味であり、信号生成はRP2040ではPIO、ESP32-S3ではGPIOまたは専用peripheralを使う個別backendにする。
- **一bitごとにhostと往復しない。** SWD transferやJTAG scanをprobe内でまとめて実行できるcommandが、USBだけでなくserial/IP transportにも必要である。

したがって、最初のreference implementationが共通して持つ範囲は、**identity/capabilities、GPIO/reset、UART、SWD、JTAG**とする。SWDとJTAGの実装順序は分けるが、Pico版とESP32-S3版の両方で二つのdebug protocolが実用操作まで動く状態を目指す。

## 調査対象とArduinoの意味

probe側のMCUと開発環境を次に限定して評価する。

| probe | Arduino環境 | 採用理由 |
|---|---|---|
| Raspberry Pi Pico / RP2040 | [Arduino-Pico](https://github.com/earlephilhower/arduino-pico) | ArduinoからPico SDKとPIOを利用できる。PIO assemblerもbuildへ統合されている |
| ESP32-S3 | [Arduino-ESP32](https://github.com/espressif/arduino-esp32) | Espressif公式core。native USBとESP-IDF由来のlow-level機能を利用できる |

PicoについてはArduino Mbed OS coreではなくArduino-Picoを前提とする。[Arduino-PicoのSDK説明](https://arduino-pico.readthedocs.io/en/stable/sdk.html)はPico SDK全体とPIO APIをArduino sketchから利用できることを明示しているためである。

ここでいうArduino対応は、実装を`digitalWrite()`等の共通APIだけに閉じることではない。Arduino IDE/CLIでbuildでき、上位の共通C++ codeを共有しつつ、時間制約の厳しい部分では各MCUのSDKとperipheralを使える、という意味である。

「ESP32で動く」の初期対象はESP32-S3とする。classic ESP32でもUART/IP transportとGPIOによるSWD/JTAGは構成できるが、native USB deviceがないため、共通PIDとdescriptor profileを実証する二つ目のboardにはならない。ESP32 familyへの移植性と、PID申請用reference boardの選定は分けて扱う。

## 実現可能なservice

| service / protocol | Pico | ESP32-S3 | 初期優先度 | 判断 |
|---|---:|---:|---:|---|
| USB上の共通protocol | ◎ | ◎ | 必須 | 両方ともnative USB deviceを構成できる |
| GPIO、target reset、boot pin | ◎ | ◎ | 必須 | 通常GPIOで可能。resetはopen-drain相当の制御を要する |
| UART bridge | ◎ | ◎ | 必須 | 両方にhardware UARTがあり、ESP32 ROM bootloader試験に使える |
| 低速SWD、DP IDCODE | ◎ | ○ | 最優先 | Picoは公式実例あり。ESP32は既存ESP-IDF実装あり、Arduino移植確認が残る |
| batched SWD DP/AP transfer | ◎ | ○ | 高 | protocol上の主単位にすべき。PIOまたはMCU固有backendで実行する |
| 低速JTAG、IDCODE | ○ | ○ | 高 | 両方で実現可能。ただしArduino向けbackendは新規作業になる |
| 高速JTAG scan | ○ | ○ | 中 | PicoはPIO、ESP32-S3はSPI/専用GPIO等の実測と調整が必要 |
| SWO UART mode capture | ○ | ○ | 中 | UART、PIO、RMT等で実現可能 |
| SWO Manchester capture | △ | △ | 低 | decodeとbufferingを含む追加開発が必要 |
| CMSIS-DAP互換 | ○ | ○ | 中 | 既存tool接続に有効。ただしOEP coreそのものとは分離する |
| OpenOCD remote-bitbang / XVC | ○ | ○ | 低 | JTAG backendの診断・互換入口として有用。主transportには遅い |
| RISC-V JTAG DTM/DMI | ○ | ○ | 後続 | generic JTAG成立後に上位serviceまたはclient logicとして追加できる |
| AVR ISP / UPDI等 | ○ | ○ | 後続 | SPI/UART/GPIO backendを利用して追加可能 |
| logic capture | ○ | ○ | 後続 | PicoはPIO、ESP32-S3はRMT/LCD_CAM等を候補にできる。debug成立の前提にはしない |

`◎`は利用できる公式基盤または近い実証が揃うもの、`○`は実装可能だがOEP用backendの作成・検証が必要なもの、`△`は追加の信号処理や性能検証が大きいものを表す。

## SWD

### 成立性

SWDは`SWCLK`と双方向の`SWDIO`を使う。line reset、JTAG-to-SWD sequence、DP/AP register transfer、WAIT/FAULT処理を組み合わせる。最小実証は、targetを停止・書換えせずに行える**DP IDCODE取得**とする。

RP2040については、Raspberry Pi公式の[debugprobe](https://github.com/raspberrypi/debugprobe)がPicoをSWD probeとして使用し、SWD信号生成を[PIOで実装](https://github.com/raspberrypi/debugprobe/blob/master/src/sw_dp_pio.c)している。したがってPicoでのSWDは推測ではなく、hardwareと公開実装の両面で成立している。ただしdebugprobeのcodeを取り込む場合は、そのlicenseとnoticeをOEP自身のMIT codeから区別する。

ESP32系については、MIT Licenseの[swd-esp](https://github.com/huming2207/swd-esp)がESP32-S2/S3を含むSWD hostを実装し、直接接続と方向制御付きlevel translatorの双方を扱っている。これはESP-IDF projectでありArduino libraryではないため、OEPではlow-level部分をArduino-ESP32 buildから利用できるかを最初に確認する。少なくともMCU能力としての成立性は示されている。

### 実装単位

SWDを単なるGPIO read/writeとして公開すると、host往復の遅延で実用にならない。共通serviceには少なくとも次のまとまりが必要である。

- clock設定
- SWJ raw sequence
- SWD raw sequenceとturnaround
- DP/AP transferのbatch
- WAIT retryのprobe内実行
- backendが許容する最大clock、最大batch長、retry能力の申告

これはCMSIS-DAPをそのまま採用するという意味ではないが、Armの[CMSIS-DAP command群](https://arm-software.github.io/CMSIS-DAP/latest/modules.html)にあるSWJ sequence、SWD sequence、transfer、transfer blockは、分割境界の実績として参考になる。

### 最初の試験

別のRaspberry Pi Picoを共通targetにし、次の二経路で同じDP IDCODEを取得する。

1. Pico probe → Pico target
2. ESP32-S3 probe → Pico target

同じPython client commandと同じOEP SWD serviceを使い、違うのはprobe内backendだけにする。この試験は「二つのMCUが同じprotocolを実装する」というPID申請時の説明にも直接使える。

## JTAG

### 成立性

JTAGは基本的に`TCK`、`TMS`、`TDI`、`TDO`を使い、必要に応じて`nTRST`とtarget resetを加える。SWDIOのような一線の方向転換がないため、低速のTAP操作は両MCUのGPIOでも実装できる。

一方、Raspberry Pi公式debugprobeは現時点でSWD中心であり、[JTAG対応は未解決の要望](https://github.com/raspberrypi/debugprobe/issues/44)として残っている。そのためPico側JTAGを「公式実装がある」とは扱わず、OEP用のPIOまたはGPIO backendを新たに検証する。

ESP32-S3では、[cmsis_dap_tcp_esp32](https://github.com/bkuschak/cmsis_dap_tcp_esp32)や[ESP32JTAG Firmware](https://github.com/EZ32Inc/esp32jtag_firmware)がJTAG/SWD probeを実装しており、MCU能力としては成立している。これらもESP-IDF実装なので、Arduino-ESP32用OEP backendとは分けて評価する。

### 実装単位

最初からCPU debug、flash algorithm、GDB serverをfirmwareへ入れる必要はない。共通JTAG serviceは次の低い層に置く。

- clock設定
- TAP reset
- TMS/TDI bit sequenceの一括出力とTDO capture
- chain構成とIDCODE取得
- 最大clock、最大sequence長等のcapability

RISC-V DTM/DMIや特定targetのflash操作は、このgeneric JTAG primitiveの上に後から構成できる。target固有処理をprobe firmwareへ必須化しないことで、JTAG以外のserviceとも同じ拡張原則を維持できる。

### 最初の試験

JTAGには、次の二段階の合格条件を置く。

1. **信号層のbring-up:** 既知targetに対するTAP reset、chain scan、IDCODE取得
2. **probeとしての完成条件:** OpenOCDからtargetを認識し、halt、register read、memory read、resumeを実行

ESP32 boardは入手しやすいが、board設定やeFuseによってexternal JTAGが無効な場合があるため、既知の設定を固定したtargetを使う。flash書込みはtarget別algorithmの検証まで含むため、最初の完成条件にはしない。

OpenOCDを使わないJTAGにも、chain上のdevice識別、IEEE 1149.1 boundary scanによる基板接続検査、SVF等によるFPGA/CPLD設定という用途がある。ただしIDCODE取得だけではprobeの実用例として弱く、boundary scanにはdevice固有のBSDLやinstruction、FPGA設定にはvendor固有手順が必要になる。MCU用debug probeとして分かりやすく示す用途では、既存target supportを持つOpenOCDへ接続する方がよい。

## UART、GPIO、reset

UARTとGPIOは、SWD/JTAGの周辺機能ではなく、最小probeの独立したserviceにする。

- UARTのbaud rate、data format、read/write
- `nRESET` pulse
- boot strap pinのassert/release
- 任意GPIOのread/write
- 必要ならDTR/RTS相当の論理制御

最初の実証では別のESP32 boardをtargetとし、ROM bootloaderとの同期とchip情報取得を行う。Espressifは[serial protocol](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/serial-protocol.html)と[boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)を公開している。

ESP32固有のSLIP packetやcommandはPython client側で扱い、probe firmwareには汎用UART/GPIO操作だけを実装する。これにより同じfirmware serviceをUART console、別のbootloader、UPDI等へ再利用できる。

## probe内の構造

```text
USB / serial / IP transport
            │
       OEP message core
            │
    service dispatcher
     ├─ gpio / reset
     ├─ uart
     ├─ swd
     ├─ jtag
     └─ trace.swo      （後続）
            │
   platform backend boundary
     ├─ RP2040: PIO + DMA、GPIO fallback
     └─ ESP32-S3: GPIO / Dedicated GPIO、必要に応じSPI・RMT等
```

上位のmessage解析、capability表現、parameter検証、error表現は共通codeにできる。信号の時間制御、DMA、interrupt対策、pin mappingはplatform backendに閉じ込める。

RP2040ではPIOがSWD/JTAGのclock生成とsamplingに適する。ESP32-S3の[RMT](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/rmt.html)は正確なpulse列の生成・受信に利用できるが、SWDのACKに応じた即時の方向転換やJTAGの同時samplingまで一つのperipheralで自然に処理できるとは限らない。したがってESP32-S3の初版は、低速で検証しやすいGPIO backendを作り、測定後に専用GPIO、SPI、RMT等へ置き換える。

### GPIO fallbackとHALの境界

JTAGとSWDはprobeがclockを生成する同期式interfaceなので、targetが許容するclock high/low時間を守れば、通常GPIOによるsoftware駆動でも成立する。CPU処理やinterruptでedge間隔が不均一になっても、最小pulse幅を破らない低速動作から始められる。JTAGでは指定edgeでTDOをsampleし、SWDではSWDIOのturnaround時に方向を正しく切り替える必要がある。

ただしHALを`set_pin()`、`get_pin()`だけで切ると、上位層が一bitずつ呼び出す構造になり、PIO、DMA、SPI等へ置き換えても高速化できない。HALは**bit列をまとめて実行するsequence engine**として定義する。

```text
OEP service command
    │  transfer / scanのbatch
    ▼
SWD engine              JTAG TAP engine
DP/AP、ACK、retry        TAP state、IR/DR scan
    │                         │
    └──── wire sequence HAL ──┘
              │
      ┌───────┼──────────┐
      ▼       ▼          ▼
  GPIO実装   RP2040 PIO  ESP32専用実装
  （基準）   + DMA       GPIO/SPI/RMT等
```

wire sequence HALが扱う単位は、概ね次のようにする。

- JTAG: TMS/TDI列、bit数、TDO capture指定をまとめたshift
- SWD: drive/read方向、bit列、turnaroundをまとめたsequence
- 共通: clock設定、idle cycle、reset pin制御、完了結果と途中error

GPIO backendは最初の正しさを確認する基準実装と、PIO等を利用できないMCUへのfallbackになる。高速backendも同じHAL contractを実装し、`max_clock_hz`、最大sequence長、DMA可否等をcapabilityとして申告する。この構造ならprotocol仕様とSWD/JTAGの上位logicを変えずに高速化できる。

### header中心の構成

reference firmwareの共通実装は、可能な範囲でheaderだけをincludeして構成できるlibraryにする。ただし、全機能を一つの巨大なheaderへ入れるのではなく、core、service、platform backendを分割し、sketchが使うものだけを明示的にincludeする。

```text
oep/
  core/
    message.h
    dispatcher.h
    capability.h
  services/
    gpio.h
    uart.h
    swd.h
    jtag.h
  backends/
    generic/gpio_sequence.h
    rp2040/pio_swd.h
    rp2040/pio_jtag.h
    esp32s3/gpio_swd.h
    esp32s3/gpio_jtag.h
  transports/
    stream.h
    usb_hid.h
    usb_vendor.h
    usb_cdc.h
```

たとえばSWDだけを持つfirmwareは`services/swd.h`と選択したSWD backendだけをincludeし、JTAG、UART、未使用transportを登録しない。capabilityは明示的に登録されたserviceから生成し、link時に偶然残ったcodeや自動登録には依存しない。

header内の実装は`inline`、`constexpr`、template等を使い、複数translation unitからincludeしてもone-definition ruleに違反しない形にする。次のものは無理にheader-onlyへ押し込まず、必要なら小さなplatform固有`.cpp`または生成物を許容する。

- C linkageで一つだけ必要なUSB descriptor callback
- interrupt vectorやSDKが単一定義を要求するobject
- PIO assembler等から生成するprogram data
- compile時間やcode sizeを著しく悪化させる大きな固定実装

「header中心」は配布形式そのものを目的にするのではなく、**必要なserviceだけを選んだprobeを小さなsketchから組み立てられること**を目的とする。`oep/all.h`のような便宜用umbrella headerを用意しても、reference firmwareとlibrary exampleでは個別headerを基本にする。

## CMSIS-DAP等との関係

CMSIS-DAPは、SWD/JTAG/SWOのcommand境界と既存debug toolとの接続に利用価値が高い。ただし、OEPの共通protocolをCMSIS-DAPそのものに限定すると、UART、GPIO、logic capture、将来の未知service、serial/IP transportを同じmodelで扱う目的から外れる。

次のいずれかを追加できる構造にする。

- probeがCMSIS-DAP用USB interfaceを追加する
- host adapterがOEP commandをCMSIS-DAP/OpenOCD側へ変換する
- OEP SWD/JTAG backendの適合試験にCMSIS-DAPと同等のsequenceを用いる

reference implementationでは、**host上のCMSIS-DAP-over-TCP bridge**を第一候補とする。現在の[OpenOCD adapter configuration](https://openocd.org/doc/html/Debug-Adapter-Configuration.html)はCMSIS-DAPのTCP backendを備えている。bridgeはOpenOCDから受け取ったCMSIS-DAP packetをOEPのbatched SWD/JTAG commandへ変換する。これならprobeのUSB descriptorにCMSIS-DAP interfaceを必須化せず、同じbridgeからPico版、ESP32-S3版、将来のserial/IP版を利用できる。

[CMSIS-DAP reference implementation](https://github.com/ARM-software/CMSIS-DAP)はApache-2.0である。OEP自身のcodeをMITで統一する方針と矛盾はしないが、codeを取り込む場合は第三者componentとしてlicenseとnoticeを保持する。既存projectを参照して独自実装する場合にも、由来を曖昧にしない。

OpenOCD remote-bitbangやXilinx Virtual Cableもbackendの動作確認には便利である。現在のOpenOCD remote-bitbangはJTAGとSWDを扱えるが、ASCIIのbit単位操作であり、OEP transportまで細かい往復にすると性能が出ない。bring-up用の簡易bridgeには使えても、完成例の主経路はbatchを保てるCMSIS-DAP TCP bridgeとする。

## USB実装に関する成立条件

両Arduino coreともnative USB deviceを利用できるが、OEPのdescriptor profileと`bcdDevice`実証には差がある。

### ESP32-S3

[Arduino-ESP32 USB API](https://github.com/espressif/arduino-esp32/blob/master/docs/en/api/usb.rst)はVID、PID、firmware version、CDC/HIDを含むUSB構成を提供し、[vendor-specific interfaceのexample](https://github.com/espressif/arduino-esp32/blob/master/libraries/USB/examples/USBVendor/USBVendor.ino)も存在する。OEPのUSB transportと複数descriptor profileを実験できる。

ESP32-S3ではUSB-OTGと内蔵USB-Serial-JTAGが同じ内部PHYを共有する制約がある。[EspressifのUSB device documentation](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_device.html)に従い、OEP独自descriptorを出すbuildではUSB-OTG側のpinとconsole経路を明示する。

### Raspberry Pi Pico

[Arduino-Pico USB documentation](https://arduino-pico.readthedocs.io/en/latest/usb.html)はPico SDK USB stackとAdafruit TinyUSBを選択でき、VID/PIDやvendor interfaceを構成できることを示している。

ただし現行coreの[USB.cpp](https://github.com/earlephilhower/arduino-pico/blob/master/cores/rp2040/USB.cpp)ではdevice descriptorの`bcdDevice`が`0x0100`に固定されており、公開setterが見当たらない。OEPが`bcdDevice`をdescriptor profile IDに使うには、次のいずれかが必要になる。

- Arduino-Picoへ設定APIをupstream提案する
- OEP buildでdescriptor callbackを差し替える
- 小さなcore patchを管理する

恒久的なforkを前提にせず、最小の設定APIで解決できるかを最初のUSB gateとする。また、composite profileはcoreが受理することだけでなく、Windows/Linuxで各interfaceが期待どおり列挙されることをprofileごとに実測する。

## 電気的な境界

ESP32-S3とPicoのGPIOは3.3 V系であり、開発boardをそのまま汎用の堅牢なprobeにはできない。最初の実験は3.3 V target、common ground、短い配線、必要なseries resistorを前提にできるが、配布可能なreference hardwareでは次を別途扱う。

- target Vrefの検出
- level translation
- SWDIOの方向制御と衝突回避
- resetのopen-drain駆動
- ESD、過電圧、逆給電への保護
- probeからtargetへ給電する場合の電流制限と明示的な制御

特にSWDIOはturnaround中にprobeとtargetが同時駆動しないことが必要である。firmwareで方向を切り替えられることと、電圧の異なるtargetへ安全に接続できることは別の成立条件として扱う。

## 実装・検証の順序

| 段階 | 実装 | 合格条件 |
|---|---|---|
| 0 | USB identity、capabilities、descriptor profile | 両boardを同じclientで列挙できる。Picoで`bcdDevice`を変更できる |
| 1 | GPIO/reset、UART | 両probeから別のESP32 targetのROM bootloaderへ接続し、chip情報を取得できる |
| 2 | SWD低速backend | 両probeからPico targetのDP IDCODEを同じcommandで取得できる |
| 3 | SWD batch | DP/AP transferとmemory readを、bitごとのhost往復なしで実行できる |
| 4 | JTAG低速backend | 両probeから既知targetのTAP reset、chain scan、IDCODE取得ができる |
| 5 | OpenOCD bridge | host上のCMSIS-DAP TCP bridgeを介し、SWD/JTAG targetのhalt、register/memory read、resumeができる |
| 6 | 性能backend | PIO、Dedicated GPIO、SPI、RMT等を比較し、最大clockとbatch上限をcapabilityに反映する |
| 7 | 任意の追加互換入口 | native CMSIS-DAP USB、remote-bitbang、XVC等から必要なものを追加する |

flash書込み、breakpoint、GDB server、全targetのdebug algorithmは、この成立性実証の必須条件にしない。それらはgeneric SWD/JTAG primitiveが成立した後にclientまたは追加serviceとして選択できる。

## 最初に固定すべき実験matrix

| probe | target | 経路 | 観測結果 |
|---|---|---|---|
| Pico | Pico | SWD | DP IDCODE |
| ESP32-S3 | Pico | SWD | 同じDP IDCODE |
| Pico | ESP32または既知JTAG MCU | JTAG | IDCODE、OpenOCD経由のhalt/read/resume |
| ESP32-S3 | 同じJTAG target | JTAG | 同じIDCODE、halt/read/resume |
| Pico | ESP32 | UART + boot/reset GPIO | ROM同期、chip情報 |
| ESP32-S3 | ESP32 | UART + boot/reset GPIO | 同じchip情報 |

このmatrixを一つのPython CLIと同じprotocol commandで実行し、差異はcapabilityと性能値として表示する。

## 実現性ゲート

実装開始前後に、次を事実で閉じる必要がある。

1. Arduino-Picoでcoreの恒久forkなしに`bcdDevice`を設定できるか
2. 両coreで採用候補のUSB composite profileを同じOS群へ安定して列挙できるか
3. Arduino-ESP32 buildから必要なESP-IDF low-level APIを安定して利用できるか
4. USB処理やFreeRTOS interrupt下でもSWD/JTAG timingを守れるbackend境界を作れるか
5. SWDIO turnaroundでGPIO方向と外付けbufferを安全に制御できるか
6. batch commandの上限とerror時の途中結果をMCU非依存に表現できるか
7. 既存SWD/JTAG codeを利用する場合、MIT project内で第三者licenseを明確に分離できるか
8. target側のdebug lock、eFuse、認証状態を「probe不良」と誤判定しない試験条件を作れるか

これらはprotocol全体の可否ではなく、reference implementationと申請用デモを再現可能にするためのgateである。

## ロードマップへの判断

PID申請までのdebug実証は、従来案の「JTAGまたはSWDのどちらか」から、両方を段階的に成立させる計画へ具体化する。

1. UART/GPIOによるESP32 ROM識別
2. **両probeによるPico targetのSWD DP IDCODE取得**
3. 両probeによるJTAG chain scanとIDCODE取得
4. 共通のhost bridgeを介したOpenOCDのhalt、register/memory read、resume

IDCODEは信号層の成立確認であり、実装例の完成とはみなさない。SWDとJTAGの双方をOpenOCDから利用できれば、Open Embedded Probeが単なるUSB-UART adapterでも専用test programでもなく、異なるMCU上で実用的なdebug serviceを提供できることを示せる。

## 追加TODO — SUMP系logic capture

PID申請用の最小実装とは分けて、SUMP protocolに近い操作modelを持つlogic capture機能を追加できるか検討する。

- sample rate、channel、trigger、sample countを設定してcaptureを開始する
- probe内のRAMまたはPSRAMへ収集し、capture完了後にhostへdownloadする
- real-time modeではprobeから共通protocolのstreamとしてsampleをhostへ送る
- host側でraw dataをtextまたはsigrokが読めるファイル形式へ保存する
- 最初の成立確認は有限長のbatch captureとし、real-time modeは独立した追加modeとして扱う
- SUMPとの完全互換を必須にせず、既存toolとの接続価値と共通protocolへ自然に載せられる範囲を比較する

ESP32-P4等のPSRAM搭載構成では、深いcapture bufferを持つ実用的な構成を候補とする。Picoでは内蔵RAMに収まる小さなsample数に限定し、同じ操作modelの最小実装が成立するかを確認する。buffer容量、最大sample rate、channel数、trigger能力は固定仕様にせずcapabilityとして申告する。

[E016](../experiments/e016_p4_parlio_psram_direct/README.ja.md)で、Arduino-ESP32 3.3.11からESP-IDF PARLIO APIを直接呼び、8-bit・8 MHz設定・8,192 sampleの有限長RXを32 MiB PSRAMへ直接DMAできることを確認した。PSRAM payloadはexternal-DMA-capableで、全8 laneを3回取得できた。[E017](../experiments/e017_p4_parlio_psram_cache_sync/README.ja.md)では、driver内部のdescriptor単位cache sync警告はburst size 0 / 64 / 128 byteで変わらず、7,936 byteまでは無警告、8,064 byte以上では各run 2件となった。全45 captureは完了後のpayload全体sync後に正しいdataを保持した。[E018](../experiments/e018_p4_parlio_psram_log_suppression/README.ja.md)では、soft delimiterのEOF長が最大65,535 byteであり、単一有限長transactionでは1 MiBを開始できないことが分かった。[E019](../experiments/e019_p4_parlio_psram_partial_ring/README.ja.md)では1 MiB PSRAM direct partial transactionを開始できたが、runtime log抑制後も4,032-byte descriptorの不整列cache sync errorが続き、27件でInterrupt WDTになった。したがってstock driverの成立範囲は**65,535 byte以下で、受信完了後にpayload全体を明示syncする有限長batch capture**までである。深いcaptureにはexternal-memory alignmentを使うdriver修正、またはinternal DMA ringからPSRAMへの退避が必要で、sample rate上限も未確認。

検討時には、少なくとも次を分けて評価する。

1. SUMP commandとの互換範囲
2. capture dataのprobe内表現とdownload方法
3. text、sigrok session等へのhost側変換
4. ESP32-P4のPSRAM帯域・容量と、PicoのRAM上限
5. capture中のUSB/IP処理、SWD/JTAG等とのresource競合

このTODOはprotocol coreへlogic analyzer固有仕様を組み込む決定ではない。logic captureを独立した追加機能として表現できるかを確認するための検討項目とする。

### sigrok real-time連携の候補

PC側applicationが提供する互換出力として、**BeagleLogic TCP**を候補に加える。

```text
probe ── 共通protocolのlogic sample stream ──> PC application
                                                   └─ BeagleLogic TCP互換server ──> sigrok / PulseView
```

probe firmwareがBeagleLogic TCPを実装する構成にはしない。probeはtransportに依存しない共通protocolでsample、sample rate、channel構成、連番、drop等を送り、PC側applicationがBeagleLogicの制御commandとraw sample列へ変換する。

この分離により、同じprobe streamから次を並行して提供できる。

- BeagleLogic TCP経由のsigrok real-time表示
- text、sigrok session、VCD等への保存
- 共通protocolを直接扱う別applicationへの配信

BeagleLogic TCPはsigrok互換adapterの候補であり、共通protocolのwire formatやcapability modelを制約しない。BeagleLogic側で表現できないchannel数、timestamp、drop情報等はPC側applicationが保持し、変換不能な条件では明示的に拒否またはcaptureを終了する。
