# 統一 bootloader の設計 — 調査結果を実装境界に落とす

状態: **draft**(解読ではなく**自前設計**。実装・実測は未)。
根拠はすべて [bootloader-survey.ja.md](bootloader-survey.ja.md) の所見 ID(`F01`〜`F43`)と
[data/bootloader-survey/](data/bootloader-survey/) の 25 テーブルにある。設計判断のたびに ID を引く。

この文書が答えるのは **「Q2 = 統一 BL は作れるか」の続き**、すなわち **どこで分けるのが最小の分割か**。

---

## 0. 結論(先に)

| 判断 | 内容 | 根拠 |
|---|---|---|
| **分割の主軸は series ではなく driver class** | flash driver は **12 series → 5 class**。unlock/lock は**全 series 同一で分岐不要** | F42 / F43 |
| **protocol 層は分岐不要** | 8 project 以上に出る `#define` の **13 個が全 series 同一、割れるのは実質 2 個**(`FLASH_Base` / `CalAddr`) | F01 / F02 |
| **blank pattern は分岐にしない** | chip の仕様値なので `ch32-device-data` から引く定数。しかも §3.2 の entry 設計を採れば**参照すら不要になる** | F25 |
| **真の障害は 1 つだけ** | `CheckNum` の**判定極性が反転**している(V003/V00X は「APP 正当の印」、他は「BL に留まれの要求」)。**意味が逆なので `#if` では吸収できず、仕様として一本化するしかない** | F07 |
| **言語は層で分ける** | 線上信号 = asm 必須 / BL 本体 = C / stub = asm | H1〜H3、§6 |
| **拡張は stub 側でやる** | BL 本体は固定。scratchpad は ch32fun で BL 本体の 1.9 倍 | H4 / F19 / F20 |
| **サイズ上限は自前で持ち込む** | EVT は 13 project 中 5 つしか強制していない | F13 |

---

## 1. 層と言語

| 層 | 言語 | 根拠 |
|---|---|---|
| **L-a. 線上信号**(software USB の bit-bang 等) | **asm 必須** | サイクル精度。`rv003usb.S` 23,474 B が実例(§6 H1) |
| **L-b. BL 本体**(state machine / transport / entry・exit / flash 呼出) | **C** | rv003usb・ch32fun とも C で成立(H2) |
| **L-c. host が送り込む stub** | **asm** | RV32EC 制約(x0–x15)・位置独立・特殊な呼出規約・最小 8 B(H3 / F17) |

**L-b を asm 化しない**。§7 の実測どおり、BL 本体を縮めても得られる余地は数百 B。拡張は L-c で稼ぐ(H4)。

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

### 3.2 提案: **「APP 正当の印」(V003 側の意味論)に一本化する**

理由は互換性ではなく**フェイルセーフ性**。

| | `==`(APP 正当の印) | `!=`(BL に留まれの要求) |
|---|---|---|
| flash が blank のとき | `blank != CheckNum` → **印なし → BL に留まる**。安全 | `blank != CheckNum` → **APP へ跳ぶ**。APP は無いので暴走 |
| 暴走を防ぐ手段 | 不要 | `FLASH_Base != blank` の外側チェックが**必須** |
| **blank pattern への依存** | **無い** | **ある**(family ごとに `0xFFFFFFFF` / `0xe339e339` を正しく選ぶ必要) |

→ **`==` を採ると、entry 判定から blank pattern 依存が消える**。F25 で「family から引く定数」に降格させた項目が、
設計次第で**参照すら不要**になる。§2 の config surface から 1 項目減らせる。

代償: **既存の WCHMcuIAP と非互換になる**(10 series ぶん)。統一 BL は host も自作する前提なので許容できるが、
「EVT の host をそのまま使いたい」場合は `!=` 側に倒すことになる。**その場合は blank pattern が必須依存に戻る**。

> **決めること**: host も自作するか、WCHMcuIAP 互換を残すか。ここが設計の分岐点で、
> 他のすべては定数で吸収できる。

### 3.3 exit は配置から自動的に決まる(F08)

| BL の配置 | exit |
|---|---|
| **BOOT 領域常駐**(V003 1,920 B / V00X・X035 3,328 B) | `BOOT_MODEKEYR` 解錠 → `FLASH_STATR` bit14 クリア → system reset |
| **user flash 先頭** | Software IRQ を pending にして、ハンドラから APP へ跳ぶ |

これは選択ではなく帰結なので、**config ではなく配置マクロ 1 個**で決まる。

---

## 4. flash driver — 5 class(F42/F43)

```
flash_unlock()  /  flash_lock()     ← 12 series で完全同一。分岐なし
flash_erase(addr, size)             ← class ごとに実装
flash_program(addr, buf, size)      ← class ごとに実装
```

| class | series | program の形 |
|---|---|---|
| **A** | V003 | `CTLR` 直叩き(BUFRST → 16×BUFLOAD → ADDR → STRT)。粒度 64 B |
| **B** | V00X / V205 / X035 / L103 | `ROM_WRITE(adr, buf, 256)` 相当 |
| **C** | V20x / V30x / V407 / X315 / H417 | `ProgramPage_Fast(adr, buf)` 相当(2 引数) |
| **D** | V103 | `BufLoad` を 4 word ずつ 8 回 + `ProgramPage_Fast(adr)`。粒度 128 B |
| **E** | M030 | `BufLoad` を **2 word ずつ** + `ProgramPage_Fast(adr)`。粒度 128 B |

**D と E は `ErasePage_Fast` / `ProgramPage_Fast` では同群**になり、`BufLoad` と `ROM_WRITE` で分かれる。
`BufLoad` を使わない実装にできれば **4 class に縮む**可能性がある(要検証)。

粒度は §2 の config から引く。**class と粒度は独立**(同じ class でも粒度が違う: B の V00X=256 / …)。

---

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

## 7. 拡張は stub 側 — scratchpad の契約

BL 本体を固定したまま能力を足す(H4)。契約は minichlink の HID scratchpad 方式に合わせるのが実績があって安全。

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

## 8. 作らないもの(non-goals)

- **WCHMcuIAP 互換**。§3.2 で `==` に倒すなら互換は捨てる。倒さないなら §3.2 の利点も捨てる
- **V103 対応を最初から入れる**。protocol 世代が単独で違い(sync head `57 AB`、`CMD_JUMP_IAP` 無し、`CalAddr` 無し。F03)、
  driver も単独 class(D)。**費用対効果が最も悪い**ので後回しにする
- **BLE / Ethernet transport**。EVT の実装は A/B slot 前提で BL が 16〜40 KB あり、別設計(F31/F32)

---

## 9. 未決

| # | 内容 | 決め方 |
|---|---|---|
| **D1** | §3.2 の極性。**host も自作するか、WCHMcuIAP 互換を残すか** | 設計判断。他のすべてがこれに従属する |
| **D2** | driver class D と E を `BufLoad` 非依存の実装で 1 本に畳めるか | 実装して `reg_ops` で等価性を確認 |
| **D3** | V407 / X315 / H417 の read-modify-write(fast erase 非対応)をどこに置くか。driver 内か上位か | 実装判断 |
| **D4** | scratchpad の契約を minichlink 互換にするか、自前にするか | 互換なら既存 host が使える。自前なら `a0` の使い方を自由にできる |
| **D5** | BOOT 領域配置と user flash 配置を**同一ソースで両対応**にするか、別ターゲットにするか | §3.3 の exit と linker が連動する |

---

## 10. 参照

- 調査結果: [bootloader-survey.ja.md](bootloader-survey.ja.md) / 調査設計: [bootloader-survey-plan.ja.md](bootloader-survey-plan.ja.md)
- 移植パラメータ: [`data/bootloader-survey/port_matrix.csv`](data/bootloader-survey/port_matrix.csv)
- chip の事実: `ch32-device-data` `evidence/flash_geometry.csv`(`blank_check_word` ほか)
- 設計空間の広い議論: [bootloader-design-space.ja.md](bootloader-design-space.ja.md)
- protocol の詳細: [../protocols/wch-iap.ja.md](../protocols/wch-iap.ja.md) / [../protocols/custom-bootloader.ja.md](../protocols/custom-bootloader.ja.md)
