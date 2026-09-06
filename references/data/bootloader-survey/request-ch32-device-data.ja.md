# 作業依頼: flash 消去後の読み出し値を `flash_geometry` に追加してほしい

- **依頼元**: `wch-protocols`(bootloader 横断調査 / [../../bootloader-survey.ja.md](../../bootloader-survey.ja.md))
- **依頼先**: `ch32-riscv-ug/ch32-device-data`
- **起票日**: 2026-09-06
- **要求 ID(案)**: `R-31`(`docs/worklist.ja.md` の「consumerからの依頼」に採番してください。既存の最大は R-30)
- **この文書は依頼のみ**。`ch32-device-data` 側のファイルには**一切手を入れていません**。

---

## 1. 何が欲しいか

**flash を消去したあとに読み出される値**を family ごとに持ちたい。

CH32 には **2 系統**ある。

| 系統 | word 読み | half 読み | 偶アドレス byte | 奇アドレス byte |
|---|---|---|---|---|
| A | `0xFFFFFFFF` | `0xFFFF` | `0xFF` | `0xFF` |
| B | **`0xe339e339`** | **`0xe339`** | **`0x39`** | **`0xe3`** |

RM に明文がある(§4)。**現状 `ch32-device-data` のどの表にもこの事実が無い**(`evidence/` 全 CSV と `curated/` を `e339` / `erased` で検索して 0 件。`evidence/link_firmware.csv` の 1 件は sha256 の偶然一致)。

## 2. なぜ要るか(consumer 側の用途)

bootloader は起動時に **「APP が焼かれているか」を flash 先頭の値で判定**する。この判定値がまさに上の 2 系統に対応している。

```c
// CH32X035 / L103 / M030 / V205 / V00X の EVT IAP
if (*(uint32_t*)FLASH_Base != 0xFFFFFFFF) { /* APP あり */ }
// CH32V20x / V30x / V407 / X315 / H417 の EVT IAP
if (*(vu32 *)FLASH_Base != 0xe339e339)     { /* APP あり */ }
```

**間違えると BL が壊れる**:
- 系統 B のチップに `0xFFFFFFFF` 判定を書くと、**APP が無いのに「ある」と誤判定**して存在しない APP へ跳ぶ
- 系統 A のチップに `0xe339e339` 判定を書くと、**APP があるのに毎回 BL に留まる**

「1 ソース + `#if` で全 series 対応の bootloader を書けるか」を検討しており、この値は **build 時に family から引きたい定数**。いま consumer 側で family ごとに手書きすると、`flash_geometry.csv` を作った動機(「consumer 側で手書きすると family の数だけ誤記リスク」)とまったく同じ問題になります。

## 3. 追加先の提案

`evidence/flash_geometry.csv` に列を足すのが自然。**この CSV は生成物**なので、実体の修正先は `tools/build_flash_geometry.py`。

### 列の案

| 列 | 内容 | 例 |
|---|---|---|
| `erased_read_word` | word 読みの値 | `0xFFFFFFFF` / `0xe339e339` |
| `erased_read_half` | half word 読み | `0xFFFF` / `0xe339` |
| `erased_read_byte_even` | 偶アドレス byte | `0xFF` / `0x39` |
| `erased_read_byte_odd` | 奇アドレス byte | `0xFF` / `0xe3` |

- 系統 A の RM は **word の値しか書いていない**(`字读- 0xFF`)ので、half / byte 列は**空**にするか、`0xFF` を導出値として入れるかは判断をお願いします。**空にして word だけ持つのが安全**だと思います(RM が言っていないことを書かない、という既存方針に沿う)。
- 系統 B は 4 つとも RM が明記しているので全部埋まります。

> **注意**: 系統 A の RM 原文は `字读- 0xFF` と **8bit 幅で書いてある**が、これは `0xFFFFFFFF` の意味(word 読みの結果)。EVT の IAP コードは `!= 0xFFFFFFFF` で比較している。原文をそのまま `0xFF` と記録するか `0xFFFFFFFF` に正規化するかは、そちらの方針(原文尊重 vs 正規化)に合わせてください。**consumer としては word 幅に正規化されているほうが使いやすい**です。

## 4. 抽出規則(そのまま使えます)

RM の**闪存章**、標準ページ消去と快速ページ消去の手順の直後に `注：` として出ます。**両方の消去方式に同じ注が付く**ので、どちらか先に当たったほうを採れば十分です。

### 中文(こちらを一次にするのを推奨。表現が完全に一定)

```
注：擦除成功后，字读- 0xe339e339，半字读- 0xe339，偶地址字节读- 0x39，奇地址读0xe3。
注：擦除成功后，字读- 0xFF。
```

```python
RM_ERASED = re.compile(
    r"擦除成功后[，,]\s*字读\s*-?\s*(?P<word>0x[0-9a-fA-F]+)"
    r"(?:\s*[，,]\s*半字读\s*-?\s*(?P<half>0x[0-9a-fA-F]+))?"
    r"(?:\s*[，,]\s*偶地址字节读\s*-?\s*(?P<even>0x[0-9a-fA-F]+))?"
    r"(?:\s*[，,]\s*奇地址读\s*(?P<odd>0x[0-9a-fA-F]+))?")
RM_NEEDLE += ("擦除成功后",)   # 既存の needle に足す
```

### 英文(zh に無い family のフォールバック。**表現が 5 通りに揺れる**)

実際に観測した全パターン:

| 原文 | 出た family |
|---|---|
| `After erasing is successful, word read - 0xe339e339, half word read - 0xe339, even address byte read - 0x39, odd address read 0xe3.` | V20x/V30x |
| `After successful erasure, words read -0xe339e339, half words read -0xe339, even address bytes read -0x39, and odd addresses read 0xe3.` | V407, X315, H417 |
| `After successful erasure, the byte reads -0xe339e339, the half-byte reads -0xe339, ...` | V407, X315, H417(快速消去側) |
| `After erasing successfully, read the word-0xFF.` | V006, L103, V205 |
| `After a successful erase, the word read - 0xFF.` / `the word reads - 0xFF.` / `the word is read -0xFF.` | X035, M030, V205 |

> 3 番目に注目 — **`word` と書くべきところを `byte` と誤訳している**(値は 32bit)。英文だけで機械抽出すると幅を取り違えます。**zh を一次、en は突合のみ**を強く推奨します(既存の F-8 で「zh 版で決める」とした方針と同じ)。

## 5. 期待値表(こちらで抽出済み。受け入れ確認に使ってください)

ページ番号は `ch32-device-data-preview` の `<doc>.<lang>/pages/NNNN.md` = PDF のページ番号。

| family | RM | 系統 | `erased_read_word` | zh 明文ページ | en 明文ページ | confidence 案 |
|---|---|:-:|---|---|---|---|
| CH32V003 | CH32V003RM | — | **記述なし** | — | — | `reference`(§6) |
| CH32V006 | CH32V00XRM | A | `0xFF` | 220, 222, 224 | — | `confirmed`(zh) |
| CH32V103 | CH32xRM | — | **記述なし** | — | — | `reference`(§6) |
| CH32V205 | CH32V205RM | A | `0xFF` | 378, 379, 380, 382 | 447, 449, 450, 452 | `confirmed` |
| CH32V20x | CH32FV2x_V3xRM | **B** | `0xe339e339` | 540, 541, 542, 544 | 611, 613, 614, 616 | `confirmed` |
| CH32V307 | CH32FV2x_V3xRM | **B** | `0xe339e339` | 同上 | 同上 | `confirmed` |
| CH32V407 | CH32V407RM | **B** | `0xe339e339` | 522, 523, 525 | 590, 592, 594, 596, 597 | `confirmed` |
| CH32X035 | CH32X035RM | A | `0xFF` | 231, 233, 235 | 245, 247, 248, 250 | `confirmed` |
| CH32X315 | CH32X315RM | **B** | `0xe339e339` | 304, 305, 307 | 358, 360, 363 | `confirmed` |
| CH32L103 | CH32L103RM | A | `0xFF` | 304, 305, 306, 308 | 358, 360, 363 | `confirmed` |
| CH32M030 | CH32M030RM | A | `0xFF` | **なし** | 259 | `reference`(en のみ) |
| CH32H417 | CH32H417RM | **B** | `0xe339e339` | 875, 876, 877 | 1037, 1038, 1041 | `confirmed` |

**CH32M030 は zh 版に注が無く en 版にだけある**(zh 251 ページ / en はそれより多い = 版がずれている。既知の「zh/en で版番号がずれる文書」に M030DS0 が挙がっていますが、RM も同様のようです)。

## 6. RM に記述が無い 2 件 — **実機で確定しました**

| family | 状況 | 結論 |
|---|---|---|
| **CH32V003** | zh/en とも RM に注が無い。消去手順に「读擦除页的数据进行校验」はあるが値を言わない | 系統 **A**。**実機の debug read で確定**(§7b)。傍証: ① EVT IAP(`CH32V003_IAP/User/main.c`)が `!= 0xFFFFFFFF` で判定 ② 同じ QingKe V2 世代の CH32V00X が zh RM で `0xFF` を明記 |
| **CH32V103** | `CH32xRM` に注が無い | 系統 **A**。**実機の debug read で確定**(§7b)。傍証: `FLASH_STATR.PGERR` の説明が **「当你试图对非 "0xFFFF" 内容的地址编程时，硬件置位」** = 消去後が `0xFFFF` である前提。EVT IAP は blank 判定を持たない(GPIO のみ)ので IAP からは裏が取れない |

いずれも RM 原文は無いので `basis` は実測になります(`measured:ch32rv(...)`)。「RM に無いものは入れない」方針であれば**空欄 + `note`「RM に記述なし。実測は A」**でも構いませんが、**consumer は V003/V103 を実際に使う**ので値としては入れてほしいところです。

## 7. 受け入れ確認(こちらで先にやってあります)

抽出した値は、**WCH 自身の EVT IAP サンプルの判定値と 10/10 一致**します。

| family | RM の系統 | EVT IAP の `FLASH_Base` 比較値 | 一致 |
|---|:-:|---|:-:|
| CH32V006 | A | `0xFFFFFFFF` | ✓ |
| CH32V205 | A | `0xFFFFFFFF` | ✓ |
| CH32X035 | A | `0xFFFFFFFF` | ✓ |
| CH32L103 | A | `0xFFFFFFFF` | ✓ |
| CH32M030 | A | `0xFFFFFFFF` | ✓ |
| CH32V20x | B | `0xe339e339` | ✓ |
| CH32V307 | B | `0xe339e339` | ✓ |
| CH32V407 | B | `0xe339e339` | ✓ |
| CH32X315 | B | `0xe339e339` | ✓ |
| CH32H417 | B | `0xe339e339` | ✓ |

出典は当方の [`entry_exit.csv`](entry_exit.csv) の `blank_pattern` 列(各行に EVT のファイル + 行番号あり)。
CH32V003 は IAP が `0xFFFFFFFF`(= A と整合)、CH32V103 は blank 判定を持たないため対象外。

→ **RM 側 5 family と EVT 側 5 family が完全に対応**しているので、抽出が正しければテストは自明に通ります。

## 7b. 実機の debug read(第 3 の独立系統。6 family / 6 一致)

RM(文書)と EVT IAP(WCH のコード)に加え、**実シリコンを debug 経由で読んだ値**が揃っています。出典は `ch32rv`(WCH-Link / WCH-LinkE の flash 実装)の実機検証で、**probe から flash を読んで得た生の値**です。

| family | 実測 part / probe | 読み値 | 系統 | RM/EVT と |
|---|---|---|:-:|:-:|
| CH32V003 | CH32V003F4P6 / WCH-LinkE | `0xff` fill(erase 後) | A | ✓(**RM に記述なしの穴を実測で充填**) |
| CH32V103 | CH32V103R8T6 / WCH-Link(CH549) | `0xff` fill(erase→read→program→erase の往復) | A | ✓(**同上**) |
| CH32X035 | CH32X035C8T6 / WCH-LinkE | `0xff` fill | A | ✓ |
| CH32V20x | CH32V203C8T6 / WCH-LinkE | `39 e3 39 e3` の繰り返し | **B** | ✓ |
| CH32V307 | CH32V307VCT6 / WCH-LinkE | `39 e3 39 e3` の繰り返し(power-off erase 後。**wlink dump も同値**) | **B** | ✓ |
| CH32L103 | CH32L103C8T6 / WCH-LinkE | (`0xff` 前提の page read-modify-write が実機で成立) | A | ✓(間接) |

**この実測が効く点が 3 つあります。**

1. **§6 の 2 件が推定でなくなる**。V003 と V103 は RM に注が無い family ですが、実機で `0xff` を読んでいるので **A で確定**です。`confidence` を `reference` でなく実測相当(`verified` 等、そちらの語彙で)に上げられます。
2. **系統 B の byte 列の並びが裏取りできた**。V203/V307 から読めるバイト列は `39 e3 39 e3 …` で、これは RM の**偶アドレス `0x39` / 奇アドレス `0xe3`** とバイト位置まで一致します(word 値 `0xe339e339` の LE 並び)。§3 の `erased_read_byte_even` / `_odd` 列は、RM だけでなく実機でも正しい値です。
3. **系統 A の half / byte 列を埋めてよい根拠になる**(§3 で保留にした判断)。実機の A 系は**全バイトが `0xff`** なので、`erased_read_half=0xFFFF` / `_byte_even=_byte_odd=0xFF` は導出でなく観測事実です。ただし「RM が言っていないことは書かない」方針を優先するなら**空欄のままで結構**です — consumer 側は word 値があれば足ります。

> **これまでの誤読を 1 つ訂正します。** `ch32rv` と当 repo の protocol ノートは、系統 B の `0xe339e339` を長らく「**WCH-Link firmware が消去済みセルに返す placeholder(実セルは 0xff)**」と書いていました。**これは誤り**で、RM が明記するとおり**チップ自身の消去後の読み出し値**です。probe を介さない RM の記述と一致し、独立実装(wlink)の dump とも一致するため、placeholder 説は成り立ちません。当該記述は [pc-to-link.ja.md](../../../protocols/pc-to-link.ja.md) §6 で訂正済みです。

## 8. 影響範囲(こちらの見込み)

- `evidence/flash_geometry.csv` の列が 4 つ増える(既存 12 行はそのまま)
- `tools/build_flash_geometry.py` に RM 正規表現 1 本と needle 1 語を追加
- `pipeline/baseline/tables.csv` / `tools/check_tables.py` に列数の期待値があれば更新が要るかもしれません
- `index/` 側への波及は無い見込み(family 粒度の evidence なので)

## 9. 補足 — こちらで確認済みの周辺事実(参考。依頼ではありません)

- この注は **標準ページ消去と快速ページ消去の両方**に同じ文言で付く。消去方式によって値は変わらない
- 系統 B の 4 family(V20x/V30x, V407, X315, H417)は、`flash_geometry.csv` で `fast_erase_bytes` が空 or `zero_wait_note` を持つ family と**ほぼ重なる**が、V20x/V307 は `fast_erase_bytes=256` を持つので**完全には一致しない**。別の軸として持つ必要があります
- `0xe339` を RV32 命令として逆アセンブルすると `bnez a4, ...`(圧縮命令)。意味のあるパターンではなく、flash セルの読み出し特性と思われます
