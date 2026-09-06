# ロジアナ付き debug 機能(DUT harness)— 議論の所在

状態: **索引**。**議論はまとめない。どこに何があるかだけを管理する。**
基準日: 2026-09-06

複数のリポジトリで同時に要求が出ており、**要件別にファイルが増えていく**ので、所在の一覧が要る。取りまとめ文書は**別に作り、できたら §6 に索引を足す**。

> **すり合わせの場はこの repo**。各リポジトリの文書がいずれもそう書いている(コア `harness-requirements` の位置づけ、ライタ `0006` の状態、ベンチ `HARNESS_REQUESTS` の前文)。**いまは要求を広げる段階**で、この索引も裁定を含まない。

## 1. リポジトリの役割

| repo | 通称 | 立場 | harness に対する持ち分 |
|---|---|---|---|
| **wch-protocols**(ここ) | protocol | **すり合わせの場**。線層・USB 層の解読と設計メモ | 構想・board 比較・パターン共存・配線データ。**裁定はここ** |
| **ArduinoCore-CH32** | **コア** | **発議元・需要側**。Arduino core の試験が詰まっている | 要求カタログ(**H-001〜H-178**)・配線設計・テスト駆動方式 |
| **ch32rv** | **ライタ** | 同梱 uploader。probe backend の受け口を持つ | 実測値・contract・排他・attach 副作用の実例 |
| **EmbedBench** | **ベンチ** | host 検証ライブラリ。デバイス模型 22 種 | 模型の置き場所・IF の分担・引用してよい数値 |
| ch32rv-probe | — | **実装の置き場**(2026-09-06 時点で `LICENSE` のみ) | まだ無い |
| ch32-device-data | — | device DB | `index/pinout.csv` ほか。配線データの一次資料 |

## 2. 文書一覧

### 2.1 wch-protocols(ここ)

| 文書 | 何が書いてあるか | 状態 |
|---|---|---|
| [dut-harness-design.ja.md](dut-harness-design.ja.md) | **構想の本体**。同一時間軸・cross-domain trigger・障害注入・RP2040-Zero のピン割当・**§8 = family 別の配線衝突表**(生成物) | アイデアメモ |
| [harness-board-survey.ja.md](harness-board-survey.ja.md) | **board 別の到達範囲を 8 軸で比較**。RP2040 / RP2350 / ESP32-S3 / CH32X035 / X033。**X03x の PIOC**、**S3 の LCD_CAM + PSRAM**、**RP2350 errata E9** | 調査メモ |
| [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md) | **書込のみ / 複数 target / 書込+LA / harness / LA のみ / monitor のみ**の共存可否。**能力は build 時、役割は実行時** | 設計検討メモ |
| [builtin-probe-and-self-update.ja.md](builtin-probe-and-self-update.ja.md) | 内蔵ライタ型と自己書換えの `caps` 化。**PID を分ける基準は descriptor であって役割ではない** | 検討メモ |
| [v003-bootloader-replacement.ja.md](v003-bootloader-replacement.ja.md) | V003 の software USB BL を入れ替えるべきか(**否**)。サイズ削減の調査項目 | 検討メモ |
| [data/harness-wiring/](data/harness-wiring/README.ja.md) | **§8 の生成データ**。`debug_pins` / `routes`(5,243) / `pin_conflicts`(1,118) / `coverage` + 抽出スクリプト | 生成データ |
| [generic-probe-design.ja.md](generic-probe-design.ja.md) | 親の設計メモ。transport 比較・latency 見積り・足りないもの | 検討メモ |
| [ecosystem-any-hardware.ja.md](ecosystem-any-hardware.ja.md) | **VID/PID 方針**・hardware 制御度 4 階層・連鎖 bootstrap | 検討メモ |
| [../protocols/dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md) | **`dmibridge/1` の仕様**。L1/L2/L3・`caps` TLV・profile。harness はこの上に載る | draft |
| [../protocols/link-to-target.ja.md](../protocols/link-to-target.ja.md) | 線層(SWIO/RVSWD)。**harness の自己観測で `attested` → `verified` にしたい対象** | RVSWD attested / SWIO todo |
| [../experiments/LEDGER.ja.md](../experiments/LEDGER.ja.md) | 実験台帳。**E005/E006**(道具の性能)、**E002/E004**(握手)、**E010**(pytest の再 upload)、候補 `bl-size-*` | 台帳 |

### 2.2 ArduinoCore-CH32(コア)— `docs/`

索引は同 repo の `docs/README.ja.md` にもある。

| 文書 | 何が書いてあるか | 状態 / 基準日 |
|---|---|---|
| **`harness-probe.ja.md`** | **採否の評価と依頼 5-1〜5-9**。方法4 が全項目 ⬜ の理由、LinkE 由来の痛み(実測)、§6 = EmbedBench 接続 | 提案 / 2026-09-06 |
| **`harness-requirements.ja.md`** | **要求カタログ。`H-001`〜`H-178`(129 件)**。§2.2 = channel 数の需要曲線、**§3 = 相反する要求 `C-1`〜`C-8`**、§4 = どちらでもよいもの | **要求の列挙(決定でも合意でもない)** / 2026-09-06 |
| **`harness-wiring.ja.md`** | **配線設計**。`index/pinout.csv` から機械的に導出。16ch class を具体化、**16 → 20ch で被覆が 18/24 → 24/24 series** | 提案 / 2026-09-06 |
| **`harness-testing.ja.md`** | **テストの駆動方式と依頼 5-10〜5-16**。4 つの宣言と resolver、セッション、`socket://`、DUT agent、**§9 = 模型の置き場所** | 提案 / 2026-09-06 |
| `test-strategy.ja.md` | HIL protocol・**論理信号名の義務化**・golden capture と replay 層 | (既存) |
| `upload-and-fixture.ja.md` | fixture manifest・識別の優先順位・preflight・**fail closed** | (既存) |
| `tests/TEST_PLAN.ja.md` | **検証方法 1〜4**。方法4(ロジアナ)が空白 | (既存) |
| `tests/manual/README.ja.md` | 既存の手動テスト・`reg_probe` の実測 | (既存) |
| `ch32rv-requests.ja.md` | 同梱 uploader への依頼。**harness とは別軸**(同梱は ch32rv に一本化) | (既存) |
| `debug-output.ja.md` / `debugger.ja.md` | **SDI / RTT / DMDATA の 3 経路**と、WCH OpenOCD 固定の現状 | (既存) |
| `research/signal-name-normalization.ja.md` | **R-19: signal 名の正規化**。論理名辞書の一次資料 | 調査済み |
| `device-data.ja.md` / `open-questions.ja.md` | device DB からの生成方針 / **Q-021・Q-044・Q-045・Q-050・Q-052** | (既存) |
| `research/upload-programmers.ja.md` | **R-17 の互換書込器 Tier 表**。harness を Tier 3 で載せる枠 | (既存) |

### 2.3 ch32rv(ライタ)— `docs/`

| 文書 | 何が書いてあるか | 状態 / 基準日 |
|---|---|---|
| **`data-requests/0006-harness-integration.ja.md`** | **ch32rv 側の事実と論点(11 節)**。§1 = 相手 2 文書の前提ずれ、§2 = 排他、**§3 = 実測値**、§4 = probe backend が実装すべき面、§5 = 再利用してほしい contract、§6 = mock の段 3、**§7 = attach で target を汚す実例**、§8 = DMI 制御の代償、§9 = データ rev の固定 | **要件出し中(draft)。まとめない** / 2026-09-06 |
| `data-requests/README.ja.md` | 依頼書の運用(**資料の持ち主の repo へ依頼する**原則) | (既存) |
| `architecture.ja.md` | **`DtmAccess` / `ProbeService` の trait 境界**。`ch32rv-probe-<name>` を P2 で予約 | (既存) |
| `protocol/wch-link.ja.md` | WCH-Link USB protocol の実装側の記録 | (既存) |
| `direction-review-2026-09-01.ja.md` | コア側からのレビュー結果 | 記録 |

### 2.4 EmbedBench(ベンチ)— `docs/`

| 文書 | 何が書いてあるか | 状態 / 基準日 |
|---|---|---|
| **`HARNESS_REQUESTS.ja.md`** | **EmbedBench 側の依頼と回答(7 節)**。§2 = 依頼事項、§3 = 相手の未決への回答、§4 = こちらから出せるもの、**§5 = 環境実装をどちらに置くか** | 提案 / 2026-09-06 |
| **`FACTS.ja.md`** | **引用してよい数値**。`tests/facts/` が検査するので古くなるとテストで落ちる。**自分で数えずにここを引く** | 自動検査つき |
| `DEVICE_IF_SCOPE.ja.md` | デバイス IF の範囲。**「物理層・波形・サイクル精度」を明示的に範囲外**とし、越える要望を実機テストへ振る | 凍結(v1 / rev004) |
| `DEVELOPMENT_PLAN.ja.md` / `RELEASE_SHAPE.ja.md` | 計画(§8「当面やらないこと」に実機テストが入る)/ 出荷形 | (既存) |
| `src/embedbench_device.h` | **凍結済みデバイス IF**。純粋 C++11 | 凍結 |

### 2.5 実装とデータ

| repo | 何があるか |
|---|---|
| **ch32rv-probe** | `LICENSE` のみ(2026-09-04 初回 commit)。**実装の置き場は確保済み** |
| **ch32-device-data** | `index/pinout.csv`(24,983 行・103 型番)/ `index/debug_interfaces.csv`(27 series)/ `index/routes.csv` / `index/conflicts.csv`。**引くのは `index/`**(`evidence/` ではない) |

## 3. 論点 → どこにあるか(横断マップ)

**この表がこの文書の本体。** 同じ論点が複数 repo に分かれているので、片方だけ読むと前提を取り違える。

| 論点 | コア | ライタ | ベンチ | protocol(ここ) |
|---|---|---|---|---|
| **採否の評価・順序** | `harness-probe` §0/§3/§4 | `0006` §4 | — | `probe-pattern-coexistence` §5 |
| **要求の一覧** | **`harness-requirements`(H-001〜178)** | `0006` 全体 | `HARNESS_REQUESTS` §2 | (裁定はまだ無い) |
| **attach で target を汚さない** | `harness-probe` §2.3 / §5-1 | **`0006` §7(実例)** | — | (**pc-to-link への還流待ち**) |
| **配線・ピン衝突** | **`harness-wiring` 全体** | — | — | `dut-harness-design` §8 + `data/harness-wiring/` |
| **channel 数と被覆範囲** | `harness-requirements` §2.2 / **C-3** | — | — | `harness-board-survey` §2.2 |
| **board 選定** | `harness-wiring` 前文 | — | — | **`harness-board-survey` 全体** |
| **`caps` の形** | `harness-testing` §2 / H-010 | `0006` §5 | — | `dmi-bridge` §5 / `probe-pattern-coexistence` §3.1 |
| **パターン共存(書込のみ/複数/+LA)** | `harness-probe` §4 / **C-2** | `0006` §4 | — | **`probe-pattern-coexistence` 全体** |
| **時間軸(原点・確度)** | `harness-probe` §5-3 / §5-5 | — | — | `dut-harness-design` §2.1 / §4.3 |
| **キャプチャの形式と provenance** | `harness-probe` §5-4 | `0006` §6 | — | `probe-pattern-coexistence` §4 |
| **排他 / lock / USB device 数** | `harness-probe` §5-6 | **`0006` §2 / §5** | — | `builtin-probe-and-self-update` §2 |
| **デバイス模型の置き場** | `harness-testing` §9 / **C-1** | — | **`HARNESS_REQUESTS` §2 / §5** | `dut-harness-design` §6 |
| **host backed の可否** | `harness-testing` §9.2 | **`0006` §3(実測)** | `HARNESS_REQUESTS` §0 | `dut-harness-design` §6 |
| **制御チャネルを DMI へ** | `harness-testing` §4.2 | **`0006` §8(代償)** | — | (**未文書化**) |
| **debug 出力 3 経路(SDI/RTT/DMDATA)** | `harness-probe` §2.4 / §5-9、`debug-output` | — | — | `../protocols/serial-and-print.ja.md` |
| **resolver / 論理信号名** | `harness-testing` §1 / §5-2、`research/signal-name-normalization` | `0006` §9 | — | (**未決。§11-2/3 の裁定待ち**) |
| **テスト駆動方式(session / socket / mock)** | **`harness-testing` 全体** | `0006` §2 / §6 | `HARNESS_REQUESTS` §2 | (**未文書化**) |
| **復旧・無人運用** | `harness-probe` §5-7 | — | — | — |
| **PID / 個体識別** | `harness-probe` §5-6a | — | — | `builtin-probe-and-self-update` / `ecosystem-any-hardware` §4 |
| **5V target** | H-085 / **C-7** | — | — | `harness-board-survey` §2.7 / `dmi-bridge` §8.2 |
| **線層の `verified` 化** | `harness-probe` §7-2(共同実験案) | `protocol/wch-link` | — | **`link-to-target` §3** |
| **実機なしで回す** | `harness-testing` §7 | `0006` §6 | `HARNESS_REQUESTS` | `../experiments/README.ja.md` §4.5(v0) |

**「未文書化」= このセッションの議論としては出ているが、まだどの文書にも入っていない。** 取りまとめのときに拾う。

## 4. 要求 ID の体系(誰が採番しているか)

| 体系 | 採番元 | 範囲 | 意味 |
|---|---|---|---|
| **`H-nnn`** | **コア** `harness-requirements` | H-001〜H-178(**129 件**) | harness への要求 |
| **`C-n`** | **コア** `harness-requirements` §3 | C-1〜C-8 | **相反する要求。protocol 側の裁定待ち** |
| `5-n` | コア `harness-probe` §5 / `harness-testing` §10 | 5-1〜5-16 | probe への依頼(H に統合済み) |
| `Q-nnn` / `R-nn` | コア `open-questions` / `research/` | — | コア内部の未決・調査 |
| `nnnn-*` | **ライタ** `docs/data-requests/` | 0001〜0006 | 外部 repo への依頼書。**harness は 0006** |
| (節番号) | ベンチ `HARNESS_REQUESTS` | §2 の各項 | ID 体系は無い |
| (文書ごとの未決リスト) | **protocol**(ここ) | 各文書 §9 等 | ID 体系は無い |

**採番していないのは protocol とベンチ。** すり合わせで一意の ID が要るなら、この repo で採るか `H-nnn` を正本にするかを決める(→ §6)。

## 5. 相反が明示されている論点(裁定待ち)

コア `harness-requirements` §3 が **C-1〜C-8** として明示している。**この索引は内容を写さない**(写すと二重管理になる)。所在だけ:

`C-1` 模型の置き場 / `C-2` multi-lane vs 1 target 専有 / `C-3` channel 数 vs 被覆 / `C-4` capture 帯域 vs control 応答性 / `C-5` 論理名を probe に持たせない vs SUMP 互換 / `C-6` debug 線を窓に入れる vs 16ch 全部をアプリに / `C-7` 5V target / `C-8` 模型を積む vs 資源

ライタ `0006` §1 は**別の種類の相反**を挙げている — **コアの 2 文書の間で前提がずれている**箇所。裁定の前にこちらを潰す。

## 6. まだ無いもの(これから足す枠)

| # | 何 | 置き場(予定) | 状態 |
|---|---|---|---|
| 1 | **取りまとめ文書**(全体の突き合わせと裁定) | この repo | **未作成**。できたらここに索引を足す |
| 2 | 一意な要求 ID の正本をどこに置くか | (§4) | 未決 |
| 3 | probe firmware の実装 | `ch32rv-probe` | `LICENSE` のみ |
| 4 | attach 副作用の還流(`RCC_CFGR0` / `FLASH ACTLR`) | `../protocols/pc-to-link.ja.md` | 未反映(材料はライタ `0006` §7) |
| 5 | 1/2 線両対応 15 series の反映 | `../protocols/link-to-target.ja.md` §1 | 未反映(材料は `data/harness-wiring/`) |

## 7. 更新の仕方

1. **新しい文書ができたら §2 に 1 行、§3 の該当する論点に所在を足す。**
2. **議論の中身はここに書かない。** 各 repo の文書が正本で、この索引は所在と状態だけを持つ。
3. **数値を写さない。** ベンチの `FACTS.ja.md` が「自分で数えずにここを引け」と言っているのと同じ理由で、写した数字は腐る。
4. 各文書の**状態と基準日**は変わるので、まとめて見直すときは各文書の冒頭を読み直す。
