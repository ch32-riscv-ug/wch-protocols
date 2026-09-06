# 書込 probe のパターンと共存可否 — 書込のみ / 複数 target / 書込 + LA を 1 つの仕組みに載せられるか

状態: **設計検討メモ**(実装・実測は未)。対象は [dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md)(`dmibridge/1`)を土台にした probe firmware / host のパターン展開。board 別の実力は [harness-board-survey.ja.md](harness-board-survey.ja.md)、計測 probe の構想は [dut-harness-design.ja.md](dut-harness-design.ja.md)。

## 0. 結論(先に)

1. **共存できる。ただし線引きが 1 本要る — 「能力は build 時に決まり、役割は実行時に決まる」。** 1 つのバイナリが全パターンをこなすのではなく、**1 つの仕様・1 つのソース木・1 つの host** が、board と用途ごとにビルドされた複数のバイナリを同じように扱う。
2. **protocol 側はすでに解けている。** [dmi-bridge の設計原則 2](../protocols/dmi-bridge.ja.md)「protocol は上限を持たない。数値はすべて `caps` が申告する」と、全 datagram ヘッダの `lane`、TLV の `caps`、`hello` の版合意。この 3 つで **host は board を知らずに能力だけを見て動ける**。追加で要るのは **cmd 番号範囲の分割**と **profile bit** だけ(§3.1)。
3. **firmware 側で守る規則は 3 つ**(§3.2):
   - **R1 能力は申告、実行時交渉なし** — 資源(PIO SM・命令メモリ・DMA・ピン)は build 時に解決し、`caps` に出す。
   - **R2 キャプチャ/イベントの流れは別チャネル・落ちてよい** — control の `max_inflight = 1` の応答性を守り、落とした量を数えて申告する。
   - **R3 profile は加算のみ。既存コマンドの意味を変えない** — flash 経路のバイト列は、LA 付きビルドでも 1 bit も変わらない。
4. **一番詰まるのは資源ではなく「ピン」と「USB 帯域」。** `W×N`(複数書込)と `W+LA` は**同じピンを争う**ので、狭い board では排他になる(Zero = どちらか / Pico = 両方少しずつ可)。USB は flash の bulk とキャプチャストリームが競合する → **優先度と degradation で解く**(§4)。
5. **時間軸の合わせ方が R3 を守る鍵。** DMI 応答にタイムスタンプを足すと flash 経路のバイト列が変わってしまう。→ **応答は変えず、キャプチャが armed のときだけ probe が「この DMI をこの時刻に実行した」記録をイベント流に注入する**。これで書込のみビルドと LA 付きビルドの flash 経路が完全に同一に保てる。
6. **副産物として "LA だけ" が成立する**。harness firmware を焼いた board は、lane を attach しなければ**単体のロジアナ**として使える。SUMP/sigrok を喋れば PulseView が初日から動く([dmi-bridge §7](../protocols/dmi-bridge.ja.md) の ardulink 互換と同じ発想)。

## 1. パターンの定義

| 記号 | 名前 | 中身 | target 向きピン(目安) | 主な用途 |
|---|---|---|---:|---|
| **W** | 書込のみ | 1 lane、DMI ブリッジ、`batch` | 3–4 | 個人の常用。最小・最安 |
| **WM** | 複数書込 | N lane、power 制御 | 3N+1 | 量産・教室・回帰の並列化 |
| **WL** | 書込 + LA | 1 lane + キャプチャ | 18–20 | 開発中の 1 枚を見る |
| **WLE** | harness | WL + 周辺エミュ + アナログ + trigger | 18(窓)+ 4(アナログ) | 障害注入・回帰試験 |
| **L** | LA のみ | キャプチャだけ | 8–16 | 単体ロジアナ(SUMP/sigrok) |
| **M** | monitor のみ | target UART / SDI print の橋渡し | 2–3 | 書込は別手段、観測だけ |

- **W が全パターンの基底**。[dmi-bridge §8.1](../protocols/dmi-bridge.ja.md) の `Core` + `Bulk` に相当し、これだけで flash/read/debug が成立する。
- **M は W の部分集合ではない**(lane を attach せずに `uart_*` と `autopoll` だけ使う)。`caps` の見え方は W より小さい。
- **L も lane を持たない**。この 2 つがあるので「lane が 0 本の probe」を仕様が許す必要がある。

## 2. 資源の衝突

RP2040-Zero(連番窓 16ch、PIO 2 blk × 4 SM、命令メモリ 32/blk、DMA 12ch、RAM 264 KB)を基準に。

| 資源 | W | WM(×2) | WM(×4) | WL | WLE | L |
|---|---:|---:|---:|---:|---:|---:|
| target 向きピン | 4 | 7 | 13 | 20 | 22 | 16 |
| **連番窓の消費** | 0 | 0 | 0 | **16** | **16** | **16** |
| phy の SM | 1 | 2 | 4 | 1 | 1 | 0 |
| キャプチャの SM | 0 | 0 | 0 | 1 | 1–2 | 1–2 |
| **PIO 命令メモリ** | ~16 | **~16** | **~16** | ~24 | ~32+ | ~8–24 |
| DMA ch | 0–1 | 0–2 | 0–4 | 2–3 | 4–6 | 2–3 |
| RAM | 数 KB | 数 KB | 数 KB | **大半** | **大半** | **大半** |
| USB 帯域 | flash 律速 | ×N | ×N | **競合** | **競合** | capture 律速 |

**読み取れること**:

1. **`WM` は命令メモリを食わない。** PIO は**同じプログラムを複数 SM で実行できる**ので、4 lane でも命令メモリは 1 本分。**WM のコストは SM とピンだけ。**
   - 例外: SWIO と RVSWD を混ぜると 2 プログラム分(~32 命令)になり、1 ブロックを使い切る。**同種 lane を並べる方が安い。**
2. **`WM` と `WL` は同じ「ピン」を争う。** これが共存の実質的な制約。
   - **RP2040-Zero**(連番 16): `WL` は窓を全部使うので `WM` の余地がほぼ無い → **どちらか**。
   - **Pico**(連番 23): `WM(×2)` = 7 ピン + 16ch 窓 = 23 → **両方載る**。
3. **`WLE` は命令メモリが最も苦しい**。ただし RP2040 の既定ピン割当ならエミュを全部 hardware 周辺に載せられるので、PIO はキャプチャと phy だけで済む([dut-harness-design §3.2](dut-harness-design.ja.md))。
4. **RAM は WL 以降で支配的**。W / WM は数 KB で足りるので、**書込専用ビルドは AVR 級でも成立する**([dmi-bridge §8.2](../protocols/dmi-bridge.ja.md) の AVR profile)。

### board 別の成立可否

| board | W | WM×2 | WM×4 | WL | WLE | L |
|---|:--:|:--:|:--:|:--:|:--:|:--:|
| **RP2040-Zero** | ◎ | ◎ | ○(裏面パッド) | ◎ | ◎ | ◎ |
| **Pico / Pico W** | ◎ | ◎ | ◎ | ◎(23ch) | ◎ | ◎ |
| Pico 2 / RP2350-Zero | ◎ | ◎ | ◎ | ○(**E9** 対策要) | ○(同) | ○(同) |
| **ESP32-S3** | ◎ | ◎ | ◎ | ◎(**深い**) | ○(アナログ弱) | ◎ |
| **CH32X035** | ◎ | ○(**PIOC は 1 組のみ**。2 本目以降は CPU bit-bang) | △ | ✗ | ✗ | ✗ |
| **CH32X033** | ◎ | △ | ✗ | ✗ | ✗ | ✗ |

**X03x の PIOC は IO が 2 本しか無い**ので、hardware phy を使える lane は 1 本だけ。`WM` にすると 2 本目からは CPU bit-bang になり、lane 間で品質が非対称になる。→ **X03x は W 専用と割り切るのが素直**(詳細は [harness-board-survey §3.5](harness-board-survey.ja.md))。

## 3. 共存を 3 層で見る

### 3.1 protocol 層 — 追加は「範囲の分割」と「profile bit」だけ

[dmi-bridge](../protocols/dmi-bridge.ja.md) は既に次を持っている:

| 既にある仕組み | 何を解決しているか |
|---|---|
| `hello` の版合意(§4.1) | 新旧の混在 |
| `caps` の TLV、未知 type は読み飛ばす(§5.1) | **版を上げずに項目を足せる** |
| 全 datagram ヘッダの `lane`、`0xFF` = probe 全体(§3) | 名前空間。**event が lane を名乗れる** |
| `status` に `ENOSUP`(caps に無い機能)(§6.2) | 能力の無いものを撃たれたとき |
| profile(Core / Bulk / Bench / Multi)(§8.1) | 適合水準の言語 |

**足すもの 1: cmd 番号範囲の分割。** 群ごとに範囲を割り、群を実装しない probe は `EBADCMD` を返すだけで済む。

| 範囲 | 群 | パターン |
|---|---|---|
| `0x01`–`0x0F` | probe 全体(hello/caps/info/ping/…) | 全部 |
| `0x10`–`0x2F` | lane / DMI / batch | W WM WL WLE |
| `0x30`–`0x4F` | 物理(power / nrst) | 任意 |
| `0x50`–`0x6F` | UART / autopoll | M WL WLE |
| **`0x70`–`0x8F`** | **キャプチャ**(arm / trigger / read / stream 設定) | WL WLE L |
| **`0x90`–`0xAF`** | **周辺エミュ**(model 定義 / block backend / 注入) | WLE |
| **`0xB0`–`0xBF`** | **アナログ**(ADC 設定 / PWM 出力) | WLE |
| `0x80`–`0xFF`(event) | event(既存の `0x80`–`0x83` を含む) | 任意 |

⚠ event の cmd 番号(`0x80`~)と req の範囲が重なっている。**event は `type` フィールドで区別される**ので衝突しないが、可読性のために event 側も群ごとに範囲を切っておく方がよい(未決 §6-1)。

**足すもの 2: profile bit。** 既存 profile に加算する形にし、`info` に 1 行で出す。

```
CH32RVProbe 0.1.0  (dmibridge/1)
profile: core+bulk+bench+capture+emulate+analog       ← これを見れば何ができるか分かる
board  : RP2040-Zero
lane0  rvswd GP14/GP15  nrst=GP11  uart=GP0/GP1   "V307 harness"
cap0   16ch  base=GP0  width=14|16  burst 200KB / stream 500kSa/s
emu0   spi-slave GP4-7 (block backend)   emu1  i2c-slave GP2/GP3 (0x68,0x76)
adc    GP26=vdd GP27=isense GP28=aux     dac  GP29=pwm
```

**足すもの 3: unit の番号付け。** キャプチャ unit / エミュ unit / アナログは **lane の子**として扱い、`lane` フィールドで所属 lane を指し、payload 先頭の 1 byte で unit 番号を指す。理由: 1 target 専有では全部 lane 0 の子になり、`WM + WL` の board では「どの lane を見ているキャプチャか」が自然に表現できる。

**この 3 つで前後方向の互換が構造的に成立する**:

| 組み合わせ | 何が起きるか |
|---|---|
| 新しい host + 古い probe | `caps` に capture が無い → host はキャプチャ機能を出さない。flash は動く |
| 古い host + 新しい probe | 未知の TLV は読み飛ばす。capture 群を撃たない。flash は動く |
| host が能力外を撃った | `ENOSUP` / `EBADCMD` が返る。**probe は必ず応答する**(§6.2) |

### 3.2 firmware 層 — build 時合成、実行時交渉なし

**R1: 能力は申告、実行時交渉なし。**

[dmi-bridge §5.3](../protocols/dmi-bridge.ja.md) の「ピンは protocol で設定しないが、protocol で申告する」をそのまま拡張する。理由も同じ:

> ESP32 の RMT channel / RP2040 の PIO SM は有限で、「lane 2 は lane 1 が bit-bang のときだけ使える」式の資源依存を `caps` で表現し始めると破綻する。**スケッチが構築できた構成だけが `caps` に現れる**なら矛盾が起きない。

キャプチャ・エミュ・アナログを足すと、この資源依存は組合せ爆発する(「16ch キャプチャは I2C を PIO にしないときだけ」等)。**build 時に解決してしまえば、`caps` は事実の報告になり、矛盾しようがない。**

```cpp
// 同じソース木。この宣言が resource 割当と caps と info を同時に決める
LANE_RVSWD (lane0, GP14, GP15, .nrst = GP11, .uart = {GP0, GP1}, "V307 harness");
CAPTURE    (cap0,  lane0, .base = GP0, .width = 16, .buffer = 200*1024);
EMU_SPI    (emu0,  lane0, GP4, GP5, GP6, GP7, .backend = HOST_BLOCK);
EMU_I2C    (emu1,  lane0, GP2, GP3, .addrs = HOST_DEFINED);
ANALOG     (adc0,  lane0, .vdd = GP26, .isense = GP27, .aux = GP28, .dac = GP29);
static CH32RVProbe probe({&lane0}, transport);
```

**build 行列は (board × パターン)**。同じソースから、それぞれの `caps` を持つバイナリが出る。**手書きの対応表を作らない**([dmi-bridge §5.3](../protocols/dmi-bridge.ja.md) の「申告は実物から自動導出する。手書きの表は必ず腐る」)。

**R3: profile は加算のみ。既存コマンドの意味を変えない。**

守るのが難しいのは 1 箇所だけ — **時間軸**。WL では DMI トランザクションもキャプチャと同じ時計に載せたいが、`dmi_read` / `dmi_write` の応答にタイムスタンプを足すと **flash 経路のバイト列が W ビルドと WL ビルドで違ってしまう**。

→ **応答は変えない。キャプチャが armed のときだけ、probe がイベント流に「この tag の DMI をこの時刻に実行した」記録を注入する。**

| 方式 | flash 経路のバイト列 | 時間軸 |
|---|---|---|
| 応答に ts を足す | **W と WL で違う**(R3 違反) | 取れる |
| **イベント流に注入**(採用案) | **完全に同一** | 取れる(`tag` で応答と対応付け) |

この選択には副作用の利点がある: **`W` ビルドの host コードが 1 行も変わらないまま、WL では時間軸が付く**。host の flash アルゴリズムは `tag` を既に持っているので、対応付けは無料。

### 3.3 host 層 — board 検出ではなく機能検出

| host の要素 | パターンによる差 |
|---|---|
| transport / L1 / L2 | **無し** |
| `hello` / `caps` / `info` | **無し**(能力を読むだけ) |
| flash アルゴリズム・DM 操作・GDB | **無し**(lane の意味が同一だから) |
| lane の並列化 | `caps.lane` の数と `max_inflight` を見る |
| キャプチャ | `caps` に capture があれば有効化。無ければ UI に出さない |
| エミュ model | **host 側のデータ**(センサ reg map / SD image)。probe には器だけ |
| 出力 | 波形 → sigrok/VCD、意味イベント → NDJSON。既存の capture 規則([captures/](../captures/README.ja.md))に合わせる |

**`ch32rv probe info` が 1 つあれば、繋がっている board が W なのか WLE なのかを人も機械も判別できる。** これが「同じ仕組み」の実体。

## 4. 実行時に本当に排他になるもの

build 時に成立した構成の中でも、**同時にできないことは残る**。

| 組み合わせ | 同時に可能か | 理由と扱い |
|---|:--:|---|
| lane の attach 中にキャプチャを回す | **可** | キャプチャは DMA が回すので CPU を取らない |
| キャプチャ中に DMI を撃つ | **可** | むしろ WL の主目的(§3.2 の時刻注入) |
| **flash の bulk 転送 + 連続キャプチャストリーム** | **競合** | USB FS の実効 ~1 MB/s を分け合う。→ 下記 |
| エミュ応答 + 線の critical section | **注意** | [dmi-bridge §6.1](../protocols/dmi-bridge.ja.md) の「critical section 中は event を送らずキューに積む」がエミュにも効く。**I2C の clock stretch / SPI の busy で待たせる**のが逃げ道(→ [dut-harness-design §6](dut-harness-design.ja.md)) |
| 複数 lane の同時書込 + キャプチャ | **実質不可** | 帯域とピンの両方で無理。build を分ける |
| PIOC を debug 線とエミュで共用(X03x) | **不可** | IO が 2 本しか無い |

### USB 帯域の競合

**優先度を固定する**: `control > flash data > capture stream`。キャプチャは**落ちてよいデータ**なので、落とした量を数えて申告する(R2)。

- [dmi-bridge §2.1](../protocols/dmi-bridge.ja.md) の L1 契約は「化けたものは届かない → 上位が再送で回復」だが、**キャプチャは再送できない**(実時間)。→ **連番 + drop 計上**で扱う。既存 `uart_data` の `dropped` と同じ idiom(§4.4)。
- **チャネルを物理的に分ける**のが素直: 第 2 CDC interface か bulk EP。control の `max_inflight = 1` の応答性を保ったまま bulk を流せる。

**「flash 中にバスを見たい」需要は実は薄い。** ただし例外が 1 つある — **bootloader / app が起動直後に周辺を触る瞬間**は見たい。この場合は「flash を終える → キャプチャを arm → NRST を放す」の順で、競合させずに済む。**trigger が NRST 解放に同期できることが効く**([dut-harness-design §2.2](dut-harness-design.ja.md))。

### 複数 probe(1 host、2 台以上)

[harness-board-survey §5](harness-board-survey.ja.md) の「S3 で深く撮り、RP2040 で probe + エミュ」は **protocol の変更を要しない**:

- 各 probe は独立した transport 接続。`uid`(chip UID)で個体識別([dmi-bridge §5.2](../protocols/dmi-bridge.ja.md))。
- 足りないのは **host 側の「セッション」概念**: 複数 probe を束ね、**共有 trigger 線のエッジで時間軸を較正する**(オフセットと drift)。
- これは host の仕事であって probe の仕事ではない。**probe を dumb に保つ原則([dmi-bridge 設計原則 1](../protocols/dmi-bridge.ja.md))と衝突しない。**

## 5. 作る順序(リスクの高いものを早く潰す)

| # | 作るもの | 何が確定するか | 依存 |
|---|---|---|---|
| 1 | **W**(RP2040-Zero、Core + Bulk) | dmibridge の実体。flash が通れば土台が立つ | 線層 |
| 2 | **L**(キャプチャのみ + SUMP/sigrok 互換) | キャプチャ経路と実効レート。**PulseView が初日から動く**ので単体で価値がある | — |
| 3 | **WL** | **§3.2 の時刻注入**(R3 が守れるか)。ここが設計の要 | 1, 2 |
| 4 | **PIO I2C slave の spike** | 多アドレス + clock stretch が成立するか。**WLE 最大のリスク** | — |
| 5 | **WLE**(まず SPI slave = hardware で簡単な側から) | エミュの器と host backed の往復 | 3, 4 |
| 6 | **WM** | lane 複線化。**1 と 3 の後なら配線とピンの話だけ**になる | 1 |
| 7 | アナログ + trigger matrix | 電流と波形の相関 | 5 |
| 8 | 複数 probe セッション(2 台分業) | 時間軸較正 | 3 + S3 側の 2 |

- **2 を 3 より先に置く**のは、キャプチャ単体で価値が出る(既存の LA を置き換える)うえ、**target を必要としない**ので今日始められるから([harness-board-survey §6](harness-board-survey.ja.md) の未決 1・8)。
- **4 を spike として独立させる**のは、これが不成立だと WLE のピン割当そのものが変わるから。早く潰す。
- **6 が後ろなのは、WM が最も「新しいことが無い」パターン**だから。lane の仕組みは既に protocol にあり、1 と 3 が通っていれば残るのは配線だけ。

## 6. 未決

1. **event の cmd 番号を群ごとに切り直すか**(§3.1 の ⚠)。既存 `0x80`–`0x83` との互換をどう扱うか。
2. **unit 番号を payload に置くか、`lane` の意味を群でスコープするか**(§3.1 の「足すもの 3」)。前者を採ったが、後者はヘッダを 1 byte も増やさない利点がある。
3. **第 2 チャネル(CDC / bulk EP)を仕様に書くか、L1 adapter の実装詳細に留めるか。** [dmi-bridge §2](../protocols/dmi-bridge.ja.md) の「L1 は中身を解釈しない」を守るなら後者だが、優先度の規則は上位に要る。
4. **`caps` に「同時実行できない組」を表現しないと決めた場合の説明責任。** R1 は build 時解決で矛盾を消すが、§4 の「flash 中はキャプチャが落ちる」のような**動的な degradation** は host に伝えたい。→ event で「今落としている」と言うだけで足りるか。
5. **profile 文字列の正式な語彙**(`core+bulk+bench+capture+emulate+analog`)と、[dmi-bridge §8.1](../protocols/dmi-bridge.ja.md) の profile 表への統合方法。
6. **`L` と `M`(lane 0 本の probe)を仕様が許すことの明記**。現行 §8.1 の Core は lane 前提で書かれている。
7. 複数 probe セッションの時間軸較正手順(共有 trigger のエッジ数・drift の測り方)。

## 7. 参照

- 土台の protocol(caps / lane / profile / L1 契約): [../protocols/dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md)
- board 別の実力(ピン・PIO・PIOC・PSRAM・E9): [harness-board-survey.ja.md](harness-board-survey.ja.md)
- 計測 probe の構想(何をやりたいか): [dut-harness-design.ja.md](dut-harness-design.ja.md)
- **ビルドイン型(内蔵ライタ)と自己書換えの capability 化**: [builtin-probe-and-self-update.ja.md](builtin-probe-and-self-update.ja.md)
- 汎用 probe の設計背景と transport 比較: [generic-probe-design.ja.md](generic-probe-design.ja.md)
- 実測の規則(計画を先に commit・証拠の水準): [../experiments/README.ja.md](../experiments/README.ja.md) / 台帳 [../experiments/LEDGER.ja.md](../experiments/LEDGER.ja.md)
- 出力の置き場と規則: [../captures/README.ja.md](../captures/README.ja.md)
