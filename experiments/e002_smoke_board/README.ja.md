# E002 スモーク: 実機 1 枚(常設 v1)

状態: **完了**(2026-09-04、仮説は**反証された**)    <!-- 計画 → 実行中 → 完了 / 中断 -->

規則: [../README.ja.md](../README.ja.md) / 台帳: [../LEDGER.ja.md](../LEDGER.ja.md) / 前段: [E001](../e001_smoke_host/README.ja.md)

## 問い

**実機 1 枚に対して upload → monitor → assert が通り、かつ「実時間」が取れるか。**

答えは yes / no。E001(実機なし)との差分は **upload・実 port・実クロック**の 3 点だけなので、ここで落ちたら原因はその 3 つに限定される。

## 仮説

**通る。** 根拠:

- E001 で pytest / arduino-cli / profile 解決 / monitor / 銘板 / expect の経路は確認済み([E001 事実 1](../e001_smoke_host/README.ja.md))。
- 対象ボードは USB-serial bridge(`1a86 USB Single Serial`)経由の UART で、native USB CDC の起動タイミング問題を持たない。
- `esp32:esp32 3.3.11` は導入済み。

未確認なのは、**別名パス**(`/run/board-identify/by-id/...`)が port として解決されるか、**bridge の DTR auto-reset** で最初の出力を取りこぼさないか、の 2 点。

## 反証条件

1. profile が解決できない / build が失敗する
2. **upload が失敗する**(port が開けない、別名パスが解決されない、reset に入らない)
3. upload は通るが monitor に何も来ない(**DTR auto-reset 直後の出力を取りこぼす**)
4. 銘板が 1 行として読めない
5. `micros()` の差分が**実時間として妥当でない**(仮想時計と区別できない)

## 方法

1. `.env` に対象ボードの別名パスを 1 行足す(`TEST_SERIAL_PORT_ESP32S3`)。**`.env.example` は一般表記のまま。**
2. `e002_smoke_board/` に 3 ファイル。sketch は E001 と同じ銘板 + 既知 3 行に、**実クロックの確認**を加える:
   ```
   t0 = micros(); delayMicroseconds(1000); t1 = micros();
   → "CLOCK delta=<t1-t0>" を出す
   ```
3. 実行:
   ```sh
   uv run --env-file .env pytest e002_smoke_board/e002_smoke_board.py
   ```
4. 銘板 → 既知 3 行 → `CLOCK delta=` を expect し、delta が **900〜1500 us** に入ることを assert する(範囲外なら反証条件 5)。
5. 3 回繰り返す。**取りこぼし(反証条件 3)は 3 回のうち 1 回でも起きたら事実として記録する** — 間欠的な取りこぼしこそがこの実験で見つけたいもの。

## 対象外

| 落とすもの | 行き先 |
|---|---|
| DTR auto-reset の詳細な挙動(いつ reset するか、待ち時間はどれだけ要るか) | 候補 `uart-dtr-reset`。**ここでは「取りこぼしたか否か」だけ**を見る |
| device lock が 2 プロセス間で効くか | 候補 `device-lock` |
| 他の board への移植性(Pico / Uno) | 別実験 |
| transport の latency・性能値 | 候補 `dmi-latency` |
| protocol、線、loopback phy | 候補 `wire-bitstream` / `loopback-inject` |
| `_runs/` への自動退避 | 候補 `runs-archive`(今回も手動) |

## 必要な環境

**peer 対の HOST 側 `esp32-s3-d0cf1359101c`(→ ttyACM0)。**

この repo が使ってよいのは peer 対の 2 台(`d0cf1359101c` = HOST / `d0cf1358fd94` = DEVICE)で、**1 台しか使わない実験は常に HOST 側を使う**。他の board(P4、3 台目の S3)は別用途なので触らない。

profile 名は既存ベンチの命名に合わせて **`s3_peer_host`** とする(→ port は `TEST_SERIAL_PORT_S3_PEER_HOST`)。同じ機材を指す変数名が repo 間で揃うので、`.env` の行をそのまま持ち回れる。

> ⚠ **upload は対象ボードの firmware を上書きする。**

`esp32:esp32 3.3.11` / `arduino-cli` / `uv`。LA・target・peer は不要。

## ベンチ種別

**常設 v1**。ここで指名したボードは以後 `.env` の `TEST_STANDING_PROFILE` に固定し、[規則 §4.5](../README.ja.md) の約束(配線を変えない・専有する・他の実験に流用しない)を適用する。

## 記録する数値

| 項目 | 単位 | 備考 |
|---|---|---|
| 反証条件 1〜5 の各段階 | 通過 / 失敗 | 失敗ならエラー全文を `_runs/` へ |
| `micros()` の delta | us | **3 回分すべて**。実クロックであることの根拠 |
| 銘板の取りこぼし | 回 / 3 回 | 間欠なら回数がそのまま事実 |
| esp32 core 版 / board FQBN / port の別名と実体 | 文字列 | 銘板と `_runs/` に残す |
| build / upload / 実行の各時間 | 秒 | **参考のみ。性能値として引用しない** |

## 完了条件

- 反証条件 1〜5 をすべて通過 → **完了**(常設 v1 が成立)
- どれかで落ちた → **原因を事実として記録し、代替を未決に書いて完了**

## 影響

仕様の status は動かない。動くのは**常設 v1 が成立するか**:

- 通れば → 実機を要する候補(`uart-dtr-reset` / `set-baud` / `dmi-latency`)を採番できる。E003(peer)へ進める
- 落ちれば → 実機経路の問題を先に潰す必要があり、実機を要する候補はすべて保留になる

---

# 結果(2026-09-04)

計画は上のまま変更していない。以下は追記。

## 結果

run: `_runs/E002_20260904T0517xxZ_s3_peer_host/`

| 反証条件 | 結果 |
|---|---|
| 1. profile 解決 / build 失敗 | 起きず |
| 2. upload 失敗 | 起きず(別名パス `/run/board-identify/by-id/...` はそのまま解決された) |
| 3. **monitor に何も来ない(起動時出力の取りこぼし)** | **発生。確定的**(1/1) |
| 4. 銘板が 1 行として読めない | **未測定** `—`(そこまで到達しない) |
| 5. `micros()` が実時間でない | **未測定** `—`(同上) |

`dut.log` の先頭:

```text
HEARTBEAT 1
HEARTBEAT 2
...
```

銘板 / `SMOKE *` / `CLOCK delta=` の出現回数は **いずれも 0**。

## 事実

1. **upload は通る。** profile 解決・build・書込とも問題なし。`.env` の**別名パス(`/run/board-identify/by-id/...`)はそのまま port として解決された**。
2. **`setup()` の出力は 1 行も届かない。** 監視が接続するのは reset より後で、**`HEARTBEAT 0` すら失われている**ことから、遅れは **1 秒以上**。
3. **heartbeat を入れておいたことで「ボードが死んでいる」と「起動時出力の取りこぼし」を区別できた。** 入れていなければ原因不明の timeout になっていた。
4. 実クロックの確認(反証条件 5)は**未測定**。E002 の問いのうち「実時間が取れるか」には答えられていない。

## 候補

起動時出力を確実に受け取る方法(いずれも未検証):

- (a) 銘板を周期的に再送する
- (b) host が port を開いたあと DTR/RTS で reset する — このプラグインは ESP 固有の reset 機能を**意図的に持たない**設計なので、自前で行う必要がある
- (c) **host が trigger を送るまで待ち、応答として銘板を返す**

**(c) が本命。** [ecosystem-any-hardware §4.5](../../references/ecosystem-any-hardware.ja.md) が既に「host は port open 後に boot 完了と `hello` 応答を待つ設計にする」と書いており、[dmi-bridge §4.1](../../protocols/dmi-bridge.ja.md) の「`hello` が成立して初めて他のコマンドを送ってよい」と同じ形。**紙の上で決めた設計が、最初の実機実験で裏付けられた。**

## 未決

- 候補 (a)(b)(c) のどれを採るか、(c) で確実に取れるか → **E004**(`e004_smoke_board_trigger`)
- 実クロックが取れるか(反証条件 5)→ E004 に引き継ぐ

## 反映

- 台帳 [E002](../LEDGER.ja.md) を完了(反証)に。
- 規則 §4.1 / §7-9: **機材は他プロジェクトと共有**である前提を明記(見えなければ skip、書込排他で待たされるのは失敗ではない)。
- [ecosystem-any-hardware §4.5](../../references/ecosystem-any-hardware.ja.md) の DTR 副作用の記述に、実測の裏付け(E002)を追記。
- 仕様の status は動かない。
