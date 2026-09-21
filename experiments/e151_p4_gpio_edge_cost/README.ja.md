# E151 ESP32-P4: GPIO 1 edge のコストは駆動方法でいくら違うか

状態: 計画

## 問い

ESP32-P4（360 MHz、Arduino-ESP32 3.3.12）で、GPIO の 1 edge（set または clear）と
1 sample（read）にかかる時間は、`digitalWrite()` / `digitalRead()`、`gpio_ll_set_level()` /
`gpio_ll_get_level()`、dedicated GPIO（CPU CSR 直結）のそれぞれで何 ns か。
さらに RVSWD の 1 bit 相当（CLK low → DIO 更新 → CLK high）は各方法で何 ns か。

## 仮説

- `digitalWrite()` は HAL 経由で数百 ns（[E005](../e005_tool_clocked_capture/README.ja.md) の予想、ESP32-S3 で数百 ns 級）。
- `gpio_ll_*` は W1TS/W1TC への 1 store なので 10〜20 ns 級。
- dedicated GPIO は CSR 書込みで 1 命令、数 ns 級。
- 現行 OEP backend（`digitalWrite` bit-bang、追加遅延 0）の DMI 1 transaction ≈ 110〜130 µs
  （E145 の 4.94 s / 31.7k DMI read から逆算）は、この edge コストで説明できる。

## 反証条件

- `digitalWrite()` と `gpio_ll` の差が 2 倍未満（HAL の overhead が支配的ではない）。
- `gpio_ll` の 1 bit 相当が 100 ns を超える（別の律速がある）。

## 方法

1. 未配線の P4 GPIO 2 本（`.env` の `TEST_P4_FREE_PIN_A` / `_B`）を output にする。
2. 各方法で N edge を連続実行し、`esp_timer_get_time()` の差から ns/edge を求める。
   fast な方法は割り込みを止めて測る。`digitalWrite` は N=20,000、それ以外は N=200,000。3 回反復。
3. read も同様に N sample の ns/sample を求める（ピンは input、値は捨てない）。
4. 1 bit 相当: pin A を CLK、pin B を DIO とし、`CLK=0; DIO=bit; CLK=1` を N bit 繰り返す。
5. host は `R` を送ってから測定を始める（起動出力の取りこぼし回避、E004）。

## 対象外

- 実 RVSWD frame の成立（→ [E152](../e152_p4_x035_rvswd_clock_ceiling/README.ja.md)）。
- 線上波形の rise/fall（LA なし。MCU 内時計のみ、`attested`）。
- USB 往復時間（別 ID）。

## 必要な環境

- probe profile `esp32p4_x035`（fixture P4 `30:ed:a0:e3:11:08`、USB-Serial/JTAG）。
- target 不要。pin A/B は fixture 配線表（oep-probe-arduino `docs/esp32-p4-x035-evaluation-2026-09-20.ja.md`）に無い GPIO を `.env` で指定する。

## ベンチ種別

一時（既設の P4/X035 fixture をそのまま使う。配線変更なし）。

## 記録する数値

`EDGE method= pin= edges= total_us= ns_per_edge=`、`READ method= ...ns_per_sample=`、
`BIT method= bits= ns_per_bit=`。各 3 回。

## 完了条件

3 方法 × (edge, read, bit) の ns が 3 回とも得られ、順序（digitalWrite > gpio_ll ≥ dedic）が確認できたら完了。
順序が逆でも数値が取れれば完了とし、反証として記録する。

## 影響

oep-probe-arduino の RVSWD PHY 実装方針（`digitalWrite` を捨てるか）、E152 の掃引範囲。
仕様の status は動かない。

## 結果

状態: 完了（2026-09-22）。採用 run: `_runs/E151_20260921T221816Z_default/`（`dut.log` に銘板 `git=9307391+dirty`、cpu 360 MHz、pins 0/1）。3 回反復はいずれも下表の値 ±1% 以内。

| 測定 | `digitalWrite` / `digitalRead` | `gpio_ll` | dedicated GPIO |
|---|---:|---:|---:|
| EDGE ns/edge | 570 | 300.0 | 52.8 |
| READ ns/sample | 498 | 300.0 | 25.0 |
| BIT ns/bit（CLK↓, DIO=bit, CLK↑） | 1,770〜1,800 | 800.5 | 94.5 |

## 事実 / 候補 / 未決

- **事実**: P4 の通常 GPIO register（W1TS/W1TC、IN）へのアクセスは read/write とも **300 ns（108 cycle）** かかり、HAL を外しても 1.9 倍しか速くならない。`digitalWrite` の 570 ns のうち HAL 分は約 270 ns。
- **事実**: dedicated GPIO（CPU CSR 直結）は write 52.8 ns、read 25.0 ns。1 bit 相当 94.5 ns で、`digitalWrite` 比 **18.8 倍**、`gpio_ll` 比 8.5 倍。
- **事実**: 現行 OEP backend の bit-bang（`digitalWrite`、追加遅延 0）は 1 bit ≈ 1.8 µs。52 bit frame + read 系で DMI 1 transaction ≈ 100 µs 超となり、E145 から逆算した 110〜130 µs と整合する。
- **反証**: 仮説「`gpio_ll` は 10〜20 ns 級」は誤り。P4 では GPIO 周辺 register の bus latency が支配的。
- **候補**: RVSWD PHY は dedicated GPIO で実装する。1 bit ≈ 95 ns なので P4 側の上限は約 10 Mbit/s、target 側上限（4 MHz 報告）が先に来る。
- **未決**: dedicated GPIO で実 X035 に対する clock 上限（→ [E153](../e153_p4_x035_rvswd_dedic_ceiling/README.ja.md)）。線上の rise/fall は LA 未使用 `—`。

## 反映

- 台帳 §1 E151 を完了。仕様 status は動かない。
- oep-probe-arduino の RVSWD PHY 方針: `digitalWrite` / `gpio_ll` を捨て dedicated GPIO へ（実装は E153 の結果後）。
