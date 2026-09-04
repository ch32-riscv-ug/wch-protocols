# custom bootloader(経路 ③: DFU / UF2 / UART / HID / OTA)

状態: **reference / attested**(実装事例は多数あるが自前 capture 未)。層は L3(各 bootloader protocol)。経路 ③ — user が自分で焼く bootloader。factory ISP([pc-to-device-isp.ja.md](pc-to-device-isp.ja.md))や WCH IAP([serial-and-print.ja.md](serial-and-print.ja.md) §1)とは別で、DFU/UF2/UART/HID/network など transport が多彩。

## 1. bootloader の種類(格納場所で分ける)

| 種類 | 格納場所 | 導入者 | 主目的 | 消去事故耐性 |
|---|---|---|---|---|
| factory ISP | system/BOOT 領域 | 工場出荷時 | USB/UART から code flash 書込 | code flash 消去後も残るが entry 条件依存 |
| custom system bootloader | 書換可 system/BOOT 領域 | 最初に debug probe 等で導入 | factory ISP 置換、user flash 全量確保 | system 領域を壊すと probe 救出要 |
| custom user-flash bootloader | code flash 先頭等 | debug probe/factory ISP で導入 | DFU/UF2/UART/OTA | mass erase で消える。APP の link offset 要 |
| application IAP | application 内 | app と同時 | 稼働中に自己更新 | app 破損時は入れない。recovery stub 併用推奨 |

## 2. 実装事例(OSS / WCH 公式)

| 実装 | target / transport | 配置・image 条件 | host | 言語 | 主な機能・注意 |
|---|---|---|---|---|---|
| WCH 公式 EVT `USART_IAP` | V003/V00X、UART | IAP + offset APP | WCHMcuIAP sample | C | erase/program/verify/jump。→ [serial-and-print.ja.md](serial-and-print.ja.md) §1 |
| WCH 公式 EVT `UART_USB_IAP` / `USB_UART` | V103/V30x/V407/X035/X315/M030 等 | user flash 予約 + APP offset | WCHMcuIAP sample | C | UART と hardware USB の両入口 |
| WCH 公式 EVT `ETH_IAP` | V307 Ethernet | user flash 予約 | sample protocol | C | network IAP。製品には認証・rollback 追加要 |
| WCH 公式 EVT `HOST_IAP` | V103/V307/X035/M030 の USB host | target が USB メモリを読む | PC 不要 | C | 現場更新。媒体 image の真正性確認は別途 |
| [rv003usb bootloader](https://github.com/cnlohr/rv003usb/tree/master/bootloader) | V003、GPIO **software USB** low-speed **HID** | 1,920 byte system 領域 | minichlink | C | **driver 不要**。~5 秒 timeout / button / host 検出。自己更新はしない。→ [software-usb.ja.md](software-usb.ja.md) |
| `rv003usb/bootloader_v006` | V006 系、software USB | V00X 向け別実装 | minichlink | C | 旧 V003 版と別。README 整備途上 |
| [ch32fun `examples_usb/bootloader`](https://github.com/cnlohr/ch32fun/tree/master/examples_usb/bootloader) | X035/CH5xx、**hardware USB** | 非破壊書込・stub 実行 | minichlink | C | rv003usb bootloader の移植・発展。**sketchpad buffer で binary stub を RAM 実行**(機能追加は host 側だけ)。→ [software-usb.ja.md](software-usb.ja.md) §5 |
| [tinyboot](https://github.com/OpenServoCore/tinyboot) | V003/V00X/V103、UART・1 線 UART・**RS-485** | system/user flash mode | Rust `tinyboot` CLI | Rust | **CRC16**・info・retry・trial boot/confirm。transport 拡張可 |
| [wch-uf2](https://github.com/ArcaneNibble/wch-uf2) | CH32V2xx の USBD | 先頭 **4 KiB** 予約、APP `0x08001000` | OS の MSC + **UF2 copy** | C | double reset、flash/RAM download。V3xx 非対応、hardcoded 値の family 化要 |
| [Swindle CH32V3x DFU BL](https://github.com/mean00/swindle_bootloader_ch32v3x) | CH32V3x hardware USB | 先頭 **16 KiB** 予約(実 ~6 KiB)、APP `0x4000` | `dfu-util` | C | RAM marker/button/invalid CRC で DFU。12-byte header + **CRC32** |
| [PlumBL](https://github.com/HaiMianBBao/PlumBL) | CH32V30x ほか、CherryUSB **DFU/U2F** | user flash 予約 | `dfu-util`/U2F tool | C | multi-platform port 例 |

## 3. transport 比較

| transport | driver/host 依存 | 速度目安 | 長所 | 短所 |
|---|---|---|---|---|
| software USB HID | OS 標準 HID | USB low-speed | V003 でも USB 端子だけで更新 | pin/clock/割込制約、signal 品質 |
| hardware USB vendor/HID | HID なら driver 不要 | FS/HS | WCH sample/minichlink 互換を作りやすい | 独自 host protocol 保守 |
| USB DFU | `dfu-util` 等 | FS/HS | 標準 protocol、CLI 既存 | Arduino 側で image/offset 管理 |
| USB UF2 MSC | OS の file copy | FS | UX 最良、追加 driver 不要 | MSC emulation 複雑、copy 完了判定・大 image 再起動注意 |
| UART | USB-UART/serial | 115.2 kbps〜 | USB なしでも移植しやすい | port/baud/reset 配線が board 依存 |
| 1 線 UART / RS-485 | transceiver/half-duplex | 配線依存 | 長距離・multi-drop・既設 bus | node address、衝突回避、DE timing |
| Ethernet/Wi-Fi/BLE | network stack | 可変 | 遠隔更新 | 認証・暗号・再送・rollback 必須 |
| USB host/SD/SPI flash | 外部媒体 | 可変 | PC なし更新 | 媒体破損・image 選択・電源断対策 |

## 4. bootloader 実装に共通の仕様項目

新規に設計/解読するとき繰り返し現れる:

- **image header**: magic / format version / target / load address / 長さ / version / hash。
- **CRC**(破損検出、例 CRC16/CRC32)と **署名**(作成元確認)は役割が別。
- **更新失敗対策**: bootloader 常駐 / A/B slot / download slot からの copy / trial boot / watchdog / app の `confirm()`。
- **boot entry**: button / double reset / RAM magic / application command / 無効 image 検出 / BOOT pin / option byte。
- **user-flash bootloader** は bootloader 領域 + metadata + APP offset のぶん app 容量が減る。
- **app への jump 時**: vector table/interrupt・clock・USB pull-up・peripheral・stack・`.data/.bss` の状態。
- **識別情報**: USB=VID/PID・serial・DFU alt setting・UF2 family ID / UART=port・node ID。

## 5. 未解読 / 要調査

- WCH IAP の **USB 側フレーム**(EVT `usbfs_device.c` から確定、UART は [serial-and-print.ja.md](serial-and-print.ja.md) §1 で確定済み)。
- wch-uf2 / Swindle DFU の header・CRC・entry の実バイト(capture)。
- 各 family の予約サイズ・APP offset の一覧(EVT/各実装から集約)。

## 参照

- WCH IAP(UART 実測済み): [serial-and-print.ja.md](serial-and-print.ja.md) §1
- factory ISP との区別: [pc-to-device-isp.ja.md](pc-to-device-isp.ja.md)
- host ツール・probe 一覧: [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md)
- **自分で BL を設計するときの設計空間**(entry 方式・能力・chip 別制約・BL↔Core↔host 契約・内蔵ライタ MCU): [../references/bootloader-design-space.ja.md](../references/bootloader-design-space.ja.md)
