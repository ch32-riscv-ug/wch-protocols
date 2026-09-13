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
- [`references/`](references/) — **【DUT harness】**の**道具としての定義とユースケース**([harness-tool-definition.ja.md](references/harness-tool-definition.ja.md):**何の道具か・他では解決できない強み・方向性の候補**。取りまとめ第 1 部)、**各論の選択肢集**([harness-choices.ja.md](references/harness-choices.ja.md):PID / 既存 protocol 対応 / profile の下端 / V003 BL。**結論は出さない**)、**probe ↔ PC の通信路の分離**([harness-channels.ja.md](references/harness-channels.ja.md):物理 IF(CDC / HID / bulk / UART / IP)と論理 IF(DMI / trace / emu / ctl)の対応、**serial stream の識別子 = (source, channel)**、**PC 側の IF は CDC か DMI のラッパーの 2 つ**、**pty は Windows で使えない**、**CDC の上限は endpoint 予算(RP2040 = 7)**、**USB クラス 8 種の得失**。target 側は範囲外)、**その内側の仮想 IF の分離**([harness-virtual-if.ja.md](references/harness-virtual-if.ja.md):**通信の形は 2 つ**(`req/resp` / `event`)で、**分けるのは「いつ送るべきかを知っているのはどちらか」**という導出。**`stream` は `event` の高頻度な場合だが「連続していること自体が情報」の 1 点だけ違う**。**24 種を全列挙**したうえで、**分離を強制するのは輻輳時の 2 現象だけ**と判定 — **列の占有**(HID で 1024 B の波形が先に入ると `dmi` が 1 ms → 16 ms)と**運命の共有**(**波形は「落とさずに済ませられない/撮り直せる」、serial は「落とさずに済む/取り返せない」で完全な裏返し** → 混ぜると落とさずに済んだ方が落ちる)。**`mtu` 超えは分離の理由ではなく断片化の話**。**案 A(1 本)〜D(channel 別)を 2 現象で評価**し、**現行仕様は案 B + `dropped` による部分解**。**現行の各選択の理由**と、**波形を入れる場合に足りない 6 点**)、**議論の所在**([harness-index.ja.md](references/harness-index.ja.md):コア(ArduinoCore-CH32)/ ライタ(ch32rv)/ ベンチ(EmbedBench)/ ここ の 4 repo に分散した文書と論点の索引。**論点 → どこにあるか**の横断マップ)、自作 probe・host ツールの landscape([probe-ecosystem.ja.md](references/probe-ecosystem.ja.md):採用事例・言語・license・リンク)、**汎用 probe と PC 連携の検討**([generic-probe-design.ja.md](references/generic-probe-design.ja.md):任意 MCU をライタにする設計案・transport 比較・ブラウザ書込・足りないもの)、**マイコンレス直接書込のための bootloader 設計空間**([bootloader-design-space.ja.md](references/bootloader-design-space.ja.md):BL への entry 方式・能力・chip 別制約・BL↔Core↔host 契約・内蔵ライタ MCU という第 3 の道)、**bootloader 横断調査**([bootloader-survey-plan.ja.md](references/bootloader-survey-plan.ja.md):調査設計 / [bootloader-survey.ja.md](references/bootloader-survey.ja.md):分析結果 — **差は series ではなく EVT サンプルの系譜で決まる**、protocol は `#define` 5 個で統一可能、stub 47 本が RV32EC 安全、flash driver は 12 series → 5 class / [data/bootloader-survey/](references/data/bootloader-survey/):生データ 26 テーブル + stub の hex・逆アセンブル / [unified-bootloader-design.ja.md](references/unified-bootloader-design.ja.md):**調査結果から「どこで分けられるか」を並べた設計空間**。決定書ではない。status = draft)、**エコシステムの前提**([ecosystem-any-hardware.ja.md](references/ecosystem-any-hardware.ja.md):hardware 制御度 4 階層・共通/差替の境界・連鎖 bootstrap・**USB VID/PID/serial 方針 = pid.codes + chip UID**)、**1 target 専有の計測 probe**([dut-harness-design.ja.md](references/dut-harness-design.ja.md):ロジアナ + 周辺エミュ + debug 線を 1 つの時間軸に載せる案 — RP2040-Zero のピン割当、cross-domain trigger、**CH32 family 別の配線衝突表は [ch32-device-data から生成](references/data/harness-wiring/README.ja.md)**。**アイデアメモ**)、**board 別の到達範囲**([harness-board-survey.ja.md](references/harness-board-survey.ja.md):RP2040 / RP2350 / ESP32-S3 / CH32X035 / X033 を 8 軸で比較 — **X03x の PIOC**(2 ピン専用の 48 MHz 単一サイクル engine)、**S3 の LCD_CAM + PSRAM で深さだけ桁違い**、**RP2350 の errata E9 は LA 用途に直撃**)、**probe パターンの共存**([probe-pattern-coexistence.ja.md](references/probe-pattern-coexistence.ja.md):書込のみ / 複数 target / 書込+LA / harness を 1 仕様に載せられるか — **能力は build 時、役割は実行時**)、**ビルドイン型 probe と自己書換えの caps 化**([builtin-probe-and-self-update.ja.md](references/builtin-probe-and-self-update.ja.md):**PID を分ける基準は descriptor であって役割ではない** — `attach`(external/onboard/socket/self)と `self_update` を caps で名乗れば、board が何種類あっても PID は 4 つで足りる。X035(HW USB)と V003(software USB)が同じ形で載る)、**V003 の BL を入れ替えるべきか**([v003-bootloader-replacement.ja.md](references/v003-bootloader-replacement.ja.md):**否 — 標準 BL は既に stub 実行型で 1,920 B の限界**。機能追加は host stub → app 側 → BL の順で、software USB の app 利用は `demo_terminal` が既にある)
- **ESP32-P4 の USB 2.0 HS と 2 channel capture の到達点**([references/p4-usb-hs-summary.ja.md](references/p4-usb-hs-summary.ja.md):E063〜E085 を 1 枚に。**vendor bulk 約 24 MB/s**(効くのは 1 転送あたりの packet 数。host 側の URB は大きさも depth も効かない。漸近線は 24.64 MB/s = microframe あたり 6 transaction)、**2 channel は batch 160 Msps / 継ぎ目なし 96 Msps で sample 精度**、**Windows で WinUSB が当たる**(MS OS 2.0 を単一 interface では flat に置く)、**stock の PulseView / sigrok が IP 経由で継ぎ目なく取れる**、**CDC は転送途中で packet を捨てる**(13%)ので帯域が要る経路では使わない)。改修依頼は [EspUsbDevice 宛](references/espusbdevice-change-requests.ja.md)(**全件対応済み**) / [EspUsbHost 宛](references/espusbhost-change-requests.ja.md)(未依頼)、**着手順の提案**は [usb-library-change-plan](references/usb-library-change-plan.ja.md)
- **適当に挿してから対応を探す**([references/pin-discovery.ja.md](references/pin-discovery.ja.md):WT9932P4-TINY で**触ってよいピンは実測で 34 本**、推奨は **IO16〜23 / IO26〜33 の 16 本**。**IO24 / IO25 は J3 の USB で禁止**。**CH32 は DAC を持たないので 3 値を作るのは P4 側** — `pull-up + pull-down` で **1.47 V**。符号を使えば 16 本を 5 slot で当てられる)
- **ロジアナの sample rate をどう選ばせるか**([references/p4-sample-rate-selection.ja.md](references/p4-sample-rate-selection.ja.md):**clock は 1 MHz 刻みでも 10 MHz 刻みでも全部出る**(160 MHz の分数分周)。**上限は byte rate 23 MB/s で決まり、8ch 23 / 4ch 46 / 2ch 96 Msps**。batch なら 160 Msps。**pin は飛び飛び・順不同で自由**。**FX2 の実用域 16〜20 Msps は置き換えになる**)
- **capture を転送前に圧縮できるか**([references/capture-compression.ja.md](references/capture-compression.ja.md):効くかどうかは信号周波数ではなく **oversampling 比 K** で決まる。**RLE の圧縮率 ≈ K/8** で、**K<8 では 2 倍に膨らむ**。実データ(2ch PWM @ 90 Msps)で RLE 14.1x / deflate 257x。**SPI なら圧縮より CS で gate するほうが強く、CPU も使わない**)
- [`captures/`](captures/) — 検証用 capture の取り方と参照 fixture([captures/README.ja.md](captures/README.ja.md))
- [`experiments/`](experiments/) — **実測の規則と実験台帳**([README.ja.md](experiments/README.ja.md):計画 → 実行 → レポート、フォルダ構成、証拠の水準 / [LEDGER.ja.md](experiments/LEDGER.ja.md):事実・候補・未決)。実験コードは `ch32rv-probe` 側

## 言語

現状は検討中の内容が多いため日本語(`.ja.md`)。仕様が安定した領域から英語主 + `.ja.md` 相互リンクへ移行する(byte レイアウト・レジスタ表・コードは元から言語非依存)。

## ライセンス

MIT([LICENSE](LICENSE))。
