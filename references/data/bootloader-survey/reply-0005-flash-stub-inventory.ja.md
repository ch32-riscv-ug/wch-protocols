# 依頼 0005 への回答: wlink 由来 flash stub 5 本を目録化した

- **依頼元**: `ch32rv`(`docs/data-requests/0005-flash-stub-inventory.ja.md`)
- **回答**: `wch-protocols` `references/data/bootloader-survey/`
- **日付**: 2026-09-06
- **状態**: **納品済み**(2026-09-06 追記で Q4 と純正判定を確定)。§2 の 4 問すべてに答えた。`ch32rv` 側には**一切書き込んでいない**(`crates/flash/src/stub.rs` を読んだだけ)。

## 1. 納品物

| 追加/更新 | 内容 |
|---|---|
| `stubs.csv` | **51 → 56 行**。`wlink-CH32V307` / `-CH32V103` / `-CH32V003` / `-CH643` / `-CH32L103` を追加(`host_tool` = `wlink/flash_op`) |
| `stubs_hex/wlink-*.hex` | 5 本の生バイト(space 区切り 16 進)。**劣化なし** |
| `stub_disasm/wlink-*.asm` | 上を `riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric` した結果。先頭に `fnv1a64` と出所を記載 |
| `wlink_stub_comparison.csv` | **17 行**。§2 の Q1〜Q3 の突き合わせ結果を機械計算で残したもの |
| `equiv_groups.csv` | `eg-wlink-*` を 5 行追加 |
| `stub_args.csv` | `wlink_flash_op` の ABI を 8 行追加 |
| `extract5.py` | 再実行スクリプト(`WCH_ROOT` 配下の `ch32rv/crates/flash/src/stub.rs` を読む) |

**サイズと fnv1a64 は依頼表と全一致**しました。転記は無事です。

| stub | size | fnv1a64 | 依頼表と |
|---|---:|---|:-:|
| `CH32V307` | 446 | `8442a2fdf21f3a2a` | ✓ |
| `CH32V103` | 494 | `742030ff5a123b1b` | ✓ |
| `CH32V003` | 498 | `59bd20460d05dc7b` | ✓ |
| `CH643` | 488 | `e83c3783efcabbc6` | ✓ |
| `CH32L103` | 512 | `fae70418d222d66e` | ✓ |

## 2. 4 つの問いへの回答

### Q1. `CH32L103`(512 B)は minichlink の `linke-flashloader-v1`/`v2`(ともに 512 B)と同一か

**同一ではない。別系統。**

| 比較 | 完全一致 | 先頭 16 B 一致 | `0xff` 除去後の実体長 |
|---|:-:|:-:|---|
| `wlink-CH32L103` vs `linke-flashloader-v1` | ✗ | ✗ | 488 vs 464 |
| `wlink-CH32L103` vs `linke-flashloader-v2` | ✗ | ✗ | 488 vs 416 |

512 という数字が一致していたのは**どちらも 512 B 境界に padding しているから**で、中身は無関係です。

> **ただし別の一致が見つかりました** — §Q3 を先に読んでください。`CH32L103` は同じ wlink 群の `CH643` と**同一 blob**でした。「LinkE 用 loader は 1 系統」ではなく、**wlink 系と minichlink 系という 2 系統が並存**しており、wlink 系の中で 5 本が 4 本に縮みます。

### Q2. `CH32V003` だけ先頭が違う理由 — **RV32EC 専用ビルドで確定**

逆アセンブルで裏が取れました。**先頭 4 byte の差はそのまま prologue の差**です。

```
CH32V307 系: 1101  addi x2,x2,-32   /  ce02  sw x0,28(x2)      → 01 11 02 ce
CH32V003   : 1111  addi x2,x2,-28   /  cc22  sw x8,24(x2)      → 11 11 22 cc
                                       ca26  sw x9,20(x2)   ← s0/s1 を退避している
```

使用レジスタ集合(逆アセンブルから機械判定、`stubs.csv` の `reg_set` / `rv32ec_safe`):

| stub | 最大レジスタ | `rv32ec_safe` |
|---|---|:-:|
| `wlink-CH32V003` | **x15** | **1** |
| `wlink-CH32V307` | x28 | 0 |
| `wlink-CH32V103` | x29 | 0 |
| `wlink-CH643` | x30 | 0 |
| `wlink-CH32L103` | x30 | 0 |

→ **V003 版だけが x0–x15 に収まり、他 4 本は x28〜x30 を使う**。他 4 本は V003 で走りません。想定どおり別ビルドです。
V003 版が s0/s1 をスタックへ退避しているのは、レジスタが 16 本しかないぶんスピルが必要になったためで、**フレームは 28 B と逆に小さい**(退避 2 本ぶんを足しても、失われた caller-saved 領域が効いている)。

> 参考: 同じ `rv32ec_safe` 判定を minichlink 側の b003 stub 47 本にも掛けてあり、**そちらは 47/47 が x0–x15**。これは V003 で走らせる必要があるからで、逆に wlink 側は family ごとに別ビルドを持つので制約を掛けていない、という設計差が見えます。

### Q3. 5 本の相互差分 — **実質 4 本。`CH643` と `CH32L103` は同一 blob**

```
CH643    : 488 B
CH32L103 : 512 B = CH643 の 488 B  +  0xff × 24
           先頭 488 B がバイト完全一致
```

`CH32L103` は `CH643` を 512 B 境界へ `0xff` で埋めただけです。**X035 / CH643 / L103 は 1 本でカバーできます**。

共通接頭辞・接尾辞の全ペア(`wlink_stub_comparison.csv` の Q3 行):

| ペア | 共通接頭辞 | 共通接尾辞 |
|---|---:|---:|
| **CH643 ↔ CH32L103** | **488 B(= 全体)** | 0 |
| CH32V103 ↔ CH643 | 79 B | 1 |
| CH32V103 ↔ CH32L103 | 79 B | 0 |
| CH32V307 ↔ CH32V103 | 42 B | 1 |
| CH32V307 ↔ CH643 | 42 B | 1 |
| CH32V307 ↔ CH32L103 | 42 B | 0 |
| **CH32V003 ↔ 他 4 本すべて** | **0 B** | **0 B** |

**`equiv_group` は 4 つ**に振りました:

| equiv_group | 本数 | カバーする family |
|---|:-:|---|
| `eg-wlink-x035-l103` | 2(実体 1) | X035(`0x0d`)/ CH643(`0x0c`)/ L103(`0x0e`) |
| `eg-wlink-v20x-v30x` | 1 | V20x(`0x05`)/ V30x(`0x06`) |
| `eg-wlink-v103` | 1 | V103(`0x01`) |
| `eg-wlink-v003` | 1 | V003(`0x09`)/ CH641(`0x49`) |

→ **「1 本を source 化すれば何本ぶんカバーできるか」への答え: 4 本書けば 5 blob = 9 family byte 全部**。うち V003 版だけは `-march=rv32ec` の別ビルドが要ります。
42 B / 79 B の共通接頭辞は**共通 preamble**(unlock 判定まで)と見られるので、source 化するなら **共通部 + family 差分**の構成が自然です。ただし接尾辞がほぼ共通でない(0〜1 B)ので、**共通化できるのは前半だけ**です。

### Q4. stub の ABI — **確定しました**(2026-09-06 追記、F45)

`a0` が USB に現れないというご指摘のとおり capture では埋まりませんでしたが、**probe firmware を読むまでもなく
stub 自身の逆アセンブルで全ビットが確定**しました。分岐先が何をするかを追えば意味は一意に決まります。

| 項目 | 内容 |
|---|---|
| エントリ | **blob 先頭(offset 0)**。probe firmware が target の `a0`/`a1`/`a2` を設定して走らせる |
| prologue | `addi x2,x2,-32`(V003 版は `-28` + s0/s1 退避)→ **SP が有効な状態で呼ばれる前提** |
| **`a0`** | **操作ビットマスク**(下記) |
| **`a1`** | **対象アドレス**(bit2 / bit3 / bit4 が使う) |
| **`a2`** | **長さ(byte)**。ページ数 = `(a2+255)>>8`、比較語数 = `(a2+3)>>2` |
| **データ buffer** | **`0x20001000`** 固定。probe が data EP から受けた内容を置く |
| **戻り値** | `a0` に **0 = 成功 / 16 = verify 不一致**。bit4 が立っていなければ無条件で 0 |

| bit | 意味 | 実際のレジスタ操作 | host API 対応 |
|:-:|---|---|---|
| **0** | **unlock** | `KEYR <- KEY1,KEY2` / `MODEKEYR <- KEY1,KEY2` | WriteFlashOP ✓(ご提示のヒントと一致) |
| **1** | **mass erase** | `CTLR \|= 0x4`(MER) | EraseFlash ✓(同上) |
| **2** | **fast page erase ループ** | `(a2+255)>>8` 回: `CTLR \|= 0x20000`(FTER)→ `ADDR = addr` → `CTLR \|= 0x40`(STRT)→ `wait STATR&1` → `CTLR &= ~0x20000`、addr += 256 | — |
| **3** | **program ループ** | `(a2+255)>>8` 回: `CTLR \|= 0x10000`(FTPG)→ buffer `0x20001000` から **64 語 = 256 B** ずつ | WriteFlash と推定 |
| **4** | **verify** | buffer `0x20001000` と `a1` 以降を 4 byte 単位で比較。不一致で `a0 = 16` | — |

**自前ビルドの stub が満たすべき契約**:

1. エントリは blob の先頭(オフセットや header 無し)
2. **`a0` のビットマスク 5 ビットを解釈する**
3. **`a1` / `a2` をアドレスと長さとして受ける**
4. **データは `0x20001000` から読む**
5. **`a0` に 0(成功)/ 16(verify 不一致)を返す**

出典は [`stub_args.csv`](stub_args.csv) の `wlink_flash_op` 行(12 行)と
[`stub_disasm/wlink-CH32V307.asm`](stub_disasm/wlink-CH32V307.asm)。

### Q3-bis. 「どちらが純正か」— **probe firmware では決着しない**(F46)

ご提案の「LinkE が loader を内蔵しているかどうか」を実施しました。**内蔵していません**。

WCH-LinkUtility(MounRiver 同梱)の firmware image **10 本すべて**を、既知 9 blob の
**全長 / 先頭 64 B / 先頭 32 B** で検索して**全て不一致**でした。

| firmware | size | loader |
|---|---:|---|
| `FIRMWARE_CH32V203.bin` | 28,100 | — |
| `FIRMWARE_CH32V208.bin` | 114,264 | — |
| `FIRMWARE_CH32V305.bin` | 109,544 | — |
| `FIRMWARE_CH549.bin` / `FIRMWARE_DAP_CH549.bin` | 42,712 / 24,662 | — |
| `WCH-LinkE-APP-IAP.bin` / `WCH-LinkW-APP-IAP.bin` | 117,736 / 122,456 | — |
| `WCH-Link_APP_IAP_RV.bin` / `_ARM.bin` / `WCH-DAPLink_APP_IAP.bin` | 45,784 / 27,734 / 36,292 | — |

→ **loader は host が供給する**。protocol(`WriteFlashOP` → data EP へ送出)と整合します。

> `FIRMWARE_CH32V305.bin` には FLASH KEY の `lui` 対が 10 箇所、FLASH base(`0x40022`)参照が 116 箇所ありますが、
> これは **probe 自身(CH32V305)の自己書換**用と解されます。target の flash は DMI 経由で叩くので、
> probe が自分の FLASH controller を触るのは firmware IAP のためです。

→ **「純正」の意味は「probe firmware から抽出したもの」ではなく「WCH-LinkUtility 本体か WCH EVT の flash ルーチン由来」**
に絞られました。次に読むなら **WCH-LinkUtility の実行ファイル本体**です(こちらは未取得)。

## 3. こちらから逆に確認したいこと

1. ~~`a0` に何を積んでいるか~~ → **解決**。`a0` が USB に現れないというご指摘は正しく、代わりに **stub 自身の逆アセンブル**で 5 ビット全部と `a1`/`a2`/buffer/戻り値まで確定しました(§Q4)。ご提示の bit0↔WriteFlashOP / bit1↔EraseFlash も裏付けられました
2. ~~`CH643` と `CH32L103` の統合~~ → そちらで実施済みとのこと、了解です
3. ~~どちらが純正か~~ → **probe firmware では決着しない**ことが判明(§Q3-bis)。firmware 10 本に loader は入っていません。**次に読むなら WCH-LinkUtility の実行ファイル本体**ですが、こちらは未取得です。もし手元にあれば教えてください

## 4. 出所とライセンス

`stubs.csv` / `stub_disasm/` の `basis` に次を記録しました。

```
oss:ch32-rs/wlink/src/flash_op.rs (via ch32rv/crates/flash/src/stub.rs)
```

wlink は MIT OR Apache-2.0、blob 自体は WCH EVT の flash ルーチン由来という注記もそのまま残してあります。

## 5. 再実行

```sh
export WCH_ROOT=<repo の親>
python3 extract5.py     # ch32rv/crates/flash/src/stub.rs を読んで hex/disasm/比較を再生成
```
