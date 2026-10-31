<!-- en: Reverse-engineered & verified protocol notes for programming and debugging WCH CH32
     RISC-V microcontrollers (WCH-Link probe, factory ISP, RISC-V Debug Module, CMSIS-DAP).
     Language-neutral byte layouts so tools in any language (Python, Rust, C, ...) can reuse them. -->

# wch-protocols

WCH の CH32 RISC-V マイコンを**書き込み・デバッグする際の protocol** を、層ごとに解読・検証してためる知識ベース。特定言語のツールに依存しない仕様(byte レイアウト・レジスタ番地・手順)を置き、Python / Rust / C など任意の言語でツールを作るときの一次資料にする。

先行実装(wlink / probe-rs / minichlink / WCH OpenOCD / [ch32rv](https://github.com/ch32-riscv-ug/ch32rv) など)は「仕様書として読む」対象。ここに載せる項目には、どこまで裏が取れているかを [status](#status) で明示する。`verified` は自前の実機での確認があるものだけに付ける。未確認の項目を使うかどうかは実装側が status を見て決める。

## この repo の位置づけ(運用)

1. **protocol の事実の正本はここ**。実装(ch32rv など)の repo は protocol の仕様を持たず、ここを引く。
2. 実装の側で測った事実(capture、実機ログ)は、測った作業の中でここに書く。測定の原本(ログ、fixture)は実装の repo に置いてよいが、仕様の主張にはここから出典として引く。
3. 外の repo からここを引くときは、版のタグを付けた GitHub の URL と、明示したアンカー(`#cmd-rst-out` の形)か E 番号を使う。見出しの文字列から自動で作られるアンカーや § 番号は、見出しを直すと変わるので使わない。

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

| 領域 | ファイル | status([語彙](#status)) |
|---|---|---|
| PC 側 USB ドライバ層(**Windows は 2 系統**) | [protocols/pc-usb-driver.ja.md](protocols/pc-usb-driver.ja.md) | Windows `verified` / 他 `attested` |
| PC ↔ WCH-Link(USB) | [protocols/pc-to-link.ja.md](protocols/pc-to-link.ja.md)(コマンド表の正本は [protocols/data/wchlink-commands.csv](protocols/data/wchlink-commands.csv)) | 大半 `verified`(LinkE fw 2.22 / CH549 fw 2.12)。一部 `single-source` / `refuted` |
| RISC-V Debug Module(DMI 上) | [protocols/riscv-debug-module.ja.md](protocols/riscv-debug-module.ja.md) | 大半 `verified` |
| WCH-Link ↔ target(SWIO/RVSWD 線) | [protocols/link-to-target.ja.md](protocols/link-to-target.ja.md) | V003 の SWIO は end-to-end `verified`、RVSWD の主要 frame は `verified`(LinkE fw 2.22)。電気的な許容限界は `todo` |
| PC ↔ target(factory ISP、USB / UART シリアル) | [protocols/pc-to-device-isp.ja.md](protocols/pc-to-device-isp.ja.md) | USB は X035(BTVER 2.60)で `verified`、他の系列は `attested`。UART は `attested` |
| **WCH IAP**(EVT の app 内 bootloader、UART / USB 書込。3 世代・12 シリーズ) | [protocols/wch-iap.ja.md](protocols/wch-iap.ja.md) | `attested`(EVT 転記、自前 capture なし) |
| target 側シリアル I/O(USART printf / SDI printf、全シリーズ表) | [protocols/serial-and-print.ja.md](protocols/serial-and-print.ja.md) | `attested`(WCH 公式 EVT ソース)。SDI の線上の poll は `verified` |
| custom bootloader(BOOT 領域表・切替レジスタ・HID scratchpad BL protocol、DFU/UF2/UART/OTA 事例) | [protocols/custom-bootloader.ja.md](protocols/custom-bootloader.ja.md) | `attested`(BOOT 領域・HID BL)。事例の節は解説 |
| software USB(V003 系の bit-bang USB。hardware USB 無し chip) | [protocols/software-usb.ja.md](protocols/software-usb.ja.md) | `attested`(解説が主) |
| CMSIS-DAP(ARM mode) | [protocols/dap.ja.md](protocols/dap.ja.md) | `todo`(mode 切替だけ `verified`) |
| 自作 probe / host ツール landscape(採用事例・言語・リンク) | [references/probe-ecosystem.ja.md](references/probe-ecosystem.ja.md) | 索引(status の対象外) |

<a id="status"></a>

## status 語彙(この repo の唯一の定義)

仕様の各項目・各行に次のどれか 1 語を付ける。ほかの文書はこの節を参照し、定義を書き写さない。

| 語 | 意味 | 必要な根拠 |
|---|---|---|
| `verified` | 自前の実機で確認した。**範囲(probe の variant と fw、chip)を併記する** | 自前の USB capture、線の capture、実機ログ、ADC などの計測。範囲の外には及ばない |
| `refuted` | 自前の実機で、主張(byte 列、受理、効果)が成り立たないことを確認した。範囲を併記する | `verified` と同じ |
| `attested` | 独立した複数の先行実装・資料が一致している。自前の実機では見ていない | 実装 2 つ以上(source の行を併記) |
| `single-source` | 1 つの実装・資料にしか無い(例: WCH OpenOCD の source だけ) | その 1 つの出典 |
| `conflict` | 実装・資料の間で食い違っている | 食い違う出典の両方 |
| `todo` | 存在の証拠(関数名、文字列)しか無い | — |
| `draft` | 解読ではなく自前の設計。実装・実測は未 | — |

規則:

- **「受理された」と「効果があった」は別に判定する。** probe が `82 …` で応答しても、target に効いたとは限らない(例: SetPower は LinkE fw 2.22 で受理されるが 3V3 は切れない)。機械可読の表([protocols/data/](protocols/data/README.ja.md))は `accepted` と `effect` を別の列に持ち、人向けの 1 語は次の順で決める。
  1. 範囲内で効果を測った: 効いた → `verified`、効かなかった → `refuted`。
  2. 効果は測っていないが受理を測った: 受理されなかった(`ff` 応答など) → `refuted`。受理された → 効果についての出典の数で `attested` / `single-source` を付け、受理の実測は `accepted` 列と本文に書く。
  3. 実機で何も測っていない: 出典の数で `attested` / `single-source` / `conflict` / `todo`。
- 根拠の種類は `evidence` 列に書く: `usb-capture` / `wire-capture` / `hw-log` / `adc` / `impl-agree` / `source` / `binary`。source や binary を読んだだけのものは `verified` にしない。
- 範囲の外(別の fw、別の chip)へは status を広げない。範囲ごとに行を分ける。
- `references/data/bootloader-survey/` の CSV の `confidence` 列は、まだこの語彙に揃えていない(そこでの `verified` は「binary / source を機械的に照合した」の意味)。

各ドキュメントの**実装可否判定と、穴・次に集める情報**は [coverage.ja.md](coverage.ja.md)。

## ディレクトリ

- [`protocols/`](protocols/) — 領域別のプロトコル仕様([索引](protocols/README.ja.md))。機械可読の表は [`protocols/data/`](protocols/data/README.ja.md)
- [`guides/`](guides/) — 全体を説明するガイド(初心者向け [overview.ja.md](guides/overview.ja.md) / 上級者向け [advanced.ja.md](guides/advanced.ja.md))
- [`coverage.ja.md`](coverage.ja.md) — 文書ごとの実装可否と、残っている問い
- [`captures/`](captures/) — 仕様の根拠にした capture(USB / 線)と解析 script([captures/README.ja.md](captures/README.ja.md))
- [`experiments/`](experiments/) — 実測の規則と実験台帳([README.ja.md](experiments/README.ja.md):計画 → 実行 → レポート、証拠の水準 / [LEDGER.ja.md](experiments/LEDGER.ja.md):事実・候補・未決)。E 番号は変えない(他の repo から番号で引用されている)
- [`references/`](references/) — 調査メモと周辺の設計検討。protocol の仕様ではない
  - 先行実装・道具の索引: [probe-ecosystem](references/probe-ecosystem.ja.md)
  - bootloader と flash stub の横断調査: [bootloader-survey](references/bootloader-survey.ja.md)(データは [data/bootloader-survey/](references/data/bootloader-survey/))、[bootloader-design-space](references/bootloader-design-space.ja.md)、[unified-bootloader-design](references/unified-bootloader-design.ja.md)(draft)、[v003-bootloader-replacement](references/v003-bootloader-replacement.ja.md)
  - 自作 probe / DUT harness の検討(OEP の前身。論点の索引は [harness-index](references/harness-index.ja.md)): harness-* 、[generic-probe-design](references/generic-probe-design.ja.md)、[ecosystem-any-hardware](references/ecosystem-any-hardware.ja.md)、[dut-harness-design](references/dut-harness-design.ja.md) ほか
  - ESP32-P4 の USB HS と logic capture: [p4-usb-hs-summary](references/p4-usb-hs-summary.ja.md)、[p4-probe-roadmap](references/p4-probe-roadmap.ja.md)、[p4-sample-rate-selection](references/p4-sample-rate-selection.ja.md)、[p4-profiling-methods](references/p4-profiling-methods.ja.md)、[capture-compression](references/capture-compression.ja.md)、[pin-discovery](references/pin-discovery.ja.md)
  - USB の識別子と driver: [windows-winusb-binding](references/windows-winusb-binding.ja.md)、[usb-host-descriptor-persistence](references/usb-host-descriptor-persistence.ja.md)、[pid-acquisition-roadmap](references/pid-acquisition-roadmap.ja.md)
- [`tools/`](tools/) — repo の保守用 script([gen_command_tables.py](tools/gen_command_tables.py): コマンド表を CSV から作る / [check_anchors.py](tools/check_anchors.py): 見出しの明示アンカーと、アンカーへのリンクを確かめる)
- [`CHANGELOG.md`](CHANGELOG.md) — 版ごとの変更

## 言語

現状は検討中の内容が多いため日本語(`.ja.md`)。仕様が安定した領域から英語主 + `.ja.md` 相互リンクへ移行する(byte レイアウト・レジスタ表・コードは元から言語非依存)。

## ライセンス

MIT([LICENSE](LICENSE))。
