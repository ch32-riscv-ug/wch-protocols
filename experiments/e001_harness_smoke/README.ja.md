# E001 ハーネスのスモーク(実機なし)

状態: **完了**(2026-09-04)    <!-- 計画 → 実行中 → 完了 / 中断 -->

規則: [wch-protocols/experiments/README.ja.md](../README.ja.md) / 台帳: [../LEDGER.ja.md](../LEDGER.ja.md)

## 問い

**pytest + arduino-cli + host Arduino core で、実機を一切使わずに sketch を build・実行し、その出力を assert できるか。**

答えは yes / no。yes なら、以後の実験は「実機が要るもの」だけを実機に降ろせる([規則 §4.5](../README.ja.md) v0)。

## 仮説

**できる。** 根拠:

- `pytest-embedded-arduino-cli` は `sketch.yaml` の profile で `port: socket://localhost` を扱う設計になっており、`TEST_SERIAL_PORT_HOST=socket://localhost` が想定されている。
- host Arduino core(`lang-ship:host:host`)は公開 index から取得でき、sketch を PC 上で走らせて socket に繋ぐ。

未確認なのは「**この repo の構成で**通るか」だけ。具体的には uv project の場所(`tests/`)、profile 名、銘板が 1 行として読めるか(改行・バッファリング)。

## 反証条件

次のいずれかが起きたら仮説は誤り:

1. `arduino-cli` が `lang-ship:host:host` を解決できない(index 取得・core 導入で失敗)
2. build は通るが `socket://localhost` の DUT が起動しない / 接続できない
3. 起動はするが `dut.expect` がタイムアウトする(出力が届かない、または行として切れない)
4. 銘板が 1 行として読めない(分割される、順序が崩れる)

## 方法

1. `experiments/` に uv project を作る。依存は `pytest` / `pytest-embedded` / `pytest-embedded-serial` / `pytest-embedded-arduino-cli` のみ。
2. `experiments/.env` に `TEST_SERIAL_PORT_HOST=socket://localhost` だけを置く(`.env.example` も同時に作る)。
3. `e001_harness_smoke/` に 3 ファイル:
   - `sketch.yaml` — profile `host` = `lang-ship:host:host`、`port: socket://localhost`、platform を版で pin
   - `e001_harness_smoke.ino` — `setup()` の先頭で**銘板 1 行**、続けて既知の 3 行(`A`/`B`/`C` のような固定文字列)を出して停止
   - `e001_harness_smoke.py` — 銘板 → 3 行の順に `expect` する(`test_` を付けない。[規則 §1.3](../README.ja.md))
4. 実行:
   ```sh
   cd experiments
   uv sync
   uv run --env-file .env pytest e001_harness_smoke/e001_harness_smoke.py
   ```
5. 反証条件のどこで落ちたかを記録する。落ちた場合も、**どこまで進んだか**を段階ごとに残す(build までは通った、等)。

銘板の形式([規則 §5](../README.ja.md)):

```text
# EXP E001 v1 fw=<sketch hash> core=lang-ship:host:<ver> probe=host target=none t=<ISO8601>
```

## 対象外

**この実験では答えない**(それぞれ別の問いに割る):

| 落とすもの | 行き先 |
|---|---|
| `_runs/` への結果退避が conftest で動くか | 候補 `runs-archive` |
| bare `pytest` が実験ファイルを収集しないこと | 候補 `collection-guard` |
| device lock(物理 port が無いので効かない) | E002 |
| 実機での upload / 実時間 / DTR reset | E002 |
| 銘板に載せる版情報を自動で埋める仕組み | 候補 `banner-autofill` |
| 線の bit 列、protocol、loopback phy | 候補 `wire-bitstream` / `loopback-inject` |

E001 の結果は**手で** `experiments/_runs/E001_<UTC>_host/` に置く。退避の仕組みそのものは未確認なので、それに依存しない(仕組みの確認は `runs-archive`)。

## 必要な環境

PC のみ。`arduino-cli`、`uv`、初回の core index 取得にネットワーク。**実機・target・LA すべて不要。**

## ベンチ種別

**常設 v0**(実機なし)。専有する機材が無いので、board の可否確認を待たずに実行できる。

## 記録する数値

| 項目 | 単位 | 備考 |
|---|---|---|
| 反証条件 1〜4 の各段階 | 通過 / 失敗 | 失敗ならエラー全文を `_runs/` へ |
| `arduino-cli` 版 / host core 版 / プラグイン版 / Python 版 | 文字列 | 銘板と `_runs/` に残す |
| build 時間 / 実行時間 | 秒 | **参考のみ。性能値として引用しない**(仮想時計・環境依存、[規則 §7-2](../README.ja.md)) |

繰り返しは 3 回(初回は core 取得を含むので別に記録する)。

## 完了条件

- 反証条件 1〜4 をすべて通過 → **完了**(仮説が支持された)
- どれかで落ちた → **原因を事実として記録し、代替(実機に降りる / 別 core を使う / 経路を変える)を未決に書いて完了**

どちらの場合も台帳に節を書く。「答えが出なかった」も完了([規則 §3.5](../README.ja.md))。

## 影響

仕様([wch-protocols/protocols/](../../protocols/))の status は動かない。動くのは**規則が実行可能かどうか**:

- 通れば → 常設 v0 が成立し、`wire-bitstream` / `loopback-inject` など**実機を要さない候補**を先に消化できる。使い捨てベンチ(LA)で取るべき問いが「タイミングだけ」に絞れる
- 落ちれば → v0 が使えないので常設 v1(実機 1 枚)から始めることになり、E002 が最初の実験になる

---

# 結果(2026-09-04)

計画は上のまま変更していない。以下は追記。

## 結果

run: `_runs/E001_20260904T050917Z_host/`(`dut.log` と `env.txt`)

| 反証条件 | 結果 |
|---|---|
| 1. `lang-ship:host` を解決できない | **起きず**(1.7.1 が導入済み。index 取得は不要だった) |
| 2. `socket://localhost` の DUT が起動しない | **起きず** |
| 3. `dut.expect` がタイムアウトする | **起きず** |
| 4. 銘板が 1 行として読めない | **起きず** |

| 項目 | 値 |
|---|---|
| 繰り返し | **3/3 pass** |
| pytest 実行時間 | 7.50 / 7.32 / 7.45 s(wall 7.85 / 7.61 / 9.14 s)※参考のみ、性能値として引用しない |
| arduino-cli | 1.3.1 |
| host core | lang-ship:host 1.7.1 |
| plugin | pytest-embedded 2.9.3 / pytest-embedded-arduino-cli 1.4.1 / pytest 9.1.1 / Python 3.13.3 |

`dut.log` の全内容(期待どおり 5 行のみ):

```text
# EXP E001 v1 core=lang-ship:host probe=host target=none build=Sep  4 2026 14:08:09
SMOKE A
SMOKE B
SMOKE C
SMOKE done
```

## 事実

1. **実機なしで build → 実行 → assert が成立する。** 仮説は支持された。常設 v0 は使える。
2. **DUT はテスト関数ごとに生成される。** 同じファイルに 2 つのテスト関数を書くと、2 つ目の DUT 起動で `SerialException: Could not open port socket://localhost:<port>: Connection refused` になった(1 つ目は pass)。**1 実験 1 テスト関数**にまとめると 3/3 で通る。
3. **ビルド生成物は `<実験>/build/<profile>/` に出る**(`output/` ではない)。plugin が `--build-path` にこの位置を渡している。
4. **生ログは `/tmp/pytest-embedded/<UTC>/<test 名>/dut.log`。** 銘板を含めて期待どおりの内容だけが入る。
5. **実行の明示指定はファイルパスでなければならない。** ディレクトリを渡すと収集 0 件(`pytest e001_harness_smoke` → 0 items、`pytest e001_harness_smoke/e001_harness_smoke.py` → 1 item)。
6. 銘板の `build=` は `__DATE__ " " __TIME__` で埋まる(`Sep  4 2026 14:08:09`)。sketch を変えずに再ビルドしても更新される。

## 候補

- **1 実験 1 テスト関数**(採用)。他に手段(DUT の scope 変更など)があるかは未確認。
- 銘板の `build=` に `__DATE__`/`__TIME__` を使う。fw hash の代用で、`banner-autofill` が来たら置き換える。

## 未決

- DUT を module scope にできるか。1 実験で複数のテスト関数を書きたくなったときに必要 → 候補 `dut-scope`。
- `_runs/` への退避は**今回は手動でコピーした**。自動化は候補 `runs-archive`(計画の `対象外` どおり)。
- 6 番の観測(bare `pytest` が 0 件)は候補 `collection-guard` の対象。ここでは**観測しただけ**で、実プロジェクト構成での保証は別途行う。

## 反映

- 規則 §3.4 の表: 破棄対象を `output/` → **`build/`** に修正(事実 3)。
- 規則 §1.3: 「**明示指定はファイルパス**。ディレクトリでは 0 件」を追記(事実 5)。
- 規則 §1.3: 「1 実験 1 テスト関数」を追記(事実 2)。
- 規則に**言語ルール**(コードは英語、未確定の設計文書は日本語)を追加。
- 台帳 [E001](../LEDGER.ja.md) を完了に。
- 仕様([../../protocols/](../../protocols/))の status は動かない(計画どおり)。
