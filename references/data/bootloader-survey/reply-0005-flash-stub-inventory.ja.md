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

## 6. 追記(2026-09-06)— WCH 純正 OpenOCD から loader を全数取得した

`~/dev_wch/tools` に WCH 純正 OpenOCD の**バイナリとソースの両方**が置かれたので、§Q3-bis を先に進めました。
**バイナリが strip されておらず**、loader がシンボル名付きで並んでいました。

### 6.1 「どちらが純正か」の最終回答 = **両方**

wlink の 5 本と minichlink の `linke-flashloader-v3`/`v4` は、純正 OpenOCD のシンボルと**バイト一致**します。

| 手元の名前 | 純正シンボル | family |
|---|---|---|
| `CH32V003` | `flash_op003` | CH32V003 / CH641 |
| `CH32V103` | `flash_op103` | CH32V103 |
| `CH32V307` | `flash_op307` | CH32V20x / CH32V30x |
| `CH643` / `CH32L103` | `flash_op643` | CH643 / CH32X035 |
| `linke-flashloader-v3` | `flash_op583` | CH58x / CH59x |
| `linke-flashloader-v4` | `flash_op573` | CH57x |

**例外**: minichlink の `v1`/`v2`(V20x/V30x 用)だけは純正と**共通接頭辞 0 B で不一致**。出所が別です。

> なお `flash_op643` と `flash_opl103` は**バイト完全一致**でした。**そちらが `0x0E`(L103)に `CH643` を使っている判断は、
> WCH 自身の命名からも裏付けられます**。

### 6.2 **`0x4E`(CH32V00X)には専用 loader がある** — これが一番効く話

`params_for_family()` が `0x4E` を `_ => return None` に落としているのは(意図どおり)問題ありません。
ただし**将来 V00X を足すとき、`CH32V003` を流用してはいけません**。

WCH は **`flash_op00X`(500 B)** を別に持っており、`flash_op003`(498 B)との差は**ページサイズだけ**です:

```
flash_op003 : addi x15,x12,63    srli x15,x15,0x6    addi x14,x14,64     ← 64 B ページ
flash_op00X : addi x15,x12,255   srli x15,x15,0x8    addi x14,x14,256    ← 256 B ページ
```

命令数は 199 対 199、共通接頭辞 83 B・接尾辞 109 B。**同一ソースをページ定数だけ変えてビルドしたもの**です。
V003 版を V00X に使うと 64 B 刻みで動き、実ページ 256 B と食い違います。

### 6.3 family byte → loader の dispatch(全 21 分岐)

純正バイナリの `wlink_ready_write` にある jump table(`0x32a5e8`)を展開しました。
**そちらの `params_for_family()` を広げるときの一次資料**になります。

| `riscvchip` | loader | 送出サイズ | 公開ソースにも |
|---|---|---:|:-:|
| `0x01` | `flash_op103` | 512 | ✓ |
| `0x05` `0x06` | `flash_op307` | 512 | ✓ |
| `0x09` | `flash_op003` | 512 | ✓ |
| `0x0c` | `flash_op643` | 512 | ✓ |
| `0x0e` | `flash_opl103` | 512 | ✓ |
| **`0x4e`** | **`flash_op00X`** | **512** | ✗ |
| **`0x8e`** | **`flash_opm030`** | — | ✗ |
| **`0xc6`** | **`flash_op417`** | — | ✗ |
| **`0x86` / `0xa6`** | **`flash_op317`** | — | ✗ |
| `0x02` `0x03` `0x07` `0x0b` `0x0a` `0x0f` `0x46` `0x4b` `0x8b` `0xcb` | CH5xx 系 8 種 | — | 一部のみ |

**穴が 2 つ**: **`0x0d`(CH32X035)と `0x49`(CH641)は dispatch に無い**。
WCH 自身は X035 / CH641 を stub 経路で書いていません。
そちらが `0x0D`→`CH643`、`0x49`→`CH32V003` を流用して実機検証しているのは、**純正より広い対応**ということになります。

### 6.4 ABI は CH32V 系 10 本で共通(§Q4 の一般化)

`a0` bit0..4 / `a1` addr / `a2` len / 戻り値 0\|16 は **CH32V/X/L/M 系 10 本すべてに共通**でした。
buffer 番地だけ 2 系統 — **`0x20000xxx`(V003 / V00X = RAM 2〜4 KB の小容量品)** と `0x20001xxx`(他)。
**CH5xx 系 8 本は別 ABI**で `a0` bit0 しか見ません。

→ **§Q4 の契約のまま 10 family へ広げられます**。

### 6.5 loader のページ定数が `ch32-device-data` と 9/9 一致

| | V003 | V00X | V103 | L103 | V20x | V30x | X035 | M030 | H417 |
|---|---|---|---|---|---|---|---|---|---|
| loader 埋め込み | 64 | 256 | 128 | 256 | 256 | 256 | 256 | 128 | 256 |
| `flash_geometry.fast_program_bytes` | 64 | 256 | 128 | 256 | 256 | 256 | 256 | 128 | 256 |

**probe 経路(WCH バイナリ)と RM/EVT 由来のデータが独立に一致**したので、相互の裏取りになります。

### 6.6 公開 GPL ソースのバグ(そちらには影響しません)

`riscv-openocd-wch` の `wlinke.c` L1187 が `wlink_ramcodewrite(flash_op643, sizeof(flash_op8571))` と
**別配列のサイズ**を渡しています(512 B の配列から 1408 B 送出)。**配布バイナリでは修正済み**です。
公開ソースは 9 loader / 11 分岐で、**バイナリ(18 loader / 21 分岐)より明確に古い**ことも分かりました。

### 6.7 生バイトの扱い

新規 12 本の hex と逆アセンブルは、**当リポジトリには保存していません**(WCH 配布 GPL バイナリ由来のため)。
`wch_openocd_loaders.csv` に**事実だけ**(symbol / サイズ / `fnv1a64` / family / `a0_bits` / `buffer_base` / `page_bytes`)を
置いてあります。中身が要るときは手元の OpenOCD から再生成してください:

```sh
EMIT_BLOBS=1 python3 extract8.py
```
