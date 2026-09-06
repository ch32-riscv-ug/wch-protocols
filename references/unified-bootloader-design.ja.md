# 統一 bootloader の設計空間 — 調査結果から「どこで分けられるか」を見る

状態: **draft**(解読ではなく**自前設計の検討**。実装・実測は未)。

**この文書は決定書ではありません。** 「何が事実として分かったか」と「その結果どういう選択肢が残るか」を並べるところまで。
選ぶのは後の工程。§9 では**事実で決着したもの**と**まだ選択肢のままのもの**を分けてある。
根拠はすべて [bootloader-survey.ja.md](bootloader-survey.ja.md) の所見 ID(`F01`〜`F58`)と
[data/bootloader-survey/](data/bootloader-survey/) の 28 テーブルにある。設計判断のたびに ID を引く。

この文書が扱うのは **「Q2 = 統一 BL は作れるか」の続き**、すなわち **どこで分けられる可能性があるか**。

---

## 0. 分かったこと(先に)

| 事実 | 内容 | 根拠 |
|---|---|---|
| **分割の主軸は series ではなく制御レジスタ列** | flash driver は **5 関数 + 4 パラメータ**で 12 series を覆える(erase 1 形 / program 2 形 / unlock は分岐なし) | F42 / F43 / **F44** |
| **protocol 層は分岐不要** | 8 project 以上に出る `#define` の **13 個が全 series 同一、割れるのは実質 2 個**(`FLASH_Base` / `CalAddr`) | F01 / F02 |
| **blank pattern は分岐ではなく定数** | chip の仕様値なので `ch32-device-data` から引ける。**entry 設計次第では参照すら不要にできる**(§3.2) | F25 |
| **真の障害は 1 つだけ** | `CheckNum` の**判定極性が反転**している(V003/V00X は「APP 正当の印」、他は「BL に留まれの要求」)。**意味が逆なので `#if` では吸収できず、仕様として一本化するしかない** | F07 |
| **言語は層で分かれている** | 既存実装はどれも 線上信号 = asm / BL 本体 = C / stub = asm | H1〜H3、§1 |
| **拡張余地は flash でなく RAM 側にある** | BL 本体は固定。scratchpad は ch32fun で BL 本体の 1.9 倍 | H4 / F19 / F20 |
| **サイズ上限は EVT が強制していない** | 13 project 中 5 つだけ。自作するなら自前で linker assert が要る | F13 |
| **移植 config は 11〜13 定数 + 2 選択に収まりそう** | §9b。数は §3.2 の選択で動く。うち 6 個は `ch32-device-data` から引くだけ | F42〜F58 |

---

## 1. 層と言語

| 層 | 言語 | 根拠 |
|---|---|---|
| **L-a. 線上信号**(software USB の bit-bang 等) | **asm 必須** | サイクル精度。`rv003usb.S` 23,474 B が実例(§6 H1) |
| **L-b. BL 本体**(state machine / transport / entry・exit / flash 呼出) | **C** | rv003usb・ch32fun とも C で成立(H2) |
| **L-c. host が送り込む stub** | **asm** | RV32EC 制約(x0–x15)・位置独立・特殊な呼出規約・最小 8 B(H3 / F17) |

**L-b を asm 化しても割に合わない**。§5 の実測どおり、BL 本体を縮めて得られる余地は数百 B。一方 scratchpad は ch32fun で BL 本体の 1.9 倍ある(H4)。

---

## 2. config surface — 何が変わり、どこから引くか

**再導出しない**。chip の事実は `ch32-device-data`、protocol と entry の事実は本調査の表から引く。

| 種別 | 引く先 | 項目 |
|---|---|---|
| **chip の事実** | `ch32-device-data` `evidence/flash_geometry.csv` | `blank_check_word` / `fast_program_bytes` / `fast_erase_bytes` / `page_erase_bytes` |
| 同上 | 同 `evidence/product_attributes.csv` + `index/parts.csv` | Code FLASH 総容量(`CalAddr` を導出するなら必要) |
| **protocol / entry の事実** | 本調査 [`port_matrix.csv`](data/bootloader-survey/port_matrix.csv) | `flash_base` / `cal_addr` / `polarity` / `exit_method` / `uart_port` / `baud` / `vid` / `pid_*` / `gpio_pin` / `driver_class` |

### 2.1 port matrix(13 行 = 12 series + H417 の 2 core)

| series | driver | `FLASH_Base` | blank(chip 仕様) | 極性 | fast prog | fast erase | page erase | UART |
|---|:-:|---|---|:-:|---:|---:|---:|---|
| CH32V003 | **A** | `0x08000000` | `0xFFFFFFFF` | **`==`** | 64 | 64 | 1024 | USART1 |
| CH32V00X | **B** | `0x08000000` | `0xFFFFFFFF` | **`==`** | 256 | 256 | 1024 | USART1 |
| CH32V103 | **D** | — | `0xFFFFFFFF` | — | 128 | 128 | 1024 | USART3 |
| CH32V205 | **B** | `0x08005000` | `0xFFFFFFFF` | `!=` | 256 | 256 | 2048 | USART2 |
| CH32V20x | **C** | `0x08005000` | `0xe339e339` | `!=` | 256 | 256 | 4096 | USART3 |
| CH32V30x | **C** | `0x08005000` | `0xe339e339` | `!=` | 256 | 256 | 4096 | USART3 |
| CH32V407 | **C** | `0x08005000` | `0xe339e339` | `!=` | 256 | — | 4096 | USART2 |
| CH32X035 | **B** | `0x08005000` | `0xFFFFFFFF` | `!=` | 256 | 256 | 1024 | USART2 |
| CH32X315 | **C** | `0x08005000` | `0xe339e339` | `!=` | 256 | — | 4096 | USART2 |
| CH32L103 | **B** | `0x08005000` | `0xFFFFFFFF` | `!=` | 256 | 256 | 2048 | USART2 |
| CH32M030 | **E** | `0x08005000` | `0xFFFFFFFF` | `!=` | 128 | 128 | 1024 | USART1 |
| CH32H417 | **C** | `0x08006000` | `0xe339e339` | `!=` | 256 | — | 4096 | USART1 |

`fast erase` が空の 3 series(V407 / X315 / H417)は **per-page の快速消去を持たず block 消去のみ**。
→ 256 B を書くのに 4 KB 消す必要があり、**read-modify-write が要る唯一の群**。

---

## 3. entry / exit の契約 — **ここだけは仕様を決める**

### 3.1 現状は 2 つの互換性のない意味論(F07)

```c
/* V003 / V00X — CheckNum は「APP は正当」の印 */
if (*(u32*)FLASH_Base != blank)          // APP らしきものがある
    if (*(u32*)CalAddr == CheckNum)      //   かつ 正当の印がある
        jump_to_app();

/* 他 10 series — CheckNum は「BL に留まれ」の要求 */
if (*(u32*)FLASH_Base != blank)          // APP らしきものがある
    if (*(u32*)CalAddr != CheckNum)      //   かつ 留まれの要求が無い
        jump_to_app();
```

同じ `CheckNum = 0x5aa55aa5` を使いながら**意味が逆**。`#if` で切り替えると、
**host 側(WCHMcuIAP 相当)の書き込む値も切り替わる**ので、契約が 2 つに割れたままになる。

### 3.2 2 群は完全な鏡像 — 誰が印を書き、誰が消すか

| | **`==` 群**(V003 / V00X) | **`!=` 群**(他 10 series) |
|---|---|---|
| 印の意味 | **「APP は正当」** | **「BL に留まれ」** |
| BL が書込完了時に | page 消去 → **`FLASH_ProgramWord(CalAddr, CheckNum)`**(印を立てる) | page 消去のみ(印を消す) |
| APP が BL へ落ちたいとき | **page 消去**(印を消す) | **page の read-modify-write**(印を立てる) |

出典: V003 = `IAP/User/iap.c:124-125` / `APP/User/iap.c:82`、X035 = `IAP/User/iap.c:121` / `APP/User/iap.c:56-66,104-105`。

`!=` 群の APP は印が page 内にあるため `Program_Buf_Modify()` で**周囲 63 word を読んで保存**してから焼き直す。
`==` 群は 1 word 書くだけ。**実装量は `==` のほうが軽い**。

### 3.2a 選択肢と帰結(**まだ選ばない**)

| 案 | 内容 | blank flash | probe 直焼き | config 数 | EVT host 互換 |
|---|---|:-:|:-:|:-:|---|
| **A** | **`!=` のまま**(EVT 多数派に合わせる) | △ 外側の `FLASH_Base != blank` が必須 | ✓ 起動する | 13 | **WCHMcuIAP が使える** |
| **B** | **`==`** + 印を **APP イメージに埋める**(linker section) | ✓ 印が無い → BL に留まる | ✓ イメージに印が含まれる | 12 | 不可 |
| **C** | B + 印を **flash 末尾でなく APP 先頭のヘッダ**へ | ✓ | ✓ | **11** | 不可 |

各案で何が起きるか:

- **A の弱点**: blank flash で `blank != CheckNum` が成立して **APP へ跳んでしまう**。
  外側の `FLASH_Base != blank` チェックが暴走の唯一の歯止めなので、**blank pattern が必須依存**になる(F25)
- **`==` を素朴に採ると別の穴が開く**: BL が印を書く方式だと、**debug probe で APP を直焼きしたとき印が付かず起動しない**。
  開発中は毎回これに当たる。→ **B はこれを「印を APP イメージ自身に含める」ことで塞ぐ**
  (`.app_valid : { LONG(0x5aa55aa5); } > FLASH`)。BL は印を書く処理すら不要になる
- **B が残す弱点**: `CalAddr` は Code FLASH 末尾 −4 なので、① **`.bin` に穴が開く**(`.hex`/`.elf` なら可)
  ② **総容量を知る必要がある** — F15 の「`parts.csv` は零等待領域」問題が設計に入ってくる。→ **C はこれを塞ぐ**
- **C の代償**: EVT からさらに離れる。WCHMcuIAP は完全に使えない

**共通する軸は「印を誰が書くか」**。BL が書く(EVT 両群)/ **イメージ自身が持つ**(B・C)の 2 系統で、
後者にすると **BL・probe・host のどこから焼いても同じ契約**になる。ここが可能性として一番大きい。

### 3.3 exit は配置から自動的に決まる(F08)

| BL の配置 | exit |
|---|---|
| **BOOT 領域常駐**(V003 1,920 B / V00X・X035 3,328 B) | `BOOT_MODEKEYR` 解錠 → `FLASH_STATR` bit14 クリア → system reset |
| **user flash 先頭** | Software IRQ を pending にして、ハンドラから APP へ跳ぶ |

これは選択ではなく帰結なので、**config ではなく配置マクロ 1 個**で決まる。

---

## 4. flash driver — **program 2 形 + erase 1 形**(F42/F43/F44)

§6b.10 の「5 class」は API 名と生の語書き込み回数まで含めた粒度だった。**制御レジスタ列だけで測り直すと
もっと縮む**(D2 の答え)。

### 4.1 unlock / lock — 分岐なし(F43)

```
KEYR     <- KEY1, KEY2
MODEKEYR <- KEY1, KEY2      # 12 series で完全同一
```

### 4.2 erase — **1 実装**(F44)

```
CTLR &= ~(OPTER | PAGE_ER)   # ← 防御的クリア。x035 群だけが持つが、
CTLR |=  PAGE_ER             #    既に 0 のビットを落とすだけなので他群に足しても無害
ADDR  =  addr
CTLR |=  STRT
wait STATR & BSY
CTLR &= ~PAGE_ER
```

2 群あった差は**先頭 1 行だけ**。常に入れれば **erase は 1 本で 12 series を覆える**。

### 4.3 program — **2 形**(F44)

| 形 | series | 制御列 |
|---|---|---|
| **(a) buffer-then-commit** | v003, v103, m030, v00x, v205, x035, l103 | 語は別途 `BufLoad` で buffer へ。この関数は `CTLR\|=PAGE_PG` → `ADDR=a` → `CTLR\|=STRT` → `wait BSY` → `CTLR&=~PAGE_PG` |
| **(b) inline-write-then-commit** | v20x, v30x, v407, x315, h417 | 関数内で語を書く。`CTLR\|=PAGE_PG` → `wait BSY,WR_BSY` → `loop{ *a=*buf; wait WR_BSY }` → `CTLR\|=PG_STRT` → `wait BSY` → `CTLR&=~PAGE_PG` |

**(a) は buffer 充填が別関数**なので、`BufLoad` 側にもう 1 形が要る:

```
CTLR |= PAGE_PG
<N 語を buffer へ>          # N はパラメータ: v003=1 / m030=2 / v103=4 / 他=1
CTLR |= BUF_LOAD
wait STATR & BSY
CTLR &= ~PAGE_PG
*(0x40022034) = ...          # ★ 未文書の commit 副作用。v103 と m030 が持つ
```

> **`0x40022034` への書き込みは未文書**(D6 の答え、F58)。SDK 全 12 series を走査した結果、
> **持つのは V103(10 箇所)と M030(8 箇所)の 2 series だけ**、他 10 series は 0 箇所。
> **XOR マスクも違う** — `*(0x40022034) = *((addr & ~3) ^ MASK)` で **V103 = `0x1000` / M030 = `0x100`**。
> → config の `commit_xor_mask`(0 = 副作用なし / 0x1000 / 0x100)**1 個で表せる**。分岐は不要。

### 4.4 まとめ — driver の実装本数

| 部品 | 本数 | パラメータ |
|---|:-:|---|
| unlock / lock | **1** | — |
| erase | **1** | 粒度(64 / 128 / 256 / 4096 B) |
| program | **2**(buffer-then-commit / inline-write-then-commit) | 粒度 |
| buffer 充填(形 a のみ) | **1** | `words_per_bufload`(1 / 2 / 4)、commit 副作用の有無 |

→ **合計 5 関数で 12 series**。「5 driver class」ではなく「**5 関数 + 4 パラメータ**」が正しい定式化。

> **確度**: 制御列の一致は SDK ソースの正規化から導いた `attested`。
> **1 本の C 実装が全 series で同じバイナリ挙動になるかは未検証**(実機で確認するまで `verified` にしない)。

### 4.5 read-modify-write の置き場(D3 — 選択肢)

`fast erase` を持たない **V407 / X315 / H417** は、256 B を書くのに **4 KB 消す**必要がある。
これを driver に入れると driver がバッファと状態を持ち、§4.4 の「5 関数」が崩れる。

選択肢は 2 つ:

| 案 | 内容 | 帰結 |
|---|---|---|
| **driver の中** | `program()` が消去粒度との差を自分で吸収 | driver がバッファと状態を持ち、**§4.4 の「5 関数」が崩れる** |
| **driver の上** | `erase_gran > program_gran` を**上位の page cache 層**で吸収 | driver はレジスタの薄い包みのまま。この層は series 非依存で `erase_gran` / `program_gran` の 2 定数だけ見る |

**「5 関数」という数え方を保ちたいなら後者**、という関係にある。どちらを採るかは実装時の判断。

## 5. サイズ予算

| 配置 | 予算 | 実測の余裕 |
|---|---:|---|
| V003 BOOT 領域 | **1,920 B** | rv003usb の全機能 off が **1,896 B** → **残り 20 B**(F28) |
| V00X / X035 BOOT 領域 | **3,328 B** | ch32fun BL が収まっている |
| user flash 先頭 | 20 KB(H417 は 24 KB) | EVT の慣行。**ただし強制しているのは 13 中 5 project だけ**(F13) |

### 5.1 実装上の決めごと

1. **linker で上限を強制する**。EVT は半分以上の project で強制しておらず、超過しても build が通って APP を侵食する(F13)。
   `ASSERT(SIZEOF(.text) <= __bl_max, "...")` を必ず置く。
2. **サイズは同一ビルド条件でしか比較しない**。gcc 8.2.0 / 14.3.0 / 15.2.0 で **4 B 動く**(F29)。
   予算残 20 B の世界ではこれが機能 1 個ぶんに相当する。CI で toolchain を固定する。
3. **機能は加算で見積もらない**。`TIMEOUT_PWR`(+8)と `TIMEOUT_USB`(+4)を両方入れると **+28 B**(F28)。
   予算表は**実測で埋める**。

---

## 6. transport

| transport | 予約 | 備考 |
|---|---|---|
| UART only | 1,920 / 3,328 B に収まる | V003/V00X の実績 |
| UART + USB device | 20 KB | 8 series の実績 |
| **USB host**(`/APP.BIN` を読む) | **32 KB(H417 は 48 KB)** | UART/USB より **+12〜24 KB**(F38)。「20 KB 予約」は通用しない |
| software USB HID | 1,920 B(V003) | BL 本体と stub の分業が前提 |

**USB IAP には Vendor / HID の 2 モード**があり、新しい 5 project は `DEF_USB_IAP_MODE` で切り替える(F11)。
**VID は 2 種**(V103・V20x = `0x4348`、他 = `0x1A86`。PID は全て `0x55E0`)(F12)。
自作 BL では pid.codes を使う方針なので、これは**互換のためだけに要る値**。

---

## 7. 拡張余地は stub 側 — scratchpad の契約

BL 本体を固定したまま能力を足せる(H4)。実績があるのは minichlink の HID scratchpad 方式なので、まずそれを基準に置く。

```
scratchpad: [0..3] report ID + "00 00 00"   [4..] stub 本体   [4+len..] 引数   … [pad-4..] 0x1234ABCD
HID report ID = 0xAA + pad_size/1024,  pad_size ∈ {128, 1152, 2176, 3200, 4096, 5248, 6272}
完了印: scratchpad[0..3] = 0xFFFFFFFF
```

**stub は x0–x15 だけを使う**。b003 系 47 本が全部これを守っており(F17)、守れば **1 本の blob が RV32EC と RV32IMAC の両方で動く**。
`-march=rv32ec` でビルドするか、asm で手書きする。

実測コスト: `run_app_new` 8 B / `halt_wait` 10 B / `write64_flash` 48 B / `erase_block` 52 B / `write_block` 104 B(F19)。
**BL の flash 予算を一切消費しない**(RAM の scratchpad に載る)。

> **注意**: `pgm-b003fun.c` のコメントにある引数 offset `@76/@80/@84` は**古くて誤り**。
> 実際は `@108/@112/@116`(F26)。契約を実装するときは [`stub_args.csv`](data/bootloader-survey/stub_args.csv) を見る。

---

### 7.1 stub 契約の選択肢(D4 — 選択肢)

| 契約 | 出所 | 特徴 |
|---|---|---|
| **(i) minichlink HID scratchpad** | `pgm-b003fun.c` | host が**任意の機械語**を送って実行させる。能力は host 側の stub で無限に増える。既存 host が 3 実装ある |
| **(ii) WCH 純正 loader ABI** | WCH OpenOCD の 18 loader(F45/F56) | `a0` = 操作ビットマスク(unlock / mass erase / page erase / program / verify)、`a1` = addr、`a2` = len、buffer 固定、戻り値 0\|16。**能力が 5 操作に固定** |
| (iii) 自前 | — | 自由だが host も全部自作 |

**軸は「能力を後から足せるか」**。(i) は host が任意の機械語を送れるので能力が開いている。
(ii) は 5 操作に閉じているぶん契約が単純で、**WCH 純正 probe 経路がそのまま使える**。

そして **(i) と (ii) は排他ではありません**。(ii) は probe 側の経路なので、
**BL が壊れたときの復旧路**として同じ target に併存できる。「BL 経由は (i)、救出は (ii)」という組み合わせも取れる。

## 8. 費用対効果が悪い候補(non-goals の候補)

切り捨てを決めたわけではなく、**コストが偏っている箇所**の記録。

- **WCHMcuIAP 互換**。§3.2a の A を選べば得られ、B/C を選べば失う。**D1 と同じ判断の裏表**
- **V103**。protocol 世代が単独で違い(sync head `57 AB`、`CMD_JUMP_IAP` 無し、`CalAddr` 無し。F03)、
  driver も単独形。**1 series のために protocol と driver の両方に分岐が要る**ので、単位コストが最も高い
- **BLE / Ethernet transport**。EVT の実装は A/B slot 前提で BL が 16〜40 KB あり、UART/USB とは別設計(F31/F32)
- **HOST_IAP(USB host)**。BL 予約が UART/USB より +12〜24 KB(F38)。「20 KB 予約」の前提が通らない

---

## 9. 事実で決着したもの / まだ選択肢のもの

**この 2 つは性質が違う**ので分けて置く。前者は調べれば答えが 1 つに決まるもの、後者は設計判断。

### 9.1 事実で決着(選ぶ余地なし)

| # | 問い | 答え |
|---|---|---|
| **D2** | driver class D と E を畳めるか | **畳める**(F44)。制御列で測れば同形で、差は `words_per_bufload`(4 vs 2)だけ。全体も **erase 1 形 + program 2 形**に縮む |
| **D6** | commit 副作用 `*(0x40022034)` はどこまで要るか | **V103 と M030 の 2 series だけ**(F58)。XOR マスクも違う(`0x1000` / `0x100`)。config 定数 1 個で表せる |

### 9.2 まだ選択肢(設計判断。**この文書では選ばない**)

| # | 論点 | 選択肢 | 何で決まるか |
|---|---|---|---|
| **D1** | APP 存在フラグの極性と置き場 | **A** `!=` のまま / **B** `==` + 印を APP イメージに埋める / **C** B + 印を APP 先頭ヘッダへ(§3.2a) | **EVT host(WCHMcuIAP)を使い続けるか**。使うなら A 一択。使わないなら B/C で blank pattern 依存と `.bin` の穴が消える |
| **D3** | read-modify-write の置き場 | driver の中 / **driver の上(page cache 層)** | 「driver 5 関数」という粒度を保ちたいか(§4.5) |
| **D4** | stub 契約 | (i) minichlink HID scratchpad / (ii) WCH 純正 loader ABI / (iii) 自前 | **能力を後から足したいか**。(i) は開いている、(ii) は 5 操作固定だが純正 probe 経路が使える。**排他ではなく併存可**(§7.1) |
| **D5** | BOOT 領域配置と user flash 配置 | 同一ソース両対応 / ビルド構成を分ける | 差は (a) linker script (b) exit 方式 (c) `FLASH_Base` の **3 点だけ**でいずれも定数。論理的には両対応可。ただし **V003 の 1,920 B は BOOT 側だけで予算が尽きる**(§5) |

**D1 が他に波及する**: B/C を採ると §9b の config から `blank_pattern`(と C なら `APP_MARKER_ADDR` も)が落ちる。
それ以外の D3〜D5 は互いに独立。

---

## 9b. config surface の見積り

移植 1 件に必要なのは **11〜13 個の定数と 2 個の選択**。**数は D1 の選び方で動く**。
値は [`port_matrix.csv`](data/bootloader-survey/port_matrix.csv) と
`ch32-device-data` `evidence/flash_geometry.csv` から引ける(§2)。

| # | 名前 | 出所 | 取りうる値 |
|---|---|---|---|
| 1 | `FLASH_BASE`(APP 先頭) | port_matrix | `0x08000000` / `0x08005000` / `0x08006000` |
| 2 | `BL_MAX_BYTES` | 配置から | 1920 / 3328 / 20K / 24K |
| 3 | `APP_MARKER_ADDR` | port_matrix(`cal_addr`) | Code FLASH 末尾 −4 |
| 4 | `APP_MARKER_VALUE` | 全 series 共通 | `0x5aa55aa5` |
| 5 | `ERASE_GRAN` | flash_geometry | 64 / 128 / 256 / 4096 |
| 6 | `PROGRAM_GRAN` | flash_geometry(`fast_program_bytes`) | 64 / 128 / 256 |
| 7 | `WORDS_PER_BUFLOAD` | reg_ops(F44) | 1 / 2 / 4 |
| 8 | `COMMIT_XOR_MASK` | F58 | **0(不要)** / `0x1000`(V103)/ `0x100`(M030) |
| 9 | `UART_PORT` | port_matrix | USART1 / 2 / 3 |
| 10 | `UART_BAUD` | port_matrix | 460800(V103 のみ 57600) |
| 11 | `ENTRY_GPIO` | port_matrix | PA0 / PC0 / PB4 |
| 12 | `USB_VID` / `USB_PID` | 自前(pid.codes) | — |
| 13 | `SCRATCHPAD_BASE` / `_SIZE` | 実装で決める | RAM 容量に従う |

| # | 選択 | 値 |
|---|---|---|
| S1 | **BL の配置** | BOOT 領域 / user flash 先頭 → §3.3 の exit が自動的に決まる |
| S2 | **program の形** | (a) buffer-then-commit / (b) inline-write-then-commit → §4.3 |

**D1 による増減**:

| D1 の案 | 定数の数 | 差分 |
|---|:-:|---|
| **A**(`!=` のまま) | **13** | 上の 13 に加えて `BLANK_WORD`(`0xFFFFFFFF` / `0xe339e339`)が**必須**。逆に `APP_MARKER_*` は EVT 準拠 |
| **B**(`==` + イメージに埋める) | **12** | `BLANK_WORD` が落ちる |
| **C**(B + 先頭ヘッダ) | **11** | さらに `APP_MARKER_ADDR` が落ちる(Code FLASH 総容量への依存も消える) |

---

## 10. 参照

- 調査結果: [bootloader-survey.ja.md](bootloader-survey.ja.md) / 調査設計: [bootloader-survey-plan.ja.md](bootloader-survey-plan.ja.md)
- 移植パラメータ: [`data/bootloader-survey/port_matrix.csv`](data/bootloader-survey/port_matrix.csv)
- chip の事実: `ch32-device-data` `evidence/flash_geometry.csv`(`blank_check_word` ほか)
- 設計空間の広い議論: [bootloader-design-space.ja.md](bootloader-design-space.ja.md)
- protocol の詳細: [../protocols/wch-iap.ja.md](../protocols/wch-iap.ja.md) / [../protocols/custom-bootloader.ja.md](../protocols/custom-bootloader.ja.md)
