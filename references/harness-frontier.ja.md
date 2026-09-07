# core を無理なく広げても届かないところ

状態: **見積り**(実測は未)。**上限をどこに取るかは決めない。**
基準日: 2026-09-07

**問い**: core を作り、[existing-standards §6](harness-existing-standards.ja.md) の「無理なく広げられる範囲」を全部足したとして、**それでも届かない対象は何か**。

> **「同時に動かない」は本書の対象外。** 資源の取り合い(PIO 命令メモリ / RAM / 帯域 / CPU)は**後で構成を調整すれば済む**(構成を分ける・board を上げる・機能を落とす)。**本書が扱うのは「調整しても届かないもの」** — 置き場所が無い、silicon が足りない、市場に無い、制度が許さない。

## 0. 届かないものの 4 分類

| 分類 | 何に届かないか |
|---|---|
| **1. firmware の置き場所** | **そこに載せられない**(容量が無い / 領域が無い) |
| **2. probe になる silicon** | **その chip では上の段に行けない** |
| **3. target の series** | **試せない / 既存が非対応** |
| **4. 配布と体験** | 制度・OS・ブラウザが許さない |

**1 と 2 が本題。** 3 は市場、4 は制度。

---

## 1. firmware の置き場所として届かない

**BOOT 領域(system memory)の大きさが series で 30 倍違う**([custom-bootloader §2a](../protocols/custom-bootloader.ja.md))。**ここに入らなければ「BL として届かない」。**

| BOOT 領域 | series | Core profile を載せられるか |
|---:|---|---|
| **無し** | **CH32M030** | **✗ 届かない。** BOOT 領域が存在しない(information store 512 B のみ)。→ **APP のみ** |
| **1,920 B** | **CH32V003** / CH641 | **✗ 届かない見込み。** 既存 BL(rv003usb)が **1,916 B + secret 4 B で埋まっている**。**software USB stack だけで大半を使う**ので、`Core`(hello/caps/info/ping + lane×3 + dmi r/w + batch)を足す余地が無い |
| 2 KB + 1,792 B(**2 分割**) | **CH32V103** | **△ 難しい。** 分割の間に option bytes と vendor word が挟まる。ch32fun BL も「非対応見込み」 |
| **3,328 B** | V002/004/005/006/007, **X033/X035**, L103 | **△ 要見積り。** 1,920 B 版 + 約 1,400 B の余地。**入るかは測っていない** |
| **28 KB** | V20x / V30x / V31x, V407, X315 | **○ 余裕**(USB stack + protocol が入る) |
| 56 / 28 KB | H417 | ○ 余裕 |

### V003 の BL が届かないことの意味

**ユーザ指摘のとおり、ここが最も明確な「届かない」。**

- **1,920 B は増えない。** 構成を調整しても、board を変えても増えない。**唯一の逃げ道は profile を小さくすること**。
- **`Nano` profile(`hello` + `dmi_read` + `dmi_write` だけ)なら可能性は残る**が、**既存 BL はその 1,920 B を「USB + scratchpad 実行 + entry 判定」で使い切っている**ので、**scratchpad 実行を捨てて Nano に置き換える**形になる。それは**機能の交換**で、**上位互換ではない**([v003-bootloader-replacement](v003-bootloader-replacement.ja.md) の「完全上位 6 条件」を満たさない)。
- **測っていない**(→ D12)。だが**予算からは「入らない」が既定**。

→ **V003 は「BL としては届かない。APP としては届く」。** これが [定義 §5.8](harness-tool-definition.ja.md) の境目の根拠。

### 3,328 B の 3 series が本当の未決

**V00x / X03x / L103** は **1,400 B ほどの余地**がある。ここが入るかどうかで、

- **X03x を「BL からも喋れる probe」にできるか**(X03x は PIOC を持つので probe 候補)
- **L103(実在する穴)を BL 経由で扱えるか**

が変わる。→ **測る価値がある**(D12 の拡張)。

---

## 2. probe になる silicon として届かない

**core を広げても、その chip では上の段に行けない**もの。

| silicon | 届く段 | **届かない理由(調整では消えない)** |
|---|---|---|
| **CH32V003** | **L0 / L1 止まり** | **RAM 2 KB**(capture ring が取れない)/ **並列 capture の機構が無い** / **software USB(low-speed、CDC 不可)** / **PIOC を持たない**(CPU bit-bang) |
| **CH32X03x** | **L0 / L1 + アナログ** | **並列 capture の機構が無い**(TIM+DMA で GPIO を舐める程度)/ **RAM 20 KB** / **PIOC は 2 ピン**(debug 線とエミュを同時に持てない) |
| **AVR(Uno / Nano)** | **L0 の一部** | **2 線 phy の timing を出せない**(PIO 相当が無い)。**1 線 open-drain の低速のみ**。5 V の V003 に届くことが価値 |
| RP2040 | L0〜L8 | (深い capture と MB 級バッファは RAM で届かない → S3) |
| RP2350 | L0〜L8 | (**errata E9**。Hi-Z 入力に外部 pull-down が要る) |
| ESP32-S3 | L0〜L8 + 深い capture | **アナログが弱い**(ADC 連続 83.3 kSa/s、**DAC 無し**)/ **GPIO19/20 が USB** |
| — | **USB HS / Ethernet / 並列バスの相手** | **どの候補 board でも届かない。ESP32-P4 級が要る** |

→ **「X03x や V003 を probe にすると capture に届かない」は調整では消えない**(機構が無い)。**board を選ぶ話**だが、**「安い chip で全部やる」という道は無い**という形の限界。

---

## 3. target の series として届かない

| | series | 理由 |
|---|---|---|
| **試せない** | H415 / H416 / H417 / M030 / M103 / V407 / V467 / X305 / X315(**9**) | **未発売**。市場に無いので**書けも試せもしない**。→ **`caps` で名乗れるようにしておくだけ**(発売後に届く) |
| **既存が非対応、原因が未知** | **V103** | rvswdio が「**テスト済み・非対応**」。**線の仕様に series 差がある可能性**。→ **測るまで届くか分からない**(D3) |
| 射程外 | CH5xx / CH64x ほか | rvswdio は CH57x / 585 / 59x を主張。**我々が対象にするかは決めていない**(D2 の外) |

→ **未発売の 9 は「届かない」ではなく「まだ来ていない」。** V103 だけが**現在進行形で届いていない**。

---

## 4. 配布と体験として届かない — **PID の制約が最も厳しい**

### 4.1 なぜ PID が一番厳しいか

**PID はこのエコシステム全体で共有される単一の希少資源で、harness だけの問題ではない。**

| 事実 | 帰結 |
|---|---|
| **pid.codes は 1 project 1 PID が原則** | **増やせない**(交渉の余地はあるが前提にできない) |
| **WCH には vendor community program が無い** | **CH32 上の自前 USB firmware は pid.codes 一択** |
| RP2040 / RP2350 は **Raspberry Pi `0x2E8A`**、ESP32 は **Espressif `0x303A`** が無償・公認 | **RP2040 / ESP32 の build は pid.codes を消費しない** |
| 他人の PID は名乗れない / 同じ VID:PID で interface 構成を変えられない | **descriptor は固定。mode は enumeration で判別できない** |

**そして CH32 上で「自前 USB を名乗りたいもの」が複数ある**:

| # | 自前 USB を出したいもの | 誰の出荷物か |
|---|---|---|
| 1 | **Arduino core の app 既定(CDC)** | **ArduinoCore-CH32**(X035 等は USB を持つ) |
| 2 | **BL mode** | エコシステム |
| 3 | **X03x を probe にする build** | harness |
| 4 | **V003 を probe にする build**(software USB) | harness |

→ **4 つが pid.codes の 1 個を争う。** [ecosystem §4.2](ecosystem-any-hardware.ja.md) が「**PID を最低 3 つ**」と書き、同 §4.2b が「**1 project 1 PID が原則**」と書いている — **この衝突が「一番厳しい制約」の正体**。

### 4.2 PID の制約で届かなくなるもの

| 届かないこと | なぜ |
|---|---|
| **CH32 上で mode(BL / app / probe)を descriptor で区別して提供する** | 1 PID なら descriptor 固定。**列挙では区別できず、`hello` / `caps` で判別する形しか無い** |
| **「V003 でも動く descriptor」と「capture の帯域が出る descriptor」を同じ PID で持つ** | **low-speed に bulk が無い**(HID 固定)。**帯域が要る形とは interface 構成が違う** → 別 PID が要る |
| **後から function(CDC 等)を足す** | **「単機能 → composite」は最も破壊的な変更**で不可。**最初から composite にして末尾に足す形なら見込みあり**。→ 詳細は [choices §2](harness-choices.ja.md)「何を固定しなければならないのか」 |
| **Arduino app の CDC と、CH32 probe の自前 USB を両方** | **どちらかが PID を持てない** |
| **CH32 で「挿すだけ」を複数 build に渡って提供する** | 同上 |

### 4.3 帰結 — **PID 制約は silicon 選択を歪める**

**RP2040 / ESP32 は vendor program で PID を無償に取れるが、CH32 は取れない。** つまり:

> **「X03x の PIOC が phy に向いている」「V003 が $0.1 で probe になる」という技術的な魅力があっても、自前 USB を出すなら pid.codes の 1 個を食う。** 同じことを RP2040 でやれば **0 個**で済む。

→ **PID の観点だけで見ると、probe は RP2040 / ESP32 に寄せ、CH32 は「UART / 既存 bridge 経由」または「Arduino app の PID を優先」に倒れる。** これは **[定義 §5.6](harness-tool-definition.ja.md) の「probe としての V003 は価値が高い」と正面から衝突する**。

**⚠ ただし「V003 を入れると 1 PID が固定される」は言い過ぎだった(訂正)。** **`bcdDevice`(REV)は hardware ID に入る**ので、**build ごとに `bcdDevice` を分ければ、HID 単機能の V003 と composite の RP2040 を同じ PID で共存させられる見込み**(**serial では分離できない** — instance ID であって hardware ID ではない)。**固定されるのは「同じ `bcdDevice` を共有する build 群」だけ。** → [choices §2](harness-choices.ja.md)。**代償は「USB 仕様の趣旨から外れる」「VID:PID だけ見るツールが区別できない」の 2 点**で、**未確認**。

**逃げ道は 3 つ(どれも代償がある)**:

| 逃げ道 | 代償 |
|---|---|
| **CH32 の probe を UART / 既存 bridge に寄せる** | **「挿すだけ」の体験を失う**。V003 は USART が 1 本しかないので、**target の serial を橋渡しできなくなる** |
| **pid.codes に複数申請する**(BL / app / probe を別 project として) | **通るか分からない**。前例を調べる価値はある(→ [choices §2](harness-choices.ja.md) の「決めるために要る情報」1) |
| **descriptor を全 mode で HID 1 interface に固定し、1 PID で回す** | **enumeration で mode が分からない**。**capture の帯域が出ない**(CH32 build に capture は無いので実害は小さい) |

### 4.4 その他の配布・体験の限界

| 届かないこと | 理由 |
|---|---|
| **1 つの PID で、enumeration だけで mode(BL / app / probe)を判別する** | descriptor を固定するしかないので、**列挙では区別できない**。`hello` / `caps` で判別する形になる |
| **他人の PID を自分の firmware で名乗る** | `1209:B003` は他の project の ID。**制度上不可**(host が他人の protocol を喋るのは別問題で可) |
| **全ブラウザで driver レス** | WebHID / WebSerial / WebUSB は **Chromium 系のみ**。**全ブラウザに届くのは WebSocket(= IP transport)だけ** |
| **PID を使わずに「挿すだけ」** | 既存 USB-serial bridge 経由なら PID は不要だが、**COM/tty の選択が入る**。「挿すだけ」の体験は自前 descriptor が前提 |

---

## 5. 要約

| 届かないもの | 調整で消えるか |
|---|:--:|
| **PID が 1 個しか無い(CH32 上の自前 USB)** | **△ 見込みは変わった。** 制度としては消えないが、**`bcdDevice` の major を descriptor 世代に割り当てれば、1 PID で mode / silicon / descriptor 世代を分離できる見込み**(→ [choices §2](harness-choices.ja.md)「`bcdDevice` の割り方」)。**実機確認が最優先** |
| **V003 の BOOT 領域(1,920 B)** | **✗ 消えない**(容量は増えない) |
| **M030 の BOOT 領域(存在しない)** | **✗** |
| V103 の BOOT 領域(2 分割) | ✗(構造) |
| **V003 / X03x を probe にしたときの capture** | **✗**(機構が無い) |
| **AVR での 2 線** | **✗**(timing) |
| USB HS / Ethernet / 並列バスの相手 | ✗(P4 級が要る) |
| **未発売 9 series** | **待てば消える** |
| **V103 の非対応** | **測れば分かる**(D3) |
| 3,328 B の BOOT 領域に入るか | **測れば分かる**(D12) |
| 1 PID での enumeration 判別 / 他人の PID | ✗(制度) |
| 全ブラウザ driver レス | ✗(ブラウザ実装) |
| **資源の取り合い(PIO / RAM / 帯域 / CPU)** | **○ 後で調整できる**(本書の対象外) |

**確定して届かないのは 8 件**、**測れば分かるのが 2 件**、**待てば届くのが 1 件**。

**そのうち PID だけが性質が違う。** 他の 7 件は「その対象に届かない」だけだが、**PID は「どの silicon を probe に選ぶか」を歪める**(§4.3)。

**⚠ ただし `bcdDevice` の major を descriptor 世代に使う案が出たので、PID の壁は下がる見込み**(→ [choices §2](harness-choices.ja.md))。**`major = 破壊的変更` は semver / USB 仕様の趣旨と一致する**ので hack ではなく、**実害が残るのは「VID:PID しか見ない自動検出」だけ**。**実機で混ぜて確認するのが最優先の実験。**

## 6. 参照

- BOOT 領域の一次データ: [../protocols/custom-bootloader.ja.md](../protocols/custom-bootloader.ja.md) §2a
- V003 BL のサイズ実態: [v003-bootloader-replacement.ja.md](v003-bootloader-replacement.ja.md) §2
- silicon 別の到達範囲: [harness-board-survey.ja.md](harness-board-survey.ja.md)
- series 別の入手可否と穴: [harness-scope-decisions.ja.md](harness-scope-decisions.ja.md) §2.2
- 無理なく広げられる範囲(本書の出発点): [harness-existing-standards.ja.md](harness-existing-standards.ja.md) §6
- 所在: [harness-index.ja.md](harness-index.ja.md)
