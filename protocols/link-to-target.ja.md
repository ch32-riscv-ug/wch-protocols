# WCH-Link ↔ target(SWIO 1 線 / RVSWD 2 線)

状態: **線上エンコードは todo(WCH 公式仕様は非公開)だが、第三者実装が複数あり解読の入口は揃っている**。層は L1 物理 + L2 線上 DMI。WCH 自身は QingKe core の debug を「1-wire / 2-wire DTM」と説明する。

PC 側からは [pc-to-link.ja.md](pc-to-link.ja.md) の `DmiOp` を送るだけで、線上の toggling は WCH-Link firmware が担う。**PC ツールを作るだけならこの層は不要**(DMI 抽象で足りる)。この層が要るのは、WCH-Link 無しで直結する自作 probe(minichlink 系)を作る場合。運ぶ中身は RISC-V の **DMI トランザクション**([riscv-debug-module.ja.md](riscv-debug-module.ja.md))。

## 1. 2 つの物理形態

| 名称 | 信号 | 主な target(core) | 備考 |
|---|---|---|---|
| **1 線 SWIO/SDI** | data 1 本(要 pull-up) | CH32V003・CH641(V2A)、CH32V00X・M007(V2C) | **パルス幅で 0/1 を表す**。外部 pull-up を要する自作例が多い。ARM SWD とは無関係 |
| **2 線 RVSWD** | data + clock(SWDIO/SWCLK) | CH32V103(V3)、CH32V20x/V30x/V317・X03x・L103・CH643(V4)ほか | WCH 固有の RISC-V debug transport。**pin 名が ARM SWD に似るが protocol は非互換** |
| 1/2 線 切替可 | option/config で変わる | V00X・M007、M030、CH564、CH584/585、CH570/572 等 | family 条件で interface が変わる |

- 「CMSIS-DAP 対応」「ARM SWD 対応」は **CH32 RISC-V の 2 線 RVSWD 対応を意味しない**(別 protocol)。
- core 世代と線の対応: V2A/V2C = 1 線、V3/V4 = 2 線([serial-and-print.ja.md](serial-and-print.ja.md) の core 表と一致)。

## 2. USB protocol 層には現れない

attach/DMI/flash の WCH-Link コマンドは 1 線/2 線で**同一**。配線差は WCH-LinkE firmware が吸収する。ただし:

- 1 線 target は **LinkE/LinkW のみ**(旧 CH549 Link は不可)。
- 実運用の非対称は [pc-to-link.ja.md](pc-to-link.ja.md) の family パラメータ(stub・data packet・write pack)に出る。

## 3. 第三者実装(解読の一次資料)

WCH 公開仕様は薄いが、**動作を主張する第三者実装が複数あり**、線上を解読するならこれらが出発点。

### 1 線 SWIO

| 実装 | probe | 参考価値 |
|---|---|---|
| [WCH 公式 CH32F103 1-Line 例](https://github.com/openwch/ch32v003/tree/main/CH32V003_1Line_Base_on_CH32F103) | CH32F103 | **一次資料**。移植の基準(完成 host 製品ではない) |
| [PicoRVD](https://github.com/aappleby/picorvd) | RP2040 PIO | **層分離が明快**(PIO 物理層 / RISC-V DM / V003 flash / SW breakpoint / GDB server)。読みやすい参照 |
| [ESP32-S2 funprog](https://github.com/cnlohr/esp32s2-cookbook/tree/master/ch32v003programmer) | ESP32-S2 bitbang | timing-sensitive な GPIO 操作 + critical section。現行 source は 1 線/2 線両方 |
| [rvswdio_programmer](https://github.com/cnlohr/rv003usb/tree/master/rvswdio_programmer) | CH32V003 | `opmode=1`=SWIO / `opmode=2`=RVSWD を自動判別 |

- SWIO は pulse 幅符号化。minichlink 系の bit-bang GPIO 実装が実際の timing の一次資料。

### 2 線 RVSWD

| 実装 | probe | 参考価値 |
|---|---|---|
| [Swindle](https://github.com/mean00/swindle) | RP2040 PIO | `rvswd.pio` が **start/stop・clock・turnaround・read/write** を生成。Black Magic 由来の target 層。V203/208/303/305/307 を識別(GPL-3.0 系に注意) |
| [rvswdio_programmer](https://github.com/cnlohr/rv003usb/tree/master/rvswdio_programmer) | CH32V003 | RVSWD read/write、V003/00x/20x/30x/X03x 等を掲げる |
| ESP32-S2 funprog | ESP32-S2 | SWCLK pin・RVSWD read/write・family 検出あり(非 V003 の検証範囲は不明確) |
| [RINS](https://perigoso.github.io/rins/) | — | 第三者実装向けに **RVSWD の物理・論理層を文書化**(「SWD ではない」と明記) |
| [WCH RVSWD protocol 初期解析](https://github-wiki-see.page/m/fxsheep/openocd_wchlink-rv/wiki/WCH-RVSWD-protocol) | — | 早期リバース。RINS と整合 |

## 4. 未解読 / 要調査

- SWIO 1 線の bit タイミング/フレーミング、pull-up 前提。
- RVSWD 2 線のクロック/データ極性・turnaround・パケット構造(Swindle `rvswd.pio` が最も具体的)。
- DMI トランザクション(addr/data/op)の線上ビット配置・ACK/parity。
- entry シーケンス(target を debug mode に入れる初期化)、1/2 線切替 target の判定。

## 5. 調査の入口

1. Swindle `rvswd.pio`(2 線)/ PicoRVD PIO(1 線)を読み、波形の生成規則を抽出。
2. ロジックアナライザで LinkE ↔ target を実測し、[riscv-debug-module.ja.md](riscv-debug-module.ja.md) の DMI トランザクションと対応付ける。
3. RINS の論理層記述で裏を取り、解読できたら status を上げる。

## 参照

- 第三者 probe の全体像(host protocol 方式・probe MCU 比較): [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md)
- 運ぶ中身: [riscv-debug-module.ja.md](riscv-debug-module.ja.md) / それを USB に載せる殻: [pc-to-link.ja.md](pc-to-link.ja.md)
