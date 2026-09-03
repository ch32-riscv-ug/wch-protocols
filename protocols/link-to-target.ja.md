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

## 3. RVSWD 2 線の線上フレーム(具体)

状態: **attested**(fxsheep のリバース解析 + RINS が一致。自前ロジアナ実測で `verified` 化が要る)。出典: [WCH RVSWD protocol 解析](https://github-wiki-see.page/m/fxsheep/openocd_wchlink-rv/wiki/WCH-RVSWD-protocol)、Swindle `doc/rvswd.jpg`。

**要点: RVSWD の 1 トランザクション = RISC-V DTM の `dmi` レジスタ(addr7 + data32 + op2)そのもの**。つまり USB の `DmiOp`(cmd `0x08`、payload `[addr, data_be32, op]`。[pc-to-link.ja.md](pc-to-link.ja.md) §4)は、この線上フレームを byte 詰めしただけ。WCH-Link は透過ブリッジ。

### 信号とアイドル

- 2 線: **SWDIO(data)/ SWCLK(clock)**。無トランザクション時は**両方 HIGH**。
- pin 名は ARM SWD に似るが**別 protocol**(RISC-V Debug 0.13、designer=WCH)。

### start / reset

- RISC-V mode で起動時、SWDIO/SWCLK を HIGH にし、**IO を HIGH のまま 100 クロック(100 個の 1)を送り、STOP 条件**を出す(初期化)。

### bit の駆動と sample

- **clock が HIGH の間に bit を sample**(SWDIO=HIGH → 1、LOW → 0)。
- **data は clock が LOW の間だけ変化**させる。
- **全フィールド MSB first**。

### 1 トランザクションのフレーム(順に)

| 位相 | 送信側 | bit 数 | 内容 |
|---|---|---:|---|
| Address | host | **7** | DMI レジスタ番地 |
| Data | host | **32** | 書込データ(read 時は don't-care) |
| Operation | host | **2** | op(RISC-V DTM: 0 nop / 1 read / 2 write) |
| Parity1 | host | **1** | Address+Data+Operation の **odd parity** |
| Address | target | **7** | エコー |
| Data | target | **32** | 読出データ |
| Status | target | **2** | status(0 success / 2 failed / 3 busy) |
| Parity2 | target | **1** | Address+Data+Status の **even parity** |

- host 位相(7+32+2+1)→ target 位相(7+32+2+1)と続き、明示の turnaround bit は文書化されていない(位相の並びで暗黙に切替)。
- これは [riscv-debug-module.ja.md](riscv-debug-module.ja.md) の DMI トランザクションと 1:1(op/status のコード、addr=DMDATA0=`0x04`/DMCONTROL=`0x10` 等がそのまま線上の 7bit addr に乗る)。
- USB `DmiOp` 応答 `[addr, data_be32, status]` の status(0/2/3)も、この target 位相の 2bit status と同じ。

### SWIO 1 線との関係(transaction は同じ、bit 符号化だけ違う)

- **SWIO も運ぶ中身は同じ DMI トランザクション**。minichlink の programmer 抽象は 1 線/2 線とも `WriteReg32(reg_7bit, u32)` / `ReadReg32(reg_7bit, *u32)` = **7bit reg + 32bit data**(= §3 の RVSWD host 位相と同一)。
- 違いは**物理だけ**: RVSWD は clock 線で bit を刻む。SWIO は clock 線が無く、**1 本の line を host が LOW に引くパルスの幅で 0/1 を符号化**する(line は pull-up で HIGH がアイドル)。
- **exact な pulse 幅/タイミングは未確定**(gap)。一次資料候補: WCH `CH32V003RM`(datasheet の debug/SDI 章)、cnlohr の bit-bang firmware(ESP32-S2 funprog / AVR zooswio の timing)。

### まだ不明

- (RVSWD)STOP 条件の波形詳細(SWDIO 遷移のタイミング)、クロック周波数、複数トランザクション間のアイドル規則。
- (RVSWD)7bit addr が RISC-V 標準 DTM(通常 abits 可変)とどう対応するか(WCH は 7bit 固定と観測)。
- (SWIO)LOW パルス幅の 0/1 閾値・start/frame・turnaround の実値。

## 4. 第三者実装(解読の一次資料)

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
| [Swindle](https://github.com/mean00/swindle) | RP2040 | **RVSWD の protocol 図 `doc/rvswd.jpg`** が最も分かりやすい。RP2040 firmware が線を生成し host は BMP-remote 経由(`blackmagic_addon/hosted/remote_rv_protocol.c`)。target 層は `blackmagic_addon/target/CH32V3xx/`。V203/208/303/305/307。GPL-3.0 系。**現行 repo に `rvswd.pio` は無い**(§3 の bit 仕様は attic + fxsheep から) |
| [rvswdio_programmer](https://github.com/cnlohr/rv003usb/tree/master/rvswdio_programmer) | CH32V003 | RVSWD read/write、V003/00x/20x/30x/X03x 等を掲げる |
| ESP32-S2 funprog | ESP32-S2 | SWCLK pin・RVSWD read/write・family 検出あり(非 V003 の検証範囲は不明確) |
| [RINS](https://perigoso.github.io/rins/) | — | 第三者実装向けに **RVSWD の物理・論理層を文書化**(「SWD ではない」と明記) |
| [WCH RVSWD protocol 初期解析](https://github-wiki-see.page/m/fxsheep/openocd_wchlink-rv/wiki/WCH-RVSWD-protocol) | — | 早期リバース。RINS と整合 |

## 5. 未解読 / 要調査

- **SWIO 1 線**の bit タイミング/フレーミング(pulse 幅符号化)、pull-up 前提。RVSWD(§3)ほど整理された公開解析がまだ無い。
- RVSWD の STOP 条件波形・クロック周波数・トランザクション間アイドル(§3 末尾)。
- 1/2 線切替 target の判定と entry シーケンス(debug mode 突入の初期化)。
- §3 の bit 仕様をロジアナ自前実測で `verified` 化。

## 6. 調査の入口

1. §3 の RVSWD フレームをロジックアナライザで LinkE ↔ target 実測し、[riscv-debug-module.ja.md](riscv-debug-module.ja.md) の DMI トランザクション(addr/data/op/status)と 1:1 対応を確認。
2. SWIO(1 線)は PicoRVD PIO / cnlohr minichlink の bit-bang を読み、pulse 幅規則を抽出。
3. RINS の論理層記述と fxsheep 解析で裏を取り、status を上げる。

## 参照

- 第三者 probe の全体像(host protocol 方式・probe MCU 比較): [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md)
- 運ぶ中身: [riscv-debug-module.ja.md](riscv-debug-module.ja.md) / それを USB に載せる殻: [pc-to-link.ja.md](pc-to-link.ja.md)
