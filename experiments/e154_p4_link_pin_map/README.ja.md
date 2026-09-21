# E154 ESP32-P4 二台の 8 本 GPIO 直結は申告どおり対応しているか

状態: 計画

## 問い

fixture P4（`30eda0e31108`）と peer P4（`30eda0e34a0e`）の間に結線された 8 本
（申告: GPIO 33, 32, 26, 27, 28, 29, 30, 31 を同番号同士）は、両方向とも 1 対 1 で対応し、他の申告 pin へ漏れていないか。
また pytest の `peers` fixture で二台へ同時に転送・監視できるか（8 本リンク phase のハーネス確認）。

## 仮説

8×8 の行列は両方向とも単位行列（駆動した pin だけが相手側の同番号で 1）になる。

## 反証条件

- 単位行列にならない（対応違い、短絡、未接続）。
- 二台同時の upload / lock / monitor が通らない。

## 方法

E003 と同じ。両 board は 8 本を `INPUT_PULLDOWN` に置く。駆動側が 1 本だけ push-pull HIGH にし、観測側が 8 本を読む。
読み終えたら駆動側は LOW → INPUT へ戻す。fixture → peer、peer → fixture の両方向。
**駆動対象は申告 8 本に限定する。** fixture P4 の他 pin は X035F8U6 と RVSWD / GPIO 配線で繋がっているため触らない。
pin list は `.env` の `TEST_P4_LINK_PINS`。

## 対象外

8 本以外の結線の探索（申告外の pin は駆動しない）。電気特性・速度。

## 必要な環境

- profile `esp32p4_x035`（fixture P4）と peer `p4b` / profile `esp32p4_peer`（`.env` `TEST_SERIAL_PORT_PEER_P4B_ESP32P4_PEER`）。
- 8 本の結線（持ち主が 2026-09-22 に申告）。

## ベンチ種別

一時（8 本リンク phase。HS USB とは排他）。

## 記録する数値

両方向の 8×8 行列（行 = 駆動 pin、列 = 観測 pin、値 0/1）。

## 完了条件

両方向の行列が得られること。単位行列なら「対応確定」、そうでなければ差分を記録して完了。

## 影響

以後の peer 実験（E155〜）の pin 前提。rebuild plan の Phase A 配線記録。

## 結果

状態: 完了（2026-09-22）。採用 run: `_runs/E154_20260921T223034Z_default/`（`dut.log` = fixture、`peer-p4b.log` = peer、銘板 `git=37620b7+dirty`）。
初回 run（`_runs/E154_20260921T222859Z_default/`）は sketch の `Z` 処理（長さ 1 行を `ERR` にしていた）で `RELEASED` を待ってタイムアウトした。走査データは同じだったが、採用しない。

両方向とも 8×8 の**単位行列**。

```text
fixture -> peer           peer -> fixture
      33 32 26 27 28 29 30 31
  33   1  0  0  0  0  0  0  0      同じ
  32   0  1  0  0  0  0  0  0
  26   0  0  1  0  0  0  0  0
  27   0  0  0  1  0  0  0  0
  28   0  0  0  0  1  0  0  0
  29   0  0  0  0  0  1  0  0
  30   0  0  0  0  0  0  1  0
  31   0  0  0  0  0  0  0  1
```

## 事実 / 候補 / 未決

- **事実**: GPIO 33, 32, 26, 27, 28, 29, 30, 31 は両 board 間で同番号同士に 1 対 1 で結線され、申告 8 本の中に短絡・漏れはない。
- **事実**: `peers` fixture（`peer_p4b/`、`TEST_SERIAL_PORT_PEER_P4B_ESP32P4_PEER`）で二台の build / upload / lock / monitor が 1 回の pytest で通る。
- **事実（ハーネス）**: pytest-embedded の `write()` は改行を付ける。sketch は空行を無視する必要がある。
- **未決**: 申告外の pin の結線 `—`（駆動しない方針のため探索しない）。

## 反映

台帳 §1 E154 を完了。rebuild plan Phase A に pin list を記録。
