# DUT harness とは何か — 機能・解消する不満・ユースケース

状態: **取りまとめ・第 1 部**(**定義とユースケースの洗い出し**)。**細かい要求の裁定は含まない**(第 2 部)。
基準日: 2026-09-06

議論の所在は [harness-index.ja.md](harness-index.ja.md)。要求の一覧はコア `harness-requirements`(`H-nnn` / `C-n`)、ベンチ `HARNESS_REQUESTS`(`B-nnn`)、ライタ `0006` が正本で、**本書はその上位にある「これは何の道具か」を固定する**ことを目的にする。

> **なぜ定義を先に置くか。** 要求が 143 + 7 件、相反が 13 件まで増えた。個々を裁く前に**何の道具かが共有されていないと、裁定の基準が無い**。逆に定義が決まれば、相反の多くは「その道具にとって必要か」で自動的に片付く。

## 1. 定義 — これは 1 つの製品ではなく、1 本の protocol が覆う幅

> **手持ちの MCU board を CH32 の書込器にする最小の firmware から、1 台の target を専有して debug 線・周辺バス・アナログ・時間軸を 1 つの時計に載せる計測器まで**を、**同じ protocol・同じ host・同じソース木**で覆う。

**「ロジアナ付き書込器」を作る話ではない。** 幅の**下端**は「LinkE を買わずに書きたい」、**上端**は「target に何が起きたかと何を起こすかを同じ時計で扱う」。この 2 つは別の製品に見えるが、**下端が普及していないと上端は誰も使わない**(上端は下端の firmware を焼いた board の上に載る)。

幅を 1 本にできる根拠は [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md) の **「能力は build 時に決まり、役割は実行時に決まる」** — **1 つの仕様・1 つのソース木から、board と用途ごとのビルドが出る**。だから幅の議論は「どのビルドまで作るか」であって、「何個の製品を作るか」ではない。

段階の定義と、どこまでを対象にするかは **§5**。

## 2. 何が不満なのか(現状の痛み)

すべて 4 repo のいずれかに**実測または実地の記録**がある。

**§2.0 は一般ユーザの不満、P1 以降は 4 repo の開発上の不満**。前者は 4 repo の文書のどこにも書かれていない — **書いた人が全員すでに LinkE を持っている**から。だが**数の上ではこちらが圧倒的に多い**。

### 2.0 入口の不満(ここが最大多数)

| # | 不満 | 中身 |
|---|---|---|
| **P0a** | **書くのに専用の装置を買わないといけない** | CH32 に触るには WCH-Link が要る。**入手経路・送料・届くまでの時間**が国によって重い。教室や複数人なら**台数分**要る |
| **P0b** | **ARM なら手持ちの board で済んだのに** | SWD には **CMSIS-DAP** があり、**任意の MCU 上の firmware が、任意の host tool(OpenOCD / pyOCD / probe-rs)から使える**。CH32 RISC-V にはこれが無い |
| **P0c** | **DIY probe は 10 種以上あるのに、横展開できない** | PicoRVD / Swindle / rvswdio / funprog / NHC-Link042 / ardulink / WebLink …。だが **MCU × transport × host tool が 1 対 1 に固定**されていて、「手元の board で、好きな host から」ができない([probe-ecosystem](probe-ecosystem.ja.md) §1、[generic-probe-design](generic-probe-design.ja.md) §1) |
| **P0d** | **リモートにしたいだけなのに 2 台要る** | WCH-LinkW(CH32V208)は **PC↔probe 間が無線**になる装置。だが **dongle + probe の 2 台構成**で、値段も 2 台分。**「間の線が無いだけ」なのに** |
| **P0e** | **書けても printf が見えない / 見るのに別の道具が要る** | `SerialSDI` は LinkE 専用、`SerialRTT` は ELF が要る、`SerialDMDATA` は minichlink 専用(P13 と同根だが、入口では「Arduino 体験が成立しない」という形で出る) |
| **P0f** | **情報が「装置を持っている人」の側にしか無い** | 知識が 10 以上の project・vendor 資料・非公式解析に分散していて、**LinkE を既に持っている人しか到達できない場所**にある。**最初の 1 台を作りたい人が最も情報に届かない** |
| **P0g** | **どの probe が何に対応しているのか分からない** | 「1 線だけ」「V003 だけ」「read-back 未実装」「GDB は別」…が project ごとにバラバラで、**買う/焼く前に判定できない**。host 側も型番の表で対処するしかない |

> **P0b/P0c の要点は「hardware が無い」ではなく「標準 protocol が無い」。** firmware は既にたくさんある。**足りないのは、どの firmware でも同じ host から使える共通の口**で、それが [dmi-bridge](../protocols/dmi-bridge.ja.md) の存在理由。CMSIS-DAP が ARM で解いた問題が、CH32 RISC-V で未解決のまま残っている。

### 2.1 開発上の不満(4 repo の実測)

| # | 不満 | 出どころ |
|---|---|---|
| P1 | **波形が見えないので、試験項目がまとめて着手できない**。「16ch LA + connector 治具 + 刺激用 controller」を同時に決めないと 1 項目も進まない | コア `TEST_PLAN` 方法4 が全項目 ⬜ / Q-050 |
| P2 | **相手役がいない**。入力側 API(受信・NACK・エッジ・既知電圧)は passive な観測だけでは検証できない | コア `test-strategy` |
| P3 | **probe が attach で target を汚す**。LinkE は `RCC_CFGR0` と `FLASH ACTLR` を書き換える(V307、probe-rs / ch32rv 双方で同一)。WCH OpenOCD は flash 先頭 48 B を書き換えて戻さない | コア `harness-probe` §2.3 / ライタ `0006` §7 |
| P4 | **クロックツリーを probe 経由で検証できない**(P3 の帰結)。sketch 側で読んで戻している | コア `harness-probe` §2.3 |
| P5 | **1 往復が遅い**。UART bridge で PING→PONG 約 5 秒、handshake 12 秒。DMI 1 往復 471 µs | コア / **ライタ `0006` §3(実測)** |
| P6 | **プロセス起動が支配的**。1 レジスタ読むごとに 1 プロセスで、1 board 30〜130 秒 | コア `harness-testing` §3.1 |
| P7 | **probe が固まる**。16.7 KB の書込で無応答化し、USB 抜き差ししか復旧手段が無い → 無人運用が成立しない | コア `harness-probe` §2.3 |
| P8 | **probe firmware の版が自分の手に無い**。2.11 では走らない/2.12 で正常、という差を見えなくしていた(※ ライタが Linux/macOS 更新を実装して**根拠は半分解消**) | コア H-126 / **ライタ `0006` §12(訂正)** |
| P9 | **USB port が足りない**。WSL の `vhci_hcd` が 8 port、probe 6 台で既に窮屈。LA を別 device で足すと即詰まる | コア `harness-probe` §5-6d |
| P10 | **配線されていない試験が静かに緑になる**。「配線は利用者の責任」が「何も試験していない」に化ける | コア `harness-testing` §8 |
| P11 | **host からバス応答を返す形は原理的に成立しない**。SPI と UART は待たせる手段が無く、I2C も clock stretch で 20 倍遅い相手になる | コア `harness-testing` §9.2 / ベンチ §0 |
| P12 | **制御チャネルが DUT の USART を潰す**。V003 は USART が 1 本しかなく、掴んだ瞬間その USART は試験対象から外れる | コア `harness-testing` §4.2 |
| P13 | **debug 出力の 3 経路がツールに縛られている**。SDI は LinkE 専用、RTT は ELF が要る、DMDATA は minichlink 専用 | コア `harness-probe` §2.4 |
| P14 | **線層が未検証のまま**。SWIO のパルス幅閾値・RVSWD の STOP 波形が `attested` 止まりで、自作 probe の phy を書く根拠が弱い | ここ `link-to-target` §3 |
| P15 | **host で通るのに実機で落ちる**を波形以外で説明できない | ベンチ `DEVICE_IF_SCOPE`(物理層を範囲外と明示) |

## 3. 機能 → どの不満を消すか

**機能は 7 つに畳める。** 個々の要求(`H-nnn`)はこの 7 つのどこかに属する。

| # | 機能 | 中身 | 消す不満 |
|---|---|---|---|
| **F0** | **共通 protocol(標準の口)** | transport 非依存の byte protocol。**どの MCU の firmware でも、どの host tool からも同じ口で使える** | **P0b / P0c**(= CMSIS-DAP が ARM で解いた問題) |
| **F1** | **観測(capture)** | 並列デジタルキャプチャ + trigger + 保存形式 + provenance | P1 / P14 / P15 |
| **F2** | **相手役(emulation)** | I2C / SPI / UART の slave を演じる。器(register file / block device / byte stream)+ 模型 | P2 / P11 |
| **F3** | **刺激と障害注入(stimulus)** | GPIO エッジ・既知電圧・NACK・clock stretch・bus stuck・framing error・電源断 | P2 |
| **F4** | **debug 線(DMI)** | 書込・halt・レジスタ・メモリ・print 3 経路。**書かない attach** | **P0a** / P3 / P4 / P13 / P0e |
| **F5** | **アナログ** | VDD・電流の観測、疑似 DAC 出力 | P2(既知電圧)/ 低消費電力 |
| **F6** | **1 つの時間軸** | 上の全部を同じ時計で刻み、原点を一意に決める | P1 / P15 |
| **F7** | **運用と transport**(session / IP / 識別 / 排他 / 復旧 / カバレッジ) | セッション、**IP transport**、UID、lock、watchdog、self-test、実行したものの報告 | **P0d** / P5 / P6 / P7 / P8 / P9 / P10 / P12 |

**上端では F6 が本体**。F1〜F5 は個別に既存品があるが(LA・治具・LinkE・電流計)、**同じ時計に載っていないので人間が繋いでいる**。

**下端では F0 が本体**。L0 に必要なのは新しい hardware ではなく、**firmware がどれでも同じ host から使える口**([generic-probe-design](generic-probe-design.ja.md) §8 の優先度 1 が「汎用 probe protocol 仕様」なのはこの理由)。**F0 が無いと L0 が普及せず、L0 が普及しないと L5 以上を焼く土台が無い。**

## 4. 統合されていることからしか出ない機能

**この 5 つが「なぜ既存品の組み合わせでは駄目か」の答え**。相反を裁くときの判断基準にする。

| # | 機能 | なぜ統合が要るか |
|---|---|---|
| **U1** | **意味イベントが推定でなく確定値** | 相手役を自分で演じているので、返した byte を知っている。**波形が粗くても byte 列は正しい** |
| **U2** | **cross-domain trigger** | 「I2C の reg 0x6B に 0x80 が書かれた瞬間に DMI で core を halt」。**バス事象を breakpoint にできる**。周辺事象に watchpoint が無い core で効く |
| **U3** | **原点を marker pin 無しで作れる** | DMI 側と相手役側の両方を持っているので、**GPIO が 6 本の V003 SOP8 でも測定開始点が決まる** |
| **U4** | **自己観測** | 自分が出した debug 線を自分で見る。「線に何も出していない」を**証拠として出せる**(P3)。同じ仕掛けで P14 も埋まる |
| **U5** | **比率測定** | DUT の MCO を同じ窓で撮れば、時間を「DUT の実コア clock 何周期分」で測れる。**HSI ±1% と probe の水晶 ±30 ppm を式から消せる** |

## 5. ユースケースの梯子(小 → 大)と、幅の線引き

**段階で並べる。** 「誰が」ではなく「**どこまで欲しいか**」で並べると、必要な機能・必要な hardware・作る順序が一意に決まる。

各段の `パターン` は [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md) §1 の記号(**W** 書込 / **M** monitor / **WM** 複数書込 / **L** LA / **WL** 書込+LA / **WLE** harness)。

### 5.1 梯子

| 段 | 欲しいこと | 今の壁 | 要る機能 | **要る hardware** | パターン |
|:--:|---|---|---|---|:--:|
| **L0** | **とにかく書きたい。専用機を買わずに** | P0a/P0b/P0c。**標準 protocol が無い** | F4 の最小(DMI read/write + flash) | **手持ちの何でも**。Uno / Nano / ESP32 DevKit / Pico / 別の CH32。**既存 USB-serial bridge 上の UART なら USB descriptor も PID も要らない**([ecosystem](ecosystem-any-hardware.ja.md) §4.5) | **W** |
| **L1** | 書けて、**printf が見えれば十分** | P0e / P13。Arduino 体験の最小単位が成立しない | + monitor(SDI / DMDATA / RTT を probe が native に) | 同上 | **W+M** |
| **L2** | **離れた所に置きたい** | P0d。LinkW は無線化のためだけに 2 台 | + IP transport(TCP / WebSocket) | **Wi-Fi を持つ board 1 枚**(ESP32 系 / Pico W)。**PC 側の Wi-Fi を使うので dongle が要らない** | **W** over IP |
| **L3** | **何台も同時に書きたい** | 人数分の probe が要る | + lane 複線化、power 制御 | ピンのある board(Pico 級) | **WM** |
| **L4** | **止めて中を見たい**(halt / step / GDB) | WCH OpenOCD 固定。`arduino-cli debug` が繋がらない | + DMI 完全 + host 側 GDB server | 同 L0 | **W** + GDB |
| **L5** | **波形が見たい**(単体ロジアナとして) | 安い LA は持っていても**時間軸が別**。買い足す判断が要る | **F1** + SUMP/sigrok 互換 | **RP2040 級**(PIO + RAM)。**target 不要で今日成立** | **L** |
| **L6** | **書きながら見たい** | 上の 2 つが別装置で、人間が時刻を繋いでいる | F1 + F4 + **F6**(時刻注入) | RP2040 級 | **WL** |
| **L7** | **相手役が欲しい** | センサ / SD が手元に無い。故障を再現できない | **F2** + F3 の一部 | RP2040 級 | WLE の一部 |
| **L8** | **計測器として使いたい** | 障害注入・電流・**バス事象で halt** が既存品の組合せでは作れない | F1〜F6 全部 + **U1〜U5** | RP2040 級 + アナログ配線 | **WLE** |
| **L9** | **無人で回したい**(board farm / 量産治具) | probe が固まる(P7)、USB port 不足(P9)、静かな skip(P10) | + F7 の運用面全部 | 複数台 + runner | WLE + 運用 |
| **L10** | **線そのものを解読したい** | SWIO/RVSWD が `attested` 止まり(P14) | F1 + **U4**(自己観測) | RP2040 級 | L / WL |

### 5.2 各段の具体例

| 段 | 例 |
|:--:|---|
| **L0** | 手元の Pico で V003 に書く / 教室で先生の 1 台から全員分を焼く / **既に書けた board が次の board の probe になる**(連鎖 bootstrap)/ Uno しか無い人が 5V の V003 を叩く |
| **L1** | upload → 動く → `Serial.println` が同じ USB で見える / `SerialSDI` が LinkE 以外でも使える / V003 で **USART を潰さずに** 内部状態を見る |
| **L2** | 別室のベンチの board を焼く / 現場に据えた機器を更新する / **教室で生徒の board を先生の PC から** / ブラウザ(WebSocket)から driver レスで焼く |
| **L3** | 教室で 10 台 / 小ロットの初期書込 / 同じ試験を複数 series で並列に |
| **L4** | breakpoint を置いて変数を見る / `arduino-cli debug` が繋がる / 起動直後で止める |
| **L5** | I2C が動かないので波形を見る / **PulseView で開く** / 「I2C とは何か」を教材で見せる |
| **L6** | 書込直後の初期化シーケンスを見る / LinkE が attach で線に何を出しているか(P3 の root cause) |
| **L7** | センサが無くても I2C センサ相手のコードを書く / SD が無くても FAT を試す / **時々 NACK するセンサ**を再現する / SPI ディスプレイを繋がずに描画内容を見る |
| **L8** | **バス事象で core を halt** してレジスタを見る / 転送と電流を重ねて低消費電力を測る / clock を上げて**壊れる境界**を探す / **MCO 併走で HSI トリムを分離**(U5) |
| **L9** | 夜間 CI / カバレッジ報告つきの回帰 / 量産治具として carrier に直付け |
| **L10** | **SWIO のパルス幅閾値**を自己観測で確定 / **RVSWD の STOP 波形**を `verified` に |

### 5.3 幅の線引き(提案)

| 区分 | 段 | 理由 |
|---|---|---|
| **中核(この道具の定義)** | **L0 〜 L8** | 下端は「手持ちで書ける」、上端は「1 台の target を 1 つの時計で扱う」。**L0 が普及しないと L5 以上は誰も使わない**ので、両端を切り離さない |
| **当初対応外(設計では塞がないが、最初はやらない)** | **L9** | **device の話ではなく運用の話**(runner・farm・排他・無人復旧)。装置を増やす方向で、装置を深める方向ではない。L8 が固まってから |
| 同上 | **L3 の大規模**(数十台) | L3 の 2〜4 lane までは中核。それ以上は L9 と同じ運用問題になる |
| **中核だが優先度は別** | **L10** | 機能としては **L5 + U4** で足りる(新規の機能が要らない)。**この repo の本業**なので順序は別に決める |
| **やらない(永久に非目標)** | §6 の N1〜N7 | 下記 |

**線を引く根拠**: **L0〜L8 は「1 台の probe と 1 台の target」で閉じる**。L9 だけが「複数の装置と runner」を要求し、要求の性質が変わる(F7 の運用面が支配的になる)。**閉じる/閉じないが自然な切れ目**。

### 5.4 幅は hardware の境界も跨ぐ

**L0〜L4 と L5 以上の間に hardware の段差がある。**

| | L0〜L4 | L5 以上 |
|---|---|---|
| 要る board | **ほぼ何でも**(AVR 級でも protocol 上は成立) | **RP2040 級**(PIO の決定論 + RAM + DMA) |
| transport | 既存 USB-serial bridge 上の UART で足りる → **USB descriptor も PID も不要** | 帯域が要るので native USB(自前 descriptor → PID が要る) |
| 価値 | **「買わなくていい」** | **「既存品の組合せでは作れない」** |

→ **同じ protocol で両方を覆うが、同じビルドではない。** これが §1 の「1 つの製品ではない」の実体で、**caps で申告して host が機能を出し分ける**([dmi-bridge](../protocols/dmi-bridge.ja.md) 設計原則 2)ことで 1 本の host が両方を扱える。

### 5.5 段ごとの ⬜ の分布

| 段 | 今できるか |
|:--:|---|
| L0 | 🔧 **firmware は複数あるが、host が固定**(P0c)。**標準 protocol があれば今日成立する** |
| L1 | 🔧 monitor が probe / tool に縛られている |
| L2 | 🔧 WebLink 等はあるが V003 限定・未完成 |
| L3 | ⬜ |
| L4 | 🔧 probe 上 GDB(Swindle/PicoRVD)はあるが board 固定 |
| L5〜L8 | **⬜ 全部** |
| L9 | ⬜ |
| L10 | ⬜ |

**L0〜L4 は「実装が無い」のではなく「揃っていない」。L5 以上は「実装が無い」。** 作る順序の議論はこの差を踏まえる。

### 5.6 ch32rv だけで登れる段と、その限界

**梯子の下の方は、firmware を 1 行も書かずに登れる。** host(ch32rv)が**既にある probe の口を全部喋る**ようにすれば、**世の中に既にある firmware がそのまま使えるようになる**。これは P0c(10 種以上あるのに横展開できない)への、**dmi-bridge とは別の解**。

#### 成立する理由

**[dmi-bridge 設計原則 1「probe は chip を知らない」の裏返し**。chip 知識(family 別 FLASH 手順・stub・DM 操作)が**全部 host 側にある**なら、**口さえ合えば chip 対応は host だけで完結する**。probe は `(addr7, data32, op2)` を運ぶ土管でよく、**その土管の形が何であれ host が合わせられる**。

ch32rv には既にその継ぎ目がある — **`DtmAccess` trait**。`ch32rv-probe-<name>` を **P2 で予約済み**で、想定候補として **funprog HID / NHC-Link042 / ardulink** が名指しされている(`docs/architecture.ja.md`)。**設計はもう入っている。**

#### 今日繋がる候補([probe-ecosystem](probe-ecosystem.ja.md) §1 より)

| 既存の口 | 繋がる board | 難易度 | 効果 |
|---|---|:--:|---|
| **ardulink**(UART 6 byte: `w reg d[4]` / `r reg` / `?` / `p`/`P`) | **AVR Arduino 全般**(Uno / Nano) | **最小** | **「手持ちの Uno で書ける」が成立**。5V の V003 に直結できる唯一の道 |
| **minichlink funprog HID** | ESP32-S2 | 小 | HID なので driver レス |
| **rvswdio_programmer**(low-speed HID) | **CH32V003 1 個** | 小 | **$0.1 の chip が probe になる**。1/2 線自動判別つき |
| **NHC-Link042**(USB vendor bulk) | STM32F042 board | 小 | 既存 board の再利用 |
| **B003 HID scratchpad**(`1209:B003`) | V003 / X035 の BL | 小 | **この repo に byte 単位の仕様がある**([custom-bootloader](../protocols/custom-bootloader.ja.md) §2b) |
| WCH-Link USB | LinkE / LinkW / CH549 / LinkS | **実装済** | (現状) |

→ **上 5 つを足すだけで、L0 の「手持ちの board」の対象が AVR / ESP32-S2 / STM32F042 / V003 単体まで一気に広がる。** 新しい firmware は要らない。

#### 限界 — どこで止まるか

**判定規則は 1 行**:

> **その機能が probe 側に「時刻・状態・並列性・自発送信」を要求するなら、host だけでは解けない。**

原則 1 が host に渡したのは **chip 知識**だけで、この 4 つは渡していない。だから境界がここに落ちる。

| # | 限界 | 何が起きるか | 効く段 |
|---|---|---|---|
| **1** | **能力が既存 protocol の下限に張り付く** | ardulink は `w`/`r` だけで **batch が無い**。64 KB を per-op で書くと **UART 115200 で約 5 分**([generic-probe-design](generic-probe-design.ja.md) §6)。**書けるが遅い** | L0 は成立、実用速度は別 |
| **2** | **`caps` が無いので host が型番の表を持つ** | 「この probe は何ができるか」を **host 側の手書き表**で持つことになる。ch32rv は既に `capabilities --json` でこれを抱えていて、**backend が増えた分だけ保守が増える**。[ecosystem](ecosystem-any-hardware.ja.md) §4.2b の「host が VID:PID の表を持つことになる」と同型 | 全段(保守コスト) |
| **3** | **event(probe → host の自発送信)が無い** | 既存の口は全部 request/response。**target UART の push、capture stream、autopoll が乗らない** | L1 の一部 / L5 以上 |
| **4** | **時間軸が無い。後付けできない** | `w reg data` に時刻は入らない。**F6(1 つの時計)が原理的に構成できない** | **L5〜L8 が構造的に不可** |
| **5** | **lane の概念が無い** | どれも 1 target 前提 | **L3 不可** |
| **6** | **対象 chip が V003 に偏る** | 2 線(RVSWD)を主張しているのは **rvswdio と funprog だけ**。他は V003 / 1 線 | L0 の**適用範囲**が狭い |
| **7** | **firmware の保守が他人の手にある** | 成熟度は「very alpha」「experimental/RFC」「WIP/不安定」、**license も混在**(Swindle は GPL-3 系)。**probe 側のバグを ch32rv が直せない** | 全段(信頼性) |
| **8** | **識別・排他が揃わない** | serial を持たない個体がある → **fail-closed の識別規則(H-001/006)を一律に当てられない** | L3 / L9 |

#### 梯子との対応

| 段 | ch32rv 単独で | 理由 |
|:--:|:--:|---|
| **L0** 書きたい | **◎ 届く** | chip 知識は host 側。口を足すだけ |
| **L1** printf | **○ 大半届く** | **`SerialDMDATA` は DM の DATA0/1 を DMI で polling するだけ**なので **probe 非依存**(ライタ `0006` §13)。push 型の monitor だけ限界 3 に当たる |
| **L2** リモート | **✕** | **transport は probe 側**。IP を喋る firmware が要る |
| **L3** 複数 | **✕** | 限界 5 |
| **L4** GDB | **○ 届く** | **GDB server は host 側**に置ける([generic-probe-design](generic-probe-design.ja.md) §8-7) |
| **L5〜L8** | **✕ 構造的に不可** | 限界 4(時間軸)。速さの問題ではない |
| L9 / L10 | ✕ | 上の帰結 |

#### 結論 — 競合ではなく順序

| | ch32rv の multi-backend | dmi-bridge |
|---|---|---|
| 買うもの | **幅**(今日、既にある firmware が使える) | **深さ**(明日、上の段に登れる) |
| 対象 | **L0 / L1 / L4** | **L2 以上**(と、L0 の速度と保守性) |
| コスト | backend ごとの実装 + **限界 2 の表の保守** | firmware を書く。普及に時間がかかる |
| 関係 | — | **dmibridge は ch32rv の backend の 1 つになる**(`ch32rv-probe-dmibridge`) |

**両者は並行して置ける。優先度は未定**(どちらを先に出すかは決まっていない)。確かなのは 2 点だけ:

- **L0 の幅を今日広げたいなら ch32rv 側の作業のほうが速い**(firmware が要らない)。
- **それは dmi-bridge の価値を下げない** — **上の段は dmi-bridge でしか登れない**ことが限界 4 で確定しているから。

⚠ **ただし限界 2 は先に効く。** backend を増やすほど「probe × できること」の手書き表が育つ。**`caps` を持つ backend(dmibridge)が 1 つあると、その表が「申告に従う」1 行で済む**ので、**増やす前に caps の形を決めておく**ほうが後で楽になる。→ これは §5.7 の PID の話とも同じ結論になる。

### 5.7 USB と PID — **最も希少な資源からの逆算**

**USB に対応したい理由は「便利さ」**。挿すだけで driver レスに使える(CDC / HID / WinUSB はどれも class driver で VID を問わない)。だが**自前 descriptor を名乗る = PID が要る**。

> **本節は結論を出さない。** PID は幅の議論に**制約として効く**ので、**動かせない事実**と**選択の軸**だけを並べる。**どこまでの幅を取るかが決まってから**、この軸の上で選ぶ。

#### 動かせない事実

| # | 事実 | 出どころ |
|---|---|---|
| **K1** | **pid.codes は「1 project 1 PID が原則」**(複数は理由付きで) | [ecosystem](ecosystem-any-hardware.ja.md) §4.2b |
| **K2** | **WCH には vendor community program が無い**。Raspberry Pi `0x2E8A` / Espressif `0x303A` は**その silicon 上でのみ**無償・公認 | 同 §4.2b |
| **K3** | **他人の PID を自分の firmware が名乗るのは NG**。同じ ID を別の device が使うと host が判別できなくなる — **この repo に実例がある**(LinkE の IAP mode と factory ISP がどちらも `4348:55E0`)。**共有 ID(`0x1209:0x0001`〜、`0x6666`、`0xCAFE`)も配布物では同じ理由で不可** | 同 §4.1 / §4.2b |
| **K4** | **Windows は VID:PID(+MI_xx)単位で driver 割当を cache する**。同じ ID で **interface 構成**を変えると壊れる | 同 §4.3 |
| **K5** | **低速 USB は control と interrupt しか持たない**(bulk が無い)。→ **V003 の software USB では CDC が成立せず、HID しか選べない** | [software-usb](../protocols/software-usb.ja.md) / B003 が HID である理由 |
| **K6** | **既存 USB-serial bridge 上の UART と IP は、自前 descriptor を持たないので PID を消費しない** | [ecosystem](ecosystem-any-hardware.ja.md) §4.5 |

> **K3 の帰結(切り分け)**: **host が他人の protocol を喋るのは自由**(host は誰の ID も名乗らない)。**禁じられるのは、自分の firmware が他人の PID を名乗ること**。→ 「dmibridge の **host** が B003 protocol を喋る」は問題なし。「dmibridge の **firmware** が `1209:B003` を名乗る」は不可。

#### 選択の軸(**決めない**)

| 軸 | 選択肢 | 得るもの | 失うもの |
|---|---|---|---|
| **A. CH32 上で自前 USB を出すか** | **A1 出す** | V003 単体 probe / X035 の driver レス | **pid.codes の 1 個を消費**(K2 より他に手が無い) |
| | **A2 出さない**(UART / IP に逃がす) | **pid.codes を消費しない** | 「挿すだけ」の体験を CH32 build で出せない |
| **B. V003(low-speed)を USB-native の対象に含めるか** | **B1 含める** | **$0.1 の chip が単体 probe**。連鎖 bootstrap の下端 | K5 より **descriptor が HID 固定**になり、その ID では帯域を上げられない |
| | **B2 含めない**(**V003 を諦める**) | descriptor に **CDC / bulk / composite** が選べる。**帯域が出せる** | 最も安い入口を失う。UIAPduino 系の資産も外れる |
| **C. 1 個の PID で mode(BL / app / probe)を跨ぐか** | **C1 跨ぐ** | PID 1 個で済む。[設計原則 4](../protocols/dmi-bridge.ja.md)(正体は handshake)と整合 | K4 より **descriptor を永久固定**。「enumeration だけで mode が分かる」を失う |
| | **C2 跨がない** | mode が enumeration で分かる(§4.3 の利点) | **PID が複数要る** → K1 と衝突。**複数申請 / 別 project として申請 / vendor program** のどれかが要る |
| **D. 高帯域(capture)をどう出すか** | **D1 自前 USB bulk** | 速い | PID が要る(**silicon が RP2040 / S3 なら vendor program で 0**) |
| | **D2 既存 bridge の UART** | **0** | 遅い |
| | **D3 IP** | **0**。遠隔も同時に得る | Wi-Fi 機に限る |

#### 軸を組むと出てくる案(**並べるだけ。選ばない**)

| 案 | A | B | C | D | pid.codes の消費 | 幅への影響 |
|---|:--:|:--:|:--:|:--:|:--:|---|
| **P-1** | A1 | B1 | C1 | D1(vendor program) | **1** | 全段。ただし CH32 build は HID 固定なので**その ID では L5 以上を出せない** |
| **P-2** | A1 | **B2** | C1 | D1 | **1** | **V003 を諦める**代わりに CH32 build も CDC/bulk が使え、**同じ ID で上の段まで出せる** |
| **P-3** | **A2** | — | — | D2 / D3 | **0** | **pid.codes を一切使わない**。CH32 は UART / ardulink 経由。RP2040 / S3 は vendor program |
| **P-4** | A1 | B1 | **C2** | D1 | **複数** | K1 と衝突。**複数申請の交渉が要る**(BL と probe を別 project と主張しうる) |

**どれも幅を狭める判断を含む**。P-1 は「CH32 の自前 USB を L0/L1 に限る」、P-2 は「V003 を切る」、P-3 は「挿すだけの体験を捨てる」、P-4 は「交渉に賭ける」。**幅をどこまで取るかが決まる前に選ぶと、幅の方が PID に引きずられる。**

#### 決めるために要る情報

| # | 要る情報 | 効く軸 |
|---|---|---|
| 1 | **pid.codes に複数申請が実際どこまで通るか**(前例を調べる) | C / P-4 |
| 2 | **V003 単体 probe の需要はどれくらいか**。UIAPduino 以外に配線した board があるか | B |
| 3 | **「挿すだけ(driver レス)」と「UART で 1 手間」の体験差**が、L0 の普及にどれだけ効くか | A |
| 4 | RP2040 / ESP32-S3 の vendor program 申請の実際の手間 | D |
| 5 | **CH32 build に L5 以上を求めるか**(求めないなら B1 の代償が小さくなる) | B / D |

**5 が幅の議論そのもの。** ここが決まらないうちは PID も決まらない。

#### L0 の低減策 — Core より下の profile を置く案(**採否は未定**)

[dmi-bridge §8.1](../protocols/dmi-bridge.ja.md) の最小は **Core**(`hello` `caps` `info` `ping` / `lane_attach` `lane_detach` `line_reset` / `dmi_read` `dmi_write` / `batch` 8 op 以上)。**これでも V003 の BL には入らない**(BL は `FLASH 1,916 B` + secret 4 B で**既に埋まっている**。[v003-bootloader-replacement](v003-bootloader-replacement.ja.md) §2)。

→ **Core の下にもう 1 段**(仮に **Nano**)を置く案。**「幅の下端をどこに取るか」が決まってから採否を決める**(下端に V003 の BL や 8 bit 級を含めないなら、この段は要らない):

| | Nano(案) | Core |
|---|---|---|
| コマンド | **`hello` / `dmi_read` / `dmi_write` のみ** | + `caps` `info` `ping` `lane_*` `batch` |
| `caps` | **固定の最小応答**(数個の TLV を定数で返す) | TLV で申告 |
| lane | **0 固定**(ヘッダの `lane` は無視) | 0..N |
| batch | **無し**(per-op。遅い) | 8 op 以上 |
| 想定 | **下端に含めるなら**: V003 の BL、AVR、8 bit 級 | 通常の probe |

**ヘッダ(`type` / `lane` / `tag` / `cmd`)と L1 framing は変えない。** そうすれば **同じ host コードが Nano も Core も扱える**(`hello` の応答で見分ける)。

#### V003 の BL に載せるか — 2 方向あり、どちらも未定

**制約**: V003 の BL は **1,920 B に対して実サイズがほぼ 1,920 B** で、**新しい protocol を足す余地はほぼ無い**([v003-bootloader-replacement](v003-bootloader-replacement.ja.md) §2)。

| 方向 | 中身 | 得失 |
|---|---|---|
| **(i) protocol を BL に入れる** | Nano profile を BL に実装 | BL が dmibridge を喋る。**ただし 1,920 B に入るかは未検証**(→ `bl-size-baseline`)。入れるには何かを削る |
| **(ii) protocol が既存実装に歩み寄る** | **host 側**が B003 の HID scratchpad protocol を喋る([custom-bootloader](../protocols/custom-bootloader.ja.md) §2b、**byte 単位で解読済み**)。[dmi-bridge §7](../protocols/dmi-bridge.ja.md) の **ardulink 互換モード**と同じ発想 | **BL を触らない**。§5.6 の「ch32rv が口を増やす」と同じ性質の作業 |

⚠ **(ii) は host 側の互換モードであって、PID の話ではない。** **K3 のとおり、自分の firmware が `1209:B003` を名乗ることは不可**(それは他人の project の ID)。**host が B003 を喋るのは自由**(host は誰の ID も名乗らない)。前版でここを混同していた。

**どちらを採るか、そもそも V003 を下端に含めるかは未定**(§5.7 の軸 B)。

#### `caps` は 2 つの問題を同時に解く

| 問題 | `caps` がどう効くか |
|---|---|
| **P0g** どの probe が何に対応しているか分からない | **個体が自分で申告する**。`ch32rv probe info` が「この個体は何ができるか」を出す。**型番の表を host が持たなくてよい**(§5.6 の限界 2) |
| **PID の希少性** | **役割・board・段を PID で区別しなくてよくなる**([builtin-probe-and-self-update](builtin-probe-and-self-update.ja.md) §2.2)。ただし **§5.7 の軸 C(mode を跨ぐか)は別問題**で、`caps` があっても K4(Windows の cache)は消えない |

**同じ 1 つの仕組みが、ユーザ体験(何ができるか分かる)と資源制約(PID を増やさない)の両方に効く。** **幅をどこに取っても `caps` は要る**ので、ここは幅の議論と独立に固められる数少ない部分。

## 6. やらないこと(永久の非目標)

**当初対応外**(後で足しうるもの)は §5.3。ここは**設計としてやらないと決めるもの**で、相反の裁定でここを越える要求は落とす。

| # | 非目標 | 理由 |
|---|---|---|
| N1 | **連続ストリーミングのロジアナ** | USB FS で 16ch ~500 kSa/s が上限。コアの方法4 は全部「短い窓」なので burst で足る。連続が要るなら FX2LP を併用 |
| N2 | **サイクル精度のシミュレータ** | 実機を測る道具。ベンチ `DEVICE_IF_SCOPE` が範囲外と明示している側 |
| N3 | **LinkE の互換品を作ること** | 目標は **LinkE が要らない場面を増やす**ことであって、LinkE の代替品を名乗ることではない。USB protocol の互換も VID の詐称もしない([ecosystem](ecosystem-any-hardware.ja.md) §4.1)。コアのリリース経路では**当面 LinkE が焼き、harness が観る**(認定 probe を残したまま足すのが最も安全) |
| N4 | **同梱アップローダ** | 1.0 前は不可。同梱は ch32rv に一本化(コア `ch32rv-requests`) |
| N5 | **全 24 series を書ける書込器** | 線層は RVSWD が `attested` 止まり。**書込 probe としての完成は遠い**(だから ⑤ 5.5 が先に効く) |
| N6 | **host からバス転送を返す形** | P11。SPI / UART は原理的に不可 |
| N7 | **chip 固有知識を firmware に入れる** | `dmibridge` の設計原則 1。**ただし「演じるデバイスの知識」は別**(器と模型の線引き = C-1) |

## 7. 一言で言える価値(段ごとに違う)

| 段 | 一言 |
|---|---|
| **L0〜L2** | **買わなくていい。** 手持ちの board が書込器になり、Wi-Fi 付きなら 1 枚でリモートになる。**実装ではなく標準の口が足りていないだけ**なので、最も安く最も多くの人に届く |
| 全段 | **分散した情報を 1 か所に刈り取る。** いま知識は 10 以上の project に散り、**LinkE を持っている人しか到達できない**(P0f)。**「どの probe が何をできるか」を個体が自分で申告する**(P0g / `caps`)ので、買う前・焼く前に判定できる |
| **L5〜L8** | **既存品の組み合わせでは作れない機能が 5 つある**(U1〜U5)。統合の対価はここで回収される |
| 共通 | **観測と刺激だけなら、リリース経路にリスクをゼロで足せる**(LinkE を残したまま harness を観測専用で入れる) |

**順序の含意**: 価値の**幅**は L0 が最大、価値の**深さ**は L8 が最大。**⬜ の数は L5 以上に集中する**が、**人数は L0 に集中する**。どちらを先に出すかは、この 2 つのどちらを取るかの選択になる。

## 8. 定義が決まると片付く相反

第 2 部の入口として、**§4 の U1〜U5 と §6 の N1〜N7 を基準にすると、13 件の相反のうち何が自動的に決まるか**を並べておく。**裁定はしない**。

| 相反 | 定義から言えること |
|---|---|
| **C-1**(模型の置き場) | **N6 + U1 から probe 側**。器と模型の線引きだけが残る(ベンチ §3 が「両立する」) |
| **C-2**(multi-lane vs 1 target 専有) | **§5.3 で L3(2〜4 lane)は中核、それ以上は L9 と同じ運用問題**と線を引いた。→ **排他ではなく別ビルド**。上端の定義が「1 台の target を専有」なだけで、下端の L3 は覆う |
| **C-4**(capture 帯域 vs control) | **N1 を受け入れるなら**、capture は落ちてよい側 |
| **C-6**(debug 線を窓に入れる) | **U4 が定義に入っているなら入れる**。§5.5 の 6 件が U4 に依存 |
| **C-12**(DMI をどの段階で要求するか) | **U2 / U3 / U4 が全部 DMI を要る**。ただし §5.1 の 1〜12 は DMI 無しでも動く → **段階を切る根拠は定義側にある** |
| C-3 / C-5 / C-7 / C-9 / C-10 / C-11 / C-13 | 定義からは決まらない。**第 2 部で裁く** |

## 9. 参照

- 議論の所在: [harness-index.ja.md](harness-index.ja.md)
- 構想の本体・ピン割当・family 衝突表: [dut-harness-design.ja.md](dut-harness-design.ja.md)
- board 別の到達範囲: [harness-board-survey.ja.md](harness-board-survey.ja.md)
- パターン共存(書込のみ / 複数 / +LA): [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md)
- protocol の土台: [../protocols/dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md)
- 埋めたい穴(P14): [../protocols/link-to-target.ja.md](../protocols/link-to-target.ja.md) §3
- 要求の正本: コア `docs/harness-requirements.ja.md`(`H-nnn` / `C-n`)/ ベンチ `docs/HARNESS_REQUESTS.ja.md`(`B-nnn`)/ ライタ `docs/data-requests/0006-harness-integration.ja.md`
