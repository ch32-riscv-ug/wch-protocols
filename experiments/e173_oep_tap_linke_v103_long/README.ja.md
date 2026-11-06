# E173 WCH-LinkE と CH32V103 の long 形式を線で見る（OEP の短い形式と並べる）

状態: **収録済み(2026-11-06)**。OEP 側の対応は oep-probe-arduino の `OepRvswdFrame.h`（`longFrame`）。

## 問い

- WCH-LinkE は、CH32V103 とどの形式の RVSWD でやり取りするのか（[link-to-target.ja.md](../../protocols/link-to-target.ja.md) で「LinkE の V103 の線は未収録」とされていた所）。
- OEP のプローブ（短い 52 ビットの形式）は、なぜ V103 につながらないのか。

## 手順

- **LinkE と V103**: WCH-LinkE（fw 2.22、`FB868F068059`）と CH32V103C8T6 の評価ボード（緑の `CH32V103C-R1-1v1`、UID `1035abcd7869bc67`）。その間の SWCLK と SWDIO を利用者が切らずに枝分かれさせ、ESP32-P4X の OEP プローブ `80f1b2d80a34` の GPIO44（SWCLK）と GPIO45（SWDIO）につないだ。GND も共通。その間に `ch32rv target info --probe wchlink://FB868F068059 --speed low`（ch32rv 0.29.1）を 1 回流し、OEP の logic capture で 20 MS/s で取った（GPIO42〜47。動いたのは 44、45、46。46 は隣の線からの漏れと見ている）。
- **OEP と V103**: 同じ P4X の GPIO18（SWDIO）と GPIO17（SWCLK）を、別の CH32V103C8T6 の評価ボード（`v103evt`）の PA13 と PA14 につないでいる。そこで OEP の `oep.wire.rvswd` の attach（halt、idle_clock low、max_speed 200 kHz）を流しながら、同じ 2 本を同じプローブの capture で取った（capture は聞くだけ）。attach は失敗した。
- **OEP と V208（対照）**: ESP32-P4 の OEP プローブ `30eda0e31451` の GPIO14（SWDIO）と GPIO15（SWCLK）につながった CH32V208WBU6 で、同じ attach（run、max_speed 200 kHz）を流して取った。DMSTATUS `0x30c82` が読めた。
- **読み出し**: WireSkein の decode で読む（RVSWD の short / long 形式、START / STOP の区切り、DMI と debug module の解釈は WireSkein のもの）。例: `wireskein decode out/11_info_low.wireskein --spec '{"buses":[{"protocol":"rvswd","pins":{"swdio":"swdio","swclk":"swclk"}}]}'`（10 と 20 / 30 は線の名前が違う: 10 は g45 / g44、20 / 30 は dio / clk）。各フレームの形（`form`）、status、target の答えの遅れ（`answer_delay`）も出る。

## 収録

| ファイル | 線 | 中身 |
|---|---|---|
| `out/10_linke_v103c8t6_target_info.wireskein` | g44 = SWCLK、g45 = SWDIO（g42〜47） | LinkE が V103 を `target info` で読む間 |
| `out/20_oep_v103c8t6_attach_short.wireskein` | clk = GPIO17、dio = GPIO18 | OEP が V103 に attach しようとして失敗する間 |
| `out/30_oep_v208_attach_short.wireskein` | clk = GPIO15、dio = GPIO14 | OEP が V208 に attach する間（成功） |
| `out/11_info_low.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv target info --speed low`（rc 0） |
| `out/12_info_medium.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv target info --speed medium`（rc 0） |
| `out/13_info_high.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv target info --speed high`（rc 0） |
| `out/14_option_get.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv target option get --speed high`（rc 0） |
| `out/15_read_option.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv read --region option -o /dev/null --speed high`（rc 0） |
| `out/16_read_ram.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv read --region ram+0+20480 -o /dev/null --speed high`（rc 0） |
| `out/17_read_code.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv read --region code -o /dev/null --speed high`（rc 0） |
| `out/18_dbg_halt.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv dbg halt --speed medium`（rc 0） |
| `out/19_dbg_regs.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv dbg regs --speed medium`（rc 0） |
| `out/21_dbg_resume_low.wireskein` | swclk = GPIO44、swdio = GPIO45、80 MS/s | `ch32rv dbg resume --speed low`（rc 0） |

`out/11_*` 以降は [`linke_tap.py`](linke_tap.py) で取った。読むだけの操作で、V103 には書き込んでいない（最後に resume）。各 `.json` に ch32rv のコマンドと出力がある。`20_dbg_resume` は medium の速さで `dmi write failed` になったので消し、low で取り直した（`21_dbg_resume_low`）。

## 結果

- **LinkE と V103**: START と STOP の間は 85 クロックで、long 形式だった。最初の 1 つだけ 80 クロックで、link-to-target の「long 形式の問い合わせ」と見ている。読み出しの例（WireSkein の decode で out/10 を読んだもの）は次のとおり。DMCONTROL（0x10）、DATA0（0x04）、COMMAND（0x17）、PROGBUF（0x20〜0x22）の書き込みで、bit 41 は 0、書き込みでは bit 83 も 0 だった。
  ```
  85 clocks  host write addr 0x10 data 0x40000000 [0] | target addr 0x10 data 0x40000000 status 0 [0]
  85 clocks  host write addr 0x10 data 0x80000001 [0] | target addr 0x10 data 0x80000001 status 0 [0]
  85 clocks  host write addr 0x04 data 0xe0000380 [0] | target addr 0x04 data 0xe0000380 status 0 [0]
  85 clocks  host write addr 0x17 data 0x0023100d [0] | target addr 0x17 data 0x0023100d status 0 [0]
  85 clocks  host write addr 0x20 data 0x00058483 [0] | target addr 0x20 data 0x00058483 status 0 [0]
  ```
- **OEP と V208**: 53 クロック（52 ビットの短い形式と STOP 前の 1 クロック）でやり取りし、つながった。
- **OEP と V103**: OEP は V103 にも短い形式で話しかけ、V103 は答えなかった。V103 は long 形式でしか答えない。
- **bit 83 はホストのもの**: LinkE の書き込みは bit 83 が 0、読みは 1。OEP が long 形式を足した最初の版は、bit 83 で線を放したまま（引き上げで 1）にしていて、V103 は書き込みを受け付けなかった（DMCONTROL の書き込みに status 1、PROGBUF0 は書いても 0 のまま）。bit 83 をホストが書き込みで 0、読みで 1 にして出すと、OEP は V103 に attach できた（halt、DMSTATUS 0x382、494 kHz）。link-to-target の表は bit 83 を target の位相に入れているが、ホストが出すビットと読める。
- 読み出しの区切り: STOP は「SWDIO が SWCLK の high の間に上がり、その high がその前の 16 回の high の中央値の 1.5 倍より長いもの」と読む（WireSkein の決まり）。high の長さは、LinkE の STOP が 3.3〜3.75 倍、OEP の short 形式の STOP が約 2 倍、target が SWCLK の high の間に線を動かす所が 1.2 倍まで。決まった 2 µs では、速い LinkE（13、17）の STOP の high（1.84〜2.0 µs）を取りこぼし、2 つのフレームが 1 つ（170 クロック）にまとまる。11〜21 は全部 85 クロックで、30 は 53 クロックで切れる。20 は V103 が答えず SWDIO が low のままなので、START と STOP では切れない。
- **ch32rv が終わるときの V103 の答え**: 13、14、19 の終わりの 1〜2 フレームは、V103 の答えが 1 クロック遅れる（0x10 を 0x08 として返し、データも 1 ビットずれる。13 には status 2 のフレームもある）。wireskein の解析（ee37142）による。
- **LinkE は long 形式から問い合わせる**: V203 の target info の記録でも、LinkE はまず long 形式の問い合わせを 202 回送り（target の位相は全部 1、V203 は答えない）、そのあと short 形式に移る（wireskein の解析）。
- **動いている V103 に short 形式を送ると止まる**: 動いている（dmactive の立った）V103 は、short 形式のフレームを受けるか、線を放されるか、フレームの途中で線が切れると、それ以降のフレームに status 2 を返し続け、電源を切るまで戻らない（OEP の probe `80f1b2d80a34` の v103evt、別の CH32V103R8T6）。OEP の probe は、LinkE と同じく long 形式から問い合わせ、閉じた後も線を駆動したまま保つ（oep-probe-arduino 32d5a33）。
