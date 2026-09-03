# probe / ツールのエコシステム(採用事例・言語・リンク)

状態: **reference**(landscape 情報。protocol 仕様そのものではなく「どの実装を読めば線上・host protocol がわかるか」「他言語で作る人がどれを参考にできるか」の索引)。出典は各 project の公開 repo / WCH 公式資料。

WCH-LinkE 以外にも、**汎用 MCU(CH32V003・ESP32-S2/S3・RP2040 等)を書込/デバッグ probe に仕立てる firmware** が多数ある。ここは線上解読([link-to-target.ja.md](../protocols/link-to-target.ja.md))の一次資料であり、Python/Rust/C 等でツールを作るときの参照実装表でもある。

## 1. 汎用 MCU を probe 化する firmware(自作 probe)

| project | probe MCU | 言語 | host I/F | host protocol | 1線 | 2線 | flash | debug | target print | license | 対象・成熟度 |
|---|---|---|---|---|:--:|:--:|:--:|:--:|:--:|---|---|
| [WCH 公式 CH32F103 1-Line 例](https://github.com/openwch/ch32v003/tree/main/CH32V003_1Line_Base_on_CH32F103) | CH32F103 | C | 独自 sample | — | ○ | × | 基礎 | DMI primitive | × | (WCH) | **一次資料**。完成 host 製品ではない |
| [PicoRVD](https://github.com/aappleby/picorvd) | RP2040 (PIO) | C/C++ | USB CDC | probe 上 GDB server | ○ | × | GDB `load` | ○ break/step | probe console 別 | MIT | V003 専用、very alpha。**層分離が明快** |
| [Swindle](https://github.com/mean00/swindle) | RP2040 (PIO) | C/C++ + Rust | USB CDC | probe 上 GDB server(Black Magic 系) | × | ○ | GDB `load` | ○ break/step | RTT・UART(非 SDI) | GPL-3.0 系 | V20x/V30x。RP2040 stable / RP2350 experimental。`rvswd.pio` が 2 線の一次資料 |
| [rvswdio_programmer](https://github.com/cnlohr/rv003usb/tree/master/rvswdio_programmer) | CH32V003 | C | low-speed USB HID | minichlink | ○ | ○ | ○ | basic GDB | minichlink semihost | MIT 系 | V003/00x/20x/30x/X03x/CH57x 等。experimental/RFC。**1/2 線自動判別** |
| [ESP32-S2 funprog](https://github.com/cnlohr/esp32s2-cookbook/tree/master/ch32v003programmer) | ESP32-S2 | C | vendor HID | minichlink funprog | ○ | ○(source) | ○ | minichlink 側 | minichlink terminal | MIT 系 | README は V003 中心。現行 source に RVSWD/family 検出。timing-sensitive |
| [ESP32-S3 CH32 programmer](https://github.com/Ishu1519/esp32s3-ch32-programmer) | ESP32-S3 | C + Python(host) | USB serial | 独自 + Python | ○ | × | ○ + verify | DMI 操作(GDB 無) | × | (repo 参照) | V003 baseline。2026 開始で新しい |
| [NHC-Link042](https://github.com/NgoHungCuong/NHC-Link042) | STM32F042 | C | USB vendor bulk | minichlink backend | ○ | × | ○ | △ generic GDB | △ minichlink terminal | **MIT** | V003。既存 STM32F042 board 再利用 |
| [Flipper Zero flasher](https://github.com/sukvojte/wch_swio_flasher) | Flipper Zero | C | — | NHC-Link042 emulation/minichlink | ○ | × | ○ | △ | △ | (repo 参照) | V003 で確認 |
| [Ardulink / zooswio](https://github.com/zoobab/zooswio) | AVR Arduino 等 | C | UART | minichlink `-C ardulink` | ○ | × | △ | × | × | (repo 参照) | Uno/Nano 等旧機材で bootstrap。WIP/不安定表記 |
| [WCH_WebLink](https://github.com/Subjective-Reality-Labs/WCH_WebLink) | ESP32 / ESP32-C3 | C | Wi-Fi WebSocket / Web UI | 独自(browser) | ○ | × | ○ | source debug 無 | SWIO terminal / UART | (repo 参照) | V003 のみ。読出し等未実装 |

## 2. host protocol 方式(probe が PC とどう話すか)

自作 probe が採る「PC との会話方式」は 3 系統。新しく probe やツールを作るときの設計選択。

| 方式 | 概要 | 利点 | 問題 |
|---|---|---|---|
| **WCH-Link USB 互換** | 本物の WCH-Link と同じ USB protocol([pc-to-link.ja.md](../protocols/pc-to-link.ja.md))を名乗る | probe-rs / wlink / WCH OpenOCD を小変更で使える。probe が DMI を出せば probe-rs の target 層を再利用 | 公式 VID `0x1a86` を独自製品で名乗れない(独自 VID/PID を host に追加要)。非公式解析で firmware 版差。version 偽装は将来 host が未実装 command を送る危険 |
| **minichlink funprog HID 互換** | ESP32-S2/CH32V003 firmware + minichlink host | HID control transfer で一般 OS が driver 不要。low-level read/write・block write・power・terminal | probe-rs/OpenOCD から直接使えない。minichlink の target/debug 層と密結合。low-speed HID は帯域/latency 上限 |
| **probe 上 GDB server**(PicoRVD/Swindle 型) | probe 内に GDB server を持ち host は標準 GDB | host は GDB だけ(VS Code 等に繋ぎやすい)。DMI 往復の一部を probe 内で完結 | ELF/Arduino uploader・machine-readable な probe 列挙・複数 lane は別途。chip DB/flash algorithm が probe firmware に入りがち。Black Magic 系だと GPL-3.0 |

## 3. probe MCU の選択(採用実績)

| MCU | 特性 | 弱点 |
|---|---|---|
| **RP2040** | PIO で 1 線パルス幅符号化と 2 線 clocked を決定論的に実装。PicoRVD(1 線)と Swindle(2 線)の資産が同一 MCU。USB device・固有 ID・UF2 recovery・安価 board | native high-speed USB でない。安価互換 board は level shifter/Vref/保護/power switch 無し |
| RP2350 | RP2040 より RAM/性能に余裕。Swindle で experimental | 確認できる実装例が RP2040 より少ない |
| **ESP32-S2/S3** | SWIO/RVSWD code、Wi-Fi/Web UI や funprog HID | S2 は timing-sensitive な GPIO + critical section。非 V003 の検証範囲が不明確 |
| **CH32V003 自身** | `rvswdio_programmer` が probe MCU に使用。安価 | USB は low-speed software 実装、RAM/flash 小。初回書込に別 programmer が要る |

## 4. 公式・市販 probe

| 装置 | probe MCU/形態 | 1線 | 2線 | 公式 SDI | 状態 |
|---|---|:--:|:--:|:--:|---|
| **WCH-LinkE** | CH32V305 系公式 probe | ○ | ○ | **○** | 現行の基準装置。[WCH-Link manual V2.4](https://www.wch.cn/uploads/file/20250124/1737704462135866.pdf) |
| WCH-LinkW | CH32V208 系、wireless | ○ | ○ | × (manual では LinkE 限定) | host tool 側の対応差あり |
| 旧 WCH-Link | CH549 系 | × (V003/V00X 不可) | ○ | × | 生産終了。最大 baud 低い |
| WCH-DAPLink | WCH 公式 CMSIS-DAP 系 | × | × | × | **ARM 用**(CH32 RVSWD 用ではない)。HID/WinUSB mode |
| [WCH-MCU-DL](https://www.wch.cn/uploads/file/20240821/1724227120114035.pdf) | offline/batch writer | mode 依存 | ○ SWD mode | × | PC で設定後 standalone。量産向け |

## 5. host ツールと言語(他言語で作る人向け)

| tool | 言語 | 用途 | license |
|---|---|---|---|
| [probe-rs](https://github.com/probe-rs/probe-rs) | **Rust** | probe 統合・flash・debug(GDB/DAP)。3 OS prebuilt | MIT OR Apache-2.0 |
| [wlink](https://github.com/ch32-rs/wlink) | **Rust**(nusb/libusb) | WCH-Link 専用 CLI/lib。power/復旧 erase/SDI/mode 切替 | MIT OR Apache-2.0 |
| [wchisp](https://github.com/ch32-rs/wchisp) | **Rust**(libusb/serial) | USB/UART factory ISP | GPL-2.0 |
| [minichlink](https://github.com/cnlohr/ch32fun/tree/master/minichlink) | **C**(libusb/HID/serial) | WCH/互換 probe/ISP 統合。自作 probe backend 多数 | MIT |
| WCH OpenOCD | **C**(OpenOCD fork) | WCH-Link flash/debug(GDB) | GPL 系 |
| [tinyboot](https://github.com/OpenServoCore/tinyboot) | **Rust**(serial) | UART/RS-485 custom bootloader host。V003/V00X/V103 | MIT OR Apache-2.0 |
| [rvprog.py](https://github.com/wagiminator/MCU-Flash-Tools/blob/main/rvprog.py) | **Python**(USB) | 小さい WCH-Link flasher **参考実装**(protocol 理解用) | MIT |
| [dfu-util](https://dfu-util.sourceforge.net/) | **C**(libusb) | USB DFU custom bootloader。標準 DFU なら vendor 非依存 | GPL-2.0 |

- **他言語で WCH-Link を叩く最短の参考**: Python は `rvprog.py`、Rust は `wlink`、C は `minichlink`。protocol 本体は [pc-to-link.ja.md](../protocols/pc-to-link.ja.md)。
- SDI print は probe-rs が[未対応 issue](https://github.com/probe-rs/probe-rs/issues/3023)、wlink は SDI + LinkE UART watch を同一 session で扱える。

## 参照

- 線上(SWIO/RVSWD)の解読: [../protocols/link-to-target.ja.md](../protocols/link-to-target.ja.md)
- WCH-Link USB protocol: [../protocols/pc-to-link.ja.md](../protocols/pc-to-link.ja.md)
- custom bootloader(DFU/UF2/UART/HID)の実装事例: [../protocols/custom-bootloader.ja.md](../protocols/custom-bootloader.ja.md)
