# WCH-Link ↔ target(SWIO 1 線 / RVSWD 2 線)

状態: **V003のSWIOはESP32実装でend-to-end verified、RVSWDも主要フレームを実測済み。電気的な許容限界は未確定**。層は L1 物理 + L2 線上 DMI。WCH 自身は QingKe core の debug を「1-wire / 2-wire DTM」と説明する。

PC 側からは [pc-to-link.ja.md](pc-to-link.ja.md) の `DmiOp` を送るだけで、線上の toggling は WCH-Link firmware が担う。**PC ツールを作るだけならこの層は不要**(DMI 抽象で足りる)。この層が要るのは、WCH-Link 無しで直結する自作 probe(minichlink 系)を作る場合。運ぶ中身は RISC-V の **DMI トランザクション**([riscv-debug-module.ja.md](riscv-debug-module.ja.md))。

## 1. 2 つの物理形態

| 名称 | 信号 | 主な target(core) | 備考 |
|---|---|---|---|
| **1 線 SWIO/SDI** | data 1 本(要 pull-up) | CH32V003・CH641(V2A)、CH32V00X・M007(V2C) | **パルス幅で 0/1 を表す**。外部 pull-up を要する自作例が多い。ARM SWD とは無関係 |
| **2 線 RVSWD** | data + clock(SWDIO/SWCLK) | CH32V103(V3)、CH32V20x/V30x/V317・X03x・L103・CH643(V4)ほか | WCH 固有の RISC-V debug transport。**pin 名が ARM SWD に似るが protocol は非互換** |
| 1/2 線 切替可 | option/config で変わる | V00X・M007、M030、CH564、CH584/585、CH570/572 等 | family 条件で interface が変わる |

- 「CMSIS-DAP 対応」「ARM SWD 対応」は **CH32 RISC-V の 2 線 RVSWD 対応を意味しない**(別 protocol)。**なぜ別 protocol になったのかは §3b**。
- core 世代と線の対応: V2A/V2C = 1 線、V3/V4 = 2 線([serial-and-print.ja.md](serial-and-print.ja.md) の core 表と一致)。

## 2. USB protocol 層には現れない

attach/DMI/flash の WCH-Link コマンドは 1 線/2 線で**同一**。配線差は WCH-LinkE firmware が吸収する。ただし:

- 1 線 target は **LinkE/LinkW のみ**(旧 CH549 Link は不可)。
- 実運用の非対称は [pc-to-link.ja.md](pc-to-link.ja.md) の family パラメータ(stub・data packet・write pack)に出る。

## 3. RVSWD 2 線の線上フレーム(具体)

状態: **形式別**。52-bit short形式はWCH-LinkE + X035/L103実測と複数の実機動作実装により主要境界を`verified`。84-bit long形式は複数資料で`attested`で、LinkEの接続時にhost位相だけを実測した（targetは応答しない。下記）。WCH-LinkEの585-edge bulk burstは構造のみ実測済み。詳細は実測report [2026-09-11 V003/X035](../captures/fixtures/wire-flash-v003-x035-2026-09-11/README.ja.md) / [2026-09-25 L103/V203/V003](../captures/fixtures/wire-linke-p4-2026-09-25/README.ja.md)。

**要点: RVSWDはDMI address/data/operation/statusを運ぶが、線上形式は一つではない。** USBの`DmiOp`（cmd `0x08`）から、WCH-Link firmwareがshort/long/burstの選択、busy再試行、内部DMI操作を加えることがあるため、常にbyte列の透過ブリッジとは限らない。

### 信号とアイドル

- 2 線: **SWDIO(data)/ SWCLK(clock)**。無トランザクション時は**両方 HIGH**。
- pin 名は ARM SWD に似るが**別 protocol**(RISC-V Debug 0.13、designer=WCH)。

### start / reset

- RISC-V mode で起動時、SWDIO/SWCLK を HIGH にし、**IO を HIGH のまま 100 クロック(100 個の 1)を送り、STOP 条件**を出す(初期化)。

### bit の駆動と sample

- **clock が HIGH の間に bit を sample**(SWDIO=HIGH → 1、LOW → 0)。
- **data は clock が LOW の間だけ変化**させる。
- **全フィールド MSB first**。

### 84-bit long形式（従来資料）

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

**LinkE の接続時に出る long 形式（2026-09-25 実測、L103/V203/V003）。** USB `81 0d 01 02`(AttachChip)の低速区間(約 475 kHz)の先頭に、START + 85 clock + STOP の frame が 202 個並ぶ。short の 52 bit + 終端 1 clock と同じく、上の表の 84 bit + 終端 1 clock と読める。

| 部分 | 1 個目 | 2〜202 個目(201 個、5 回の接続で 1005/1005 が同一) |
|---|---|---|
| host 位相 | addr `0x11`、data `0x19`、op `00`、parity `0` | addr `0x11`(DMSTATUS)、data `0`、op `01`(read)、parity `1` |
| target 位相 | addr `0010010`、data `0xffffffff`、status `11`、parity `1` | addr `1111101`、data `0xffffffff`、status `11`、parity `1` |
| 終端 clock | `0` | `0` |

- target 位相は data が全部 1 なので、線が pull-up のまま(誰も駆動していない)と読める。**LinkE はまず long 形式で DMSTATUS を問い合わせ、応答が無いので short 形式へ移る**という解釈(推定)。L103 と V203 で同じ列が出る。long 形式に応答する target(V103 等)では未確認。
- **1 線の V003 でも同じ問い合わせが SWIO の pin に出る**。V003 の接続時には LOW 4.0 / 8.3 / 9.3 / 102 µs の組が 202 回並ぶが、これは L103 の long frame 中の SWDIO の LOW 区間(4.1 / 8.3 / 9.3 / 102.3 µs)と同じ。続けて 0x7e/0x7d 書込みの RVSWD short frame(L103 と同じ LOW 区間の並び)が数回出て、約 2 ms の LOW の後に SWIO の通信が始まる。LinkE は target の線の種類を知らずに、まず 2 線の形で問い合わせていると読める。
- 2〜202 個目の host parity は addr+data+op と合わせて**偶数**になる。表の「odd parity」と合わない。1 個目は op `00` で、何のための frame かは分からない。
- 解析: `captures/tools/rvswd.py` / `swio.py` / `linke_repeat_compare.py`(実行方法は [captures/README](../captures/README.ja.md)「解析環境(uv)」)。

**frame の区切り方(実装・解析向け、LinkE 2.22 実測)。** clock の空き時間ではなく、START / STOP で区切る。
- START: SWCLK が high(アイドル)の間に SWDIO が立ち下がる。高速区間では立ち下がりの約 160 ns 前、低速区間では high の中ほどに来る。
- STOP: SWCLK が high の間に SWDIO が立ち上がり、その後 SWCLK の high が 2 µs 以上続く。
- read 中は **target も SWCLK の立ち上がりから 0.3 µs 以内に、high の間に SWDIO を変える**。形の上では START/STOP と同じなので、「frame の外の立下りだけを START とする」「後に長い high が続く立上りだけを STOP とする」という状態の区別が要る。
- 空き時間で区切ると、低速設定の burst が 46 + 14×(3+35) + 7 のように割れる。また前後の clock を 1 つ取り込むので、read が 54 clock に見える(fixture 付属の `dmi_decode.py` はこの方式で、2,018 件の write を R と表示し、60 frame を取りこぼしていた。値は START/STOP 方式と全件一致)。

### 52-bit short形式（LinkE + X035/L103実測）

2026-09-11の実測では、通常packetは`addr7 + R/W1 + parity1 + aux5 + data32 + parity1 + aux5`の52 bitで、その後にSTOP条件が続いた。data/parity位置は8,628 packetsすべてで検算済み。2026-09-20のESP32-P4→X035実機試験では、hostがauxを`10101`/`10111`として駆動する独立実装と同じ方式でDMI read/writeが成立した。**従来ここでbit 48–49をtarget statusと解釈した記述は誤り**で、short direct-DMIではUSB応答のstatusと対応付けない。

さらに4 KiB readbackでは585-edge/15-word burstを64回観測した。park/paddingはLinkEが一定値に固定せず、既存probeが使う`10101`/`10111`とも異なるが、X035はその固定値でも実機動作している。したがって同期語ではなくdon’t-careとして扱う。

2026-09-25 の L103 実測([fixture](../captures/fixtures/wire-linke-p4-2026-09-25/README.ja.md))で、次のことが加わった。

- **read も write も 53 clock**(52 bit + 終端 1 clock。09-11 の X035 と同じ)。START/STOP で区切ると、L103 の全 capture(12 操作 + extra)の 23,447 個と繰返し 5 回分がすべて 53 clock で、header / data の parity はすべて一致した。以前ここに書いた「read は 54 clock」は、空き時間で区切った解析の誤りだった。
- 同じ操作を 5 回繰り返すと、frame の並びと値は bit 単位で一致した(違いは RAM 上で動いている counter の 1 word だけ)。一方で park/padding(bit 9–13)と末尾(bit 47–52)は run ごとに変わり、53 clock の frame に write で 32 通り、read で 17〜22 通りが出た。その状態で parity は一致し、操作もすべて成功したので、**don't-care という扱いの根拠が強まった**。
  - X035 で見えた「bit 9 は常に bit 8(parity)と同値」は L103 では成り立たない(618/660、737/775)。
- burst は 15 word(585 clock)のほかに 7 word(281)と 3 word(129)も出た。長さは常に 15 + 38×N clock。
- clock: L103 の高速区間は約 2.5 MHz、接続時の低速区間は約 475 kHz。V203 は高速区間の一部で **SWCLK の周期が 60〜100 ns**(high 約 20〜30 ns)。
  - 50 MHz の収録では記録しきれない。100 MHz では 1 sample のひげと区別しきれない所が残る(99.7 % を parity 一致で復元)。160 MHz なら既知の長さだけに区切れた。
  - SDI monitor の 25 MHz 収録では、約 0.2 % の frame で clock が 1 つ落ちる。

### SWIO 1 線との関係(transaction は同じ、bit 符号化だけ違う)

- **SWIO も運ぶ中身は同じ DMI トランザクション**。minichlink の programmer 抽象は 1 線/2 線とも `WriteReg32(reg_7bit, u32)` / `ReadReg32(reg_7bit, *u32)` = **7bit reg + 32bit data**(= §3 の RVSWD host 位相と同一)。
- 違いは**物理だけ**: RVSWD は clock 線で bit を刻む。SWIO は clock 線が無く、**1 本の line を host が LOW に引くパルスの幅で 0/1 を符号化**する(line は pull-up で HIGH がアイドル)。
- **既知のpulse幅実装はV003実機で動作確認済み**。classic ESP32 GPIO16からattach、DMI read/write、halt、RAM実行、user flash全82 page書込み、user code実行、bootloader再entryまで成立した（[E123〜E131](../experiments/LEDGER.ja.md)）。ただし0/1 pulse幅の**受理限界**と電気条件の限界は未確定。

### まだ不明

- (RVSWD)STOP 条件の波形詳細(SWDIO 遷移のタイミング)、複数トランザクション間のアイドル規則。clock 周波数は LinkE 2.22 の実測値がある(上記)が、速度設定(`0x0c`)との対応表はまだ作っていない(`extra/*/speed_*` に収録済み)。
- (RVSWD)接続時の long 形式の問い合わせ(上記)に target が応答した場合の続き、long/short を選ぶ規則。
- (RVSWD)7bit addr が RISC-V 標準 DTM(通常 abits 可変)とどう対応するか(WCH は 7bit 固定と観測)。
  - **新しい材料(2026-09-07、Swindle の source 読解)**: BMDA 側は **`dmi->address_width = 8U`** と宣言し、probe 側の responder は **`address as u8`** で受けている(`blackmagic_addon/hosted/remote_rv_protocol.c` / `rs_swindle/src/native/rpc_target/mod.rs`)。→ **8 bit 幅で上位未使用**か、**Swindle が余裕を取っている**かのどちらか。**線上が 7 か 8 かは依然未測定**。
- (SWIO)動作点のpulse幅とframeは実装・実機検証済み。未確定なのはLOWパルス幅の0/1**許容閾値**、pull-up/open-drain条件の限界、fast-read応答先頭bit。

## 3b. なぜ ARM SWD と別 protocol なのか

状態: **解説**(一次資料は ARM ADI/SWD 仕様と RISC-V Debug Spec = どちらも公開標準。RVSWD 側は §3 の観測。**「JTAG scan の時間多重」という読みは本書の解釈**)。

**動機は同じ(ピン数削減)だが、載せている上位アーキテクチャが違う。**

### 何をシリアライズしているか

| | **ARM SWD** | **RVSWD** |
|---|---|---|
| 上位 | **ADIv5 の DP/AP レジスタ空間**(バス) | **RISC-V DTM の `dmi` レジスタ**(1 本) |
| 運ぶもの | 「どの AP の、どのレジスタを read/write」 | **`(addr, data32, op)` の 1 組**(= §3) |
| 元の姿 | **SWD 自体が ADI の正式な 2 線 transport** | **JTAG の `dmi` スキャン** |
| framing の性格 | **packet protocol**(アドレス・応答・データが framing の構造) | **shift protocol**(framing に構造は無く、アドレスは payload) |

- **SWD のフレーム**: 8 bit の packet request(Start / APnDP / RnW / A[3:2] / Parity / Stop / Park)→ turnaround → **3 bit ACK**(OK/WAIT/FAULT)→ turnaround → data 32 + parity。
- **RVSWD のフレーム**(§3): `addr7 + data32 + op2 + parity1` を送り、続けて `addr7 + data32 + status2 + parity1` を受ける。これは **RISC-V 標準 DTM の `dmi` DR(`abits + 32 + 2`)そのもの**。

> **読み**: JTAG は TDI と TDO が別線なので押し込みと吐き出しが同時に起きる。**RVSWD は線が 1 本なので、それを前後に時間分割しただけ**。つまり **SWD は「ADI のバスアクセスを 2 線に詰めた packet protocol」、RVSWD は「JTAG の dmi スキャンを 2 線に時間多重した shift protocol」**。

### なぜ WCH が自分で作る必要があったか

**RISC-V Debug Spec は DM と DMI の"インタフェース"は標準化したが、DTM(transport)は JTAG しか具体的に規定していない。** 「2 線でやりたい」なら**各ベンダが発明するしかない**。ARM は SWD を ADI の一部として自分で規定したので全社共通 — **ここが非対称**。

ピン数が動機なのは共通で、**ARM は 4→2(SWD)、WCH は 4→2(RVSWD)と 4→1(SWIO)**で解いた。8〜20 ピンの chip に JTAG の 4〜5 本は重い。

### SWD が背負っている要件は RVSWD には要らない

| SWD が持つもの | なぜ要るか | RVSWD |
|---|---|---|
| **multi-drop**(SWD v2 の `TARGETSEL`) | 1 バスに複数 target | **不要** |
| **JTAG↔SWD 切替シーケンス**(`0xE79E`) | 既存 JTAG との後方互換・両対応パッド | **不要**(§3 の初期化は「IO HIGH で 100 クロック + STOP」だけ) |
| **3 状態 ACK**(OK / WAIT / FAULT) | AP の先が AHB/APB ブリッジで stall しうるので、**待ちを framing に持つ**必要がある | **不要。** DMI の **`busy`(status=3)** が **RISC-V DMI のセマンティクスに元から入っている = payload 側にある** |

### これが §3 末尾の「turnaround が文書化されていない」を説明する

§3 は「**明示の turnaround bit は文書化されていない(位相の並びで暗黙に切替)**」と記録した。理由は上表の 3 行目:

- **SWD が turnaround を明示的に持つのは、ACK をフレーム途中で読むから**(方向が 2 回変わる)。
- **RVSWD はフレーム途中で何も判断しない**(全部押し込んでから全部読む)。**方向が変わる境界は host 位相 → target 位相の 1 箇所だけ**。→ **仕様に書く turnaround bit が存在しない。**

⚠ ただし**その 1 箇所の電気的な振る舞い**(どちらがいつ線を放すか)は**依然未測定**。§3 末尾の「STOP 条件の波形詳細」と同じ枠に残る。

### pin 名だけ借りている

`SWDIO` / `SWCLK` という名前とコネクタ配置は ARM から借りている。**基板設計者とプローブのヘッダが既にその形を知っているので便利**で、実際 WCH-Link の 4 ピンは SWD ヘッダに見える。→ **§1 の警告(pin 名が似るが非互換)はこのため。名前が一番の罠。**

### 実装上の帰結(自作 probe に効く)

| | |
|---|---|
| **bit-bang しやすい** | フレーム途中に turnaround のタイミングハザードが無い |
| **効率は悪い** | nop でも常に 42 + 42 bit 流れる。SWD は 8 bit の request で「読むだけ」が済む |
| **PIO / PIOC 向き** | 「N bit 押し込んで N bit 読む」だけなので状態機械が小さい。**CH32X035 の PIOC が「2 ピンのプロトコル制御」用に作られている**のと噛み合う(→ [../references/harness-board-survey.ja.md](../references/harness-board-survey.ja.md) §2.1) |

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
| [sigrok-rvswd](https://github.com/perigoso/sigrok-rvswd) | PulseView/sigrok decoder | 52-bit short / 84-bit longの両packetを実装。今回のX035通常packet境界と一致（bulk burstは未対応） |
| [esp32-component-rvswd](https://github.com/Nicolai-Electronics/esp32-component-rvswd) | ESP32 | CH32V203で検証された52-clock実装。data/parity境界は実測と一致するが固定control値はLinkE/X035と相違 |
| [RVSWD_pico](https://github.com/ImproperCatGirl/RVSWD_pico) | RP2040 PIO | 上記ESP32実装を参照したQingKe V4向けprobe。52-clock構造と固定control値を継承 |
| [SaleaeRVSWDAnalyzer](https://github.com/bmx/SaleaeRVSWDAnalyzer) | Saleae decoder | 52/84-bit両形式を独立実装。shortの主要field境界が実測と一致 |
| [ch32-tapioca-probe](https://github.com/pierrejay/ch32-tapioca-probe) | CH32X035 PIOC | LinkE→V307 captureで52-bitを復号し、X035/V203/V307で実機試験。turnaround/statusまで文書化 |
| [pico-rvswd](https://github.com/i-infra/pico-rvswd) | RP2350 PIO | X035で52-bit frameを実機試験。400 kHz–4 MHzでparity error 0を報告 |
| [rvswdog](https://github.com/coocoscoocos/rvswdog) | STM8 | ESP32実装と同じ52-clock bit-bang。target検証範囲はREADME上不明 |

## 5. 未解読 / 要調査

2026-09-11 に WCH-LinkE ↔ V003/X035 の線上波形を50 MHzで収録し、同時間帯のUSB DMI captureと既知4 KiB payloadを保存した。生データ・再現script・詳細解析は[captures/fixtures/wire-flash-v003-x035-2026-09-11/](../captures/fixtures/wire-flash-v003-x035-2026-09-11/README.ja.md)。V003の41/33-pulse frame、X035の52-bit short packetと585-edge/15-word burstは4096 byte全体で検算した。shortのparity/status/turnaroundは既存実装との照合でも支持された。

- **SWIO 1 線**の実 target が受理する 0/1 pulse 幅の限界値、pull-up/open-drain の電気条件、33-pulse fast-read response 先頭 bit の意味。LinkE 2.22 の通常 41-pulse frame と出力 pulse 幅は上記 capture で実測済み。
- RVSWD bulk burstを選ぶcommand条件、termination clockの役割、long/short選択規則、トランザクション間アイドル規則。LinkE 2.22 + X035の52/585構造と位相ごとのclock周期は実測済み。
- 1/2 線切替 target の判定と entry シーケンス(debug mode 突入の初期化)。
- `status=2/3`（fail/busy）を意図的に発生させ、shortとbulkの再試行動作を実測する。

2026-09-25 に WCH-LinkE ↔ L103/V203(RVSWD)/V003(SWIO)を ESP32-P4 で収録した。12 操作に加え、速度・DMI 単発・誤り・option byte・読出し保護・clock・monitor・反復・RedetectChip を収めてある([fixture](../captures/fixtures/wire-linke-p4-2026-09-25/README.ja.md))。上の未解決点のうち、ここで進んだもの:

- **bulk burst の条件(L103)**: `abstractauto = 1` → `data1 = 番地` → `command = 0x02280000`(memory read、postincrement)の直後に 585 clock の burst(15 word)が出て、続く通常の data0 read 1 回で 16 word = 64 byte になる。read_flash_4k で 64 回、read_ram_256 で 4 回。burst 先頭の 14 bit は short packet の先頭と同じ並び(addr `0x04`、read、parity、park、padding 4)で、padding には `0000` / `0100` / `0101` が出た。
- **long/short**: 接続時に long 形式の DMSTATUS 問い合わせが 202 回出る(§3)。
- **接続の DMI 列(3 target 共通の部分)**: long 形式の問い合わせの後、次の順で進む。
  1. 非標準の DMI 番地 `0x7e` と `0x7d` に `0x5aa50400` を書く(V003 では続けて `0x7c` = `0x00010403`、`0x7d` = `0x5aa50401` を読む)。
  2. DMSTATUS を読み、DMCONTROL = `0x80000001`(haltreq)を 2 回書く。**接続中 hart は halt している**。
  3. `0x7f` を読む。chip ID が返る(L103 `0x10310710`、V003 `0x00300500`)。
  4. CSR `0x7C0` に `0x300` を書く。
  5. clock の組み直し(L103/V203 のみ。[pc-to-link](pc-to-link.ja.md) §11)と ESIG の読出し。
  6. 最後に DMCONTROL = `0x40000001`(resumereq)→ `0x40000000`。
  - 0x7c〜0x7f は RISC-V Debug 仕様では未定義で、WCH 独自と見られる。意味は分からない。
- **SDI monitor**(L103、`more/l103/monitor_sdi`): 「49〜52 clock の未知の frame」は、25 MHz 収録で clock を落とした **DMDATA0 の read** だった。LinkE は DMDATA0 を約 29 µs ごとに読み続ける。0 以外(低 byte = 文字数、上位 3 byte = 文字)なら、文字数が 4 以上のとき DMDATA1 も読み、DMDATA0 に 0 を書いて受領を返す。[serial-and-print](serial-and-print.ja.md) §3 の郵便受け方式が線上でもそのまま見える。
- **V003 SWIO**: 41 パルス(start + addr7 + R/W + data32)と 33 パルス(fast-read)だけで、パルス幅は 1 = 約 260 ns、0 = 約 860 ns(09-11 と同じ)。
  - メモリの読出しは abstract memory access ではなく、program buffer 8 語に置いた routine を command `0x00040000`(postexec のみ)で実行する方式。番地は data1、値は data0 で受け渡す。
  - **V003 の接続では RCC に触れない**。
- **V203 の高速区間**: 100 MHz 収録を hold filter なしで読み、START/STOP の判定だけ 3 sample の filter を使うと、99.7 % が parity 一致で区切れる。160 MHz 収録では全 frame が既知の長さになる。
- 未解読のまま: 1 個目の long frame(op `00`、data `0x19`)の目的、非標準番地 0x7c〜0x7f の意味、V003 で接続前に 1 個出る 33 パルスの frame。

## 6. 調査の入口

1. 単独 `DmiOp` ごとに GPIO marker を併記して LinkE ↔ target を収録し、USB request と線上の変換・再試行・内部 polling を 1:1 で時刻対応させる。
2. SWIO(1 線)は PicoRVD PIO / cnlohr minichlink の bit-bang を読み、pulse 幅規則を抽出。
3. RINS の論理層記述と fxsheep 解析で裏を取り、status を上げる。

## 参照

- 第三者 probe の全体像(host protocol 方式・probe MCU 比較): [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md)
- 運ぶ中身: [riscv-debug-module.ja.md](riscv-debug-module.ja.md) / それを USB に載せる殻: [pc-to-link.ja.md](pc-to-link.ja.md)
