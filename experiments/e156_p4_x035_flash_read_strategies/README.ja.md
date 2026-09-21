# E156 ESP32-P4 → CH32X035: dedicated GPIO PHY 上で flash 全域読出しは方式で何倍違うか

状態: 計画

## 問い

[E153](../e153_p4_x035_rvswd_dedic_ceiling/README.ja.md) の PHY（dedicated GPIO、push-pull、half 0 ns、1 DMI ≈ 10 µs）で、
X035F8U6 の code flash 63,488 byte を halt 中に全域読むとき、次の 3 方式の所要時間はいくらで、結果（CRC32）は一致するか。

1. `scalar`: word ごとに program buffer（`lw`; `ebreak`）を設定し abstract command で読む（現 OEP backend の `readWord`）。
2. `autoexec_poll`: DMDATA1 を address、DMDATA0 を autoexec にした連続 reader（現 OEP の `readSequentialWord`）。word ごとに ABSTRACTCS を poll してから DMDATA0 を読む。
3. `autoexec_nopoll`: 同じ reader で、word ごとの poll を省き DMDATA0 だけを読む。全域の後に ABSTRACTCS の cmderr が 0 であることを確認する。

あわせて、program buffer 1 回の実行が完了するまでの時間（abstract command busy の継続 µs）を測る。

## 仮説

- DMI 1 回 ≈ 10 µs なので、`scalar` ≈ 12 DMI/word ≈ 1.9 s、`autoexec_poll` ≈ 2 DMI/word ≈ 0.32 s、`autoexec_nopoll` ≈ 1 DMI/word ≈ 0.16 s。
- X035（48 MHz）の program buffer 実行は 1 µs 未満で、DMI 1 回（10 µs）より短いので `nopoll` でも busy 中の DMDATA0 read は起きず cmderr は 0 になる。
- 3 方式の CRC32 は一致する。

## 反証条件

- `nopoll` の cmderr が非 0、または CRC32 が他と不一致（poll 省略は不可）。
- `scalar` と `autoexec_poll` の CRC32 が不一致（reader の誤り）。
- halt / resume 後に target が実行を再開しない（DMSTATUS の allresumeack が立たない）。

## 方法

1. `?` で銘板。`R` で開始。
2. attach（dmactive）→ haltreq → allhalted を待つ。ABSTRACTCS の cmderr を clear。
3. 各方式で 0x08000000 から 63,488 byte を読み CRC32（IEEE）と所要 µs、DMI 回数、error 数を出す。方式間に target の状態変更はない。
4. program buffer 1 回の busy 時間: `lw` 1 回の command を発行し、ABSTRACTCS.busy が落ちるまでの µs を 100 回測って min / median / max。
5. resumereq → allresumeack を確認 → dmcontrol=0 → 線を Hi-Z。flash は書かない。

## 対象外

flash program / erase（→ E157）。USB 越しの転送（E155）。線上波形（LA なし、`attested`）。

## 必要な環境

profile `esp32p4_x035`（fixture P4）、target X035F8U6（E153 と同じ）。target の flash 内容は問わない（読むだけ）。

## ベンチ種別

一時（既設 fixture、配線変更なし）。

## 記録する数値

`READ strategy= bytes=63488 us= dmi= errors= cmderr= crc32=`、`BUSY samples=100 min_us= median_us= max_us=`、`RESUME ok=`。

## 完了条件

3 方式の行と BUSY 行、RESUME 行が得られ、CRC32 の一致・不一致が判定できたら完了。

## 影響

oep-probe-arduino の target.memory 実装（poll の要否）、rebuild plan の E156 と S3。仕様 status は動かない。

## 結果

状態: 完了（2026-09-22）。採用 run: `_runs/E156_20260921T233323Z_default/`、再現 run: `_runs/E156_20260921T233414Z_default/`、
`_runs/E156_20260921T233432Z_default/`（3 回とも同値、銘板 `git=d5f1b3f+dirty`）。

実装上の追加（計画外、方法の変更ではない）: (1) parity 失敗時に同じ DMI read を最大 200 回まで再読みして `retries` に数える。
(2) 方式 3 は DMDATA0 read の前に固定 gap {0, 10, 20, 50, 100} µs を入れた 5 点で測る。(3) 診断として halt 直後に
DMSTATUS / ABSTRACTCS を各 half period で 1000 回読み、全数一致する最小 half を選ぶ（毎回 half 0 ns が選ばれた）。

| 方式 | 所要 | 実効 | DMI 回数 | retries | errors | cmderr（終了時） | CRC32 |
|---|---:|---:|---:|---:|---:|---:|---|
| scalar（現 OEP `readWord`） | 1.445 s | 43.9 kB/s | 174,592 | 0 | 0 | 0 | `0x4ac14d04` |
| autoexec + word ごと poll（現 OEP `readSequentialWord`） | 0.279 s | 227.8 kB/s | 31,761 | 0 | 0 | 3 | `0x4ac14d04` |
| **autoexec、poll なし、gap 0** | **0.146 s** | **435.5 kB/s** | 15,889 | 0 | 0 | 3 | `0x4ac14d04` |
| autoexec、poll なし、gap 10 / 20 / 50 / 100 µs | 0.317 / 0.476 / 0.952 / 1.745 s | 200 / 133 / 67 / 36 kB/s | 15,889 | 0 | 0 | 3 | 同値 |

- BUSY（`lw`+`ebreak` の program buffer 1 回、command write から最初の not-busy 観測まで）: min 16〜17 µs、median 17〜18 µs、max 34〜35 µs（= DMI 2 回分）。
- RESUME ok=1（allresumeack）。3 回とも target は実行を再開した。
- cmderr=3 は終端の look-ahead read（0x0800F800、flash 外）の exception で、E145 の記録と同じ。範囲内の word には影響しない（3 方式の CRC 一致）。

## 事実 / 候補 / 未決

- **事実**: dedicated GPIO PHY（1 DMI ≈ 10 µs）で 63,488 byte を **0.146 s（435 kB/s）** で読める。現 OEP の 4.94 s（E145）の約 34 倍、LinkE の 1.22 s の 8 倍速い。3 方式の CRC32 は一致。
- **事実**: program buffer 1 回の実行は DMI 1 回（10 µs）以内に終わる。word ごとの ABSTRACTCS poll は不要で、poll を省くと 1.9 倍速い。
- **事実**: 3 回の反復で retries 0、CRC 同値。
- **観測（採用 run の前の 2 run、`_runs/E156_20260921T233023Z_default/`、`_runs/E156_20260921T233159Z_default/`）**: halt 直後に診断掃引を挟まず abstract command を始めると、DMI read の約 6 割が parity 不一致になり（retry 315,614 / 174,592 read）、CRC も不一致だった。診断掃引（約 0.3 s の DMSTATUS/ABSTRACTCS read）を halt と最初の abstract command の間に置いた 3 run はすべて正常。
- **候補**: target.memory の実装は「autoexec、poll なし、末尾で cmderr 確認」。verify は transport（E155: 320 kB/s）が律速になり、62 KiB で 0.35 s 前後を見込む。
- **未決**: halt 直後の不安定期間の正体 `—`（時間か、read 回数か、特定 register への最初の access か）。次 ID で「halt → 待ち 0 / 1 / 10 / 100 ms → abstract command」を掃引する（候補 slug `x035-halt-settle`）。parity 1 bit では garbage の半分が通るため、**読出し結果の検証は DMI parity ではなく上位の hash / CRC で行う**（S3 の設計条件）。
- **未決**: 書込み側（page program の RAM loader 対 word DMI）`—`（→ E157）。

## 反映

台帳 §1 E156 を完了。rebuild plan の E156 行と S3 の target.memory 設計条件を更新。仕様 status は動かない。
