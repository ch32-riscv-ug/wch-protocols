# bootloader 横断調査 — 分析結果(第 1 回)

状態: **attested**(WCH 公式 EVT 12 series の IAP 13 project + OSS BL 3 + stub 51 を実ソースから機械抽出。実機 capture 未)。
調査設計: [bootloader-survey-plan.ja.md](bootloader-survey-plan.ja.md) / 生データ: [data/bootloader-survey/](data/bootloader-survey/)(15 テーブル・約 1,400 行 + stub の hex/逆アセンブル 34 対)

この文書の主張はすべて `data/bootloader-survey/findings.csv` の行(`F01`〜`F24`)に対応し、各行は CSV 経由で原典の行番号まで辿れる。

---

## 0. 結論(先に)

| 問い | 答え |
|---|---|
| **Q1** 差はどの軸から生まれるか | **series 軸ではない。「EVT サンプルの系譜」軸**。設計クラスタは 7 群で、`CH32V407` と `CH32X315` が完全一致、`CH32V205` と `CH32L103` が完全一致など、**系列名を横断して束になる**(F04/F05) |
| **Q2** 統一 BL は作れるか | **protocol 層は作れる**。8 project 以上に現れる `#define` のうち **13 個が全 project 同一値、割れるのは 5 個だけ**(F02)。難所は protocol ではなく **entry/exit と flash driver** |
| **Q3** V003 は特殊か | **特殊。ただし「V003 が」ではなく「V003+V00X が」**。この 2 つだけ BOOT 領域常駐・`==` 極性・BOOT_MODE レジスタ経由の jump(F07/F08/F13)。V003 単独で特殊なのは flash 粒度 64 B と inline 実装だけ(F09/F10) |
| **Q4** stub にどこまで差を押し出せるか | **押し出せている**。b003 系 stub **47 本すべてが x0–x15 のみ**で RV32EC/IMAC 両対応(F17)。機能 1 個 = **8〜120 B** で BL の flash 予算を消費しない(F19) |
| **Q5** 容量に何が入るか | BL 本体は 1,920 B(V003)〜 20 KB。**拡張余地は flash ではなく RAM の scratchpad** にあり、ch32fun では **BL 本体の 1.9 倍**(F20) |
| **Q6** protocol は 3 世代で足りるか | **足りない。4 世代**。UART 世代 A / V103 世代 B / UART+USB 生バイト descriptor 世代 C / **USB マクロ + Vendor·HID 切替の世代 D**(F11) |
| **Q7** どの言語で書くべきか | **層で分ける。BL 本体は C、stub は asm**。H1〜H4 は棄却されず、H3 は実測で裏付いた(§6) |

**いちばん効く発見**: 統一 BL を阻むのは series の多さではなく、**同じ定数 `CheckNum` の判定極性が反転している**(F07)ことと、**APP 存在判定の blank pattern が 2 系統ある**(F06)こと。どちらも series では説明できず、`#if` で吸収するとき最初に踏む地雷。

---

## 1. Q1 — 差はどの軸から生まれるか

### 1.1 属性行列(13 IAP project × 10 属性)

| project | sync | blank | 極性 | exit | VID | descr | erase API | program API | erase 粒度 | UART |
|---|---|---|:-:|---|---|---|---|---|---:|---|
| v003 | `aa 55` | `FFFFFFFF` | **`==`** | **boot-mode** | — | — | ErasePage_Fast | *(inline)* | **64** | USART1 |
| v00x | `aa 55` | `FFFFFFFF` | **`==`** | **boot-mode** | — | — | ROM_ERASE | ROM_WRITE | 256 | USART1 |
| v103 | **`57 ab`** | — | — | sw-irq | **`4348`** | raw | ErasePage_Fast | BufLoad+PPF | *(128)* | USART3 |
| v205 | `aa 55` | `FFFFFFFF` | `!=` | sw-irq | `1A86` | **macro** | ROM_ERASE | ROM_WRITE | 256 | USART2 |
| v20x | `aa 55` | **`e339e339`** | `!=` | sw-irq | **`4348`** | raw | ErasePage_Fast | ProgramPage_Fast | 256 | USART3 |
| v30x | `aa 55` | **`e339e339`** | `!=` | sw-irq | `1A86` | raw | ErasePage_Fast | ProgramPage_Fast | 256 | USART3 |
| v407 | `aa 55` | **`e339e339`** | `!=` | sw-irq | `1A86` | **macro** | ROM_ERASE | ROM_WRITE | **4096** | USART2 |
| x035 | `aa 55` | `FFFFFFFF` | `!=` | sw-irq | `1A86` | raw | ErasePage_Fast | BufLoad+PPF | 256 | USART2 |
| x315 | `aa 55` | **`e339e339`** | `!=` | sw-irq | `1A86` | **macro** | ROM_ERASE | ROM_WRITE | **4096** | USART2 |
| l103 | `aa 55` | `FFFFFFFF` | `!=` | sw-irq | `1A86` | **macro** | ROM_ERASE | ROM_WRITE | 256 | USART2 |
| m030 | `aa 55` | `FFFFFFFF` | `!=` | sw-irq | `1A86` | raw | ErasePage_Fast | BufLoad+PPF | **128** | USART1 |
| h417-v3f | `aa 55` | **`e339e339`** | `!=` | sw-irq | `1A86` | **macro** | — | ProgramPage_Fast | — | — |
| h417-v5f | `aa 55` | **`e339e339`** | `!=` | sw-irq | `1A86` | **macro** | — | ProgramPage_Fast | — | — |

出典: `entry_exit.csv` / `usb.csv` / `flash_ops.csv` / `clock_uart.csv` / `protocol.csv`

### 1.2 相互一致数(10 属性中)— **完全一致するペアが 3 組**

| ペア | 一致 | 備考 |
|---|:-:|---|
| **v205 ↔ l103** | **10 / 10** | `CH32V205`(汎用)と `CH32L103`(低消費電力)が完全同一 |
| **v407 ↔ x315** | **10 / 10** | `doc_date` も同じ **2025/12/01** |
| **h417-v3f ↔ h417-v5f** | **10 / 10** | IAP 本体が `Common/` 共有なので当然 |
| v20x ↔ v30x | 9 / 10 | 差は **VID のみ**(`4348` vs `1A86`) |
| x035 ↔ m030 | 8 / 10 | |
| v003 ↔ v00x | 7 / 10 | 差は flash API と粒度 |
| **v103 ↔ 他すべて** | **0〜5 / 10** | v00x とは **0/10**。完全な孤立点 |

### 1.3 設計クラスタは 7 群、しかも系列名と一致しない

`(blank, polarity, exit, program API)` で分類:

| 群 | 構成 | 性格 |
|---|---|---|
| 1 | `v003` | BOOT 領域常駐 / 64 B inline |
| 2 | `v00x` | BOOT 領域常駐 / 256 B ROM_WRITE |
| 3 | `v103` | 2020 年世代 |
| 4 | `v205`, `l103` | `FFFFFFFF` + ROM_WRITE |
| 5 | `v20x`, `v30x`, `h417-v3f`, `h417-v5f` | `e339e339` + ProgramPage_Fast |
| 6 | `v407`, `x315` | `e339e339` + ROM_WRITE + 4 KB erase |
| 7 | `x035`, `m030` | `FFFFFFFF` + BufLoad |

**13 series → 7 設計クラス**。そして群 4・6・7 はいずれも系列名(V/X/L/M)を横断する。

### 1.4 系譜の裏付け — `doc_date`

| doc_date | project |
|---|---|
| **2020/12/16** | `v103` ← 他より 4 年以上古い。孤立点の原因 |
| 2025/01/09 | `v003`, `v205`, `v20x`, `v30x` |
| 2025/01/13 | `v00x`, `l103`, `m030` |
| 2025/10/27 | `x035` |
| **2025/12/01** | `v407`, `x315` ← 完全一致ペア |

→ **差は「どの時点のサンプルからコピーされたか」で決まっている**。チップの都合(core / 電圧 / 容量)は、flash driver と一部の粒度にしか現れない。

> **Q1 の答え**: 一次的な軸は **EVT サンプルの系譜(コピー元と時期)**。series は二次的で、core・電圧は三次的。

---

## 2. Q2 — 統一 BL は作れるか

### 2.1 protocol 層: **ほぼ完全に共通**

13 IAP project の `#define` を横断集計(`constants.csv` 602 行、8 project 以上に現れるもの):

| 全 project で同一値 — **13 個** | 値 |
|---|---|
| `CMD_IAP_PROM` / `ERASE` / `VERIFY` / `END` / `JUMP_IAP` | `0x80` / `0x81` / `0x82` / `0x83` / `0x84` |
| `ERR_SUCCESS` / `ERR_ERROR` / `ERR_End` | `0x00` / `0x01` / `0x02` |
| `CheckNum` | `(0x5aa55aa5)` |
| `USBD_DATA_SIZE` | `64` |
| `UPGRADE_MODE` / `_COMMAND` / `_IO` | `UPGRADE_MODE_COMMAND` / `0` / `1` |

| 割れる — **5 個だけ** | 通り数 | 値 |
|---|:-:|---|
| `CalAddr` | 7 | `0x08004000-4` … `0x080F8000-4` |
| `FLASH_Base` | 3 | `0x08000000` / `0x08005000` / `0x08006000` |
| `isp_cmd_t` | 2 | バッファ変数名の違いだけ(`EP2_Rx_Buffer` / `IAP_Deal_Buf`) |
| `Uart_Sync_Head1` / `2` | 各 2 | `0xaa,0x55` / **V103 のみ** `0x57,0xab` |

→ **protocol は `#define` 5 個で全 series を吸収できる**。しかもうち 1 個は変数名、2 個は V103 だけ。**実質 `FLASH_Base` と `CalAddr` の 2 個**。

### 2.2 吸収の難易度で 3 段階に分ける

| 段階 | 項目 | 対処 |
|---|---|---|
| **A: 定数で吸収**(易) | `FLASH_Base`, `CalAddr`, sync head, UART port(3 通り), baud(2 通り), VID(2 通り), GPIO pin(3 通り), scratchpad 番地 | `#if` / linker symbol / 設定ヘッダ 1 枚 |
| **B: 関数差替で吸収**(中) | flash erase/program(**4 API × 4 粒度**)、USB descriptor(生バイト / マクロ)、周辺 de-init 列 | driver 関数 3 本(`erase` / `program` / `wait`)+ 粒度定数。→ §4 |
| **C: 構造が違う**(難) | **極性反転**(`==` vs `!=`)、**blank pattern 2 系統**、**exit 方式 2 系統**(BOOT_MODE レジスタ vs Software IRQ)、V103 の protocol 世代 | ここだけは条件分岐ではなく**仕様として一本化を決める**しかない |

### 2.3 段階 C の中身(統一 BL の実際の障害)

**(a) 極性反転** — `entry_exit.csv`

```c
// v003 / v00x : CalAddr が CheckNum と一致 → APP へ
if (*(uint32_t*)CalAddr == CheckNum) { IAP_2_APP(); }
// 他 10 project : 一致しない → APP へ
if (*(uint32_t*)CalAddr != CheckNum) { IAP_2_APP(); }
```

同じ `CheckNum = 0x5aa55aa5` を使いながら**意味が逆**。前者は「フラグが立っていたら APP」、後者は「フラグが立っていたら BL に留まる」。host 側(WCHMcuIAP)は書き込む値でこれを制御するので、**BL と host の契約がクラスタで違う**。

**(b) blank pattern 2 系統**

`0xFFFFFFFF`(消去後の生値)= v003, v00x, v205, x035, l103, m030
`0xe339e339` = v20x, v30x, v407, x315, h417×2

後者は「消去後」ではなく**特定の命令パターン**。統一するなら「両方を空とみなす」で吸収できるが、**片方だけ実装すると起動しない**。

**(c) exit 方式 2 系統**

```c
// v003 / v00x: BOOT 領域から user 領域へ切り替えて reset
FLASH_Unlock(); FLASH->BOOT_MODEKEYR = KEY1; FLASH->BOOT_MODEKEYR = KEY2;
FLASH->STATR &= ~(1<<14); FLASH_Lock(); NVIC_SystemReset();
// 他 11: Software 割込みを pending にするだけ(ハンドラが APP へ跳ぶ)
NVIC_EnableIRQ(Software_IRQn); NVIC_SetPendingIRQ(Software_IRQn);
```

これは**BL の置き場所(BOOT 領域 / user flash 先頭)から必然的に決まる**ので、統一 BL では「置き場所」を選んだ時点で自動的に決まる。段階 C の中では唯一きれいに分岐できる。

> **Q2 の答え**: **作れる。ただし `#if` の数ではなく「段階 C を仕様として一本化する」ことが本体**。protocol は既にほぼ統一されており、労力は entry/exit の契約と flash driver に集中する。

---

## 3. Q3 — V003 は特殊か

**特殊。ただし境界は「V003」ではなく「V003 + V00X」**。

| 項目 | v003 | v00x | 他 11 |
|---|---|---|---|
| BL の置き場所 | **BOOT 領域 1,920 B** | **BOOT 領域 3,328 B** | user flash 先頭 20 KB(H417 は 24 KB) |
| `FLASH_Base`(= APP 先頭) | `0x08000000` | `0x08000000` | `0x08005000` / `0x08006000` |
| 極性 | **`==`** | **`==`** | `!=` |
| exit | **BOOT_MODE レジスタ + reset** | 同左 | Software IRQ |
| GPIO entry pin | **PC0** | **PC0** | PA0(m030 のみ PB4) |
| USB | **無し**(UART only) | **無し** | あり |
| `.ld` | **IAP 側にあり(1920)** | **IAP 側にあり(3328)** | APP 側にあり |
| 出力形式 | **`.bin`** | **`.bin`** | `.hex` |

**V003 単独で特殊なのは 2 点だけ**:
- flash 粒度 **64 B**(V00X 以降はすべて 128 B 以上)
- flash program が **唯一 inline のレジスタ直叩き**(`FLASH->CTLR` の `PAGE_PG` / `BUF_RST` / `BUF_LOAD` / `STRT` を手で並べる)。他は WCH の SDK 関数を呼ぶ

さらに **core が RV32EC(x0–x15)** なのは V003/V00X だけで、これは stub 設計に効く(§5)。

> **Q3 の答え**: 「V003 が特殊」は**やや不正確**。正しくは **「BOOT 領域常駐グループ(V003 + V00X)が特殊」**で、その中で V003 が flash 粒度と実装形態でさらに外れる。統一 BL では **V003/V00X を 1 つの variant として括る**のが自然。

---

## 4. Q5 と flash driver — 差替が必要な最小単位

`flash_ops.csv` 47 行から:

| program API | project | 引数の形 |
|---|---|---|
| inline レジスタ直叩き | v003 | — |
| `FLASH_ROM_WRITE(adr, buf, 256)` | v00x, v205, v407, x315, l103 | **5** |
| `FLASH_ProgramPage_Fast(adr, buf)` | v20x, v30x, h417×2 | **4**(2 引数) |
| `FLASH_BufLoad(...)` ×N + `FLASH_ProgramPage_Fast(adr)` | v103, x035, m030 | **3**(1 引数) |

erase 粒度は **4 種**: 64 B(v003)/ 128 B(m030, v103)/ 256 B(v00x, v205, v20x, v30x, x035, l103)/ **4096 B**(v407, x315)。

注目すべき細部:
- **M030 の erase マスクは `0xFFFFFF80` = 128 B** で、同じ「BufLoad 群」の x035(`0xFFFFFF00` = 256 B)と違う
- **M030 の `FLASH_BufLoad(adr+4*j, buf[j], buf[j+1])` は 2 word ずつ**渡す。x035 は `FLASH_BufLoad(adr+4*i, buf[i])` で 1 word。**同じ関数名で引数の数が違う**
- v407 / x315 だけ erase が 4 KB。**「256 B 書きたいのに 4 KB 消す」**ので read-modify-write が要る

→ 差替が必要なのは **`erase(addr, size)` / `program(addr, buf, size)` / `wait()` の 3 本 + 粒度定数 2 個(erase_gran, program_gran)**。API 名の違いは薄いラッパで吸収でき、**本質的な差は粒度だけ**。

---

## 5. Q4 — stub にどこまで差を押し出せるか

### 5.1 実測: **b003 系 stub 47 本すべてが RV32EC 安全**

`stubs.csv` の `rv32ec_safe` 列(逆アセンブルして使用レジスタ集合を機械判定):

| 群 | 本数 | 判定方法 | `rv32ec_safe` | 使用レジスタ |
|---|---:|---|:-:|---|
| minichlink inline hex(b003) | **15**(有効 13 + コメントアウト 2) | 逆アセンブル | **すべて 1** | 最大でも `x5 x6 x10..x15` |
| 生成 header(`stubs/b003/*.h`) | **15** | 逆アセンブル | **すべて 1** | 同上 |
| asm ソース(`.S` 15 + `.asm` 2) | **17** | ソース表記の ABI 名 | **すべて 1** | 同上 |
| 小計(b003 系) | **47** | — | **47 / 47 が 1** | |
| **WCH 純正 flashloader blob** | **4** | 逆アセンブル | **すべて 0** | `x16` 以降を使用 |

逆アセンブルで実測したのは 30 本(hex 15 + 生成 header 15)、asm 17 本はソースのレジスタ表記から集計。

`stubs/b003/Makefile` は `-march=rv32imac_zicsr` でビルドしているので、**x0–x15 の制限は toolchain ではなく人手の規律**。逆に WCH 純正 blob は V003 で走らせる必要が無いので規律が無い。

→ **「V003 で走らせる必要があるか」が、そのままレジスタ規律の有無になっている**。これは §6 の H3 を実測で裏付ける。

### 5.2 実測: stub 1 個のコスト

| stub | バイト |
|---|---:|
| `run_app_new` | **8** |
| `halt_wait` | **10** |
| `write64_flash` | 48 |
| `byte/half/word_wise_read/write` | 各 48 |
| `erase_block` | 52 |
| `write_block` / `write_block_v20x_v30x` | 各 104 |
| `run_app` | 120 |
| ch5xx 系(12 本) | 26 〜 388 |

対して BL 本体は **rv003usb 1,916 B / ch32fun 3,328 B(X035)** で固定。scratchpad は **rv003usb 128 B / ch32fun 6,144+128 B**。

→ **ch32fun では拡張領域(RAM 6,272 B)が BL 本体(flash 3,328 B)の 1.9 倍**。「機能を足す余地」は flash ではなく RAM にある。

### 5.3 stub にも series 差は漏れている

- `write_block_bin` と `write_block_bin_v20x_v30x` が**別実装**(BUSY/WRBUSY 待ちの違い)。104 B で同サイズ
- ただし **inline hex と生成 header はバイト完全一致**(`write_block` / `write_block_v20x` を `stubs_hex/` で `cmp` して確認)
- **例外**: `erase_block` は 52 B 同士なのに **2 byte ずれる**(末尾の `01 00` の位置)。→ **二重管理で片方が古い可能性**。`equiv_groups.csv` に `confidence=conflict` で記録

> **Q4 の答え**: 押し出せている。**series 差が stub 側に漏れているのは 1 箇所(V20x/V30x の待ち条件)だけ**で、それも同サイズの別 blob で吸収できている。RV32EC 制約も 31 本すべてクリア。

---

## 6. Q7 — どの言語で書くべきか(仮説の検証)

| 仮説 | 判定 | 根拠 |
|---|---|---|
| **H1** 層ごとに言語が決まる | **支持** | `rv003usb.S`(23,474 B、サイクル精度の bit-bang)+ `rv003usb.c`(11,643 B、state machine)の分業が実在。BL 本体は 2 実装とも C |
| **H2** C で BL 本体は成立するが V003 では予算ギリギリ | **支持**(定量は未) | rv003usb は C で 1,916 B に収まる。ただし `noreturn` に *"saves 2-4 bytes"*、`Delay_Ms` を 2 命令の inline asm に置換して *"helps to fit into a tight BOOT area"* とコメント。**機能フラグ 1 個あたりの増分バイト実測は未実施** |
| **H3** stub は asm。理由はサイズより制約 | **実測で支持** | §5.1。47/47 が x0–x15(うち 30 本は逆アセンブルで実測)。C(`-march=rv32imac`)では compiler が x16+ を使い V003 で動かない。最小 stub は **8 B**(`run_app_new` = `lw a3,-4(a0); jr a3`)で、C の関数フレームが成立しない規模 |
| **H4** 機能を足す余地は asm ではなく stub 側で稼ぐ | **支持** | §5.2。BL 本体 3,328 B に対し scratchpad 6,272 B。stub 1 個 8〜120 B。**BL を asm 化して 30 % 縮めても得られる余地は約 1 KB、stub 方式なら 6 KB が最初からある** |

> **Q7 の答え**: **BL 本体は C、stub は asm、線上信号は asm**。言語を統一する理由は無く、統一 BL の設計目標は「BL を小さく保つ」ではなく **「BL を小さく保ったまま scratchpad と stub の契約を series 間で統一する」**。

---

## 7. 副産物 — 既存文書の更新が必要な点

| # | 内容 | 影響 |
|---|---|---|
| **1** | **USB VID の矛盾は解決**。`0x4348` = v103 / v20x、`0x1A86` = 他 9。PID は全て `0x55E0`。既存 doc の `1A86:55E0` も V20x sample の `4348:55E0` も**どちらも正しい** | `protocols/custom-bootloader.ja.md` §2、`protocols/wch-iap.ja.md` |
| **2** | **世代は 3 つではなく 4 つ**。世代 D(`DEF_USB_VID` マクロ + **Vendor / HID モード切替**、`PID_HID = 0xFE17`)が v205 / v407 / x315 / l103 / h417 に存在。既定は 4 つが HID、**x315 だけ VENDOR** | `protocols/wch-iap.ja.md` |
| **3** | 「9 series が `FLASH_ProgramPage_Fast` に収束」は**誤り**。実際は 4 API に割れ、最大群は `FLASH_ROM_WRITE`(5) | 本 repo の従来メモ |
| **4** | IAP 側の `.ld` が実際にサイズを縛るのは 13 中 **5 つだけ**(x315 と h417-v5f は `.ld` があるが series 既定と同値) | `bootloader-survey-plan.ja.md` §8.8(反映済み) |

---

## 8. 未解決 / 次にやること

| # | 内容 | 効く問い |
|---|---|---|
| **U1** | **`CalAddr` の妥当性**。`ch32-device-data/index/parts.csv` と突き合わせると v20x の `CalAddr`(223 KB)は family 最小品種(32 KB)の flash 外。v30x / v407 / x315 は parts.csv の `flash_bytes` を超える(parts.csv が zero-wait 領域のみを数えている可能性)。**RM と突合が必要**(F15、`confidence=conflict`) | Q2 / Q5 |
| **U2** | `erase_block` の inline hex と生成 header の **2 byte 差**(F18)。どちらが正か | Q4 |
| **U3** | H2 の定量化。rv003usb の機能フラグ(timeout / button / host 検出)を 1 つずつ有効にして**増分バイトを実測**。toolchain は `riscv32-wch-elf-gcc 15.2.0`(`rv32ec_zca` 対応)が使える | Q7 |
| **U4** | `stub_args.csv` が 2 行しか埋まっていない。scratchpad の引数配置を `pgm-b003fun.c` の呼び出し側から起こす | Q4 |
| **U5** | `reg_ops.csv` を検証セット(5 実装)から全 project へ展開 | Q1 / Q2 |
| **U6** | 副対象(ETH_IAP 2 / HOST_IAP 13 / BLE IAP 4 / BootAsUser 3)が未収録 | Q5 |
| **U7** | 未取得の OSS BL(`wch-uf2` / Swindle DFU / PlumBL / tinyboot) | Q6 |

---

## 9. 参照

- 調査設計(軸・スキーマ・方法論): [bootloader-survey-plan.ja.md](bootloader-survey-plan.ja.md)
- 生データと再実行手順: [data/bootloader-survey/README.ja.md](data/bootloader-survey/README.ja.md)
- protocol 仕様: [../protocols/wch-iap.ja.md](../protocols/wch-iap.ja.md) / [../protocols/custom-bootloader.ja.md](../protocols/custom-bootloader.ja.md)
- 設計空間: [bootloader-design-space.ja.md](bootloader-design-space.ja.md)
