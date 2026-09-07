# harness を通る信号の分離 — 論理 IF と物理 IF

状態: **整理**(帯域は理論値と概算。実測は未)。
基準日: 2026-09-07

**問い**: harness を通る信号をどう分離するか。最小構成(UART / HID)では **1 本の物理 IF に複数の論理 IF が同居する**。そのとき **素の UART が使えなくなる / HID だと遅い**といった制限は何か。

## 1. 何を通すか — **probe ↔ PC の間だけを決める**

> **議題は probe ↔ PC。** target までの経路(1 線 SWIO / 2 線 RVSWD)は**この決定に持ち込まない**。持ち込むと **1 線のときだけの特殊処理が全部の話に混ざる**。
>
> **持ち込まなくてよい根拠**: **1 線と 2 線の差は PC 側に現れない**。[dmi-bridge §2](../protocols/dmi-bridge.ja.md) のとおり **attach / DMI / flash のコマンドは 1 線 / 2 線で同一**で、配線差は probe firmware が吸収する。**線層は probe 内部の実装詳細で、仕様には現れない**(同 §1)。→ **PC から見えるのは常に `(addr7, data32, op2)` の往復だけ。**

> **論理 IF の括り自体はまだ決まっていない。** 決まっているのは **dmi-bridge が実際に持っているコマンド**だけで、波形 / 相手役 / ADC は**未設計**。前版は未設計の部分を決まったように書いていた(訂正)。

### 1.1 対応表 — **PC 側でどう分離するか**

**分離の手段は 4 つ**: **同じ datagram 列**(`cmd` 番号 / `type` で分ける)/ **別 USB interface**(CDC / bulk)/ **別 IP port** / **HID の別 report ID**。

| 通すもの | PC から見た形 | 帯域 | **PC 側の分離** | 状況 |
|---|---|---|---|---|
| **DMI トランザクション + `batch`** | req/resp、1 往復 6〜9 B ×2 | 低 | 同じ datagram 列(`0x20`〜`0x22`) | **draft がこう置いている** |
| **probe 管理**(`hello` / `caps` / `info` / `ping` / `set_baud` / `reset_probe`) | req/resp | 低 | 同じ列(`0x01`〜`0x06`、`lane=0xFF`) | draft |
| **lane 操作**(`lane_attach` / `detach` / `line_reset`) | req/resp | 低 | 同じ列(`0x10`〜`0x12`) | draft |
| **power / NRST** | req/resp | 低 | 同じ列(`0x30` / `0x31`) | draft |
| **autopoll 設定 + hit** | req/resp + push | 低 | 同じ列(`0x60`/`0x61` + event `0x81`) | draft |
| **probe の log / lane 状態** | push | 低 | 同じ列(`type=0x02`、`0x82`/`0x83`) | draft |
| **target の serial(素の byte 列)** | **双方向の byte 流** | 中(115200 = 11.5 kB/s) | **未決定** — (a) **datagram に包む**(`0x50`〜`0x52` + `uart_data` `0x80`。**仕様にはこれがある**)/ (b) **別 CDC で素の serial として出す** / (c) (a) を host が pty に展開 | **未決定** |
| **波形サンプル** | push | **最大**(16ch で 0.5〜1 MB/s) | **未決定** | **未設計** |
| **意味イベント**(相手役が返した byte 列) | push、**時刻つき** | 低〜中 | **未決定** | **未設計** |
| **DMI 実行印**(この `tag` をこの時刻に実行した) | push、**時刻つき** | 低 | **未決定**(応答に付けると **[R3](probe-pattern-coexistence.ja.md) 違反**) | **未設計** |
| **ADC サンプル** | push、**時刻つき** | 中 | **未決定** | **未設計** |
| **模型の定義 / preload / readback** | req/resp、たまに大きい | 中 | **未決定** | **未設計** |
| **障害注入の設定** | req/resp | 低 | **未決定** | **未設計** |

→ **6 件は [dmi-bridge](../protocols/dmi-bridge.ja.md) の現行 draft が「同じ datagram 列」と置いている。7 件は未設計。**

⚠ **「現行 draft の選択」は決定ではない。** dmi-bridge の状態は **`draft`**(実装・実測は未)で、**分離の仕方は変えられる**。たとえば target serial を datagram に載せている(`0x50`〜`0x52`)のも 1 つの選択で、**別 CDC に移す判断は開いている**(§1.1b)。**本書は「いま draft がこう置いている」と「まだ置いていない」を区別するだけで、どれも固定していない。**

### 1.1b 素の byte 列(print / serial)を PC にどう持ってくるか

**PC 側で唯一 datagram に馴染まないもの。** 他は「コマンドと応答」だが、これは**そのまま流れる byte**。

#### 経路は 2 系統ある

| 系統 | 出どころ | 使える条件 |
|---|---|---|
| **(I) target の物理 USART** | probe の UART ピン ↔ target の USART | **target に空いている USART があり、probe に UART ピンがある** |
| **(II) DMI 経由の print** | **RTT / DMDATA / SDI**(DMI の read/write で運ぶ) | **USART が要らない**。**V003 のように USART が 1 本しかない chip でも成立** |

→ **(I) が使えないときの逃げ道が (II)**、というユーザの整理どおり。**逆も成り立つ**(USART を試験対象にしたいときは (II) に寄せる = `H-115`)。

#### PC 側での出し方は 3 通り × 2 系統

| 出し方 | (I) target USART | (II) DMI 経由 print |
|---|---|---|
| **a. datagram に包む** | **draft がこれ**(`uart_*` + `uart_data`) | **draft に無い**(host が DMI read で自分で取る形) |
| **b. probe が CDC に出す** | **可能**。素の serial になる | **可能。前例あり**(下記) |
| **c. host が pty / 仮想 serial に展開** | 可能(`H-114` の Pluggable Monitor) | **可能**。host は既に byte を持っている |

#### **「DMI 経由 print を probe が CDC に出せるか」→ 可能。LinkE が既にやっている**

**前例**: **WCH-LinkE の SDI virtual serial** — [serial-and-print.ja.md §4b](../protocols/serial-and-print.ja.md) が
「**SWIO/RVSWD → LinkE firmware → USB COM**」と記録している。**probe が DMDATA を polling して、自分の CDC に転送する。**
`SerialSDI` が LinkE 専用なのはこれが理由(= `P13` / `H-110`)。

**ただし「誰が知識を持つか」が論点になる**:

| 案 | probe が知る必要があるもの | [設計原則 1](../protocols/dmi-bridge.ja.md)(probe は chip を知らない)との関係 |
|---|---|---|
| **B-1. probe が全部やる**(LinkE 型) | **DMDATA の番地**(series で 3 系統)+ **print protocol の形式**(長さ byte。**2 方式ある**) | **原則 1 に反する。** 新 chip で番地が変わると firmware 更新 |
| **B-2. host が番地と形式を教え、probe が polling して CDC に流す** | **教えられた番地と形式だけ**(chip 名は知らない) | **原則 1 を守れる。** **`autopoll` の自然な拡張**(`autopoll_set(addr7, mask, expect, interval_us)` が既にある → 「hit したら CDC に流す」を足す) |
| **B-3. host が polling し、host 側で pty に出す** | **何も知らない** | **原則 1 を完全に守る。** ただし **host に常駐が要る** |

→ **B-2 が原則と両立する形**。ただし **「print protocol の形式を probe が解釈する」ことを許すか**は判断が要る(chip 知識ではないが、protocol 知識ではある)。

#### probe が CDC に出す / host が pty に出す の得失

| | **probe が CDC に出す**(b) | **host が pty に出す**(c) |
|---|---|---|
| **対応 OS** | **全 OS**(class driver) | **Linux / macOS のみ**。**Windows には serial の pty が無い**(→ 補足 K) |
| **host の常駐** | **不要**。Arduino IDE / `screen` が直接開ける | **要る** |
| **descriptor** | **composite が要る** → `bcdDevice` の major が変わる。**V003 では作れない**(low-speed に bulk が無い) | **変わらない**(PID / major 不変) |
| **probe 側の知識** | **要る**(B-1 なら chip 知識、B-2 なら形式知識) | **不要** |
| **probe 単体で動くか** | **動く**(PC のツールを選ばない) | host のツールに依存 |
| **落ちた byte の計上** | probe 側で数える | datagram 経由なら `dropped` が使える |

→ **どちらも成立し、両方を持つこともできる**(`caps` でどちらを提供するか申告)。**本書は決めない。**

### 1.1c serial 系のまとめ(**理解の確認と補足**)

**基本の理解はこれで合っている**:

| # | 理解 | 判定 |
|---|---|:--:|
| 1 | **CDC で欲しいときは、CDC が使える環境に限られる** | **○** |
| 2 | **CDC に出るものは probe 側が選択できる** | **○**(ただし「本数」と「中身」で決まる時点が違う → 補足 B) |
| 3 | 選択肢は「hw USART のみ / USART + RTT / 全部混合 / 複数 CDC にバラ」。**2 CDC でミックス + ミックスはやらない方がよい** | **○ 同意**(理由 → 補足 C) |
| 4 | **DMI 経由でも出せる。ch32rv では個別に取得できるが CDC でないので接続が面倒な場合がある。バラのみか混合を許すかは未決** | **○**(ch32rv は今**バラのみ** → 補足 D) |

#### 補足 A — 「CDC が使える環境」の中身

| transport | CDC を target serial に使えるか |
|---|---|
| **native USB(full-speed 以上)** | **○**(RP2040 / RP2350 / ESP32-S3 / X03x の hardware USB) |
| **V003 の software USB(low-speed)** | **✗**(bulk が無い) |
| **既存 USB-serial bridge 上の UART** | **✗ に近い。** **bridge 自体が唯一の serial port で、それを datagram が占有している**。2 本目が欲しければ **bridge を 2 個載せる**ことになる |
| **IP** | **✗**(CDC ではない)。ただし **port を分ければ同じ効果**が得られる |

→ **「CDC が使える環境」= native USB の composite を出せる silicon**。**V003 と、bridge 経由の board は入らない。**

#### 補足 B — 「本数」は build 時、「中身」は実行時

| | 決まる時点 | 理由 |
|---|---|---|
| **CDC の本数** | **build 時(固定)** | **descriptor に焼かれる**。増減すると `bcdDevice` の major が変わる([choices §2](harness-choices.ja.md)) |
| **各 CDC に何を流すか** | **実行時に選べる** | firmware の config + `caps` / `configure` |

→ **[「能力は build 時、役割は実行時」](probe-pattern-coexistence.ja.md)の serial 版。** **「本数を決める」と「中身を決める」を混ぜて考えると選択が爆発する**が、分ければ **本数は 1 回決めるだけ、中身は host が毎回選ぶ**で済む。

#### 補足 C — source は 5 種類あり、混合すると出どころが失われる

**source は 2 つではない**:

| source | 備考 |
|---|---|
| **target の物理 USART** | **複数ありうる**(USART1 / 2 / 3 …)。probe の UART ピン数で決まる |
| **RTT** | **RTT 自体が複数チャネルを持つ**(up / down 複数) |
| **DMDATA / SDI print** | 1 系統 |
| **semihosting の出力** | **これも print 経路**(write syscall) |
| **probe 自身の log** | 診断 |

**混合の代償**:

| 方式 | 出どころ | 既存ツール |
|---|---|---|
| **バラ**(1 source = 1 CDC) | **明確** | **そのまま使える** |
| **混合**(複数 source = 1 CDC) | **失われる**(どの byte がどこから来たか分からない) | そのまま使える |
| 混合 + prefix / タグ | 分かる | **素の serial ではなくなる**(人間は読めるが機械は曖昧) |

→ **ユーザの「ミックス + ミックスはやらない方がよい」に同意。** 理由は 2 つ:
**(a) `caps` で「どの CDC に何が入っているか」を表現しても、host はどの byte がどの source かを byte 単位では分けられない**、
**(b) 組み合わせが `source 数 × CDC 数` で爆発し、`configure` の検証が現実的でなくなる**。

**素直な形**: **CDC の本数は少なく固定(1〜2)し、各 CDC には 1 source を割り当てる(バラ)。** 混合は「1 本しか出せないが 2 つ見たい」ときの妥協として `caps` で申告する、くらいに留める。

#### 補足 K — **(c) の pty は Windows で使えない**

**ch32rv が DMI ラッパーの出力を「仮想 tty」に出す形は、Windows 以外に限られる。**

| OS | pty / 仮想 serial | 中身 |
|---|:--:|---|
| **Linux** | **○** | `/dev/ptmx` → `/dev/pts/N`。symlink を張れば `screen` / `minicom` / Arduino IDE が開ける |
| **macOS** | **○** | `openpty` → `/dev/ttys00N` |
| **Windows** | **✗** | **serial の pty が標準に無い**。仮想 COM を作るには **kernel driver の install が要る**(com0com 等)→ **driver レスの前提と衝突** |

**Windows での代替**:

| 代替 | 中身 | 汎用ツールから使えるか |
|---|---|---|
| **Pluggable Monitor(stdio JSON)** | **ch32rv に実装済み**(`arduino monitor`。HELLO/DESCRIBE/CONFIGURE/OPEN/CLOSE/QUIT に応答し、OPEN で IDE 指定の `<host:port>` へ TCP client 接続) | **Arduino IDE からは ○**。汎用ツールは ✗ |
| **TCP port で出す** | 同じ仕組みの delivery 先を TCP にする | **PuTTY 等が raw TCP を開ける ○**。ただし**「素の serial」ではない** |
| **probe が CDC に出す(b)** | **全 OS で素の serial** | **○** |

→ **帰結**: **「Windows で汎用 serial ツールから素の serial として読みたい」なら、(b) probe 側 CDC が要る。** 非 Windows なら **(c) pty で足り、descriptor を変えずに済む**(PID / `bcdDevice` major が動かない)。

**この非対称が (b)/(c) の選択に直接効く**:

| 前提 | 選択 |
|---|---|
| Linux / macOS だけで足りる | **(c)**。**V003 も含められる**(descriptor を変えない) |
| **Windows でも汎用ツールから読みたい** | **(b)** → **composite が要る** → **`bcdDevice` major が変わり、V003 は外れる** |
| Windows だが **Arduino IDE 経由で足りる** | **(c) + Pluggable Monitor**。**ch32rv に実装済みなので追加コストが小さい** |

⚠ **3 行目が現実的な折衷**に見える(コアの主な使用者は Arduino IDE)。ただし **`screen` / PuTTY で直接見たい人**は取りこぼす。

#### 補足 E — **serial stream の識別子は (source, channel)**

**stream は「source だけ」では特定できない。channel を持つ。**

| source | channel の意味 | 個数 |
|---|---|---|
| **target の物理 USART** | **USART インスタンス番号**(USART1 / 2 / 3 / 4…) | target と probe のピン数次第 |
| **RTT** | **2 層ある** — **(1) buffer index**(up 0..N-1 = target→host、down 0..M-1 = host→target。慣習で up 0 が Terminal。典型は 3 up / 3 down)、**(2) buffer 0 内の virtual terminal 0〜15**(`0xFF <n>` のエスケープで切替) | **複数**。**最大 16 論理 stream が buffer 0 だけで乗る** |
| **DMDATA / SDI print** | 無し | 1 |
| **semihosting** | file descriptor(stdout / stderr) | 1〜2 |
| **probe 自身の log** | level | 1 |

→ **`(source, channel)` が stream の一意な名前**。**RTT だけは 2 段**(`rtt:up0:term3` のような形になる)。

#### 補足 F — PC 側の IF は **CDC か「DMI のラッパー」の 2 つ**

**ch32rv の実装がすでにこの 2 分類になっている**(`docs/cli.ja.md`):

> **実装は 2 backend に割れる: CDC serial backend(`uart`/`sdi`)と DMI backend(`dmdata`/`rtt`。core を halt せず running 中に DMI read/write)**

| PC 側の IF | どの source が来るか | 性質 |
|---|---|---|
| **CDC**(実 serial port) | **target の物理 USART**、**probe が転送する print**(LinkE の SDI forward 型) | **素で開ける**。既存ツールがそのまま |
| **DMI のラッパー** | **DMDATA / SDI(直読み)/ RTT / semihosting** | **CDC を一切使わない**。host が DMI read/write で取り、**stdout / pty / TCP / Pluggable Monitor に出す** |

→ **ユーザの整理どおり**。そして **DMI ラッパー側は「どの probe でも動く」**(ch32rv が「LinkE forward 不要でどの probe でも動き、双方向」と書いている)。

#### 補足 G — **ch32rv に channel 指定は無い**(ご指摘のとおり)

`monitor --source uart|sdi|dmdata|rtt` は **source だけ**。オプションは `--port` / `--baud`(uart のみ)/ `--timestamps` / `--log` / `--raw` / `--reconnect` で、**channel を選ぶものが無い**。

**RTT の実装は `_SEGGER_RTT` を symbol / scan で発見する**ところまで書かれているが、**どの buffer を読むかの指定が無い**(既定で up 0 と推測される)。

→ **要求候補(ch32rv 向け)**: **`--source rtt --channel <up0|down0|...>` と、buffer 0 内の virtual terminal の選択**。**RTT を native に扱う(`H-111`)を満たすには channel が要る。**

#### 補足 H — **「混合すると分離できない」は ch32rv が既に踏んでいる**

補足 C で「混合すると出どころが失われる」と書いたが、**実例が既に文書化されている**:

> **`uart` と `sdi` は LinkE の同じ 1 本の CDC port に出る**。`sdi` は「LinkE に SDI forward を有効化させる」probe 側の**設定変更**であって別 port ではない。**両方使うと 1 つの monitor 窓に混在して届き分離できない。**

→ **LinkE の設計がまさに「混合」で、それが実害になっている。** **補足 C の「バラに寄せる」判断はこの実例で裏付けられる。**

#### 補足 D — ch32rv の現状は「バラのみ」

`monitor --source uart|sdi|dmdata|rtt` は **単一指定**(ch32rv `docs/cli.ja.md`)。→ **今はバラのみ。**

**混合を許すかは未決**で、判断材料は:

| | |
|---|---|
| **バラのみに留める** | host の実装が単純。**出どころが常に明確**。複数見たいときは **monitor を複数起動** |
| **混合を許す** | 1 本の port / 1 つの窓で全部見える。**出どころが失われる**。**ログの相関を取りたいなら `trace`(時刻つき)の仕事**で、serial の仕事ではないかもしれない |

→ **「複数 source を時刻つきで相関させたい」なら `trace` 側の話**([§1.3](#13-未決定の-7-件--何で決まるか))。**serial は「素で読める」ことに価値がある**ので、**バラに寄せるのが筋が通る**。ただし**未決**。

#### 補足 I — **serial の残り 3 つは各論。本質ではない**

「CDC の本数」「各 CDC に何を割り当てるか」「混合を許すか」は**各論**で、**大枠の判断には要らない**。理由:

| 論点 | なぜ各論か |
|---|---|
| **CDC の本数** | **決める対象ではなく `caps` の申告値**。**0 から probe の上限まで**の範囲で、**silicon と build が決めた事実を申告するだけ**([dmi-bridge 設計原則 2](../protocols/dmi-bridge.ja.md)「数値はすべて `caps` が申告する」)。**0 = CDC を出さない probe も正当**(DMI ラッパーだけで成立する) |
| **各 CDC への割当** | **実行時の `configure`**。host が毎回選ぶ(補足 B) |
| **混合を許すか** | host UX の話。**バラに寄せる根拠は既にある**(補足 H の LinkE 実例)が、**仕様を縛る必要が無い** |

→ **serial 系で大枠に効くのは「CDC が使える環境かどうか」(補足 A)だけ。** 残りは実装時に決まる。

#### 補足 J — **DMI ラッパー(書込・デバッグ)には議論が無い**

**`(addr7, data32, op2)` の往復という形に、独立した 3 実装 + 標準が収束している**:

| 出どころ | 形 |
|---|---|
| **RISC-V Debug Spec の DTM** | `dmi` レジスタ = `abits + 32 + 2`(**標準そのもの**) |
| **WCH-Link の USB protocol** | `DmiOp`(cmd `0x08`、payload `[addr, data_be32, op]`)。この repo で **verified** |
| **Black Magic / Swindle の remote protocol** | `dmi->read(addr, *value)` / `dmi->write(addr, value)`、`address_width = 8`。**2026-09-07 に source で確認** |
| **minichlink の programmer 抽象** | `WriteReg32(reg_7bit, u32)` / `ReadReg32(reg_7bit, *u32)` |

→ **形に争点が無い。** そして **flash / debug / print / semihosting は全部この上に乗る**(§1.2)ので、**「書込とデバッグをどう通すか」は論点として閉じている**。

**残る細部**(いずれも各論):

| 細部 | 状況 |
|---|---|
| `batch` の op 集合(`poll` / `write_rep` / `read_rep` をどこまで) | draft にある。**必要性に争点は無い**(per-op では 64 KB が UART で約 5 分) |
| **busy(status=3)の再試行を probe が吸収するか host か** | draft は **probe 側**と置いた。**未実測** |
| timeout の所在 | draft は **host 側**と置いた |
| `max_inflight` の既定 | draft は **1** |



### 1.2 「DMI の上」が意味すること

**確定 11 件は、線に出るものが全部 `(addr7, data32, op2)` の往復だけ**になる。

```
機能層(すべて host 側の知識)
  flash / verify / erase / option / chip 識別 / GDB / RTT / DMDATA・SDI / semihosting / agent
        ↓  全部これ 1 種類の往復で表現される
DM 層  halt / step / GPR・CSR r/w / memory r/w / abstract command / PROGBUF
        ↓  DM レジスタの read/write = DMI トランザクション
DMI    (addr7, data32, op2)          ← 「細い管」
        ↓  L1 framing + L2 多重化
物理 IF  UART / CDC / HID / bulk / IP        ← ここまでが本書の議題(probe ↔ PC)
- - - - - - - - - - - - - - - - - - - - - - -
線      SWIO 1 線 / RVSWD 2 線                ← probe 内部。**議題外**(差は PC 側に現れない)
```

→ **`DMI` 1 本で「書ける・デバッグできる・printf が見える」まで届く。** これが [dmi-bridge 設計原則 1](../protocols/dmi-bridge.ja.md)(probe は chip を知らない)の実体で、**新 chip 対応が host の更新だけで済む**理由。

**⚠ 例外は `tty.uart` だけ**: target の**物理 USART** は DMI では運べない(probe の UART ピンが要る)。**printf を DMI 経由で見る道(RTT / DMDATA / SDI)があるので、USART が 1 本しかない chip でも printf は成立する** — この 2 系統の区別が実務で効く。

### 1.3 未決定の 7 件 — 何で決まるか

**3 軸で性質が分かれる。** 同パイプに載せるか個別にするかは、この 3 軸で決まる。

| 通すもの | 帯域 | 落ちてよいか | **時刻を持つか** | 個別 channel が要るか |
|---|---|:--:|:--:|---|
| **生サンプル** | **最大**(16ch で 0.5〜1 MB/s) | **○** | **○** | **要る可能性が高い**(帯域が 2 桁違う) |
| **意味イベント** | 低〜中 | ○ | **○** | **生サンプルと同じ列に混ぜるかが争点** |
| **DMI 実行印** | 低 | ○ | **○** | **`dmi` の応答に付けると flash 経路のバイト列が変わる**([R3](probe-pattern-coexistence.ja.md) 違反)→ **別の列に出す案が有力だが未決** |
| **ADC サンプル** | 中(3〜4ch × 数百 kSa/s) | ○ | **○** | 同じ列 / 単発読み / 別 の 3 択 |
| **模型の定義 / preload** | **たまに大きい**(SD image 等) | ✗ | ✗ | **同パイプで足りる可能性**(req/resp) |
| **readback** | 中 | ✗ | ✗ | 同上 |
| **障害注入の設定** | 低 | ✗ | ✗ | **同パイプで足りる** |

**争点は 2 つに絞られる**:

| 争点 | 中身 |
|---|---|
| **(1) 時刻を持つ 4 つを 1 本の列にまとめるか** | まとめると **同じ時計になる**([定義 U1〜U5](harness-tool-definition.ja.md) の前提)。別々にすると **host が時刻を突き合わせ直す**ことになり、**U1 の利点(意味イベントが確定値)が薄まる** |
| **(2) その列を同パイプに載せるか、個別 channel にするか** | 帯域が 2 桁違うので個別が有力。だが**個別にすると descriptor が増える**(= `bcdDevice` の major が変わる → [choices §2](harness-choices.ja.md))。**最小構成では個別が作れない**(§6) |

⚠ **本書は (1)(2) を決めない。** 前版は「1 本の `trace` 列にまとめ、個別 channel にする」を既定のように書いていた — **あれは提案の 1 つ**。

### 1.4 いまの呼び方(**暫定**)

未決定を含めて話すために、**呼び名だけ置く**(括りが変わる前提):

| 仮の名 | PC 側の分離 | 決定状況 |
|---|---|---|
| **`dmi`** | 同じ datagram 列 | **確定** |
| **`mgmt`** | 同じ datagram 列(`cmd` 番号で分離) | **確定** |
| **`evt`** | 同じ datagram 列(`type=0x02`) | **確定** |
| **`tty`** | **未決定**(datagram / 別 CDC / host の pty) | **未決定** |
| **`emu`**(仮) | **未決定** | **未設計** |
| **`trace`**(仮) | **未決定**(1 本にまとめるか / 個別 channel か) | **未設計** |

⚠ **`tty.dmi` / `tty.uart` の区別は target 側の話**なので、この表(probe ↔ PC)では **`tty` 1 つ**にした。**PC 側では「素の byte 列が来る」という点で同じ**。

## 2. 物理 IF — 何本の線があるか

| 物理 IF | 実効帯域(**概算**) | PID | 素の serial として開けるか |
|---|---|:--:|:--:|
| **既存 bridge の UART @115200** | **11.5 kB/s** | 0 | **多重化したら ✗** |
| 既存 bridge の UART @921600〜2M | 92〜200 kB/s | 0 | 同上 |
| **native USB CDC ×1** | **~1 MB/s 級**(FS bulk の理論 1.2 MB/s、実効はこれ以下) | 1 | 同上 |
| **native USB CDC ×2** | 合計 ~1 MB/s | 1 | **2 本目を素の serial にできる ◎** |
| **native USB HID(full-speed)** | **~64 kB/s**(interrupt 64 B / 1 ms) | 1 | ✗ |
| **native USB HID(low-speed = V003)** | **~1 kB/s**(8 B / 10 ms)。**feature report(control)経由は更に遅い傾向** | 1 | ✗ |
| native USB bulk(vendor / WinUSB) | ~1 MB/s 級 | 1 | ✗ |
| **IP(TCP / WebSocket)** | LAN 次第(十分) | **0** | **port を分ければ ◎** |
| 物理 UART ピン(USB を介さない) | 線次第 | 0 | ○ |

⚠ **数値は理論値と一般的な実測の範囲**。**実測は未**(→ §6)。

## 3. 多重化の方式は 3 つ

| 方式 | 中身 | 使える物理 IF |
|---|---|---|
| **M1 L2 ヘッダで多重化** | **dmibridge が既に持っている** — 全 datagram の先頭 4 byte(`type` / `lane` / `tag` / `cmd`)。`type` が req / resp / **event** を分ける | **byte stream 全部**(UART / CDC ×1 / IP / bulk) |
| **M2 物理 channel を分ける** | CDC ×2、HID + bulk、CDC + bulk、IP の port 2 つ | native USB(composite)/ IP |
| **M3 HID の report ID で分ける** | report ID ごとに用途を割る(**B003 が feature report `0xAA` でやっている形**) | HID |

**M1 は既に仕様にある**([dmi-bridge §3](../protocols/dmi-bridge.ja.md))。`dmi` / `mgmt` / `emu` は **`cmd` 番号範囲**で、`evt` は **`type`** で分かれる。**M2 は descriptor が増える = `bcdDevice` の major が変わる**(→ [choices §2](harness-choices.ja.md))。

## 4. 組み合わせ — どの物理 IF でどの論理 IF が成立するか

| 物理 IF | `dmi`+`mgmt`+`emu` | **`trace`** | `tty.uart` | `evt` | 大きい payload |
|---|:--:|:--:|:--:|:--:|:--:|
| **UART @115200** | ○(遅い) | **✗** | **△ 同じ線を食う** | ○ | △(遅い) |
| UART @921600+ | ◎ | △(8ch 低速のみ) | ○ | ○ | ○ |
| **CDC ×1** | ◎ | **△ 圧縮前提** | ○ | ○ | ◎ |
| **CDC ×2** | ◎ | △ | **◎ 2 本目を専用に** | ◎ | ◎ |
| **HID(FS)** | ○ | **✗**(64 kB/s では届かない) | ○(115200 なら足りる) | ○ | △(遅い) |
| **HID(LS = V003)** | **△ 遅い** | **✗** | **✗ 帯域不足**(11.5 kB/s > 1 kB/s) | ○ | **✗ 実用外** |
| bulk | ◎ | ○ | ○ | ◎ | ◎ |
| **IP** | ◎ | ○ | ◎ | ◎ | ◎ |

## 5. 制限事項(**物理 IF が使えないとき何が起きるか**)

### 5.1 素の serial が使えなくなる — **最小構成の最大の制限**

**1 本の物理 UART / CDC に M1 で多重化すると、その port は「素の serial」として開けない**(framing の中に入るので `screen /dev/ttyACM0` では読めない)。

| 影響 | 中身 |
|---|---|
| **Arduino IDE の Serial Monitor が直接使えない** | **これが実害**。`H-113`(3 経路を CDC として出す)がまさにこの要求 |
| `minichlink -T` / 既存ツールの端末が使えない | 同じ |
| **回避策 3 つ** | **(a) host が demux して pty / 仮想 serial を作る**(socat / pty。**pluggable monitor で包む**)/ **(b) 2 本目の CDC を素で出す**(`bcdDevice` major が変わる)/ **(c) IP なら port を分ける** |

→ **「素の UART が使えなくなる」は起きる。ただし host 側 demux で回復できる。** **`H-114`(Pluggable Monitor 対応)がこの回避策そのもの。**

### 5.2 HID だと遅い — **どこが遅いか**

| 用途 | HID(FS、~64 kB/s) | HID(LS、~1 kB/s) |
|---|---|---|
| **`dmi` 1 往復** | **~3,500 往復/s**。**batch を使えば実用**。per-op でも耐える | **~60 往復/s**。**per-op は非実用**、batch が必須 |
| **flash 64 KB** | per-op で **~45 s**、**batch + stub なら数 s** | **データだけで 80 s 級**、実際は数分。**rvswdio が遅い理由** |
| **`trace`** | **✗ 全く足りない**(必要 0.5〜1 MB/s) | **✗** |
| **`tty.uart`** | **○ 115200 なら足りる**(11.5 kB/s < 64 kB/s) | **✗ 足りない**(11.5 kB/s > 1 kB/s) |

→ **HID の遅さが効くのは `trace` と flash の生転送**。**`dmi` は batch があれば HID でも実用**。
→ **low-speed(V003)では `tty.uart` が帯域的に無理**(`tty.dmi` に寄せるしかない)。**これは「V003 の USART が 1 本しかない」とは別の、独立した制限**。

### 5.3 物理 IF ごとの「できないこと」

| 物理 IF が使えないと | できなくなること |
|---|---|
| **native USB が無い**(既存 bridge の UART のみ) | **`trace` の連続ストリーム**(115200 では論外、921600 でも 8ch 低速まで)/ **「挿すだけ」の体験**(COM 選択が入る) |
| **bulk が無い**(HID 単機能) | **`trace`**。**flash も遅い** |
| **CDC が 1 本しかない** | **素の serial monitor**(→ §5.1 の回避策) |
| **IP が無い**(Wi-Fi 機でない) | **遠隔ベンチ**、**全ブラウザ対応**(WebSocket 以外は Chromium 系のみ) |
| **low-speed しかない**(V003) | **`trace` / `tty.uart` / 実用速度の flash**。→ **V003 は「DMI を細く通す」用途に限られる** |

## 6. 最小構成での帰結

**最小構成 = 1 本の byte stream(既存 bridge の UART or CDC ×1)+ M1 多重化。**

| 論理 IF | 最小構成で | 備考 |
|---|:--:|---|
| `dmi` / `mgmt` / `emu` | **○** | dmibridge がそのまま動く |
| `tty.uart` | **○** | **ただし素の serial としては開けない**(§5.1) |
| `evt` | **○** | |
| 大きい payload(`batch` / `emu`) | △ | 帯域次第 |
| **`trace`** | **✗** | **最小構成では成立しない**。**入れるなら物理 IF を足す(= `bcdDevice` major を上げる)か IP** |

→ **最小構成で足りるのは L0 / L1 / L3 / L4。L5 以上は物理 IF の追加が前提**([定義 §9](harness-tool-definition.ja.md) の F1 が `△` である理由がここでも出る)。

## 6b. CDC の本数の上限と `caps` の bit 予算

### 6b.1 上限は endpoint 予算で決まる

**CDC 1 本の endpoint 消費**: **IN 2 本**(notification の interrupt IN + data の bulk IN)+ **OUT 1 本**(data の bulk OUT)。

| silicon | endpoint | CDC の上限 |
|---|---|---|
| **RP2040** | EP0 + **IN 15 / OUT 15** | **IN が先に尽きる: 15 ÷ 2 = 7 本** |
| RP2350 | 同等 | 同等(**要確認**) |
| ESP32-S3(OTG FS) | endpoint 数が異なる | **要確認** |
| **V003(software USB / low-speed)** | — | **0**(bulk が無いので CDC 不可) |

→ **RP2040 で 7 本**。ただし **TinyUSB の設定と RAM(DPRAM)も効く**ので、**実装時に確認する**。

### 6b.2 bit 予算は後で決める

**`caps` に何 bit 取るかは後回しでよい**。理由: **実用上 0〜3 で足りる見込み**(そんなに使わない)ので、**2 bit でも足りるかもしれない**が、**上限 7 を表現するなら 3 bit**。

| 案 | 表現範囲 | 備考 |
|---|---|---|
| 2 bit | 0〜3 | **実用には足りる見込み** |
| 3 bit | 0〜7 | **RP2040 の上限まで表現できる** |
| byte | 0〜255 | TLV なら 1 byte 使っても大差ない |

→ **`caps` は TLV(`u8 type, u8 len, len byte`)なので、bit を惜しむ必要が薄い**([dmi-bridge §5.1](../protocols/dmi-bridge.ja.md))。**bit field に詰めるかどうかも含めて後で決める。**

### 6b.3 「オールゼロ = HID のみ」という基準線の案

**問い**: HID は既定であり、他は bit で有無を示す設計か。

| 案 | 中身 | 得失 |
|---|---|---|
| **A. HID を必須の基準線にする** | **能力オールゼロ = HID 1 interface のみ**。CDC / bulk / MSC は bit で申告 | **V003 が基準線に入る**(low-speed でも HID は出せる)。**「最小の probe」が定義できる** |
| **B. HID も bit にする** | 何も必須にしない | **UART / IP transport の probe は USB を持たない**ので、**HID 必須は不自然**。**基準線が空になる** |

⚠ **B の指摘が効く**: **既存 bridge の UART や IP で繋がる probe は、そもそも自前 USB を持たない**。→ **「HID 必須」は自前 USB を出す probe に限った話**にしかならない。

→ **`caps` は「USB を出すか」「出すなら何を」の 2 段にするのが素直**かもしれない。**ただし未決**(後で議論)。

## 6c. **拡張部分を DMI に混ぜるか、別ラインにするか**

**これが本質的な論点**([定義 §9b](harness-tool-definition.ja.md) の残る論点 2 と同じ)。

| | **混ぜる**(同じ datagram 列) | **別ライン**(別 interface / 別 port) |
|---|---|---|
| **物理 IF** | **1 本で済む** | **2 本以上要る** |
| **descriptor** | **増えない** → **PID / `bcdDevice` major が動かない** | **増える** → **major が変わる** |
| **最小構成**(UART / CDC×1 / IP 単線) | **動く** | **動かない**(IP は port 追加で可) |
| **V003** | **入る** | **入らない** |
| **帯域** | **capture が control の応答性を食う**。優先度制御が必須 | **独立**。互いを邪魔しない |
| **落ちたときの扱い** | 同じ列なので **drop 計上が全体に効く** | capture 側だけ落とせる |

⚠ **時刻の相関(複数の source を同じ時間軸に載せるか)はここでは扱わない。** それは[定義 §9b](harness-tool-definition.ja.md) の残る論点 2 で、**先に決めるべき別の話**。

### transport によって「別ライン」のコストが違う

| transport | 別ラインのコスト |
|---|---|
| **既存 bridge の UART** | **不可**(1 本しかない)。**bridge を 2 個載せるしかない** |
| **CDC ×1** | **不可**(混ぜるしかない) |
| **native USB composite** | **可能**。ただし **descriptor が増える** = `bcdDevice` major |
| **IP** | **ほぼゼロ**(**port を足すだけ**。descriptor の問題が無い) |

→ **IP だけは「別ライン」がほぼ無料**。**逆に単線 transport では「混ぜる」以外に選択肢が無い**。

→ **仕様としては「両方を許し、`caps` がどちらかを申告する」形が自然**に見えるが、**それは L2 の設計が両方に耐えることを要求する**(混ぜる場合の優先度制御)。**未決。**

## 6d. PC に認識される USB クラスの選択肢

**「別ライン」を選ぶとき、何のクラスにするかで PC 側の見え方と得失が変わる。**

| class | driver | 帯域(FS) | 素で開けるか | 得 | 損 |
|---|---|---|:--:|---|---|
| **HID** | **全 OS 標準・driver レス** | interrupt **64 B/1 ms = 64 kB/s**(LS は 8 B/10 ms ≈ 1 kB/s) | ✗(HID API 経由) | **low-speed でも使える唯一**。**report ID で多重化できる**。**PID の心配が最小** | **帯域が最小**。capture には届かない |
| **CDC-ACM** | 全 OS 標準 | bulk **~1 MB/s** | **○ 素の serial** | **既存ツールがそのまま**。人が見る用途に最適 | **low-speed 不可**。**Windows の COM 番号問題**。1 本 = 3 endpoint |
| **Vendor + WinUSB** | Win は **MS OS descriptor で自動 bind**、Linux/mac は libusb | bulk **最速** | ✗ | **libusb / WebUSB が使える**。**endpoint 効率が最も良い** | **素で開けない**。MS OS descriptor が要る |
| **MSC(Mass Storage)** | 全 OS 標準 | bulk | ✗(ドライブに見える) | **UF2 で firmware を配れる**。**capture を「ファイル」として出す**手もある(録り終わったら `.sr` が見える) | 実時間の stream に向かない |
| **UAC(Audio)** | 全 OS 標準 | **isochronous 最大 1023 B/frame ≈ 1 MB/s** | ✗(音声デバイスに見える) | **isoc は「帯域予約あり・再送なし」= capture の性質と一致**。**driver レスで HID の 16 倍** | **PC 側が音声として扱う**ので取得が audio API 経由。**奇策** |
| UVC(Video) | 全 OS 標準 | isoc 大 | ✗ | 帯域は最大 | 片方向。**さらに奇策** |
| **NCM / RNDIS(Ethernet)** | NCM は Win10+ inbox | bulk | ✗ | **USB 上に IP を載せられる** → **WebSocket が使え、全ブラウザ対応になる** | 実装が重い。**RNDIS は Windows で非推奨気味** |
| DFU | dfu-util | control | ✗ | firmware 更新の標準 | 実行時の用途に使えない |

**この表から見える組み合わせ**:

| 組み合わせ | 性格 |
|---|---|
| **HID のみ** | **最小**。V003 でも成立。**capture は乗らない** |
| **HID + CDC ×1〜2** | **実用最小形**。DMI は HID、target serial は CDC |
| **CDC(datagram)+ CDC(serial)** | 全部 CDC。**素で開ける口が 2 つ**。endpoint を 6 本使う |
| **HID + vendor bulk** | **capture の帯域が要るとき**。libusb / WebUSB が使える |
| **HID + UAC** | **奇策**。driver レスで capture の帯域を得る |
| **NCM 1 本** | **USB 上の IP**。全ブラウザ + 遠隔と同じ口になる |

⚠ **どれも決めない。** ただし **「HID + CDC」と「HID + vendor bulk」の 2 つが現実的な軸**で、**UAC / NCM / MSC は「もし帯域や配布で詰まったら思い出す」枠**。

## 6e. **口の分類は「誰が開けるか」で切るのが上位**

§6d は class ごとの得失を並べたが、**より上位の軸は「その口を誰が開けるか」**。

| 口 | **開ける主体** | ch32rv の関与 | 何が乗るか |
|---|---|---|---|
| **HID** | **ch32rv**(HIDAPI) | **必須**(これしか無ければ ch32rv 経由が唯一の道) | **DMI / ctl / trace** — **低速の control** |
| **Vendor bulk** | **ch32rv**(libusb)/ **ブラウザ**(WebUSB) | **必須** | **同じもの** — **高速の control** |
| **CDC** | **任意の serial ツール**(`screen` / PuTTY / Arduino IDE / 他人のスクリプト) | **任意**(`monitor --source uart` で開けるが、**開かなくても成立**) | **serial のみ** |
| **MSC** | **OS の file manager**(ドラッグ&ドロップ / `cp`) | **任意**(`uf2 flash` があるが、**無くても成立**) | **flash 書込 / capture ファイルの取り出し** |

→ **上 2 つが「ch32rv が握る口」、下 2 つが「ch32rv 管理外の口」。** 性格が根本的に違う。

### 6e.1 ch32rv が握る口 — **HID と Vendor は同じ protocol の遅い口 / 速い口**

| | HID | Vendor bulk |
|---|---|---|
| 帯域 | 64 kB/s(FS) / ~1 kB/s(LS) | **~1 MB/s 以上** |
| driver | **レス** | Win は MS OS descriptor、他は libusb |
| **datagram の中身** | **同一にできる** | **同一にできる** |

**設計として重要な帰結**: **両方に同じ datagram を載せれば、host は Vendor を試して開けなければ HID に落ちるだけで済む**(→ §6f.4)。

- **HID = 必ず通る道**(driver レス、LS でも出せる)
- **Vendor = あれば速い**(能力申告で上乗せ)
- **byte 同一なら fallback が自然**。「HID しか無い probe でも同じ host コードが動く」

⚠ これは §3 の多重化(1 本の中に lane を通す)とは**別の話**で、**「同じ論理 IF を 2 つの物理 IF に載せる」= 口の二重化**。→ [probe-pattern-coexistence の R3](probe-pattern-coexistence.ja.md)(flash path を byte 同一に保つ)と同じ発想。

**そして §6c の「混ぜるか別ラインか」への答えの一つがここにある**: **capture の帯域が要るときは Vendor 側へ「逃がす」**。→ **capture 専用の第 3 の口を作らずに済む可能性**。

### 6e.2 ch32rv 管理外の口 — **host が無くても価値が出る**

**CDC と MSC の本質的な価値は「ch32rv を必要としない」こと。**

| 口 | ツールレスで何ができるか |
|---|---|
| **CDC** | **`screen` / PuTTY / Arduino IDE の Serial Monitor で target の print が見える**。**我々の host を一切入れずに** |
| **MSC** | **UF2 をドラッグ&ドロップで書ける**。**capture を `.sr` ファイルとして取り出して PulseView に落とせる** |

→ **これは[定義 §9b](harness-tool-definition.ja.md) の残る論点 7「我々の host が完成する前に価値が出るか」に直結する。** **CDC と MSC を持たせると、host 0 行の段階で「見える・書ける」道具になる。**

⚠ 逆の見方: **CDC / MSC は ch32rv から制御できない**ので、**`caps` で名乗る意味も薄い**(OS が勝手に見せる)。**「申告するもの」ではなく「生えているもの」**。

### 6e.3 構成を並べると層になる(**案。決めない**)

| 構成 | 口 | 対象 / 性格 |
|---|---|---|
| **HID のみ** | ch32rv 専用 1 口 | **V003(software USB / LS)がここに入る**。最小。**独自 PID の下限**(§6h.1) |
| **HID + CDC** | + ツールレスの serial | **実用最小**。素の serial が全 OS で読める |
| **HID + Vendor + CDC ×n** | + 高速 control | **capture が乗る**。**HID は残るが実行時は Vendor を使う** |
| **+ MSC** | + ツールレスの書込 / 取り出し | **配布とファイル出力** |

⚠ **口が増えるほど composite になり、`bcdDevice` major が動く**([choices §2](harness-choices.ja.md))。→ **この層はそのまま major の候補**に見えるが、**まだ決めない**(段の数と major の数を一致させる必要は無い)。

## 6f. 構成の一覧 — **単独で成立するか / PID を食うか**

| 構成 | 素の serial | control の帯域 | **PID** | V003 | 単独成立 | ch32rv 依存 |
|---|:--:|---|:--:|:--:|:--:|---|
| **IP 単独** | ✗(pty / routing 依存) | **網の速度**(実質無制限) | **不要** | ✗ | **✓** | 要 |
| **既存 bridge の UART 単独** | ✗ | **~125〜375 kB/s**(1〜3 Mbps) | **不要**(bridge chip のを使う) | △(V003 に bridge を足す形なら) | **✓** | 要 |
| **CDC ×1 単独** | ✗(**混ぜれば人は読める**) | ~1 MB/s | 要 | ✗(bulk 不可) | **⚠ 技術的には成立するが選ぶ理由が無い** → §6h | 要 |
| **HID 単独** | ✗(**逃げ道なし**) | **64 kB/s** / **LS は ~1 kB/s** | 要 | **✓ 唯一** | **✓** | 要 |
| **CDC + HID** | **✓** | 64 kB/s | 要 | ✗ | ✓ | **serial は不要** |
| **CDC + HID + Vendor** | **✓** | **~1 MB/s 以上** | 要 | ✗ | ✓ | **serial は不要** |
| **MSC 単独** | ✗ | —(**片道**) | 要 | ✗ | **✓ ただし L0 専用** | **不要** |

### 6f.1 補正 1 — **PID が要らない構成が 2 つある**

**`IP 単独` と `既存 bridge の UART 単独` は PID を消費しない。**

- **IP**: USB を一切出さない(ESP32 の WiFi / Ethernet)→ **descriptor が存在しない**
- **既存 bridge**: **CH340 / FT232 が自分の vendor の PID を持っている**ので、こちらは何も名乗らない

→ **[frontier](harness-frontier.ja.md) で「PID が最も厳しい制約」と置いたが、段の下端は PID フリー。** **PID を食い始めるのは「自前 USB を出す」と決めた瞬間**から。

### 6f.2 補正 2 — **`HID 単独` は `シリアル単独` と同じではない**

構造(全部カプセル化・素の serial 無し・ch32rv 依存)は同じだが、**3 点が違う**:

| | CDC ×1 単独 | HID 単独 |
|---|---|---|
| **帯域** | ~1 MB/s | **64 kB/s(FS)/ ~1 kB/s(LS)** → **1/16 〜 1/1000** |
| **「人が読める」逃げ道** | **ある**。**混合すれば port として素で読める**(LinkE がまさにこれ) | **無い**。HID は人が読める形にならない |
| **LS で成立するか** | ✗ | **✓ 唯一**(V003) |

→ **「素の serial が無い」ことの痛みは HID 単独のほうが深い**(混ぜて逃げることすらできない)。**代わりに V003 に届く。**

### 6f.3 補正 3 — **「シリアルのみ」は 2 種類あり、帯域が 3〜8 倍違う**

| | 実効帯域 | 備考 |
|---|---|---|
| **CDC ×1(native USB)** | **~1 MB/s** | bulk。**「低速のみ実用」とは言えない**。制約は帯域ではなく**「1 本に全部詰める」こと**(§6c 混ぜる側の損 = capture が control を食う) |
| **既存 bridge の UART** | **~125〜375 kB/s** | **bridge の latency も乗る**。**こちらが「低速のみ実用」** |

### 6f.4 **問いへの答え — HID + Vendor は「使い分け」ではなく fallback**

**案は 1 つ。同じ datagram を両方の口に載せ、host は Vendor を開こうとして、開けなければ HID に落ちる。**

```
host: Vendor を開く ──成功──> Vendor で全部やる(~1 MB/s 以上)
                    └─失敗──> HID で全部やる(64 kB/s / LS ~1 kB/s)
```

**「高速は Vendor、control は HID」のような役割分割は要らない。** 分割すると **HID しか無い probe で control 以外が動かなくなる**が、fallback なら **同じ機能が遅く動くだけ**で済む。

**そして「Vendor が使えるなら Vendor だけ」でよい。HID と併用する利点は無い。** 検討した併用理由はすべて他で解ける:

| 併用したくなる理由 | なぜ不要か |
|---|---|
| Vendor が壊れたときの復旧路 | **fallback で担保済み**(開けなければ HID)。**同時に開いておく必要は無い** |
| 何が繋がっているか見たい | **開いてみれば分かる** |
| 他ツール(minichlink 等)が HID で話す | それは**併用ではなく「別 protocol の口」**。互換モードの話(→ [定義 §9b](harness-tool-definition.ja.md) 論点 7) |
| 時刻の相関 | **ここでは扱わない**(論点 2) |

**併用しないほうが得な点**:

- **descriptor と endpoint が空く**(interface 1 + endpoint 2)→ **CDC を 1 本増やせる**
- **OS が HID を勝手に掴む問題を避けられる**(custom usage page にする配慮が不要)
- **「両方同時に開かれたときの調停」という細部が消える**

→ **1 台の probe が両方持つ必要は無い。** fallback は **「同じ host コードで別の probe を扱う」ための仕組み**(Vendor を持つ probe / HID しか持たない V003)であって、**1 台の中の二重化ではない**。

**成立の条件**:

| 条件 | 理由 |
|---|---|
| **datagram が byte 同一** | host コードが 1 つで済む。口は開き方だけの違いになる |
| **`caps` が「Vendor があるか」を申告する** | ただし **host は開いてみれば分かる**ので、**申告は必須ではない** |

**`Vendor` を実装するときのコスト**(⚠ **`Vendor のみ` は規則 1 違反**(§6h.1d)。**HID と同居する前提での記録**):

| コスト | 中身 | HID なら |
|---|---|---|
| **MS OS descriptor(WCID)が要る** | Windows で WinUSB に自動 bind させるため、descriptor に BOS/MS OS descriptor を埋める | **不要**(何も足さずに bind される) |
| Linux の udev rule | libusb で開くため | **同じく要る**(hidraw も permission が必要)→ **差にならない** |

**Vendor が開けない場合の実例**(= fallback が要る理由):

| 状況 | |
|---|---|
| **probe が Vendor を持たない build** | **V003 は HID のみ**(LS に bulk が無い) |
| WebUSB 非対応のブラウザ | |
| libusb / 権限が無い環境 | Linux の udev rule 未設定など |

→ **HID = 必ず通る道、Vendor = あれば速い。** これは §6e.1 で書いた「遅い口 / 速い口」そのもので、**新しい論点ではない**。

**なお serial の本数は気にしなくてよい**(§6b。endpoint 3 本/本、RP2040 で最大 7 本、実用 0〜3。**`caps` の申告値であって設計判断ではない**)。

### 6f.5 **MSC は単独でも成立する — ただし片道**

**単独不可ではない。むしろ UF2 が世界で最も広く使われている driver レス書込。**

| 用途 | MSC 単独で成立するか |
|---|---|
| **書き込みだけ** | **✓ 完全に成立**。**UF2 bootloader そのもの**(`RPI-RP2` ドライブ、Adafruit の全 board)。**host 0 行** |
| **capture の取り出し** | **✓ 成立する**。ただし**「録れ」の指示を出す口が無い**ので、**trigger が物理ボタンに限られる**(押して録って、挿してコピー) |
| **debug(対話)** | **✗**。**双方向の応答ができない**。MSC は file system の read/write でしかない |

→ **`MSC 単独` = L0(書くだけ)専用、または「ボタンで録る LA」。** **option として足せば制約が消える**(HID で指示、MSC でファイル取り出し)。

⚠ **`MSC 単独 + ボタン` は「host 0 行で成立する capture」**という点で、**残る論点 7(host 完成前に価値が出るか)への最短の答え**になっている。

## 6g. 通常経路の階層 — **「上位を選ぶ」で合っているが、シリアルが 2 回出てくる**

### 6g.1 まず: **シリアルは役割が 2 つある**

**CDC を「control を通す口」として使う場合と「素の serial」として使う場合は、まったく別の話。**

| CDC の使い方 | 位置づけ |
|---|---|
| **素の serial として使う** | **control とは無関係に併存する**。control が Vendor でも HID でも、**CDC は別に生えている**(§6e.2)。**選択の対象ではない** |
| **control を流し込む口として使う** | **他に口が無いときの最後の手段**。**素の serial を食う**(§5.1) |

→ **階層に並ぶのは後者だけ。** 前者は階層の外。

### 6g.2 control 経路の優先順位

**帯域だけでは順序が決まらない**(CDC ×1 は ~1 MB/s で HID の 16 倍あるのに、優先度は HID より低い)。**副作用を含めて並べる**:

| 順 | 経路 | 帯域 | 副作用 | 備考 |
|:--:|---|---|---|---|
| **明示指定のみ** | **IP** | **網の速度**(実質無制限) | **無し** | **能力は最上位**(port 自由 / **遠隔できる唯一** / **PID 不要**)。**ただし自動選択されない** → §6g.5 |
| **1** | **Vendor bulk** | **~1 MB/s 以上** | **無し** | 速くて何も食わない。**build 時に MS OS descriptor のコストだけ** |
| **2** | **HID** | 64 kB/s(FS) / **~1 kB/s(LS)** | **無し** | **遅いが副作用ゼロ**。**driver レス**。**LS(V003)で成立する唯一** |
| **3** | **serial port を control に流用**<br>(**native CDC** または **既存 bridge の UART**) | native CDC: ~1 MB/s<br>bridge: ~125〜375 kB/s | **素の serial を失う** | **PC から見ると同じ serial port**。**違いは速度と baud の要否だけ** → §6g.7。**自前 USB を持たない probe の唯一の道** |

⚠ **`Vendor` と `HID` は同じ枠の速い版 / 遅い版**で、**同時には出さない**(§6f.4)。→ **実質、1・2 は「どちらか一方が生えている」**。

⚠ **IP は表の外**(自動選択の階梯に入らない)。**能力としては最上位だが、明示指定でしか選ばれない。**

### 6g.3 「選ぶ」の主体が 2 段ある

| 段 | 誰が決めるか | 何で決まるか |
|---|---|---|
| **① どの経路が生えているか** | **host は選べない** | **probe の silicon + build**。V003 → HID のみ / RP2040 → Vendor / ESP32 → Vendor + IP / bridge 載せ → UART |
| **② 生えている中でどれを control に使うか** | **host が選ぶ** | §6g.2 の順(+ 遠隔が要るかどうか) |

→ **「上位が使えればそれを選択する」は ② の話として正しい。** ただし **① で何が生えるかが先に効く**ので、**実際には「probe が持っている中で最良を開く」**。

### 6g.4 整理すると 2 系統 + 併存 1 つ

| | control | serial |
|---|---|---|
| **自前 USB を出す** | **Vendor**(速い)**か HID**(遅い、LS 可)**の一方** | **CDC ×n が併存**(0〜7。`caps` の申告値) |
| **自前 USB を出さない** | **IP**(port を好きなだけ、遠隔可、**PID 不要**)**か 既存 bridge UART**(1 本に全部詰める、遅い、**PID 不要**) | **専用の口が無いので control に混ぜるしかない** |

→ **この形なら「階層」は control 側だけの話**になり、serial は独立に足し引きできる。

### 6g.5 **IP は最上位だが opt-in — 自動選択されない**

**能力は最上位**(帯域は網の速度、port を好きなだけ作れる、**遠隔ができる唯一**、**PID 不要**)。**それでも自動探索の対象にしない。**

**選択の規則**:

```
明示指定あり(例: --probe ip:<host>:<port>)  →  IP のみを使う
指定なし                                   →  USB を enumerate し §6g.2 の 1〜4 の順で開く
                                              (IP は列挙もされない)
```

**なぜ opt-in か**:

| 理由 | 中身 |
|---|---|
| **そもそも自動探索できない** | USB は enumerate すれば見つかるが、**IP はアドレスを知らないと見つからない** |
| **mDNS / broadcast で探すと誤接続する** | **同じ網に他人の probe があると、他人の target を掴む**。**USB は物理的に繋いだものしか触れない**(接続そのものが同意) |
| **害が非対称** | 誤接続の結果は **他人の flash を消す**。自動でやってよい操作ではない |
| **意図の明示** | 遠隔は「そうしたい」ときにやること。既定で起きるべきものではない |

**既存ツールも同じ形**: `gdb` の `target remote host:port`、OpenOCD の `remote_bitbang` / `jtag_vpi`、probe-rs の network probe — **いずれも config か引数での明示指定**で、自動探索しない。

### 6g.6 ch32rv 側の現状 — **IP transport が無く、隣接する判断が「巻き取らない」になっている**

| 事実 | 出どころ |
|---|---|
| **ch32rv に IP transport は無い** | `docs/cli.ja.md` の transport は USB(WCH-Link)と serial のみ |
| **probe-rs の `serve`(remote probe)を「巻き取らない」と決めている** | `docs/requirements.ja.md`:114 / :200 —「ARM/汎用 probe 向け機能または別製品領域。**probe-rs 併用で足りる**」 |
| TCP は別用途で既に使っている | `gdb --listen 127.0.0.1:3333`(server 側)、`arduino monitor` の **OPEN で IDE 指定の `<host:port>` へ TCP client 接続**(client 側の実装が既にある) |

⚠ **ただし `serve` とは別物**:

| | probe-rs の `serve` | ここで言う IP |
|---|---|---|
| 誰が network を持つか | **PC**(手元の PC が USB probe を network に公開する) | **probe 自身**(ESP32 の WiFi / Ethernet) |
| PC の台数 | **2 台要る**(公開側と利用側) | **1 台**(probe が直接網にいる) |
| ch32rv がすべきこと | server を書く | **TCP client として繋ぐだけ**(`arduino monitor` に実装済みの形) |

→ **`serve` を巻き取らない判断と矛盾しない。** **必要なのは「TCP client として probe に繋ぐ transport」**で、**server ではない**。

→ **要求候補(ch32rv)**: `--probe ip:<host>:<port>` 相当の **transport 追加**。**`serve` の判断は変更不要**。**ただし [index の C-* に未登録](harness-index.ja.md)** なので、論点として立てる必要がある。

### 6g.7 **native CDC と既存 bridge の UART は 1 つの経路** — draft も既にそう扱っている

**別の経路として並べていたのは誤り。** 3 点の理由:

| 理由 | 中身 |
|---|---|
| **同時に使えない** | **構造上排他**。native USB がある probe に bridge を足す意味が無く、bridge を使う probe は native USB を持たない |
| **PC から見えるものが同じ** | `/dev/ttyACM0` / `COM3`(CDC)と `/dev/ttyUSB0` / `COM3`(CH340/FT232)。**どちらも serial port**。**host コードは open / read / write で同一** |
| **仕様が既に同一行で扱っている** | [dmi-bridge §2.1](../protocols/dmi-bridge.ja.md):60 の表は **「UART / USB CDC」を 1 行**にして、**両方に `magic + len + payload + CRC16 + 再同期` を課している** |

**残る違いは 2 点だけ**:

| 違い | native CDC | 既存 bridge の UART |
|---|---|---|
| **速度** | ~1 MB/s(bulk) | **~125〜375 kB/s**(1〜3 Mbps)+ **latency timer**(FTDI 等で 1〜16 ms) |
| **baud の意味** | **無意味**。`set_line_coding` は firmware に伝わるだけで、実速度は USB が決める | **実際の線速度を決める**。合っていないと通信できない |

→ **ch32rv が既にこの差を反映している**: `monitor` の `--baud` は **uart のみ**([cli.ja.md](../../ch32rv/docs/cli.ja.md))。

**信頼性の差は消える**: UART は framing error / overrun でデータが化けるが、**L1 の CRC16 + 再同期が両方に掛かっている**([dmi-bridge §2.3](../protocols/dmi-bridge.ja.md))ので、**上位から見た信頼性は同じ**。→ **L1 に CRC を置いた設計がここで効いている。**

**PID の出どころだけは違う**が、これは**経路の差ではなく board 構成の差**(§6f.1): bridge chip は **CH340 / FT232 が自分の vendor の PID を持つ**ので、**我々は何も名乗らない**。

## 6h. **独自 PID で CDC だけ出す構成は考えられない**

**PID を払っているなら descriptor は自由に組める。そこで control 用の口を足さない理由が無い。**

| | コスト | 得るもの |
|---|---|---|
| **CDC に control 用の口を足す** | **interface 1 + endpoint 1〜2 + descriptor 60〜80 B** | **§5.1 で「最小構成の最大の制限」と置いた「素の serial が使えなくなる」が消える** |

**足さないと起きること**:

- **素の serial を control datagram が食う**(§5.1)
- **人が `screen` で開くと、化けた datagram が見えるうえ、打った文字が control に混入する**
- baud / flow control の設定が control に影響する

→ **割に合わない。** **`独自 PID + CDC のみ` は選ばない**(**ただし禁止ではなく非推奨** → §6h.1h)。

### 6h.1 **決定(2026-09-07)**

> ### 規則(**確定版は §6h.1d**)
> 1. **独自 PID を名乗るなら HID を実装する** → **`CDC のみ` / `Vendor のみ` は共に NG**([定義 §8 **N9**](harness-tool-definition.ja.md) = **永久の非目標**)
> 2. **実行時は Vendor があれば Vendor を使う**(§6f.4)。**HID は使われないが必ず居る**
> 3. **PID を払わない構成(既存 bridge の UART / IP)には規則 1 は掛からない**(descriptor が無い)

**規則 1 と 2 は矛盾しない**: **1 は「descriptor に何を置くか」= build 時の話**、**2 は「どの口を開くか」= 実行時の話**。

⚠ **§6f.4 で「併用しないほうが得(descriptor と endpoint が空く)」と書いたのは踏み込みすぎだった。** あれは **「役割分割に利点がない」ことの根拠**にはなるが、**「HID を descriptor に置かない」ことの根拠にはならない**。**規則 1 がその部分を上書きする。**

### 6h.1a 構成の判定(**確定版は §6h.1d**)

| 構成 | 判定 | 位置づけ |
|---|:--:|---|
| **`HID` のみ** | **✓** | **独自 PID での最小**。**V003(software USB / LS)がここ**。**下限を定義する構成** |
| **`HID + CDC`** | **✓** | 素の serial が付く。**Vendor を持てない / 要らない chip 向け** |
| **`HID + Vendor`** | **✓** | **高速**。素の serial は無い |
| **`HID + Vendor + CDC ×n`** | **✓ 推奨** | **一番バランスがいい**。**CDC が普通の用途で使える**(target の USART / 人が `screen` で読む)ので、**日常的に一番効く構成** |
| **`Vendor のみ` / `Vendor + CDC`**(HID 無し) | **非推奨** | 規則 1 から外れる。**禁止はしない**(§6h.1h) |
| **`CDC のみ`** | **非推奨** | 規則 1 から外れる + **素の serial を潰す**。**禁止はしない**(§6h.1h) |

**HID を置いて得られる不変条件**:

| 不変条件 | 効き方 |
|---|---|
| **どの環境でも driver レスの口が 1 つある** | 「繋いだら必ず何かできる」が **保証**になる |
| **fallback が必ず着地する** | 「Vendor を開いて落ちたら HID」は、**HID が居なければ落ちる先が無い** |
| **V003 と全 probe が同じ口を持つ** | **V003 用に書いた host コードが全構成で動く**(最下位互換) |
| **descriptor の下限が定まる** | **`bcdDevice` major の基準が安定する**(→ [choices §2](harness-choices.ja.md)) |

**払うコスト**(いずれも安い):

| コスト | 対処 |
|---|---|
| interface 1 + endpoint 1〜2 + descriptor 60〜80 B | native USB がある chip では無視できる |
| **OS が HID を勝手に掴む** | **vendor-defined usage page(`0xFF00` 系)にする**。keyboard / mouse に見えない |

### 6h.1b **規則は 1 つで足りる — 「独自 PID のとき HID なしを許すか」**

**`CDC のみ` と `Vendor のみ` を別扱いにする必要は無かった。** どちらも **「独自 PID なのに HID が無い」**の一例で、**規則 1 本で両方を裁ける**:

> **独自 PID を名乗るなら HID を実装する。** → `CDC のみ` も `Vendor のみ` も **NG**。

**これを禁止にできるかは、1 つの技術的な問いに帰着する**:

> **composite が使えず `Vendor のみ` しか作れない環境はあるか?**

**あるなら禁止できない**(善意の移植を弾く)。**無いなら禁止できる**。

### 6h.1c 調査 — **composite が作れない環境は見つからなかった**

| 検討した制約 | 結果 |
|---|---|
| **endpoint が足りない** | **HID は interrupt IN 1 本で足りる**。`HID + Vendor` = **3 endpoint + EP0**。**native USB を持つ chip は最低でも 8 endpoint 級**(WCH USBFS / RP2040 は IN 15 / OUT 15)→ **足りないケースが無い** |
| **USB stack が composite 非対応** | **TinyUSB は composite が第一級**。**WCH の USBFS EVT にも CDC+HID composite の例がある**。**自作の最小 stack だと単一 interface 限定になりうるが、それは実装の選択で、環境の制約ではない** |
| **Windows が composite を扱えない** | **`usbccgp.sys`(generic parent)が interface ごとに子デバイスを作る**。**HID 子は HID driver、vendor 子は MS OS descriptor 経由で WinUSB** に bind。**標準の道** |
| **WebUSB が composite の vendor interface を掴めない** | **掴める**。Chrome の WebUSB は **HID / audio / video / MSC を保護 class として弾く**が、**弾くのはその interface だけ**で、**vendor interface は claim できる** |
| **WebHID が使えない** | HID 側は WebHID で触れる |
| **macOS / Linux** | **問題なし**(HID は IOKit / hidraw、vendor は libusb。kernel は vendor class を claim しない) |
| **software USB(V003)** | **low-speed に bulk が無いので Vendor 自体が作れない** → **`HID のみ` になり規則を満たす**。**反例にならない** |

**決定的な先例**: **CMSIS-DAP v2 がまさにこの形**(**WinUSB bulk interface + 任意の CDC の composite**)で、**世界中の probe に載って動いている**。→ **`HID + Vendor` composite は既に踏み固められた道。**

→ **`Vendor のみ` しか作れない環境は見つからなかった。** したがって **規則を禁止にできる。**

⚠ ただし **残るコストが 1 つある**(禁止の障害ではないが、実装の手間):

| コスト | 中身 |
|---|---|
| **MS OS descriptor が interface 単位になる** | 単一 interface の vendor device なら device 全体に効く compatible ID で済むが、**composite では「どの interface を WinUSB に bind するか」を指定する必要がある**(**MS OS 2.0 descriptor の function subset**)。**CMSIS-DAP v2 に参照実装がある** |

⚠ **未実測。** → **実験候補: `HID + Vendor(WinUSB) + CDC` の composite を Windows で作り、HID / WinUSB / COM の 3 つが同時に開けることを確認する。** [bcdDevice の実験](harness-choices.ja.md)と同じ台で測れる。

### 6h.1d **規則(統合後)**

> ### 規則
> 1. **dmibridge を喋る firmware が独自 PID を名乗るなら、HID を実装する。**(**強い推奨。禁止ではない** → §6h.1h)
> 2. **実行時は Vendor があれば Vendor を使う**(§6f.4)。**HID があれば使われないが居る**
> 3. **規則 1 が意味を持たないもの**: **PID を払わない構成**(既存 bridge の UART / IP。descriptor が無い)/ **dmibridge を喋らない firmware**(**`MSC のみ` の UF2 BL 等** → §6h.1e)

**判定表**:

| 構成 | 判定 | 位置づけ |
|---|:--:|---|
| **`HID` のみ** | **✓** | **独自 PID での最小**。**V003(software USB / LS)がここ**。**下限を定義する** |
| **`HID + CDC`** | **✓** | 素の serial が付く。Vendor を持てない / 要らない chip 向け |
| **`HID + Vendor`** | **✓** | **高速**。素の serial は無い |
| **`HID + Vendor + CDC ×n`** | **✓ 推奨** | **一番バランスがいい**。**CDC が普通の用途で使える** |
| **`Vendor のみ` / `Vendor + CDC`** | **非推奨** | 規則 1 から外れる。**composite は作れる**(§6h.1c)ので**技術的な言い訳は無い**が、**禁止はしない**(§6h.1h) |
| **`CDC のみ`** | **非推奨** | 規則 1 から外れる。さらに**素の serial を潰す**。**禁止はしない**(§6h.1h) |

**endpoint 予算(RP2040 で推奨構成を組む場合)**:

```
HID(IN 1) + Vendor(IN 1 / OUT 1) + CDC ×n(IN 2 / OUT 1 each)
IN:  1 + 1 + 2n ≤ 15  →  n ≤ 6
OUT: 1 + n     ≤ 15  →  制約にならない
```

→ **`HID + Vendor + CDC ×6` が RP2040 の上限**(CDC 単独なら 7 本だったが、**推奨構成では 6 本**)。

### 6h.1e **`MSC のみ` — 規則 1 の適用範囲を絞る必要がある**

**規則 1 をそのまま読むと `MSC のみ` が NG になるが、§6f.5 で「MSC 単独 = UF2 bootloader そのもので完全に成立」と書いた**。**衝突している。**

**原因**: **規則 1 の根拠(§6h.1a の 4 つの不変条件)が、いずれも「control protocol を喋る firmware」を前提にしている**。

| 不変条件 | `MSC のみ` に当てはまるか |
|---|---|
| driver レスの control 口が 1 つある | **control protocol が無い**ので無意味 |
| fallback が必ず着地する | **fallback 元の Vendor が無い**ので無意味 |
| V003 と同じ口を全構成が持つ | **host コードを共有しない**ので無意味 |
| descriptor の下限が定まる | **`caps` を名乗らない**ので無意味 |

→ **規則 1 の適用範囲を明示する**:

> **規則 1 は「dmibridge を喋る firmware」に掛かる。** `MSC のみ` は **dmibridge を一切喋らない**ので**規則の外**。

**該当する 3 つのケース**:

| ケース | 中身 | 扱い |
|---|---|---|
| **UF2 / DFU bootloader** | **ドライブに `.uf2` を落とすと書ける**。**RP2040 の `RPI-RP2`、Adafruit の全 board** | **規則の外**。**そもそも probe ではない** |
| **MSC-only programmer** | ファイルを落とすと **target に書く** device | **規則の外**。**有用だが「この仕様の実装」とは呼べない**(protocol が無い) |
| **ボタンで録る LA** | 押して録り、挿してファイルを取り出す(§6f.5) | **規則の外**。**同じ理由** |

### 6h.1f **PID を食うかどうかは別に解ける**

**`MSC のみ` の実務的な問題は「規則違反」ではなく「PID を 1 つ食うのに我々の protocol を一切喋らない」こと。** PID が最も貴重な資源([frontier](harness-frontier.ja.md))なので、ここは効く。

**ただし 2 つの逃げ道がある**:

| 逃げ道 | 中身 |
|---|---|
| **他人の PID で済む場合がある** | **RP2040 の UF2 BL は RPi の `2E8A:0003`**、Adafruit の BL は Adafruit の PID。**我々の PID を消費しない**。→ **自前で BL を書かない限り 0 コスト** |
| **同じ PID を `bcdDevice` major で分ける** | **BL と APP は同時に存在しない**(片方が reboot して他方になる)ので、**同時 binding の衝突が起きない**。**Windows は interface class で driver を選ぶ**(MSC → `usbstor` / HID → HID driver)うえ、**hardware ID が REV で分かれる** → **綺麗に共存する** |

⚠ **2 つ目は [choices §2 の `bcdDevice` 方式](harness-choices.ja.md)がそのまま効くケース**。**LinkE の IAP と factory ISP が同じ `4348:55E0` を共有して衝突している**(N4 の実例)のに対し、**bcdDevice を分ければその衝突が起きない**。→ **`MSC のみ` の BL は独自 PID を新たに要求しない。**

### 6h.1g **`MSC + HID` という手もある**

**規則 1 を満たしつつ D&D も使える構成。**

| | `MSC のみ` | **`MSC + HID`** |
|---|---|---|
| D&D で書ける | **✓** | **✓** |
| ch32rv から精密に操作できる | ✗ | **✓**(protocol が乗る) |
| 進捗 / エラーの報告 | **ファイル名や `INFO_UF2.TXT` 程度** | **protocol で返せる** |
| 規則 1 | 範囲外 | **満たす** |

**先例**: **tinyuf2 は MSC + CDC** の composite。→ **「D&D と protocol の両方」は既に踏まれている形**。

→ **自前で BL を書くなら `MSC + HID` が素直**に見えるが、**BL の size 制約次第**(→ [bootloader-design-space](bootloader-design-space.ja.md))。**未決。**

### 6h.1h **禁止しない(2026-09-07 再判定)— 規則 1 は強い推奨に留める**

**`CDC のみ` を N9 として禁止したが、根拠を詰め直すと禁止まで行かない。** 2 点が決定的:

#### 理由 1 — **不変条件が host の分岐を消していない**

禁止の最大の根拠は「**我々の PID なら HID がある**」という不変条件だった。**しかしこの不変条件は最初から狭い**:

| 構成 | HID があるか |
|---|:--:|
| 独自 PID + dmibridge | **規則 1 が保証する** |
| **既存 bridge の UART** | **無い**(規則 3) |
| **IP** | **無い**(規則 3) |
| **`MSC のみ` の BL** | **無い**(§6h.1e) |

→ **host は「HID が無い経路」をどうせ実装しなければならない**(serial transport と IP transport)。**`CDC のみ` を許しても host のコードは 1 行も増えない**(**serial transport は既に bridge UART 用に実装済みで、L1 は同じ `magic + len + CRC16`** → §6g.7)。

→ **禁止しても得るものが無い。**

#### 理由 2 — **禁止すると、狙っている構成そのものを弾く**

**「V003 の BL にも入れられる protocol であるのが好ましい」**という方針([定義 §3](harness-tool-definition.ja.md))を踏むと:

| BL の形 | 規則 1 | 妥当か |
|---|:--:|---|
| **V003 の software USB BL** | **満たす**(LS = HID のみ) | ✓ |
| **`MSC のみ` の UF2 BL**(protocol 無し) | **範囲外** | ✓ |
| **hardware USB の BL が dmibridge を CDC で喋る** | **違反** | **✗ になってしまう** |

→ **3 行目は「BL でも同じ protocol を喋る」という狙いの中心**なのに、**BL は size が最も厳しい**(→ [bootloader-design-space](bootloader-design-space.ja.md))ので **HID を足す余裕が最も無い**。

⚠ **禁止は「protocol を喋らない `MSC のみ` を許し、protocol を喋る `CDC のみ` を弾く」という逆立ちを生む。**

#### 再判定

| | 判定 |
|---|---|
| **`CDC のみ`** | **非推奨**(禁止ではない)。**素の serial を潰す**という害は残るが、**それはその build の作者と利用者に閉じる** |
| **`Vendor のみ`** | **非推奨**(禁止ではない)。同じ理由 |
| **規則 1** | **強い推奨**。**`HID + Vendor + CDC` を推奨構成として示し、外れたものは「保証が減る」と説明する** |

→ **[定義 §8](harness-tool-definition.ja.md) の N9 は非目標から降ろす**(**永久の非目標ではなく設計指針**)。

⚠ **この一連の議論で「禁止」は生まれなかった。** 残る禁止は **N4(他人の PID を firmware が名乗る)** で、あちらは **第三者に害が及ぶ**ので質が違う。

**代わりに使える手**(禁止せずに水準を示す):

| 手 | 中身 |
|---|---|
| **`caps` の水準として名乗らせる** | 例: 「基準線を満たす」と申告できるのは HID を持つ構成だけ。**host は水準を見て機能の有無を説明できる** |
| **推奨構成を明記する** | `HID + Vendor + CDC ×n` を既定として文書化(§6h.1d) |
| **host が警告する** | `CDC のみ` の probe に繋いだとき「**この probe では素の serial が使えません**」と一度出す |

### 6h.2 唯一の代替 `CDC ×2` も HID/Vendor に劣る

**CDC を 2 本出して 1 本を control、1 本を serial にすれば、HID なしで素の serial を保てる。** ただし:

| | `CDC ×2` | `CDC + HID/Vendor` |
|---|---|---|
| endpoint | **+3**(IN 2 / OUT 1) | **+1〜2** |
| PC 側の見え方 | **COM が 2 つ並び、どちらが何か分からない** | **serial は 1 つだけ。曖昧さゼロ** |
| 誤操作 | **人が control 側の COM を開いてしまえる** | **HID/Vendor は人が開けない** |
| driver レス | CDC の COM 割当に依存 | **HID は完全にレス** |

→ **成立はするが、高くて曖昧。** 選ぶ理由が無い。

### 6h.3 V003 は反例にならない

**「size 制約が最大の chip なら CDC のみに削りたい」という反論は成立しない**: **V003 は low-speed で bulk が無く、そもそも CDC を出せない**(§6f.2)。→ **size 制約が最も厳しい chip は、CDC 側ではなく HID 側にしか居ない。**

### 6h.4 帰結 — **「serial だけ」は PID フリーの構成でしか意味を持たない**

| 構成 | PID | `serial だけ` の妥当性 |
|---|---|---|
| **既存 bridge の UART** | **不要**(CH340/FT232 が vendor の PID を持つ) | **✓ 妥当**。**そもそも serial しか無い**(bridge に interface を足せない) |
| **独自 PID の CDC** | **要** | **✗ 妥当でない**。**足せるのに足さない選択になる** |

→ **「素の serial を失う」という制限(§5.1)は、PID を払わない構成に固有のもの**だった。**PID を払う構成では、その制限は設計で回避できる。**

## 7. 未確認(実測すべきもの)

| # | 測ること | 効く判断 |
|---|---|---|
| 1 | **HID(FS)の実効帯域と往復レート** | `dmi` を HID で回せるか |
| 2 | **low-speed HID の実効レート**(rvswdio の実測値) | V003 の実用範囲 |
| 3 | CDC ×1 の実効帯域(OS 別) | capture が圧縮でどこまで乗るか |
| 4 | **UART の実効上限**(CH340 / CP2102 / 内蔵 USB-Serial-JTAG 別) | 最小構成の天井 |
| 5 | **host demux + pty の実用性**(Arduino Serial Monitor が読めるか) | §5.1 の回避策が成立するか |
| 6 | `trace` の圧縮率(RLE / エッジ時刻) | `trace` が CDC ×1 に乗るか |

**5 が最も効く。** これが通れば **「素の serial が使えない」制限は実質消える**(host が包む)。

## 8. 参照

- **本書の内側**(1 本の物理 IF の中でどう論理的に分けるか): [harness-virtual-if.ja.md](harness-virtual-if.ja.md)


- L2 ヘッダの多重化(M1): [../protocols/dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md) §3
- HID adapter の分割/再組立: 同 §2.4
- transport ごとの境界と完全性: 同 §2.2
- descriptor を増やすことの意味(M2): [harness-choices.ja.md](harness-choices.ja.md) §2
- transport の固定費(PID を払わない道): [ecosystem-any-hardware.ja.md](ecosystem-any-hardware.ja.md) §4.5
- 帯域と capture の関係: [dut-harness-design.ja.md](dut-harness-design.ja.md) §4.2
- 所在: [harness-index.ja.md](harness-index.ja.md)
