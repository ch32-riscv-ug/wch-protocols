# カバレッジと実装可否(セッション棚卸し)

各ドキュメントを「**これだけで実装できるか**」で判定し、穴と次に集める情報をまとめる。判定語: **実装可**(byte 単位で足りる)/ **部分的**(コアは足りるが一部要追加)/ **reference**(理解と参照実装への索引。byte 仕様は含まない)/ **todo**(未着手)。

## 1. 層 × 経路のカバレッジ

| | L1 物理 | PC ドライバ | L2 転送 | L3 protocol | L4 アプリ |
|---|---|---|---|---|---|
| ① probe | SWIO/RVSWD([link-to-target] RVSWD=attested/SWIO=部分) | [pc-usb-driver] **実装可** | USB bulk([pc-to-link] **実装可**) | WCH-Link cmd + DMI([pc-to-link]/[riscv-debug-module] **実装可**) | flash/mem/reg/halt/print([pc-to-link]/[riscv-debug-module] **実装可**、print target 側=[serial-and-print]) |
| ② factory ISP | USB / UART | [pc-usb-driver] **実装可** | USB bulk / UART | ISP `0xAx`([pc-to-device-isp] **部分的**) | flash/option |
| ③ custom BL | USB / UART / software USB | [pc-usb-driver] / [software-usb] | 各 | WCH IAP([serial-and-print] UART=**実装可** / USB=部分)、DFU/UF2/HID([custom-bootloader] **reference**) | flash/OTA |
| ④ DAP | SWD/JTAG | [pc-usb-driver] | USB bulk | CMSIS-DAP([dap] **todo**、標準へ委譲) | ARM debug |

[link-to-target]: protocols/link-to-target.ja.md
[pc-usb-driver]: protocols/pc-usb-driver.ja.md
[pc-to-link]: protocols/pc-to-link.ja.md
[riscv-debug-module]: protocols/riscv-debug-module.ja.md
[serial-and-print]: protocols/serial-and-print.ja.md
[pc-to-device-isp]: protocols/pc-to-device-isp.ja.md
[custom-bootloader]: protocols/custom-bootloader.ja.md
[software-usb]: protocols/software-usb.ja.md
[dap]: protocols/dap.ja.md

**結論**: **① probe 経路(flash/read/debug/print)は byte 単位で実装可能**(ch32rv が実証済み)。② ISP・③ IAP-USB・④ DAP は capture 待ち。

## 2. ファイル別 実装可否判定

| ファイル | 判定 | これで作れるもの | 不足(byte 単位で足りない点) |
|---|---|---|---|
| [pc-to-link](protocols/pc-to-link.ja.md) | **実装可** | attach/probe info/chip info/setspeed/DMI/flash(stub + 直接 FLASH controller)/erase/power/monitor、**probe firmware の更新・救出・脱出(§10b。ch32rv が実装し実機往復検証済み)** | error 応答 frame 形式(§3 todo)、IAP の異常時応答形式(§10b.5)、§12 の残る未解読 vendor cmd(RV↔ARM mode 切替等)。**IAP entry・中断時の挙動は §10b で解決** |
| [riscv-debug-module](protocols/riscv-debug-module.ja.md) | **実装可** | halt/resume/step/read_reg/write_reg/**read_mem32/write_mem32/write_mem16**/breakpoint/semihosting。DMCOMMAND encode の読み方も明記 | abstract autoexec 詳細(軽微) |
| [pc-usb-driver](protocols/pc-usb-driver.ja.md) | **実装可** | 3 OS で device を開く。Windows 純正(CH375 IOCTL)含む | HID/CDC-GDB probe 系の driver 差(軽微) |
| [wch-iap](protocols/wch-iap.ja.md) | **実装可** | **3 世代**(BOOT 常駐 / user 先頭 / 旧 V103)の配置・entry 極性・jump、**12 シリーズ表**(FLASH_Base / CalAddr / page / USART・pin・baud / USB ID)、UART・USB frame、コマンド意味(addr 不使用・VERIFY で flush)、派生(HOST/ETH/BLE) | WCHMcuIAP の実 capture、V103 の UART 末尾、V4 系 `SW_Handler` の実体(MRS テンプレート) |
| [serial-and-print](protocols/serial-and-print.ja.md) | **実装可(USART / SDI target 側)** | USART printf、SDI printf(target・**dmdata 2 方式**)、host dmdata 対応 | — |
| [pc-to-device-isp](protocols/pc-to-device-isp.ja.md) | **実装可(USB)/ 部分的(UART)** | Identify / ReadConfig(config 12 B・BTVER・UID)/ IspKey(**XOR key = ΣUID、seed 0**)/ Erase(**sector 数 = ceil(len/1K)、最小 8**)/ Program(**56 B chunk、addr は 0 起点、256 B pad**)/ WriteConfig / IspEnd — minichlink・wchisp・chprog の 3 実装一致 | UART 枠(`57 AB` + checksum、`0xC5` SetBaud)、config 12 B の補数位置の 1 byte ずれ疑い。**BOOT 選択は RM から転記済み**(V103/V2x/V3x/L103/V407 = BOOT0/BOOT1 ピン、V003/V00X/X035/X315 = `STATR` bit14 のソフト選択のみ)。**自前 capture で verified 化** |
| [custom-bootloader](protocols/custom-bootloader.ja.md) | **§2a/§2b 実装可、他 reference** | **BOOT 領域の番地・サイズ(全 family、RM 転記)、`BOOT_MODEKEYR`/`STATR` bit14 切替、BootAsUser 手順**、**HID scratchpad BL の protocol**(report ID・scratchpad 構造・`0x1234ABCD`・完了印・stub 一覧・app→BL hook)、**user code からの BOOT 領域書換(V003 実証、app 側 updater で BL 自己更新可)**、**ブラウザ(WebHID)host 2 実装** | wch-uf2 / Swindle DFU / PlumBL / tinyboot の実 byte(ソース未入手)、HID BL の USB capture、V00X/X035 での BOOT 領域 self-write(V003 は実証済み) |
| [software-usb](protocols/software-usb.ja.md) | **reference** | rv003usb の仕組み理解、移植の要点(pin/clock/割込)。BL の stub protocol は custom-bootloader §2b へ | USB descriptor / HID report の実 byte(capture) |
| [link-to-target](protocols/link-to-target.ja.md) | **RVSWD=概ね実装可(要 verify)/ SWIO=部分的** | RVSWD の bit フレーム(addr7+data32+op2+parity)、host 抽象(WriteReg32/ReadReg32) | RVSWD の STOP 波形/クロック周波数、**SWIO の LOW パルス幅 0/1 閾値**。ロジアナ verify |
| [dap](protocols/dap.ja.md) | **todo** | — | mode 切替の実 byte 手順、CMSIS-DAP v1/v2 判定。DAP mode の capture |
| [captures](captures/README.ja.md) | (方法論 + 実例) | capture の取り方・replay 検証・**注釈付き実 fixture(target-info-v307)** | flash/erase/DMI/ISP/DAP の実 capture は今後追加 |
| [references/probe-ecosystem](references/probe-ecosystem.ja.md) | (reference) | probe/host ツール選定、参照実装・言語の索引 | — |
| guides([overview](guides/overview.ja.md)/[advanced](guides/advanced.ja.md)) | (ガイド) | 全体像・層モデル・RE 方法論 | — |

## 3. 穴と、次に集める情報(優先順)

各項目の実測は [experiments/LEDGER.ja.md](experiments/LEDGER.ja.md) に ID(`E01`… / `C11`…)として登録し、[experiments/README.ja.md](experiments/README.ja.md) の規則(計画を先に commit、証拠の水準、未測定と合格の区別)に従って実行する。

**済(このセッションでローカルソースから充填)**

- ~~write_mem32/8 の一般手順~~ → [riscv-debug-module](protocols/riscv-debug-module.ja.md) に転記済み(ch32rv-dmi、DMCOMMAND 実値 + encode の読み方)。
- ~~WCH IAP の USB frame~~ → [serial-and-print](protocols/serial-and-print.ja.md) §1 に確定(EP2 out/in 64B、`isp_cmd` 直載せ、`1A86:55E0`、256B page 自動前進。EVT `ch32x035_usbfs_device.c`)。
- ~~capture fixture~~ → [captures/fixtures/target-info-v307.ndjson](captures/fixtures/target-info-v307.ndjson) を注釈付きでコミット。
- SDI/dmdata の **2 方式**(EVT=長さ / ch32fun=`0x80|(count+4)`)を [serial-and-print](protocols/serial-and-print.ja.md) §3 に明記。

**P1 — 要 capture(実機・軽い)**

1. **WCH-Link error 応答 frame 形式**([pc-to-link](protocols/pc-to-link.ja.md) §3 todo): 異常系(target 無し `0x55` 等)の capture 収集。
2. **flash/erase/DMI の実 capture** を [captures/fixtures/](captures/fixtures/) に追加(ch32rv `--capture` 取得済み。annotate してコミット)。

**P2 — 要 capture(純正ツール)**

3. **factory ISP を capture で verified 化**([pc-to-device-isp](protocols/pc-to-device-isp.ja.md)): **byte 単位は minichlink 転記で埋まった(XOR key・Erase 符号化・Program chunk)**。残るは WCHISPTool の USB / Serial capture での照合と、UART 枠・config 12 B の補数位置。**「最大の穴」は解消、確認待ちに格下げ**。
4. **WCH IAP の往復順序照合**([wch-iap](protocols/wch-iap.ja.md) §7): frame・配置・entry は **12 シリーズ分 EVT から確定済み**。WCHMcuIAP の capture で host↔device 順序を照合し `verified` 化。
5. **DAP mode**([dap](protocols/dap.ja.md)): WCH-Link を DAP mode に切替え、mode 切替 byte と CMSIS-DAP のやり取りを capture。

**P3 — 自作 probe/線を作る場合のみ**

6. **SWIO の pulse 幅タイミング**([link-to-target](protocols/link-to-target.ja.md) §3): `CH32V003RM`(debug/SDI 章、ローカル)+ cnlohr bit-bang firmware から抽出。transaction 層(7bit+32bit)は確定済み、残るは物理タイミング。
7. **RVSWD の bit フレームをロジアナで verify**([link-to-target](protocols/link-to-target.ja.md) §3): attested → verified。STOP 波形・クロックも実測。
8. custom BL 各実装(wch-uf2 / Swindle DFU / PlumBL)の header/CRC/entry を source から転記([custom-bootloader](protocols/custom-bootloader.ja.md))。

## 4. 現況

**① probe 経路は byte 単位で実装可能**(driver / USB / DMI / flash / debug / print が揃った)。残る主な穴は capture 依存: **P2-3(factory ISP の XOR key・実 frame)が最大**、次いで P1-1(error frame)/ P2-5(DAP)/ P3(自作 probe 用の線タイミング)。ローカルソースで埋められるものはこのセッションで概ね充填済み。

## 5. protocol の外側 — ツール側の穴(別軸)

protocol が読めても「手に入る道具」が LinkE に偏っている。**汎用 probe(任意 MCU をライタに)と PC 連携(UART / USB / Wi-Fi / ブラウザ)**の穴と設計案は [references/generic-probe-design.ja.md](references/generic-probe-design.ja.md) にまとめた。要点: probe は DMI ブリッジだけでよい(chip 知識は host)、latency 対策に batch + RAM stub を protocol に最初から入れる、最大の空白は driver レスのブラウザ書込。

**マイコンレス直接書込**(probe 無し、USB ケーブルだけ)の設計空間は [references/bootloader-design-space.ja.md](references/bootloader-design-space.ja.md)。要点: 勝負は BL への **entry**(常時 BL 先行 + host 検出窓 + Core が全 sketch に reboot-to-BL hook を保証 = ボタン不要)、BL は小さく保ち RAM stub で能力拡張、**第 3 の道 = board 内蔵ライタ MCU**(UIAPduino V006 の実例。entry 問題を消すが、内蔵 MCU の software USB と minichlink 固定が実地の限界 → HW USB 化と共通 protocol が要件)。

上記 2 本の**前提**は [references/ecosystem-any-hardware.ja.md](references/ecosystem-any-hardware.ja.md)。要点: 目標は UIAPduino の逆(hardware に依らず**どんな board・bare chip もつながる**)。hardware 制御度を T0(一切触れない)〜T3(出荷時 pre-flash)の 4 階層で分け、**T0 で成立する設計を core** にする。共通に保つのは host・chip 知識・protocol・識別、差し替えるのは backend(probe / 内蔵ライタ / target BL / factory ISP)で、host は capability 宣言で選ぶ。初回 probe は不可避だが「書けた board が次の probe になる」連鎖 bootstrap で軽くする。**USB ID は pid.codes(`0x1209`)で BL / app / probe の PID を分け、chip UID を serial string に**(hardware USB があっても WCH VID を名乗る権利は生じない。`4348:55E0` 衝突が反例)。
