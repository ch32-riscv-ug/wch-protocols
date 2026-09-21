# E153 ESP32-P4 → CH32X035: dedicated GPIO 駆動 RVSWD の clock 上限はどこか

状態: 計画

## 問い

[E151](../e151_p4_gpio_edge_cost/README.ja.md) で 1 bit 相当 94.5 ns だった dedicated GPIO（CPU CSR 直結）で
RVSWD を駆動したとき、fixture の X035F8U6 に対して DMI read/write が parity・値ともに全数一致する最小
half period は何 ns か。SWDIO open-drain（od）と host 駆動区間 push-pull（pp）で違うか。

## 仮説

- P4 側は half 50 ns（約 10 MHz）まで出せる。X035 側は 4 MHz（half 125 ns、pico-rvswd 報告）付近が上限で、
  それより速い点で parity/値の不一致が出る。
- od は内部 pull-up の立上りで pp より低い clock で崩れる。

## 反証条件

- half 2000 ns で失敗する（ハーネス誤り。E142 で 1〜5 µs は成立）。
- half 0 ns（P4 コスト律速 ≈ 10 MHz）でも全数一致する（target 上限はこの方法では見えない）。
- pp が od より低い clock で崩れる。

## 方法

E152 と同じ手順・同じ判定（DMSTATUS read 1000、DMCONTROL read 200、DMDATA0 write+read 200、`DMCONTROL=0`、Hi-Z）を、
駆動を dedicated GPIO bundle（out: DIO=bit0, CLK=bit1、in: DIO）に替えて行う。pp の turnaround だけ
`gpio_ll_output_disable/enable`（300 ns、frame あたり 2 回）を使う。half period は
{2000, 1000, 500, 300, 200, 150, 125, 100, 75, 50, 25, 0} ns。target の flash・CPU 状態には触れない。

## 対象外

線上波形（LA なし、`attested`）。abstract command / memory access 速度。USB 往復。

## 必要な環境

E152 と同じ（profile `esp32p4_x035`、X035F8U6、GPIO2/54）。

## ベンチ種別

一時（既設 fixture、配線変更なし）。

## 記録する数値

E152 と同じ列。各 mode 12 点。

## 完了条件

両 mode 24 行が得られ、half 2000 ns が全数一致していること。全数一致する最小 half_ns と、その点の ns/read を
mode ごとに報告する。

## 影響

oep-probe-arduino RVSWD PHY の設計値（clock、drive mode、frame time）。仕様 status は動かない。

## 結果

状態: 完了（2026-09-22）。採用 run: `_runs/E153_20260921T222148Z_default/`（銘板 `git=9307391+dirty`、cpu 360 MHz、swdio=2 swclk=54）。

| mode | half_ns | parity | value | ctrl | write | ns/read | 実効 kbit/s（52 bit 換算） |
|---|---:|---:|---:|---:|---:|---:|---:|
| od | 2000 | 1000/1000 | 1000/1000 | 200/200 | 200/200 | 236,707 | 220 |
| od | 1000 | 1000 | 1000 | 200 | 200 | 125,292 | 415 |
| od | 500 | 1000 | 1000 | 200 | 200 | 69,590 | 747 |
| od | 300 | 1000 | 1000（全 `0`） | **0** | **0** | 47,329 | 1,099 |
| od | 200〜50 | 1000 | 1000（全 `0`） | **0** | **0** | 35,280〜17,458 | 1,474〜2,979 |
| od | 25 | **501** | **1** | 0 | 0 | 13,831 | 3,760 |
| od | 0 | 1000 | 1000（全 `0`） | 0 | 0 | 9,408 | 5,527 |
| pp | 2000 | 1000 | 1000 | 200 | 200 | 236,536 | 220 |
| pp | 1000 | 1000 | 1000 | 200 | 200 | 125,106 | 416 |
| pp | 500 | 1000 | 1000 | 200 | 200 | 69,427 | 749 |
| pp | 300 | 1000 | 1000 | 200 | 200 | 46,994 | 1,107 |
| pp | 200 | 1000 | 1000 | 200 | 200 | 35,280 | 1,474 |
| pp | 150 | 1000 | 1000 | 200 | 200 | 29,511 | 1,762 |
| pp | 125 | 1000 | 1000 | 200 | 200 | 25,317 | 2,054 |
| pp | 100 | 1000 | 1000 | 200 | 200 | 23,456 | 2,217 |
| pp | 75 | 1000 | 1000 | 200 | 200 | 20,004 | 2,599 |
| pp | 50 | 1000 | 1000 | 200 | 200 | 17,944 | 2,898 |
| pp | 25 | 1000 | 1000 | 200 | 200 | 14,003 | 3,713 |
| pp | **0** | 1000 | 1000 | 200 | 200 | **10,128** | **5,134** |

pp の DMSTATUS 初回値は全点 `0x00000c82`（E142 と同値）。

## 事実 / 候補 / 未決

- **事実**: dedicated GPIO + push-pull（host 駆動区間のみ出力、target 駆動区間は出力無効）では、追加遅延 0（half 0 ns、1 bit ≈ 190 ns）でも X035F8U6 の DMI read（parity・値）、DMCONTROL 読返し、DMDATA0 write/read 200 回が **全数一致**。1 DMI read = **10.1 µs**、実効 ≈ 5.1 Mbit/s。
- **事実**: 現行 OEP backend（110〜130 µs/DMI、E145 逆算）に対し **約 12 倍**。62 KiB full read を DMI read 換算（15,872 word、autoexec で word あたり DATA0 read 1 回 + poll 1 回）すると約 0.32 s 相当で、LinkE の 1.22 s を下回る余地がある。
- **事実**: open-drain + 内部 pull-up は half 300 ns 以下で崩れる（DMSTATUS が全 `0`、half 25 ns では parity 半分）。half 500 ns（≈ 1 MHz）が od の上限。**電気的には push-pull が必須**で、E152 の od/pp 差はこの立上り制限の現れだった。
- **反証（仮説 1）**: X035 側の 4 MHz 上限は観測されなかった。half 0 ns で P4 側コスト律速（≈ 5 MHz 相当）に達しても崩れない。**target 上限は本方法の範囲外**。
- **反証（反証条件 3）**: pp が od より低い clock で崩れることはなかった（仮説どおり）。
- **候補**: PHY 設計値は「dedicated GPIO、push-pull + 明示 turnaround、half 0〜50 ns」。安全側の既定値として half 50 ns（17.9 µs/read、2.9 Mbit/s）を持ち、full-image hash 一致を条件に 0 まで下げる。
- **未決**: 線上波形（rise/fall、実 clock 周波数）`—`（LA なし、`attested`）。abstract command / autoexec sequential read の word あたり時間と poll 省略の可否（→ 次 ID、候補 `flash-time`）。USB 往復（→ 次 ID、候補 `dmi-latency`）。

## 反映

台帳 §1 E153 を完了。oep-probe-arduino の RVSWD PHY は本結果を設計値の根拠にする。仕様 status は動かない。
