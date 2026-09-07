# harness — 各論の選択肢集(**結論を出さない**)

状態: **選択肢の整理**。**決めない。** 方向性が決まってから、この上で選ぶ。
基準日: 2026-09-07

**上位文書**: [harness-tool-definition.ja.md](harness-tool-definition.ja.md)(何の道具か・強み・方向性)。**順序を逆にしない** — 方向性が先で、ここは後。

議論の所在は [harness-index.ja.md](harness-index.ja.md)。

## この文書に入るもの / 入らないもの

| 入る | 入らない |
|---|---|
| 選択の軸と、軸ごとの得失 | どれを選ぶか |
| 動かせない制約(実測・仕様・規約) | 要求の一覧(各 repo が正本) |
| 「決めるために要る情報」 | byte レベルの仕様([dmi-bridge](../protocols/dmi-bridge.ja.md) が正本) |

---

## 1. 既存 protocol への対応で、どこまで登れるか

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

⚠ **ただし限界 2 は先に効く。** backend を増やすほど「probe × できること」の手書き表が育つ。**`caps` を持つ backend(dmibridge)が 1 つあると、その表が「申告に従う」1 行で済む**ので、**増やす前に caps の形を決めておく**ほうが後で楽になる。→ これは §2 の PID の話とも同じ結論になる。

## 2. USB と PID — 最も希少な資源

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

## 3. profile の下端

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

## 4. V003 の BL に載せるか

#### 方向 — 2 方向あり、どちらも未定

**制約**: V003 の BL は **1,920 B に対して実サイズがほぼ 1,920 B** で、**新しい protocol を足す余地はほぼ無い**([v003-bootloader-replacement](v003-bootloader-replacement.ja.md) §2)。

| 方向 | 中身 | 得失 |
|---|---|---|
| **(i) protocol を BL に入れる** | Nano profile を BL に実装 | BL が dmibridge を喋る。**ただし 1,920 B に入るかは未検証**(→ `bl-size-baseline`)。入れるには何かを削る |
| **(ii) protocol が既存実装に歩み寄る** | **host 側**が B003 の HID scratchpad protocol を喋る([custom-bootloader](../protocols/custom-bootloader.ja.md) §2b、**byte 単位で解読済み**)。[dmi-bridge §7](../protocols/dmi-bridge.ja.md) の **ardulink 互換モード**と同じ発想 | **BL を触らない**。§1 の「ch32rv が口を増やす」と同じ性質の作業 |

⚠ **(ii) は host 側の互換モードであって、PID の話ではない。** **K3 のとおり、自分の firmware が `1209:B003` を名乗ることは不可**(それは他人の project の ID)。**host が B003 を喋るのは自由**(host は誰の ID も名乗らない)。前版でここを混同していた。

**どちらを採るか、そもそも V003 を下端に含めるかは未定**(§2 の軸 B)。

## 5. `caps` — 幅と独立に固められる部分

| 問題 | `caps` がどう効くか |
|---|---|
| **P0g** どの probe が何に対応しているか分からない | **個体が自分で申告する**。`ch32rv probe info` が「この個体は何ができるか」を出す。**型番の表を host が持たなくてよい**(§1 の限界 2) |
| **PID の希少性** | **役割・board・段を PID で区別しなくてよくなる**([builtin-probe-and-self-update](builtin-probe-and-self-update.ja.md) §2.2)。ただし **§2 の軸 C(mode を跨ぐか)は別問題**で、`caps` があっても K4(Windows の cache)は消えない |

**同じ 1 つの仕組みが、ユーザ体験(何ができるか分かる)と資源制約(PID を増やさない)の両方に効く。** **幅をどこに取っても `caps` は要る**ので、ここは幅の議論と独立に固められる数少ない部分。


---

## 6. 参照

- **上位(何の道具か・強み・方向性)**: [harness-tool-definition.ja.md](harness-tool-definition.ja.md)
- 議論の所在: [harness-index.ja.md](harness-index.ja.md)
- protocol の土台: [../protocols/dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md)
- USB ID の一次資料: [ecosystem-any-hardware.ja.md](ecosystem-any-hardware.ja.md) §4
- 既存 probe の landscape: [probe-ecosystem.ja.md](probe-ecosystem.ja.md)
- board 別の到達範囲: [harness-board-survey.ja.md](harness-board-survey.ja.md)
- V003 BL のサイズ制約: [v003-bootloader-replacement.ja.md](v003-bootloader-replacement.ja.md)
