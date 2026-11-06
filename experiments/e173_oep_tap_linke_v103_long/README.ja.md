# E173 WCH-LinkE と CH32V103 の long 形式を線で見る（OEP の短い形式と並べる）

状態: **収録済み(2026-11-06)**。

## 問い

- WCH-LinkE は、CH32V103 とどの形式の RVSWD でやり取りするのか（[link-to-target.ja.md](../../protocols/link-to-target.ja.md) で「LinkE の V103 の線は未収録」とされていた所）。
- OEP のプローブ（短い 52 ビットの形式）は、なぜ V103 につながらないのか。

## 手順

- **LinkE と V103**: WCH-LinkE（fw 2.22、`FB868F068059`）と CH32V103C8T6 の評価ボード（緑の `CH32V103C-R1-1v1`、UID `1035abcd7869bc67`）。その間の SWCLK と SWDIO を利用者が切らずに枝分かれさせ、ESP32-P4X の OEP プローブ `80f1b2d80a34` の GPIO44（SWCLK）と GPIO45（SWDIO）につないだ。GND も共通。その間に `ch32rv target info --probe wchlink://FB868F068059 --speed low`（ch32rv 0.29.1）を 1 回流し、OEP の logic capture で 20 MS/s で取った（GPIO42〜47。動いたのは 44、45、46。46 は隣の線からの漏れと見ている）。
- **OEP と V103**: 同じ P4X の GPIO18（SWDIO）と GPIO17（SWCLK）を、別の CH32V103C8T6 の評価ボード（`v103evt`）の PA13 と PA14 につないでいる。そこで OEP の `oep.wire.rvswd` の attach（halt、idle_clock low、max_speed 200 kHz）を流しながら、同じ 2 本を同じプローブの capture で取った（capture は聞くだけ）。attach は失敗した。
- **OEP と V208（対照）**: ESP32-P4 の OEP プローブ `30eda0e31451` の GPIO14（SWDIO）と GPIO15（SWCLK）につながった CH32V208WBU6 で、同じ attach（run、max_speed 200 kHz）を流して取った。DMSTATUS `0x30c82` が読めた。
- **読み出し**: [`frames.py`](frames.py) は、START（SWCLK が high の間の SWDIO の立ち下がり）から STOP までのビット列を出す。[`long_frames.py`](long_frames.py) は、それを long 形式（host addr7 data32 op2 1 | target addr7 data32 status2 1 | 1 clock）として読む。どちらも wireskein の checkout で動かす。

## 収録

| ファイル | 線 | 中身 |
|---|---|---|
| `out/10_linke_v103c8t6_target_info.wireskein` | g44 = SWCLK、g45 = SWDIO（g42〜47） | LinkE が V103 を `target info` で読む間 |
| `out/20_oep_v103c8t6_attach_short.wireskein` | clk = GPIO17、dio = GPIO18 | OEP が V103 に attach しようとして失敗する間 |
| `out/30_oep_v208_attach_short.wireskein` | clk = GPIO15、dio = GPIO14 | OEP が V208 に attach する間（成功） |

## 結果

- **LinkE と V103**: START と STOP の間は 85 クロックで、long 形式だった。最初の 1 つだけ 80 クロックで、link-to-target の「long 形式の問い合わせ」と見ている。読み出しの例（`long_frames.py out/10_... g45 g44`）は次のとおり。DMCONTROL（0x10）、DATA0（0x04）、COMMAND（0x17）、PROGBUF（0x20〜0x22）の書き込みで、bit 41 は 0、書き込みでは bit 83 も 0 だった。
  ```
  85 clocks  host write addr 0x10 data 0x40000000 [0] | target addr 0x10 data 0x40000000 status 0 [0]
  85 clocks  host write addr 0x10 data 0x80000001 [0] | target addr 0x10 data 0x80000001 status 0 [0]
  85 clocks  host write addr 0x04 data 0xe0000380 [0] | target addr 0x04 data 0xe0000380 status 0 [0]
  85 clocks  host write addr 0x17 data 0x0023100d [0] | target addr 0x17 data 0x0023100d status 0 [0]
  85 clocks  host write addr 0x20 data 0x00058483 [0] | target addr 0x20 data 0x00058483 status 0 [0]
  ```
- **OEP と V208**: 53 クロック（52 ビットの短い形式と STOP 前の 1 クロック）でやり取りし、つながった。
- **OEP と V103**: OEP は V103 にも短い形式で話しかけ、V103 は答えなかった（8、17、11、6、2 クロックの短いフレームのあと、107 クロックのものが 1 つ）。V103 は long 形式でしか答えないと見ている。OEP のファーム（oep-probe-arduino の `OepRvswdPhy`）に long 形式を足すのが次の作業。
