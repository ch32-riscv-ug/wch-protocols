<a id="link-to-target"></a>

# WCH-Link ↔ target(SWIO 1 線 / RVSWD 2 線)

status([語彙](../README.md#status)): **V003 の SWIO は ESP32 実装で end-to-end `verified`、RVSWD の主要な frame は LinkE fw 2.22 の線で `verified`。電気的な許容限界は `todo`**。層は L1 物理 + L2 線上 DMI。WCH 自身は QingKe core の debug を「1-wire / 2-wire DTM」と説明する。

PC 側からは [pc-to-link.ja.md](pc-to-link.ja.md) の `DmiOp` を送るだけで、線上の toggling は WCH-Link firmware が担う。**PC ツールを作るだけならこの層は不要**(DMI 抽象で足りる)。この層が要るのは、WCH-Link 無しで直結する自作 probe(minichlink 系)を作る場合。運ぶ中身は RISC-V の **DMI トランザクション**([riscv-debug-module.ja.md](riscv-debug-module.ja.md))。

<a id="physical-forms"></a>

## 1. 2 つの物理形態

| 名称 | 信号 | 主な target(core) | 備考 |
|---|---|---|---|
| **1 線 SWIO/SDI** | data 1 本(要 pull-up) | CH32V003・CH641(V2A)、CH32V00X・M007(V2C) | **パルス幅で 0/1 を表す**。外部 pull-up を要する自作例が多い。ARM SWD とは無関係 |
| **2 線 RVSWD** | data + clock(SWDIO/SWCLK) | CH32V103(V3)、CH32V20x/V30x/V317・X03x・L103・CH643(V4)ほか | WCH 固有の RISC-V debug transport。**pin 名が ARM SWD に似るが protocol は非互換** |
| 1/2 線 切替可 | option/config で変わる | V00X・M007、M030、CH564、CH584/585、CH570/572 等 | family 条件で interface が変わる |

- 「CMSIS-DAP 対応」「ARM SWD 対応」は **CH32 RISC-V の 2 線 RVSWD 対応を意味しない**(別 protocol)。**なぜ別 protocol になったのかは §3b**。
- core 世代と線の対応: V2A/V2C = 1 線、V3/V4 = 2 線([serial-and-print.ja.md](serial-and-print.ja.md) の core 表と一致)。

<a id="not-in-usb"></a>

## 2. USB protocol 層には現れない

attach/DMI/flash の WCH-Link コマンドは 1 線/2 線で**同一**。配線差は WCH-LinkE firmware が吸収する。ただし:

- 1 線 target は **LinkE/LinkW のみ**(旧 CH549 Link は不可)。
- 実運用の非対称は [pc-to-link.ja.md](pc-to-link.ja.md) の family パラメータ(stub・data packet・write pack)に出る。

<a id="rvswd-frames"></a>

## 3. RVSWD 2 線の線上フレーム(具体)

status: 形式ごとに違う。**52-bit short 形式**は WCH-LinkE fw 2.22 + X035 / L103 / V203 の線と、複数の実機動作実装で `verified`。**84-bit long 形式**は CH549 Link fw 2.12 → V103 の線と LinkE の接続時の問い合わせで `verified`(target が答えるのは CH549 → V103 だけ)。**bulk burst**(15 + 38N clock)は LinkE 2.22 + X035 / L103 で構造を `verified`。根拠: [2026-09-11 V003/X035](../captures/fixtures/wire-flash-v003-x035-2026-09-11/README.ja.md) / [2026-09-25 L103/V203/V003](../captures/fixtures/wire-linke-p4-2026-09-25/README.ja.md) / [E165](../experiments/e165_linke_x035_v103_wire/README.ja.md)。

**要点: RVSWD は DMI の address / data / operation / status を運ぶが、線上形式は一つではない。** USB の `DmiOp`(cmd `0x08`)1 件は線上の DMI 1 frame になり、LinkE はその間に自分のアクセスを挟まない(LinkE 2.22 + L103)。一方、どの形式(short / long / burst)で送るか、AttachChip・flash・高速 read の中でどんな DMI 列を流すかは LinkE の firmware が決める。USB の byte 列がそのまま線に出る透過ブリッジではない。

<a id="rvswd-signals"></a>

### 信号とアイドル

- 2 線: **SWDIO(data)/ SWCLK(clock)**。無トランザクション時のレベルは **target で違う**。WCH-LinkE fw 2.22 は、X035 では**両方 HIGH**、L103 / V203 では **SWCLK LOW・SWDIO HIGH** で休ませる([E171](../experiments/e171_linke_rvswd_idle_level/README.ja.md))。L103 は両方 HIGH で約 1 ms 置くと link を失う。X035 は frame の間を SWCLK LOW で休ませると接続できない(oep-probe-arduino の実測)。
- pin 名は ARM SWD に似るが**別 protocol**(RISC-V Debug 0.13、designer=WCH)。

<a id="rvswd-start"></a>

### start / reset

- RISC-V mode で起動時、SWDIO/SWCLK を HIGH にし、**IO を HIGH のまま 100 クロック(100 個の 1)を送り、STOP 条件**を出す(初期化。`attested`)。

<a id="rvswd-bit-timing"></a>

### bit の駆動と sample

- **clock が HIGH の間に bit を sample**(SWDIO=HIGH → 1、LOW → 0)。
- **data は clock が LOW の間だけ変化**させる。
- **全フィールド MSB first**。

<a id="frame-delimiting"></a>

### frame の区切り方(実装・解析向け、LinkE 2.22 実測)

clock の空き時間ではなく、START / STOP で区切る。

- START: SWCLK が high(アイドル)の間に SWDIO が立ち下がる。高速区間では立ち下がりの約 160 ns 前、低速区間では high の中ほどに来る。
- STOP: SWCLK が high の間に SWDIO が立ち上がり、その high が**その前の 16 回の high の中央値の 1.5 倍より長い**。high の長さは、LinkE の STOP が中央値の 3.3〜3.75 倍、OEP の short 形式の STOP が約 2 倍、target が high の間に線を動かす所が 1.2 倍までだった。決まった長さ(2 µs など)で切ると、速い LinkE の STOP の high(1.84〜2.0 µs)を取りこぼし、2 つの frame が 1 つ(170 clock)にまとまる([E173](../experiments/e173_oep_tap_linke_v103_long/README.ja.md)、wireskein ee37142)。
- read 中は **target も SWCLK の立ち上がりから 0.3 µs 以内に、high の間に SWDIO を変える**。形の上では START/STOP と同じなので、「frame の外の立下りだけを START とする」「後に長い high が続く立上りだけを STOP とする」という状態の区別が要る。
- 空き時間で区切ると、低速設定の burst が 46 + 14×(3+35) + 7 のように割れ、前後の clock を 1 つ取り込んで read が 54 clock に見える(fixture 付属の `dmi_decode.py` はこの方式で、一部の write を R と表示し、frame を取りこぼす。値は START/STOP 方式と一致する)。`captures/tools/rvswd.py` は START/STOP 方式で、STOP を決まった長さ(high 1.6 µs 以上)で切る。fixture の L103 / X035 の収録はこれで切れる。

<a id="short-frame"></a>

### 52-bit short 形式(LinkE の通常の DMI)

`addr7 + R/W1 + parity1 + park/padding5 + data32 + parity1 + tail5` の 52 bit の後に、終端の 1 clock と STOP が続く(**read も write も 53 clock**)。

- header(addr + R/W)と data にそれぞれ偶数 parity が付く。X035 の 8,846 個、L103 の全 capture の 23,447 個と繰返し 5 回分で、長さはすべて 53 clock、parity はすべて一致した。
- **park / padding(bit 9–13)と末尾(bit 47–52)は don't-care**。LinkE は一定値に固定せず run ごとに変わる(53 clock の frame に write で 32 通り、read で 17〜22 通り)。既存の probe が使う `10101` / `10111` とも違うが、X035 はその固定値でも動く(ESP32-P4 → X035 の実機試験)。X035 で見えた「bit 9 は常に bit 8(parity)と同値」は L103 では成り立たない。
- **short frame に status は無い**。bit 48–49 を target status と読んではいけない。USB `DmiOp` 応答の status は short frame の値と対応しない。
- 同じ操作を 5 回繰り返すと、frame の並びと値は bit 単位で一致する(違いは RAM 上で動いている counter の 1 word だけ)。

<a id="burst-frame"></a>

### bulk burst(高速 read)

- 長さは **15 + 38×N clock**(N word。15 word = 585、7 word = 281、3 word = 129 を観測)。
- 条件(L103): `abstractauto = 1` → `data1 = 番地` → `command = 0x02280000`(memory read、postincrement)の直後に 585 clock の burst(15 word)が出て、続く通常の data0 read 1 回で 16 word = 64 byte になる。read_flash_4k で 64 回、read_ram_256 で 4 回。
- burst 先頭の 14 bit は short packet の先頭と同じ並び(addr `0x04`、read、parity、park、padding 4)で、padding には `0000` / `0100` / `0101` が出た。

<a id="long-frame"></a>

### 84-bit long 形式

| 位相 | 送信側 | bit 数 | 内容 |
|---|---|---:|---|
| Address | host | **7** | DMI レジスタ番地 |
| Data | host | **32** | 書込データ(read 時は don't-care) |
| Operation | host | **2** | op(RISC-V DTM: 0 nop / 1 read / 2 write) |
| bit 41 | host | **1** | 資料は「Address+Data+Operation の odd parity」と書く。実測は下 |
| Address | target | **7** | エコー |
| Data | target | **32** | 読出データ |
| Status | target | **2** | status(0 success / 2 failed / 3 busy) |
| bit 83 | **host** | **1** | 資料は「Address+Data+Status の even parity」と書き、target の位相に置く。実測は host が出す(下) |

- host 位相(7+32+2+1)→ target 位相(7+32+2)→ host の bit 83 と続き、最後に終端の 1 clock(`0`)。START + 85 clock + STOP。明示の turnaround bit は無い(位相の並びで切り替わる)。
- **CH549 WCH-Link fw 2.12 → V103 は、すべての DMI をこの形で送る**([E165](../experiments/e165_linke_x035_v103_wire/README.ja.md))。target 位相の番地は host の番地をそのまま返し、write では data も返し、read では読んだ値が載る。status は `00`。**bit 41 と bit 83 は parity として働いていない**: bit 41 は常に `0`、bit 83 は write で `0`、read で `1` だった。
- **bit 83 は host が出す**(LinkE 2.22 → V103 も write で `0`、read で `1`)。線を放して pull-up の `1` のままにすると、V103 は write を受けない(DMCONTROL の write に status 1、PROGBUF0 は書いても 0 のまま)。host が write で `0`、read で `1` を出すと attach できた(OEP の probe、[E173](../experiments/e173_oep_tap_linke_v103_long/README.ja.md))。
- **LinkE 2.22 → V103 も、すべての DMI をこの形で送る**([E173](../experiments/e173_oep_tap_linke_v103_long/README.ja.md)、85 clock。最初の 1 個だけ 80 clock で、下の問い合わせと見ている)。
- **動いている V103 は、short 形式の frame を受けるか、線を放されるか、frame の途中で線が切れると、以降の frame に status 2 を返し続け、電源を切るまで戻らない**([E173](../experiments/e173_oep_tap_linke_v103_long/README.ja.md)、dmactive の立った V103。別の CH32V103R8T6 でも同じ)。V103 に short 形式を送らず、閉じた後も線を駆動したまま保つ。
- **E173 では、V103 は resumereq の立った DMCONTROL の write とその次の DMSTATUS の read で答えが遅れた**([E173](../experiments/e173_oep_tap_linke_v103_long/README.ja.md) の 11〜21 を読み直した)。LinkE の DetachChip の並び(DMCONTROL ← `0x40000001`、DMSTATUS の read、DMCONTROL ← `0x40000000`)の初めの 2 個で、target が SWDIO を変える点が、SWCLK の立ち上がりから周期の約 0.8(ほかの frame)から 1.05〜1.3 に延び、次の立ち上がりを越える。そのため medium / high(12〜19)では target 位相が 1 clock 遅れて読める(0x10 が 0x08 に見え、status 2 に見える frame もある)。low(11、21)は周期が長く間に合う。3 個目の `0x40000000` では元に戻る。LinkE は DetachChip の答えを使わず、操作は rc 0 で終わる。**同じ V103 と LinkE で、つなぎ直したタップで取り直すと再現しなかった**(target info high 2 回、option get、ch32rv の resume を high / medium で 3 回。どの frame も周期の 1.0 以下で答え、ずれずに読める)。E173 のときの配線などの条件によると見ている。ch32rv の resume(`0x40000001` → DMSTATUS → `0x00000001`)は LinkE 2.22 経由で high / medium / low の各 15 回が通った。
- **LinkE 2.22 は接続時(AttachChip、約 475 kHz の低速区間)の先頭でこの形の frame を 202 個送る**(L103 / V203 / V003、5 回の接続で 1005/1005 が同一。[E173](../experiments/e173_oep_tap_linke_v103_long/README.ja.md) の V203 の target info でも 202 回の後に short 形式へ移った):

  | 部分 | 1 個目 | 2〜202 個目 |
  |---|---|---|
  | host 位相 | addr `0x11`、data `0x19`、op `00`、bit 41 `0` | addr `0x11`(DMSTATUS)、data `0`、op `01`(read)、bit 41 `1` |
  | target 位相 | addr `0010010`、data `0xffffffff`、status `11`、bit 83 `1` | addr `1111101`、data `0xffffffff`、status `11`、bit 83 `1` |
  | 終端 clock | `0` | `0` |

  - target 位相は data が全部 1 で、線が pull-up のまま(誰も駆動していない)と読める。LinkE はまず long 形式で DMSTATUS を問い合わせ、応答が無いので short 形式へ移る、という解釈(推定)。
  - 2〜202 個目の host の bit 41 は addr+data+op と合わせて**偶数**になる(資料の odd と合わない)。
  - **1 線の V003 でも同じ問い合わせが SWIO の pin に出る**。V003 の接続時には LOW 4.0 / 8.3 / 9.3 / 102 µs の組が 202 回並び、これは L103 の long frame 中の SWDIO の LOW 区間(4.1 / 8.3 / 9.3 / 102.3 µs)と同じ。続けて 0x7e/0x7d 書込みの short frame が数回出て、約 2 ms の LOW の後に SWIO の通信が始まる。LinkE は target の線の種類を知らずに、まず 2 線の形で問い合わせていると読める。
- これは [riscv-debug-module.ja.md](riscv-debug-module.ja.md) の DMI トランザクションと 1:1(op/status のコード、addr=DMDATA0=`0x04`/DMCONTROL=`0x10` 等がそのまま線上の 7bit addr に乗る)。
- 解析: `captures/tools/rvswd.py` / `swio.py` / `linke_repeat_compare.py`(実行方法は [captures/README](../captures/README.ja.md)「解析環境(uv)」)。

<a id="swio-relation"></a>

### SWIO 1 線との関係(transaction は同じ、bit 符号化だけ違う)

- **SWIO も運ぶ中身は同じ DMI トランザクション**。minichlink の programmer 抽象は 1 線/2 線とも `WriteReg32(reg_7bit, u32)` / `ReadReg32(reg_7bit, *u32)` = **7bit reg + 32bit data**。
- 違いは**物理だけ**: RVSWD は clock 線で bit を刻む。SWIO は clock 線が無く、**1 本の line を host が LOW に引くパルスの幅で 0/1 を符号化**する(line は pull-up で HIGH がアイドル)。LinkE 2.22 は 41 パルス(start + addr7 + R/W + data32)と 33 パルス(fast-read)を使い、1 = 約 260 ns、0 = 約 860 ns。
- **既知の pulse 幅の実装は V003 実機で動作確認済み**。classic ESP32 GPIO16 から attach、DMI read/write、halt、RAM 実行、user flash 全 82 page 書込み、user code 実行、bootloader 再 entry まで成立した([E123〜E131](../experiments/LEDGER.ja.md))。0/1 pulse 幅の**受理限界**と電気条件の限界は未確定。

<a id="rvswd-open"></a>

### まだ不明

- (RVSWD)STOP 条件の波形の詳細、トランザクション間のアイドル規則、host 位相 → target 位相の切り替わりの電気的な振る舞い(どちらがいつ線を放すか)。
- (RVSWD)long と short を選ぶ規則。LinkE は long 形式の問い合わせに V103 が答えればそのまま long 形式を使い、答えなければ short 形式に移ると見ている([E173](../experiments/e173_oep_tap_linke_v103_long/README.ja.md))が、判定の条件は確かめていない。
- (RVSWD)7bit addr が RISC-V 標準 DTM(abits 可変)とどう対応するか。WCH の線上は 7 bit と観測。Swindle は `dmi->address_width = 8U` と宣言し、probe 側の responder は `address as u8` で受けている(`blackmagic_addon/hosted/remote_rv_protocol.c` / `rs_swindle/src/native/rpc_target/mod.rs`)ので、8 bit で上位が未使用なのか、Swindle が余裕を取っているのかは分からない。
- (SWIO)LOW パルス幅の 0/1 の**許容閾値**、pull-up/open-drain 条件の限界、33 パルスの fast-read 応答の先頭 bit の意味。

<a id="why-not-swd"></a>

## 3b. なぜ ARM SWD と別 protocol なのか

状態: **解説**(一次資料は ARM ADI/SWD 仕様と RISC-V Debug Spec = どちらも公開標準。RVSWD 側は §3 の観測。**「JTAG scan の時間多重」という読みは本書の解釈**)。

**動機は同じ(ピン数削減)だが、載せている上位アーキテクチャが違う。**

<a id="what-is-serialized"></a>

### 何をシリアライズしているか

| | **ARM SWD** | **RVSWD** |
|---|---|---|
| 上位 | **ADIv5 の DP/AP レジスタ空間**(バス) | **RISC-V DTM の `dmi` レジスタ**(1 本) |
| 運ぶもの | 「どの AP の、どのレジスタを read/write」 | **`(addr, data32, op)` の 1 組**(= §3) |
| 元の姿 | **SWD 自体が ADI の正式な 2 線 transport** | **JTAG の `dmi` スキャン** |
| framing の性格 | **packet protocol**(アドレス・応答・データが framing の構造) | **shift protocol**(framing に構造は無く、アドレスは payload) |

- **SWD のフレーム**: 8 bit の packet request(Start / APnDP / RnW / A[3:2] / Parity / Stop / Park)→ turnaround → **3 bit ACK**(OK/WAIT/FAULT)→ turnaround → data 32 + parity。
- **RVSWD のフレーム**(§3 の long 形式): `addr7 + data32 + op2 + 1` を送り、続けて `addr7 + data32 + status2 + 1` を受ける。これは **RISC-V 標準 DTM の `dmi` DR(`abits + 32 + 2`)そのもの**。LinkE が通常使う short 形式(52 bit)は、R/W 1 bit と parity で同じ中身を半分の長さに詰めたもの。

> **読み**: JTAG は TDI と TDO が別線なので押し込みと吐き出しが同時に起きる。**RVSWD は線が 1 本なので、それを前後に時間分割しただけ**。つまり **SWD は「ADI のバスアクセスを 2 線に詰めた packet protocol」、RVSWD は「JTAG の dmi スキャンを 2 線に時間多重した shift protocol」**。

<a id="why-wch-invented"></a>

### なぜ WCH が自分で作る必要があったか

**RISC-V Debug Spec は DM と DMI の"インタフェース"は標準化したが、DTM(transport)は JTAG しか具体的に規定していない。** 「2 線でやりたい」なら**各ベンダが発明するしかない**。ARM は SWD を ADI の一部として自分で規定したので全社共通 — **ここが非対称**。

ピン数が動機なのは共通で、**ARM は 4→2(SWD)、WCH は 4→2(RVSWD)と 4→1(SWIO)**で解いた。8〜20 ピンの chip に JTAG の 4〜5 本は重い。

<a id="swd-requirements"></a>

### SWD が背負っている要件は RVSWD には要らない

| SWD が持つもの | なぜ要るか | RVSWD |
|---|---|---|
| **multi-drop**(SWD v2 の `TARGETSEL`) | 1 バスに複数 target | **不要** |
| **JTAG↔SWD 切替シーケンス**(`0xE79E`) | 既存 JTAG との後方互換・両対応パッド | **不要**(§3 の初期化は「IO HIGH で 100 クロック + STOP」だけ) |
| **3 状態 ACK**(OK / WAIT / FAULT) | AP の先が AHB/APB ブリッジで stall しうるので、**待ちを framing に持つ**必要がある | **不要。** DMI の **`busy`(status=3)** が **RISC-V DMI のセマンティクスに元から入っている = payload 側にある** |

<a id="turnaround"></a>

### これが §3 末尾の「turnaround が文書化されていない」を説明する

§3 の long 形式には**明示の turnaround bit が無い(位相の並びで切り替わる)**。理由は上表の 3 行目:

- **SWD が turnaround を明示的に持つのは、ACK をフレーム途中で読むから**(方向が 2 回変わる)。
- **RVSWD はフレーム途中で何も判断しない**(全部押し込んでから全部読む)。**方向が変わる境界は host 位相 → target 位相の 1 箇所だけ**。→ **仕様に書く turnaround bit が存在しない。**

⚠ ただし**その 1 箇所の電気的な振る舞い**(どちらがいつ線を放すか)は**未測定**(§3「まだ不明」)。

<a id="pin-names"></a>

### pin 名だけ借りている

`SWDIO` / `SWCLK` という名前とコネクタ配置は ARM から借りている。**基板設計者とプローブのヘッダが既にその形を知っているので便利**で、実際 WCH-Link の 4 ピンは SWD ヘッダに見える。→ **§1 の警告(pin 名が似るが非互換)はこのため。名前が一番の罠。**

<a id="probe-implications"></a>

### 実装上の帰結(自作 probe に効く)

| | |
|---|---|
| **bit-bang しやすい** | フレーム途中に turnaround のタイミングハザードが無い |
| **効率は悪い** | long 形式では nop でも 42 + 42 bit、short 形式でも 52 bit 流れる。SWD は 8 bit の request で「読むだけ」が済む |
| **PIO / PIOC 向き** | 「N bit 押し込んで N bit 読む」だけなので状態機械が小さい。**CH32X035 の PIOC が「2 ピンのプロトコル制御」用に作られている**のと噛み合う(→ [../references/harness-board-survey.ja.md](../references/harness-board-survey.ja.md) §2.1) |

<a id="third-party"></a>

## 4. 第三者実装(解読の一次資料)

WCH 公開仕様は薄いが、**動作を主張する第三者実装が複数あり**、線上を解読するならこれらが出発点。

<a id="third-party-swio"></a>

### 1 線 SWIO

| 実装 | probe | 参考価値 |
|---|---|---|
| [WCH 公式 CH32F103 1-Line 例](https://github.com/openwch/ch32v003/tree/main/CH32V003_1Line_Base_on_CH32F103) | CH32F103 | **一次資料**。移植の基準(完成 host 製品ではない) |
| [PicoRVD](https://github.com/aappleby/picorvd) | RP2040 PIO | **層分離が明快**(PIO 物理層 / RISC-V DM / V003 flash / SW breakpoint / GDB server)。読みやすい参照 |
| [ESP32-S2 funprog](https://github.com/cnlohr/esp32s2-cookbook/tree/master/ch32v003programmer) | ESP32-S2 bitbang | timing-sensitive な GPIO 操作 + critical section。現行 source は 1 線/2 線両方 |
| [rvswdio_programmer](https://github.com/cnlohr/rv003usb/tree/master/rvswdio_programmer) | CH32V003 | `opmode=1`=SWIO / `opmode=2`=RVSWD を自動判別 |

- SWIO は pulse 幅符号化。minichlink 系の bit-bang GPIO 実装が実際の timing の一次資料。

<a id="third-party-rvswd"></a>

### 2 線 RVSWD

| 実装 | probe | 参考価値 |
|---|---|---|
| [Swindle](https://github.com/mean00/swindle) | RP2040 | **RVSWD の protocol 図 `doc/rvswd.jpg`** が最も分かりやすい。RP2040 firmware が線を生成し host は BMP-remote 経由(`blackmagic_addon/hosted/remote_rv_protocol.c`)。target 層は `blackmagic_addon/target/CH32V3xx/`。V203/208/303/305/307。GPL-3.0 系。**現行 repo に `rvswd.pio` は無い**(§3 の bit 仕様は attic + fxsheep から) |
| [rvswdio_programmer](https://github.com/cnlohr/rv003usb/tree/master/rvswdio_programmer) | CH32V003 | RVSWD read/write、V003/00x/20x/30x/X03x 等を掲げる |
| ESP32-S2 funprog | ESP32-S2 | SWCLK pin・RVSWD read/write・family 検出あり(非 V003 の検証範囲は不明確) |
| [RINS](https://perigoso.github.io/rins/) | — | 第三者実装向けに **RVSWD の物理・論理層を文書化**(「SWD ではない」と明記) |
| [WCH RVSWD protocol 初期解析](https://github-wiki-see.page/m/fxsheep/openocd_wchlink-rv/wiki/WCH-RVSWD-protocol) | — | 早期リバース。RINS と整合 |
| [sigrok-rvswd](https://github.com/perigoso/sigrok-rvswd) | PulseView/sigrok decoder | 52-bit short / 84-bit long の両 packet を実装。X035 の short packet の境界と一致(bulk burst は未対応) |
| [esp32-component-rvswd](https://github.com/Nicolai-Electronics/esp32-component-rvswd) | ESP32 | CH32V203で検証された52-clock実装。data/parity境界は実測と一致するが固定control値はLinkE/X035と相違 |
| [RVSWD_pico](https://github.com/ImproperCatGirl/RVSWD_pico) | RP2040 PIO | 上記ESP32実装を参照したQingKe V4向けprobe。52-clock構造と固定control値を継承 |
| [SaleaeRVSWDAnalyzer](https://github.com/bmx/SaleaeRVSWDAnalyzer) | Saleae decoder | 52/84-bit両形式を独立実装。shortの主要field境界が実測と一致 |
| [ch32-tapioca-probe](https://github.com/pierrejay/ch32-tapioca-probe) | CH32X035 PIOC | LinkE→V307 captureで52-bitを復号し、X035/V203/V307で実機試験。turnaround/statusまで文書化 |
| [pico-rvswd](https://github.com/i-infra/pico-rvswd) | RP2350 PIO | X035で52-bit frameを実機試験。400 kHz–4 MHzでparity error 0を報告 |
| [rvswdog](https://github.com/coocoscoocos/rvswdog) | STM8 | ESP32実装と同じ52-clock bit-bang。target検証範囲はREADME上不明 |

<a id="linke-wire-behavior"></a>

## 5. WCH-LinkE の線上の動作(実測)

根拠: [2026-09-11 V003/X035](../captures/fixtures/wire-flash-v003-x035-2026-09-11/README.ja.md)(LA2016、50 MHz、USB の DMI capture と既知の 4 KiB payload つき)、[2026-09-25 L103/V203/V003](../captures/fixtures/wire-linke-p4-2026-09-25/README.ja.md)(ESP32-P4、12 操作 + 速度・DMI 単発・誤り・option byte・読出し保護・clock・monitor・反復・RedetectChip)。範囲は WCH-LinkE fw 2.22。CH549 Link の線は V103 の long 形式(§3)だけ。

<a id="attach-sequence"></a>

### 接続(AttachChip)の DMI 列

3 target(L103 / V203 / V003)と X035 で共通。long 形式の問い合わせ(§3)の後、次の順で進む。

1. 非標準の DMI 番地 `0x7e` と `0x7d` に `0x5aa50400` を書く(V003 では続けて `0x7c` = `0x00010403`、`0x7d` = `0x5aa50401` を読む)。
2. DMSTATUS を読み、DMCONTROL = `0x80000001`(haltreq)を 2 回書く。**接続中 hart は halt している**。
3. `0x7f` を読む。chip ID が返る(L103 `0x10310710`、X035 `0x03510601`、V003 `0x00300500`)。
4. CSR `0x7C0` に `0x300` を書く。
5. clock の組み直し(L103 / V203 / X035。[pc-to-link](pc-to-link.ja.md) §11)と ESIG(`0x1FFFF7E0` / `E8` / `EC` / `F0`)の読出し。
6. 最後に resumereq。DM は reset しない。この後 DATA0 には最後に読んだ ESIG の語が残る(V203 `0xe339e339`、L103 / V003 / X035 `0xffffffff`)。

- 0x7c〜0x7f は RISC-V Debug 仕様では未定義で、QingKe V2 manual の debug module register の表(Table 6-1)にも無い。minichlink(ch32fun `91032ac`、`minichlink/minichlink.h`)は `DMCPBR` 0x7C / `DMCFGR` 0x7D / `DMSHDWCFGR` 0x7E / `DMCHIPID` 0x7F と名付け、LinkE と同じ `0x5aa50000 | (1<<10)` を DMSHDWCFGR と DMCFGR に書いている(bit 10 に「Allow output from slave」と注記)。0x7F を chip ID として読む点も同じ。上位 16 bit の `0x5aa5` は書込みの鍵に見える。WCH の一次資料での定義は見つかっていない。
- **V003 の接続では RCC に触れない**。メモリの読出しは abstract memory access ではなく、program buffer 8 語に置いた routine を command `0x00040000`(postexec のみ)で実行する方式。番地は data1、値は data0 で受け渡す。
- **V003 の接続前の 32 パルス**: 接続の最初に、約 900 ns の LOW(SWIO の「0」の幅)が約 1.1 µs 間隔で 32 個並ぶ(20 ns のひげが 1 個混ざることがある)。同じ時刻に L103 では short frame が 3 個(dmcontrol ← `0x40000001`、dmstatus の読出し、dmcontrol ← `0x40000000`)出るが、LOW 区間の並びは一致しない。意味は分からない。

<a id="detach-sequence"></a>

### DetachChip の DMI 列

4 系統(L103 / V203 / V003 / X035)とも `W dmcontrol 0x40000001`(resumereq + dmactive)→ `R dmstatus` → `W dmcontrol 0x40000000`(resumereq、dmactive = 0)。

- dmactive = 0 は debug module を reset する(RISC-V Debug 仕様)。X035 では、これで DATA0 が 0 に戻り、target が出していた dmseq の frame が消える。接続を外すときに DMCONTROL = 0 を書く probe では、SerialDMSeq の console が 1〜10 s 戻らない(oep-probe-arduino で確認)。
- L103 / V203 で DATA0 が消えるかは未確認。LinkE 越しでは、次の AttachChip が chip ID の読出しで DATA0 を上書きするので、DetachChip の後の DATA0 は host から見えない。確かめるには、target の app が DATA0 を読んで報告する必要がある。

<a id="sdi-monitor"></a>

### SDI monitor

`verified`(WCH-LinkE 2.22 → CH32L103、`more/l103/monitor_sdi`、25 MHz 収録。復号の要約は [`monitor_sdi.dmi.txt`](../captures/fixtures/wire-linke-p4-2026-09-25/more/l103/monitor_sdi.dmi.txt)、作り方は [fixture の README](../captures/fixtures/wire-linke-p4-2026-09-25/README.ja.md))。LinkE は接続の列の後、DMDATA0 を約 29.5 µs ごとに読み続ける(38 s で 1,279,392 回、間隔の 1〜99 % 点は 28.8〜30.4 µs)。0 以外(低 byte = 文字数、上位 3 byte = 文字)を読むと、**文字数に関係なく** DMDATA1 を読み(約 30 µs 後)、DMDATA0 に 0 を書いて受領を返す(約 62 µs 後)。文字数 1 / 2 / 3 / 7 の 1,352 回すべてがこの順だった。[serial-and-print](serial-and-print.ja.md) §3 の郵便受け方式が線上でもそのまま見える。25 MHz の収録では poll の SWCLK の high が 3〜7 sample しかないので、bit clock に hold filter を掛けずに復号する(掛けると clock が落ちて 46〜52 clock に見える)。

<a id="speed"></a>

### 速度設定と clock

`0x0c` SetSpeed([pc-to-link](pc-to-link.ja.md) §4)。接続時の区間は設定に関係なく、long 3.04 µs・short 2.1 µs で一定。値は L103 と V203 で測った([E163](../experiments/e163_linke_speed_codes/README.ja.md)。解析は `captures/tools/linke_speed_table.py`)。

| 設定 | flash 書込み(Program 経路) | 単発の DMI・読出し・burst |
|---|---|---|
| high(`01`) | **L103 約 400 ns(2.5 MHz)、V203 約 100 ns(10 MHz)** | 約 1.12 µs(0.89 MHz) |
| medium(`02`) | 約 1.12 µs | 約 1.12 µs |
| low(`03`) | 約 2.12 µs(0.47 MHz) | 約 2.12 µs |

- 名目の 6 MHz / 4 MHz / 400 kHz には合わない。値は実質 `03`(と `ff`)とそれ以外の 2 種類で、それ以外の中で high だけが Program 経路を速くする(`00`・`04`〜`06` は単発の DMI では high と同じ。flash では未試験)。
- 接続の前に family `0x01` で 1 回送れば効く。接続の後に実の family で送り直しても効く(`03` → `0e:01` で 1.12 µs、`01` → `0e:03` で 2.12 µs)。
- high の Program 経路の速さは target の系統で違う(V203 では 4 KiB を書く区間が 51 ms)。X035 / V003 / V307 は未測定。V203 の高速区間は SWCLK の周期が 60〜100 ns(high 約 20〜30 ns)で、50 MHz の収録では記録しきれない。100 MHz では 99.7 % を parity 一致で復元でき、160 MHz なら既知の長さだけに区切れる。
- SWCLK は SetSpeed の設定で決まり、target の走行 clock に合わせて変えることはしない、と見ている(PLL から HSI へ落ちる reset でも DMI の reset-halt は化けない。V203 / V307 / V103 で 15/15)。走行 clock を変えて SWCLK を比べた直接の測定は無い。
- V003 の SWIO は 3 設定とも同じだった(1 = 約 260 ns、0 = 約 860 ns、パルス周期 約 1.1〜1.2 µs)。

<a id="link-open"></a>

### まだ不明

- **SWIO 1 線**の実 target が受理する 0/1 pulse 幅の限界値、pull-up/open-drain の電気条件、33-pulse fast-read response 先頭 bit の意味。
- RVSWD の bulk burst を選ぶコマンドの条件(L103 では上の abstractauto の列)、終端 clock の役割、long/short の選択規則、トランザクション間のアイドル規則。
- `status=2/3`(fail/busy)を意図的に起こしたときの short と burst の再試行の振る舞い。
- 1/2 線切替 target の判定と entry シーケンス(debug mode 突入の初期化)。
- 1 個目の long frame(op `00`、data `0x19`)の目的、0x7c〜0x7f の各 bit の意味、V003 の接続前の 32 パルス。

<a id="investigation"></a>

## 6. 調査の入口

1. 単独 `DmiOp` ごとに GPIO marker を併記して LinkE ↔ target を収録し、USB request と線上の変換・再試行・内部 polling を 1:1 で時刻対応させる。
2. SWIO(1 線)は PicoRVD PIO / cnlohr minichlink の bit-bang を読み、pulse 幅規則を抽出。
3. RINS の論理層記述と fxsheep 解析で裏を取り、status を上げる。

<a id="references"></a>

## 参照

- 第三者 probe の全体像(host protocol 方式・probe MCU 比較): [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md)
- 運ぶ中身: [riscv-debug-module.ja.md](riscv-debug-module.ja.md) / それを USB に載せる殻: [pc-to-link.ja.md](pc-to-link.ja.md)
