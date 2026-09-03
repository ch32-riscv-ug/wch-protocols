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
| [pc-to-link](protocols/pc-to-link.ja.md) | **実装可** | attach/probe info/chip info/setspeed/DMI/flash(stub + 直接 FLASH controller)/erase/power/monitor | error 応答 frame 形式(§3 todo)、§12 の未解読 vendor cmd(mode 切替等) |
| [riscv-debug-module](protocols/riscv-debug-module.ja.md) | **実装可(read 系)** | halt/resume/step/read_reg/write_reg/read_mem32/breakpoint/semihosting | **write_mem32/8 の一般手順が未記載**(read_mem32 と対称。DMCOMMAND の transfer/write bit 実値は ch32rv-dmi 実装から転記要)、abstract autoexec 詳細 |
| [pc-usb-driver](protocols/pc-usb-driver.ja.md) | **実装可** | 3 OS で device を開く。Windows 純正(CH375 IOCTL)含む | HID/CDC-GDB probe 系の driver 差(軽微) |
| [serial-and-print](protocols/serial-and-print.ja.md) | **実装可(UART IAP / USART / SDI target 側)** | WCH IAP UART 更新、USART printf、SDI printf(target)、host dmdata 対応 | **WCH IAP の USB frame(usbfs_device.c 未転記)**、series 別 IAP frame 差 |
| [pc-to-device-isp](protocols/pc-to-device-isp.ja.md) | **部分的** | コマンド体系・遷移の理解、V003 factory BL の入口 | **XOR key 生成算法(chip 系列別)**、USB/UART の**実 frame byte**、Erase sector 数エンコード。自前 capture 必須 |
| [custom-bootloader](protocols/custom-bootloader.ja.md) | **reference** | どの実装をどう選ぶか。共通仕様項目 | 各 BL(wch-uf2/Swindle DFU/PlumBL)の header/CRC/entry 実 byte |
| [software-usb](protocols/software-usb.ja.md) | **reference** | rv003usb の仕組み理解、移植の要点(pin/clock/割込) | USB descriptor / HID report / bootloader stub protocol の実 byte |
| [link-to-target](protocols/link-to-target.ja.md) | **RVSWD=概ね実装可(要 verify)/ SWIO=部分的** | RVSWD の bit フレーム(addr7+data32+op2+parity)、host 抽象(WriteReg32/ReadReg32) | RVSWD の STOP 波形/クロック周波数、**SWIO の LOW パルス幅 0/1 閾値**。ロジアナ verify |
| [dap](protocols/dap.ja.md) | **todo** | — | mode 切替の実 byte 手順、CMSIS-DAP v1/v2 判定。DAP mode の capture |
| [captures](captures/README.ja.md) | (方法論) | capture の取り方・replay 検証 | **実 fixture が repo に未commit** |
| [references/probe-ecosystem](references/probe-ecosystem.ja.md) | (reference) | probe/host ツール選定、参照実装・言語の索引 | — |
| guides([overview](guides/overview.ja.md)/[advanced](guides/advanced.ja.md)) | (ガイド) | 全体像・層モデル・RE 方法論 | — |

## 3. 穴と、次に集める情報(優先順)

**P1 — 破壊的でなく効果大**

1. **write_mem32/8 の一般手順**(riscv-debug-module): ch32rv-dmi 実装から DMCOMMAND 実値を転記([riscv-debug-module](protocols/riscv-debug-module.ja.md) を read_mem32 と対称に補完)。→ ローカルソースで即完了可。
2. **capture fixture を repo に置く**([captures](captures/README.ja.md)): ch32rv の `--capture` NDJSON(target-info/flash/read)をコミットし、pc-to-link/riscv-debug の `verified` の証跡にする。
3. **WCH-Link error 応答 frame 形式**([pc-to-link](protocols/pc-to-link.ja.md) §3 todo): 異常系(target 無し `0x55` 等)の capture 収集。

**P2 — 要 capture(実機/純正ツール)**

4. **factory ISP を byte 単位で確定**([pc-to-device-isp](protocols/pc-to-device-isp.ja.md)): WCHISPTool の USB / Serial 各モードを usbmon 収集 → `0xAx` frame・**XOR key 算法(CH32V/CH32X/CH55x 別)**・Erase エンコードを確定。ISP を実装可へ。
5. **WCH IAP の USB frame**([serial-and-print](protocols/serial-and-print.ja.md) §1): EVT `CH32X035_IAP/ch32x035_usbfs_device.c` から USB endpoint/frame を転記(ローカルにあり)+ WCHMcuIAP の capture。UART は確定済み。
6. **DAP mode**([dap](protocols/dap.ja.md)): WCH-Link を DAP mode に切替え、mode 切替 byte と CMSIS-DAP のやり取りを capture。

**P3 — 自作 probe/線を作る場合のみ**

7. **SWIO の pulse 幅タイミング**([link-to-target](protocols/link-to-target.ja.md) §3): `CH32V003RM`(datasheet の debug/SDI 章、ローカル)+ cnlohr bit-bang firmware(ESP32-S2 funprog / AVR zooswio)から抽出。
8. **RVSWD の bit フレームをロジアナで verify**([link-to-target](protocols/link-to-target.ja.md) §3): attested → verified。STOP 波形・クロックも実測。
9. custom BL 各実装(wch-uf2 / Swindle DFU / PlumBL)の header/CRC/entry を source から転記([custom-bootloader](protocols/custom-bootloader.ja.md))。

## 4. すぐ着手できる(ローカルソースだけで完結)

- P1-1(write_mem 転記、ch32rv-dmi)
- P1-2(capture fixture コミット、ch32rv)
- P2-5(IAP USB frame、EVT usbfs_device.c)
- P3-7(SWIO タイミング、CH32V003RM + cnlohr)

capture が要るのは P1-3 / P2-4 / P2-6 / P3-8(実機・純正ツール・ロジアナ)。
