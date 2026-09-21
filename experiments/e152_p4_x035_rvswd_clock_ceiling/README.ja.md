# E152 ESP32-P4 → CH32X035: register 直叩き RVSWD の clock 上限はどこか

状態: 計画

## 問い

`gpio_ll` 直叩き（[E151](../e151_p4_gpio_edge_cost/README.ja.md) の方法）で RVSWD を駆動したとき、
fixture の X035F8U6 に対して DMI read/write が parity・値ともに全数一致する最小 half period は何 ns か。
SWDIO を open-drain + pull-up（現行 OEP）で駆動する場合と、host 駆動区間だけ push-pull にする場合で
上限は違うか。

## 仮説

- 他実装（pico-rvswd、X035 で 400 kHz〜4 MHz parity error 0）から、X035 側は 4 MHz（half 125 ns）まで受ける。
- open-drain + 内部 pull-up では SWDIO の立上りが遅く、push-pull より低い clock で崩れる。
- 現行 OEP（`digitalWrite`、追加遅延 0）の実効 ≈ 0.45 MHz は P4 側の edge コストで決まっており、target 側の上限ではない。

## 反証条件

- half 2000 ns（≈ 250 kHz、[E142](../e142_p4_x035_rvswd_pair/README.ja.md) で成立した 1〜5 µs の範囲）で失敗する（ハーネスの誤り）。
- push-pull が open-drain より低い clock で崩れる。

## 方法

1. SWDIO/SWCLK は `.env`（`TEST_P4_X035_SWDIO` / `_SWCLK`）から注入。
2. half period を {0, 25, 50, 100, 150, 200, 300, 500, 1000, 2000} ns（CPU cycle spin で生成）、
   drive mode を {od, pp} で掃引する。
3. 各点で: 線を Hi-Z から初期化（HIGH clock 100 + STOP）→ `DMCONTROL.dmactive=1` を 2 回 write →
   `DMSTATUS` read 1000 回（parity 一致数、初回値との一致数、1 read の平均時間）→ `DMCONTROL` read 200 回（==1 の数）→
   `DMDATA0` へ pattern write + read back 200 回（一致数）→ `DMCONTROL=0` → Hi-Z。
4. 1 frame ごとに critical section（割り込み禁止 < 1 ms）。
5. host は `S` を送ってから掃引を始める。target の flash・CPU 状態には触れない（halt しない）。

## 対象外

- 線上波形（LA なし。frame 成立の自己申告と MCU 内時計なので `attested`）。
- abstract command / memory access の速度（→ 別 ID、`flash-time` 系）。
- USB 往復（→ 別 ID、`dmi-latency`）。

## 必要な環境

- probe profile `esp32p4_x035`（fixture P4 `30:ed:a0:e3:11:08`）。
- target: CH32X035F8U6（ESIG `0x1ffff704` = `0x035E0601`、device-data `evidence/device_ids.csv`）。
  配線 P4 GPIO2 → PC18/SWDIO、GPIO54 → PC19/SWCLK（E142）。target は別給電・実行中。

## ベンチ種別

一時（既設 fixture、配線変更なし）。

## 記録する数値

`SWEEP mode= half_ns= reads=1000 parity_ok= value_match= dmstatus=0x… ctrl_reads=200 ctrl_ok= writes=200 write_ok= ns_per_read=`。
各 mode 10 点。

## 完了条件

両 mode で全 10 点の行が得られ、half 2000 ns が全数一致していること。全数一致する最小 half_ns を mode ごとに報告する。
全点で崩れなければ「上限は half 0 ns（P4 側コスト律速）以下」として完了。

## 影響

oep-probe-arduino RVSWD PHY の設計値（clock、drive mode）。[link-to-target](../../protocols/link-to-target.ja.md) の
X035 clock 周期は LA 未使用のため status は動かない。

## 結果

状態: 完了（2026-09-22）。採用 run: `_runs/E152_20260921T222109Z_default/`（銘板 `git=9307391+dirty`、cpu 360 MHz、swdio=2 swclk=54）。
DMSTATUS の初回値は全点 `0x00000c82`（od half 0 ns を除く）。

| mode | half_ns | parity | value | ctrl | write | ns/read | 実効 kbit/s（52 bit 換算） |
|---|---:|---:|---:|---:|---:|---:|---:|
| od | 2000 | 1000/1000 | 1000/1000 | 200/200 | 200/200 | 289,305 | 180 |
| od | 1000 | 1000 | 1000 | 200 | 200 | 178,162 | 292 |
| od | 500 | 1000 | 1000 | 200 | 200 | 122,258 | 425 |
| od | 300 | 1000 | 1000 | 200 | 200 | 99,975 | 520 |
| od | 200 | 1000 | 1000 | 200 | 200 | 88,037 | 591 |
| od | 150 | 1000 | 1000 | 200 | 200 | 81,606 | 637 |
| od | 100 | 1000 | 1000 | 200 | 200 | 76,765 | 677 |
| od | 50 | 1000 | 1000 | 200 | 200 | 70,177 | 741 |
| od | 25 | 1000 | 1000 | 200 | 200 | 66,264 | 785 |
| od | **0** | 1000 | 1000（全 `0x00000000`） | **0/200** | **0/200** | 61,333 | 848 |
| pp | 2000 | 1000 | 1000 | 200 | 200 | 279,983 | 186 |
| pp | 1000 | 1000 | 1000 | 200 | 200 | 168,555 | 309 |
| pp | 500 | 1000 | 1000 | 200 | 200 | 112,880 | 461 |
| pp | 300 | 1000 | 1000 | 200 | 200 | 91,427 | 569 |
| pp | 200 | 1000 | 1000 | 200 | 200 | 78,697 | 661 |
| pp | 150 | 1000 | 1000 | 200 | 200 | 73,080 | 712 |
| pp | 100 | 1000 | 1000 | 200 | 200 | 67,495 | 770 |
| pp | 50 | 1000 | 1000 | 200 | 200 | 61,735 | 842 |
| pp | 25 | 1000 | 1000 | 200 | 200 | 56,780 | 916 |
| pp | 0 | 1000 | 1000 | 200 | 200 | 52,815 | 985 |

## 事実 / 候補 / 未決

- **事実**: `gpio_ll` 駆動では half 0 ns でも 1 DMI read ≈ 53 µs（pp）/ 61 µs（od）で、実効 ≈ 1 Mbit/s。**P4 側の GPIO register コスト（E151: 300 ns/access）が律速**で、X035 側の clock 上限はこの方法では見えない。
- **事実**: pp は全 10 点で parity・値・write 全数一致。od は half 25 ns 以上で全数一致、half 0 ns では DMSTATUS が `0x00000000`（parity は偶然一致）で DMCONTROL/DMDATA0 も不一致。追加遅延なしの open-drain 立上りが frame を崩す最初の点。
- **事実**: 現行 OEP backend（`digitalWrite`、half 0）の 110〜130 µs/DMI と比べ、`gpio_ll` + pp で約 2.2 倍。これだけでは LinkE（verify 1.22 s）には届かない。
- **反証**: 仮説「X035 側 4 MHz 付近で崩れる」は本実験では検証不能（P4 側が先に律速）。
- **候補**: PHY は dedicated GPIO（→ [E153](../e153_p4_x035_rvswd_dedic_ceiling/README.ja.md)）。SWDIO は host 駆動区間 push-pull、target 駆動区間は出力無効（turnaround 明示）。
- **未決**: X035 側 clock 上限 `—`（E153）。線上波形 `—`（LA なし）。

## 反映

台帳 §1 E152 を完了。仕様 status は動かない。
