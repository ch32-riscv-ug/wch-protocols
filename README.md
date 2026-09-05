<!-- en: Reverse-engineered & verified protocol notes for programming and debugging WCH CH32
     RISC-V microcontrollers (WCH-Link probe, factory ISP, RISC-V Debug Module, CMSIS-DAP).
     Language-neutral byte layouts so tools in any language (Python, Rust, C, ...) can reuse them. -->

# wch-protocols

WCH の CH32 RISC-V マイコンを**書き込み・デバッグする際の protocol** を、層ごとに解読・検証してためる知識ベース。特定言語のツールに依存しない仕様(byte レイアウト・レジスタ番地・手順)を置き、Python / Rust / C など任意の言語でツールを作るときの一次資料にする。

一次実装([ch32rv](../ch32rv) を含む先行実装)は「仕様書として読む」対象。ここに載せる内容は**実機 capture で裏を取ってから** `verified` にし、裏の取れないものは status を明示する。

## この repo の位置づけ(運用)

1. **まずここで下調べ**する(既知資料の突き合わせ → capture で検証 → 仕様化)。
2. 仕様が固まってから**ツール側(ch32rv 等)で実装**する。
3. ツール側の実装中に判明した事実は**ここへ還流**する。

## 全体像 — 4 つの経路 × 層モデル

target(チップ)に到達する経路は 4 つあり、通る層が違う。詳しくは [guides/overview.ja.md](guides/overview.ja.md)。

```
                                   ┌─────────── 層 ───────────┐
経路                                物理        転送         プロトコル              アプリ
─────────────────────────────────────────────────────────────────────────────────────
① debug probe   PC ─USB─ WCH-Link ─SWIO/RVSWD─ target
                          USB       USB bulk    WCH-Link cmd  ┐
                          + 1/2線   + 線上DMI   + DMI(RISC-V)├ RISC-V Debug Module
                                                              ┘  → flash/mem/reg/semihosting
② factory ISP   PC ─USB──────────────────────── target(内蔵bootloader)
                          USB       USB bulk    ISP protocol   flash/option/config
③ bootloader    PC ─USB/UART─────────────────── target(app bootloader)
                          USB/UART  DFU/UF2/HID  各protocol     flash
④ DAP           PC ─USB─ WCH-Link(DAPmode) ─SWD/JTAG─ ARM target
                          USB       USB bulk    CMSIS-DAP      ARM debug
```

- **①の要点**: RISC-V の **Debug Module / DMI**(ベンダ非依存の RISC-V Debug Spec)が、WCH-Link の USB protocol(WCH 固有)の中に `DmiOp`(cmd `0x08`)として乗り、Link が線上信号に変換する。つまり「WCH 固有の殻」の中に「RISC-V 標準の中身」が入っている。
- 各層の詳細は [guides/advanced.ja.md](guides/advanced.ja.md)。

## いまわかっていること(status)

| 領域 | ファイル | 状態 |
|---|---|---|
| PC 側 USB ドライバ層(**Windows は 2 系統**) | [protocols/pc-usb-driver.ja.md](protocols/pc-usb-driver.ja.md) | Windows **verified** / 他 attested |
| PC ↔ WCH-Link(USB) | [protocols/pc-to-link.ja.md](protocols/pc-to-link.ja.md) | **大半 verified**(実機 capture 済み) |
| RISC-V Debug Module(DMI 上) | [protocols/riscv-debug-module.ja.md](protocols/riscv-debug-module.ja.md) | **大半 verified** |
| WCH-Link ↔ target(SWIO/RVSWD 線) | [protocols/link-to-target.ja.md](protocols/link-to-target.ja.md) | **RVSWD 線は attested**(bit フレーム判明)/ SWIO は todo |
| PC ↔ target(factory ISP、USB / UART シリアル) | [protocols/pc-to-device-isp.ja.md](protocols/pc-to-device-isp.ja.md) | **attested**(3 実装一致、USB 経路は byte 化・自前 capture 未) |
| **WCH IAP**(EVT の app 内 bootloader、UART / USB 書込。3 世代・12 シリーズ) | [protocols/wch-iap.ja.md](protocols/wch-iap.ja.md) | **attested・実装可**(EVT 転記、自前 capture 未) |
| target 側シリアル I/O(USART printf / SDI printf、全シリーズ表) | [protocols/serial-and-print.ja.md](protocols/serial-and-print.ja.md) | **attested**(WCH 公式 EVT ソース) |
| custom bootloader(BOOT 領域表・切替レジスタ・HID scratchpad BL protocol、DFU/UF2/UART/OTA 事例) | [protocols/custom-bootloader.ja.md](protocols/custom-bootloader.ja.md) | **reference / attested**(BOOT 領域・HID BL は実装可) |
| software USB(V003 系の bit-bang USB。hardware USB 無し chip) | [protocols/software-usb.ja.md](protocols/software-usb.ja.md) | **reference / attested** |
| CMSIS-DAP(ARM mode) | [protocols/dap.ja.md](protocols/dap.ja.md) | **todo** |
| **DMI Bridge Protocol**(host ↔ 汎用 probe。この repo で唯一の**自前設計**) | [protocols/dmi-bridge.ja.md](protocols/dmi-bridge.ja.md) | **draft** |
| 自作 probe / host ツール landscape(採用事例・言語・リンク) | [references/probe-ecosystem.ja.md](references/probe-ecosystem.ja.md) | **reference** |

status 語彙: `verified`(自前 capture で確認)/ `attested`(複数の先行実装が一致)/ `single-source`(単一実装のみ)/ `conflict`(実装間で矛盾。要 capture)/ `todo`(存在の証拠のみ) / `draft`(**解読ではなく自前設計**。実装・実測は未)。

各ドキュメントの**実装可否判定と、穴・次に集める情報**は [coverage.ja.md](coverage.ja.md)。

## ディレクトリ

- [`guides/`](guides/) — 全体を説明するガイド(初心者向け [overview.ja.md](guides/overview.ja.md) / 上級者向け [advanced.ja.md](guides/advanced.ja.md))
- [`protocols/`](protocols/) — 領域別の実プロトコル仕様([索引](protocols/README.ja.md))
- [`references/`](references/) — 自作 probe・host ツールの landscape([probe-ecosystem.ja.md](references/probe-ecosystem.ja.md):採用事例・言語・license・リンク)、**汎用 probe と PC 連携の検討**([generic-probe-design.ja.md](references/generic-probe-design.ja.md):任意 MCU をライタにする設計案・transport 比較・ブラウザ書込・足りないもの)、**マイコンレス直接書込のための bootloader 設計空間**([bootloader-design-space.ja.md](references/bootloader-design-space.ja.md):BL への entry 方式・能力・chip 別制約・BL↔Core↔host 契約・内蔵ライタ MCU という第 3 の道)、**エコシステムの前提**([ecosystem-any-hardware.ja.md](references/ecosystem-any-hardware.ja.md):hardware 制御度 4 階層・共通/差替の境界・連鎖 bootstrap・**USB VID/PID/serial 方針 = pid.codes + chip UID**)
- [`captures/`](captures/) — 検証用 capture の取り方と参照 fixture([captures/README.ja.md](captures/README.ja.md))
- [`experiments/`](experiments/) — **実測の規則と実験台帳**([README.ja.md](experiments/README.ja.md):計画 → 実行 → レポート、フォルダ構成、証拠の水準 / [LEDGER.ja.md](experiments/LEDGER.ja.md):事実・候補・未決)。実験コードは `ch32rv-probe` 側

## 言語

現状は検討中の内容が多いため日本語(`.ja.md`)。仕様が安定した領域から英語主 + `.ja.md` 相互リンクへ移行する(byte レイアウト・レジスタ表・コードは元から言語非依存)。

## ライセンス

MIT([LICENSE](LICENSE))。
