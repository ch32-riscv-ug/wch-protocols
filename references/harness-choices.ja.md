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

#### 何を固定しなければならないのか(**階層で答えが違う**)

「descriptor を固定する」と一言で書いてきたが、**固定しなければならないのは descriptor 全体ではない**。**Windows が何に binding を紐づけるか**で階層が分かれる。

**前提(公開仕様レベル)**: Windows は USB device の **hardware ID** で driver を選ぶ。単機能 device は `USB\VID_xxxx&PID_yyyy&REV_zzzz` と `USB\VID_xxxx&PID_yyyy`、**composite device は usbccgp(USB 複合親)が interface ごとに子を作り** `USB\VID_xxxx&PID_yyyy&MI_nn`(`nn` = **interface 番号**)を持つ。

| 階層 | 後から変えられるか | 理由 |
|---|:--:|---|
| **device が composite か単機能か** | **✗ 変えられない** | **usbccgp が載るかどうかが変わる**。hardware ID の**形**が変わり、旧 binding が残る |
| **既存 interface の番号と並び** | **✗ 固定** | **`MI_nn` は interface 番号**。ずれると**既存 driver が別 function に当たる** |
| **IAD(Interface Association)のグループ分け** | **✗ 固定** | Windows は IAD で function を切る |
| **末尾への function 追加** | **△ 比較的安全** | **既存の `MI_nn` が動かず、子が 1 つ増えるだけ**。class driver(CDC / HID)なら binding は自動 |
| 各 function の class | **✗**(既存分) | class driver の binding が変わる |
| **function 内の endpoint address / 種別** | **○ ほぼ自由** | class driver は descriptor を読み直す(class の要件を満たす限り) |
| **`bcdDevice`(REV)** | **○ むしろ上げる** | **`&REV_xxxx` が別 hardware ID になる**ので、新旧を区別でき、旧 binding と衝突しにくい |
| serial string | ○ | instance 追跡と **COM 番号の安定**に効く |

⚠ **「末尾への追加は安全」は実務上の通説**で、**実機での確認が要る**(Windows の版と、既存 binding の残り方に依存)。

#### その binding 情報は再起動では消えない

**再起動でリセットされない。** レジストリと driver store に**永続化**されるので、そこが厄介。

| 何が残るか | どこに | 消え方 |
|---|---|---|
| **install 済みの driver package** | Driver Store(`%WINDIR%\System32\DriverStore`)+ INF の順位付け | **`pnputil /delete-driver` で明示削除**するまで残る |
| **device instance の状態** | `HKLM\SYSTEM\CurrentControlSet\Enum\USB\VID_xxxx&PID_yyyy\<instance-id>` | Device Manager で**「デバイスのアンインストール」+ driver 削除** |
| **device 固有の quirk flag** | `HKLM\SYSTEM\CurrentControlSet\Control\usbflags`(**VID / PID / `bcdDevice` 単位**) | 手で削除 |
| **COM 番号の割当** | `Control\COM Name Arbiter\ComDB`(使用済み番号の bitmap)+ instance の `PortName` | **device を外しても番号が予約されたまま**。これが「COM 番号が増え続ける」の原因 |
| driver が掴んだ device の一覧 | `Services\<driver>\Enum` | driver のアンインストール |

- **`instance-id` は serial string があればそれ、無ければ port 由来のパス**になる。→ **serial を出すと「同じ device」として全 port で認識される**(COM 番号が安定する。良い面)が、**古い状態も device に付いて回る**(descriptor を変えたとき悪い面)。**serial を出さないと port ごとに別 instance**になり、開発中は偶然きれいになるが **COM 番号が増殖する**。
- **ユーザープロファイルを変えても効かない**(`HKLM` = machine 全体)。

⚠ 具体的なキー配置は Windows の版で変わるので、**ベンチで一度確認する価値がある**(→ 未確認)。

#### 実務上の逃げ道 — **`bcdDevice` を上げる**

**`usbflags` と hardware ID は `bcdDevice`(REV)を含む**ので、**版を上げると別 hardware ID として扱われ、古い binding と衝突しにくくなる**。**掃除せずに「新品」に近い状態を作れる、唯一安い手**。

| 手段 | コスト |
|---|---|
| **`bcdDevice` を上げる** | **ゼロ**。descriptor の 2 byte |
| Device Manager でアンインストール | 手作業。台数分 |
| `pnputil /delete-driver ... /uninstall /force` | 手作業。**driver package を消すので影響範囲が広い** |
| `Enum\USB\...` を直接削除 | **要 SYSTEM 権限。危険** |
| ComDB を掃除して COM 番号を回収 | Device Manager の port 詳細設定か registry |
| **別 port に挿す** | **serial を出していない device に限り**新 instance になる |

**実例**: **UIAPduino は既に `bcdDevice` を版管理している** — fork の差分で `0x0000` → **`0x0141`**、さらに commit `9e30b75`「Change bcdDevice from 1.40 to 1.41」がある。**この運用は既に前例がある。**

→ **設計への含意**: **firmware の版を必ず `bcdDevice` に載せ、descriptor に触るたびに上げる**。[ecosystem §4.2](ecosystem-any-hardware.ja.md) が「`bcdDevice` は BL/probe firmware の版」としているのと同じ運用で、**Windows の cache 対策も兼ねる**。

#### 「CDC の個数は後から変えられるか」への答え

| 変更 | 可否 |
|---|:--:|
| **CDC を末尾に 1 本足す**(既存 interface の番号を動かさない) | **△ 見込みあり**。`MI_nn` が保たれ、新しい子が増えるだけ |
| CDC を**先頭 / 中間に挿す** | **✗**(既存の `MI_nn` がずれる) |
| CDC を**減らす** | **✗ に近い**(子が消え、旧 binding が孤児になる) |
| **HID 1 本(単機能)→ CDC + HID(composite)** | **✗ 最も破壊的**。usbccgp の有無が変わる |
| **最初から composite で、使わない function を予約しておく** | **○** ← **これが答え** |

→ **CDC の個数は「最初から composite にしておき、末尾に足す」なら増やせる見込み。「単機能 → composite」は不可。**

#### 本当の問題 — **能力が違う device を同じ PID で混ぜるとき**

ここまでは「**1 台の device が時間とともに変わる**」話だった。**実運用で効くのは「能力が違う複数の device を、同じ machine に同時に挿す」方**。

**ベンチの実態がまさにそれ**: probe が 6 台あり、WSL の `vhci_hcd` は 8 port([定義 P9](harness-tool-definition.ja.md))。そこに **RP2040-Zero build(capture あり)/ Pico build(23ch)/ X03x build(capture なし)/ V003 build(low-speed HID)** が混ざりうる。

##### `caps` はこれを解決しない

| 層 | 誰が能力を名乗るか |
|---|---|
| **protocol 層** | **`caps`**。lane 数・capture unit・模型・時間軸の確度 |
| **USB 層(descriptor)** | **`caps` は届かない**。interface 構成・endpoint・class は**列挙時に OS が見る** |

→ **「役割や段で PID を分けない」([builtin-probe §2.2](builtin-probe-and-self-update.ja.md))は正しいが、それは「descriptor が同じなら」という条件つき。** **descriptor が違う build を同じ PID にすると、`caps` の手前で壊れる。**

##### 何が壊れるか

| 壊れるもの | 中身 |
|---|---|
| **driver binding** | **親の hardware ID は `USB\VID&PID`(+`&REV`)で共通**。単機能 build に対して**vendor INF が install されていると**、composite build が来たときにその binding が優先されて **usbccgp が載らない**(= composite として列挙されない)おそれ |
| **`usbflags`** | **VID / PID / `bcdDevice` 単位**の quirk flag。**同じ `bcdDevice` を使い回すと、別 build の flag が効く** |
| **ComDB / COM 番号** | build ごとに CDC の本数が違うと、**番号の予約が食い違う** |
| Linux の udev | VID:PID で書いた rule が**全 build にかかる**(過剰付与) |

⚠ **class driver(CDC / HID / MSC)だけで組む場合は危険が下がる**(binding が毎回 descriptor から決まる)。**vendor INF / WinUSB を INF で入れる形が最も危ない。** → **実機確認が要る**(Windows の版依存)。

##### serial では分離できない — **hardware ID に入るのは `REV` だけ**

**serial number は instance ID に入るが、hardware ID には入らない。** ここが分かれ目:

| ID | 中身 | driver 選択に効くか |
|---|---|:--:|
| **hardware ID** | `USB\VID_xxxx&PID_yyyy&REV_zzzz` / `USB\VID_xxxx&PID_yyyy`(+ composite の子は `&MI_nn`) | **効く** |
| **instance ID** | `USB\VID_xxxx&PID_yyyy\`**`<serial>`**(serial が無ければ port 由来のパス) | **効かない** |

→ **serial が違っても driver binding の判断は同じ。** serial が分けるのは
**(a) per-device の設定(COM 番号割当など)、(b) `Enum\USB\...\<serial>` の instance 状態、(c) 「前に見た同じ個体か」の判定**だけ。

**`usbflags` も VID + PID + `bcdDevice` 単位**で、**serial は入らない**。

> **つまり descriptor の違いを分離できるのは `bcdDevice`(REV)だけ。** serial は「同じ形の device の個体識別」であって、「違う形の device の分離」には使えない。

##### **→ `bcdDevice` を使えば V003 も同じ PID に入れられる(見込み)**

**前に「V003 を USB-native に含めると descriptor が HID 単機能に固定され、1 PID がそこで固定される」と書いたのは強すぎた。** 訂正する:

| build | descriptor | `bcdDevice`(案) | hardware ID |
|---|---|---|---|
| **V003**(software USB) | **HID 単機能、low-speed** | `0x01xx` | `USB\VID&PID&REV_01xx` |
| **RP2040 / S3** | **composite(CDC + HID + 予約)** | `0x02xx` | `USB\VID&PID&REV_02xx` |

**hardware ID が分かれるので、binding と `usbflags` も分かれる見込み。** → **1 PID で「HID 単機能の V003」と「composite の RP2040」を共存させられる可能性がある。**

**固定されるのは「同じ `bcdDevice` を共有する build 群」だけ**で、**PID 全体ではない。**

##### この手の弱点(**採る前に見ておくもの**)

| # | 弱点 | 中身 |
|---|---|---|
| **1** | **USB 仕様の趣旨から外れる** | **PID は「製品」、`bcdDevice` は「その製品の版」**。**別の形の device を版番号で区別するのは本来の使い方ではない** |
| **2** | **VID:PID だけ見るツールが区別できない** | udev rule、Arduino IDE の board 検出、sigrok、その他多数。**`bcdDevice` まで見る実装は少ない** |
| **3** | **generic な hardware ID が残る** | `USB\VID&PID`(REV 無し)も候補に並ぶ。**vendor INF をその generic ID に対して install すると全 build にかかる** → **INF を出すなら `&REV_` に対して書く** |
| **4** | **未確認** | 「REV が hardware ID に入る」「class driver の binding は descriptor 駆動」はどちらも確立した挙動だが、**1 PID で descriptor が大きく違う device を混ぜたときの実挙動は測っていない** |

→ **弱点 1・2 が本質的**。**「動くが、綺麗ではない」**。**pid.codes に複数申請できるならそちらが素直**([決めるために要る情報](#決めるために要る情報) 1)。

##### 設計規則(案)

> **1. 能力が違っても descriptor が同じなら 1 PID でよい。**(`caps` が差を名乗る)
> **2. descriptor が違う build には、`bcdDevice` を別に割り当てる。**(`REV` が hardware ID に入るので binding と `usbflags` が分かれる)
> **3. serial は個体識別にだけ使う。**(descriptor の分離には使えない)

**2 が「PID を増やさずに descriptor の違いを分離する」唯一安い手**。**PID は 1 個のまま、`bcdDevice` で descriptor 世代を分ける。** ただし上の弱点 1・2 を承知の上で。

##### ただし `bcdDevice` の使い道が衝突する

[ecosystem §4.2](ecosystem-any-hardware.ja.md) は **`bcdDevice` = BL / probe firmware の版**としている。上の規則 2 は **`bcdDevice` = descriptor 世代**として使う。**同じ 16 bit を 2 つの意味で使うことになる。**

**分割案(未決)**:

| bit | 意味 |
|---|---|
| 上位 8 bit | **descriptor 世代 / build 種別**(interface 構成が変わったら上げる) |
| 下位 8 bit | **firmware の版**(descriptor に触らない変更) |

- **得**: 「descriptor が変わった」と「中身が変わった」を host が区別でき、**Windows 側も hardware ID が分かれる**。
- **損**: 版が 8 bit(255)に制限される。`bcdDevice` は BCD 表記が慣習なので、**BCD として読める割り方**にするか要検討。
- **前例**: **UIAPduino は `0x0141`(= 1.41)を使っている** → **BCD 的な「1.41」の運用と、上位/下位の分割は両立させ方を決める必要**。

##### ベンチ運用への含意

| 要求 | この問題との関係 |
|---|---|
| **H-001**(一意な serial) | **serial があると instance が serial 単位**になるので、**別 build でも別 instance として扱われる**(良い面)。ただし**古い binding が device に付いて回る**(§前節) |
| **H-004**(1 台 = USB device 1 個 / composite) | **composite を選ぶと、単機能 build と descriptor の形が変わる** → 規則 2 が必要になる |
| **H-002**(種別 / 版 / capability を machine-readable) | **`bcdDevice` は `caps` の手前で読める**ので、**「開く前に build 種別が分かる」唯一の情報**。規則 2 はこれを兼ねる |

→ **`bcdDevice` に build 種別を載せると、`caps` を読む前に host が「どの形の device か」を判別できる。** これは **H-002 を USB 層で部分的に満たす**ことになり、**enumeration だけで判別できない問題(§4.2 の「1 PID で mode 判別」)を緩める**。

#### これが軸 A / B の意味を変える

| 最初の選択 | 後から足せるか |
|---|---|
| **HID 1 interface(単機能)で出す** — **V003 の low-speed でも実装できる** | **✗ 後から CDC を足すのが最も難しい変更になる**。**PID を 1 個しか持てないなら、実質そこで固定** |
| **最初から composite(CDC + HID + 予約)で出す** | **○ 末尾に足せる**。**ただし V003(low-speed)では実装できない**(A5: bulk が無い) |

> **つまり「V003 を USB-native に含めるか」は、「後から機能を足せるか」と直結している。** V003 を入れると HID 単機能に倒れ、**1 個の PID がそこで固定される**。V003 を外すと composite にでき、**予約した分だけ後から広げられる**。

**Linux / macOS はこの制約を持たない**(class ごとに動的 bind、永続 cache が無い)。→ **Windows 固有の制約が、幅の取り方を決めてしまう**構図。

#### 予約という手が使える

composite にするなら、**使う予定の function を最初から descriptor に並べておく**(未使用でも宣言する)という手がある。

| 得 | 損 |
|---|---|
| **後から実装を足しても descriptor が変わらない** = 1 PID で回せる | 未使用 interface が列挙され、**OS 側に「使えない COM」等が見える**。Windows で「デバイスが正しく動作していない」表示になる可能性 |

→ **どこまで予約するかは、`caps` で「宣言はあるが未実装」を表現できるかとセットで決める**(protocol 側は原則 2 で表現できる。**OS の見え方は別問題**)。

#### 選択の軸(**決めない**)

| 軸 | 選択肢 | 得るもの | 失うもの |
|---|---|---|---|
| **A. CH32 上で自前 USB を出すか** | **A1 出す** | V003 単体 probe / X035 の driver レス | **pid.codes の 1 個を消費**(K2 より他に手が無い) |
| | **A2 出さない**(UART / IP に逃がす) | **pid.codes を消費しない** | 「挿すだけ」の体験を CH32 build で出せない |
| **B. V003(low-speed)を USB-native の対象に含めるか**<br>⚠ **役割で答えが違う** → [定義 §5.6](harness-tool-definition.ja.md) | **B1 含める** | **$0.1 の chip が単体 probe**(= **probe としての V003**。連鎖 bootstrap の苗)。**target としての V003 は既にエコシステムが揃っているので理由にならない** | K5 より **descriptor が HID 固定**になり、その ID では帯域を上げられない |
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

→ **Core の下にもう 1 段**(仮に **Nano**)を置く案。**「幅の下端をどこに取るか」が決まってから採否を決める**。⚠ **[定義 §5.8](harness-tool-definition.ja.md) が V003 の BL を対象外にする方向を出したので、この案の主な動機は消えている** — 残るのは **AVR / 8 bit 級**のためだけで、それは **ardulink 互換モードで足りる可能性**がある:

| | Nano(案) | Core |
|---|---|---|
| コマンド | **`hello` / `dmi_read` / `dmi_write` のみ** | + `caps` `info` `ping` `lane_*` `batch` |
| `caps` | **固定の最小応答**(数個の TLV を定数で返す) | TLV で申告 |
| lane | **0 固定**(ヘッダの `lane` は無視) | 0..N |
| batch | **無し**(per-op。遅い) | 8 op 以上 |
| 想定 | **下端に含めるなら**: V003 の BL、AVR、8 bit 級 | 通常の probe |

**ヘッダ(`type` / `lane` / `tag` / `cmd`)と L1 framing は変えない。** そうすれば **同じ host コードが Nano も Core も扱える**(`hello` の応答で見分ける)。

## 4. V003 の BL に載せるか

> ⚠ **[定義 §5.8](harness-tool-definition.ja.md) が「BL は捨て、APP は取る」を方向の候補として出した。** それを採るなら本節は**不要になる**(BL に載せない)。以下は「載せる」を検討する場合のためだけに残す。


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
