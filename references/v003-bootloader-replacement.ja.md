# CH32V003 の software USB bootloader — 入れ替えるべきか(「完全上位」の条件)

状態: **検討メモ**(判断材料は upstream 一次ソース [rv003usb](https://github.com/cnlohr/rv003usb) と本 repo の転記。自前ビルド・実測は未)。

前提: **software USB を使う V003 は、配線の制約から事実上 UIAPduino(と同等の自作 board)だけが対象**。[software-usb.ja.md §2](../protocols/software-usb.ja.md) のとおり D+/D− は **GPIO 0〜4 に限定**(`c.andi` の 5 bit 即値制約)で、USB を配線した V003 board 自体が少ない。台数が限られる以上、**標準の BL(rv003usb bootloader)を置き換えるなら「完全上位」でなければ割に合わない**。本メモはその条件を定義し、候補を 1 つずつ潰す。

## 0. 結論(先に)

1. **入れ替えなくてよい。標準 BL は既に「stub 実行型」で、機能追加は host 側の stub でできる。** upstream README の一文がそれを言っている — 「1,920 バイトで HID device を作り、列挙し、**device 上でのコード実行を可能にする**」。read / write / erase / chip 判別 / app 起動は**どれも BL の機能ではなく、host が送り込む stub**([custom-bootloader §2b](../protocols/custom-bootloader.ja.md))。→ **「スタブを足せるようにする」は既に達成済み**で、それが入れ替え動機として最も大きかったはずのもの。
2. **既に限界。数字で確認できる。** linker は `FLASH LENGTH = 1916` + `SECRET 4 B` = ちょうど **1,920 B**。upstream README は「**GPIO のピン番号を変えるだけでもサイズが変わり、1,920 B を超えうる**」と明記し、ソースには `#warning "... Code might not fit"` まで入っている。`BOOTLOADER_TIMEOUT_USB` は「**0 なら 28 B、>0 なら 48 B**」という byte 単位の予算管理。→ **何かを足すなら何かを削る。**
3. **本当に BL 側でしか解けないものは 1 つだけ**(§4): **entry 方式(timeout / button / host 検出)の同時搭載**。これは容量が理由で択一になっている。他は全部 host 側 stub か app 側で解ける。
4. **「BL 自身の更新」すら BL 側の仕事ではない。** [custom-bootloader §2a](../protocols/custom-bootloader.ja.md) のとおり `ch32_user_bootloader_flasher` が **app 側 updater として BOOT 領域を書き換えることを V003 で実証済み**(3 組のレジスタ解錠 → 64 B fast program、書く前に user flash 末尾へ 1,920 B backup)。
5. **software USB を boot 以外で使うのは「app 側でやる」で確定。しかも既に実装がある。** `demo_terminal` が PID `1209:d003` で printf terminal を出し、**minichlink の terminal と WebLink の両方から見える**。rv003usb はそもそも user hook を持つ **library** で、README は `RV003USB_CUSTOM_C` を「**bootloader のようなものに主に有用**」と書いている — **BL のほうが library の特殊な使い方**であって、逆ではない。
6. **resource 問題の本体は flash ではなく割込み。** 基本 HID ≈ 2 kB は 16 KB の 12.5% だが、それより **pin-change ISR が最優先・非 preempt 必須で critical section ≤40 サイクル**という制約が **app の割込み設計全体を従属させる**方が重い。
7. **唯一魅力のある案「BL 常駐 USB stack を app から呼んで flash を浮かせる」は、V003 では成立しない。** BOOT↔user の関数 jump に **~1 µs の遅延**がある([custom-bootloader §2a](../protocols/custom-bootloader.ja.md))。48 MHz で 1 µs = **48 サイクル**で、**jump 1 回で ≤40 サイクルの予算を使い切る**。→ **V003 では否定。CH32V00X(V002–V007)は「jump 遅延なし」なので V006 側でなら成立しうる**(§5.3)。

## 1. いま何ができるか — 標準 BL の能力の棚卸し

| 機能 | 実体 | **どこにあるか** |
|---|---|:--:|
| USB 列挙(low-speed HID、driver 不要) | `1209:B003`(UIAPduino fork は `B803`)、EP0 8 B、feature report `0xAA` 127+1 B | **BL** |
| **scratchpad へのコード投入と実行** | RAM `0x20000100`、128 B。末尾 4 B が `0x1234ABCD` で実行予約、`runwordpad`(負=カウントダウン / 0=停止 / 正=N 周後に実行) | **BL** |
| 任意番地 read(byte/half/word) | `*_wise_read_blob` | **host stub** |
| 任意番地 write(RAM / 周辺レジスタ) | `*_wise_write_blob` | **host stub** |
| flash 書込(V003 の 64 B fast program) | `write64_flash`(host が事前に `CTLR` を設定) | **host stub** |
| page erase / block write ループ | `erase_block_bin` / `write_block_bin` | **host stub** |
| chip 判別 | `0x1FFFF7C4` 等を read | **host stub** |
| app 起動 | `run_app_blob`(BOOT 領域末尾の secret `0x1FFFF77C` から `boot_usercode` の位置を読んで jump) | **host stub** |
| entry(BL に留まる判定) | power-on reset 判定 + timeout / button / host 検出。**容量の都合で択一に近い** | **BL** |
| app → BL へ戻る hook | feature report `FD 12 34 AA BB CC DD`(ID `0xFD`) | **app** |
| BL 自身の更新 | **無い**(README: "you can't update the bootloader using itself") | — |
| user flash の消費 | **0 byte**(BOOT 領域 `0x1FFFF000`) | — |

**BL がやっているのは「USB で列挙して、送られてきたコードを RAM で走らせる」ことだけ。書込アルゴリズムすら BL には無い。**

これは [dmi-bridge の設計原則 1](../protocols/dmi-bridge.ja.md)「**probe は chip を知らない**」と同じ構造で、**BL も chip を知らない**。だから **新 chip・新しい書込手順への対応は host の更新だけで済む**。既に host 実装が 3 つある(minichlink / rv003usb-webflasher / WebLink_USB)のもこの設計のおかげ。

## 2. どれくらい限界なのか(数字)

| 項目 | 値 | 出典 |
|---|---|---|
| BOOT 領域 | **1,920 B**(2K−128)@ `0x1FFFF000` | 各 RM([custom-bootloader §2a](../protocols/custom-bootloader.ja.md)) |
| linker | `FLASH 1,916` + `SECRET 4` = **1,920** | `ch32v003fun-usb-bootloader.ld` |
| 実サイズ | 「**ほぼ 1,920 B で、利用可能領域をほぼ埋め尽くす**」 | upstream `bootloader/README.md` |
| サイズが変わる要因 | **GPIO のピン番号を変えるだけでも変わる** | 同上 |
| `BOOTLOADER_TIMEOUT_USB` | **0 で 28 B、>0 で 48 B** | `bootloader.c` |
| `BOOTLOADER_KEEP_PORT_CFG` | **8–16 B** | 同上 |
| button + timeout 併用 | `#warning "... Code might not fit"` | 同上 |
| `noreturn` 属性 | 「2–4 byte 節約できるので使う」 | 同上のコメント |
| RAM | 2 KB。scratchpad 128 B @`0x20000100`、`runwordpad` @`0x20000180` | ld / §2b |
| app 側の基本 HID | **≈ 2 kB**(16 KB の **12.5%**) | upstream `README.md` |

**「entry 方式が択一に近い」は設計上の妥協ではなく、単に容量が無い。** `noreturn` で 2–4 byte を稼ぎにいくレベルの予算で回っている。

## 3. 「完全上位」の条件

入れ替えを正当化するには、**6 条件を全部満たす**必要がある。1 つでも落とせば「今より悪い部分がある」ことになり、台数の少ない V003 でわざわざ焼き直す理由が消える。

| # | 条件 | 落とすと何が起きるか |
|---|---|---|
| 1 | **user flash を 1 byte も食わない**(1,920 B に収まる) | 食い始めた瞬間、16 KB の app が痩せる。**BOOT 領域に置ける利点そのものが消える** |
| 2 | **既存 host がそのまま動く** | minichlink / rv003usb-webflasher / WebLink_USB の **3 実装を全部書き直す**ことになる |
| 3 | **stub 実行を維持** | 機能追加のたびに BL を焼き直す設計に**退化**する |
| 4 | **PID 体系を壊さない** | `1209:B003` / `B803`。→ [builtin-probe-and-self-update.ja.md](builtin-probe-and-self-update.ja.md) |
| 5 | **復旧経路が減らない** | BL を壊したら外部 probe が要る。今より悪くしない |
| 6 | **entry の選択肢が減らない** | timeout / button / host 検出のどれかが使えなくなる |

**1 と 3 の同時達成が最大の壁**。1,920 B の大半は USB stack(列挙・NRZI・bit-stuffing・CRC・HID)で、削れる余地は数十〜百 byte 単位しかない。

## 4. BL でしか解けないものは何か(候補を潰す)

| 候補 | 価値 | **BL 必須か** | 1,920 B に入るか | 判定 |
|---|---|:--:|:--:|---|
| **stub 実行** | 高 | — | — | **既にある**。動機として消える |
| **書込の高速化** | 中 | **いいえ** | — | stub 側(page ループの改良)。BL 不変 |
| **read-back / verify** | 中 | **いいえ** | — | 既に stub でできる。webflasher は**差分書込**まで実装済み |
| **非破壊部分書込** | 中 | **いいえ** | — | stub 側。ch32fun 版が既にその設計 |
| **新 chip 対応** | 中 | **いいえ** | — | host 側 |
| **BL 自身の更新** | 中 | **いいえ** ← 逆転 | — | **app 側 updater で実証済み**(`ch32_user_bootloader_flasher`、V003)。§0-4 |
| **transport 追加(UART / I2C)** | 低 | はい | **いいえ** | software USB がある以上、動機が薄い |
| **trial boot / ロールバック** | **高** | **はい** | **たぶん無理** | app が壊れている前提の機能なので app には置けない。だが 2 面管理 + CRC + confirm は 1,920 B に入らず、**16 KB を 2 分割すると app が 8 KB になる**方が痛い。→ **flash の大きい chip の話** |
| **entry 方式の同時搭載** | 中 | **はい** | **いいえ**(まさにこれが択一の原因) | **唯一残る正味の動機** |

→ **入れ替えの正味の動機は「entry 方式を 2 つ載せたい」だけ**で、それも「他の何を削って入れるか」の判断になる。

## 5. software USB を boot 以外で使う

### 5.1 resource の実際 — 重いのは flash ではない

| 資源 | 消費 | V003 の全体 | 評価 |
|---|---|---|---|
| flash | 基本 HID ≈ **2 kB** | 16 KB | 12.5%。terminal / composite でもう少し |
| RAM | endpoint buffer + 内部状態 | 2 KB | **実測が要る**(§7-1) |
| **割込み** | **pin-change ISR は最優先・非 preempt 必須。critical section ≤ 約 40 サイクル。40 サイクルを超える ISR は preemption 有効化が必須** | — | **これが本体** |
| clock | 48 MHz 固定 | — | 低消費電力運用と両立しない |

**flash 12.5% よりも、「app の割込み設計全体が USB に従属する」ことのほうが重い。** USB 受信中の割込みは禁止(「転送の途中で USB 受信コードに割り込むことは禁止」)なので、**timing に敏感な app(モータ制御、別のソフト protocol、精密な PWM)とは同居できない**。

→ ユーザーの見立て「リソース的な問題だよね?」は正しいが、**ボトルネックは flash ではなく割込み**。

### 5.2 app 側で足りる — 既に実装がある

**「ブートに手を入れなくてもアプリ側でやればいい」は、そのとおりで、しかも既に動いている。**

| 事例 | 内容 |
|---|---|
| **`demo_terminal`** | **printf を USB HID に出す**。`minichlink -kT -c 0x1209d003` の terminal と **WebLink の terminal の両方から見える**。「SWIO 経由の minichlink terminal と同じ `printf` を USB で使う」という趣旨 |
| `demo_gamepad` / `demo_composite_hid` / `demo_pikokey_hid` | HID device としての app |
| `demo_hidapi` | host 側から custom HID を叩く |
| **app → BL の hook** | app が feature report `FD 12 34 AA BB CC DD` を受けて [§2a](../protocols/custom-bootloader.ja.md) の切替シーケンスを実行。**minichlink は `B003` が無ければ `D003` を探してこれを送る** |

**rv003usb はそもそも library**: `usb_handle_user_in_request` / `usb_handle_hid_set_report_start` / `usb_handle_user_data` といった user hook を公開しており、README は `RV003USB_CUSTOM_C` を「**bootloader のようなものに主に有用**」と書いている。→ **BL のほうが library の特殊な使い方**。app 側で使うのが本来の姿。

**そして最後の hook が効いている**: app が USB を持ち、`0xFD` で BL に戻れるなら、**「USB だけで更新する」UX は BL と app の協調で既に完成している**。BL 側の変更は要らない。

### 5.3 唯一魅力のある案と、その否定

**案**: BL に常駐する USB stack を app から呼べるようにし、BOOT 領域の 1,920 B を「**USB ROM library**」として共有する。app の 2 kB が浮けば **16 KB の 12.5% が戻る**。

**V003 では成立しない**:

- [custom-bootloader §2a](../protocols/custom-bootloader.ja.md) の表に **「BOOT↔user の関数 jump に ~1 µs の遅延(頻繁に呼ぶ関数を置かない)」** と明記(**CH32V003 / CH641 のみ**。CH32V00X は「**jump 遅延なし**」)。
- 48 MHz で **1 µs = 48 サイクル**。rv003usb の要求は critical section **≤ 約 40 サイクル**。→ **jump 1 回で予算を使い切る。**
- USB の ISR は packet ごとに何度も走るので、「**頻繁に呼ぶ関数を置かない**」という注意書きに正面から反する。

→ **V003 では否定。** ただし **CH32V00X(V002–V007)は BOOT 領域 3,328 B(V003 の 1.73 倍)かつ jump 遅延なし**なので、**V006 側でなら成立しうる**。UIAPduino が V003 → V006 に移った board で意味を持つ話で、V003 では追わなくてよい。

## 6. 判断

| 問い | 答え |
|---|---|
| 標準 BL を入れ替えるべきか | **否**。既に stub 実行型・限界サイズ・host 実装 3 つ |
| stub を足せるようにするには | **既にできる**。host 側に blob を書くだけ |
| 機能を足したくなったら | **host stub → app 側 → BL** の順で検討する。**BL は最後** |
| software USB を boot 以外で | **app 側で。`demo_terminal` が既にある** |
| resource は問題か | **flash 12.5% より、割込み制約(≤40 サイクル・非 preempt)のほうが重い** |
| BL に手を入れる唯一の候補 | **entry 方式の同時搭載**。容量で択一になっているので「何を削るか」の判断 |

**入れ替えるとしたら、それは V003 ではなく V006(CH32V00X)。** BOOT 領域が **3,328 B** で **jump 遅延なし**なので、§3 の条件 1 と 3 を同時に満たす余地があり、§5.3 の USB stack 共有まで射程に入る。V003 は「**標準のまま使い、機能は host stub と app 側で足す**」で確定してよい。

## 7. 未決 / 測れば決まること

| # | 測ること | どう決まるか |
|---|---|---|
| 1 | **app 側 rv003usb の RAM 実消費**(2 KB のうち何 byte) | `demo_terminal` をビルドして `.bss`/`.data` を見る。§5.1 の空欄が埋まる |
| 2 | **現行 BL の実コンパイルサイズ**(構成ごと)と、**entry を 2 つ載せたときの超過量** | **§4 の唯一の動機の可否がこの数字だけで決まる** |
| 3 | UIAPduino fork(`B803`)と upstream(`B003`)の **BL 差分** | fork が既に何かを削って何かを足しているなら、余地の見積りが変わる |
| 4 | **V006(V00X)の BOOT 領域 3,328 B に何が入るか** | §6 の「入れ替えるなら V006」を裏付ける / 否定する |
| 5 | `demo_terminal` を UIAPduino で動かしたときの、app の他機能との**割込み干渉** | §5.1 の「同居できない app」の実際の線引き |
| 6 | app 側 updater(`ch32_user_bootloader_flasher`)を UIAPduino で走らせたときの成否 | V003 で実証済みだが board 差がある。§0-4 の裏取り |

**2 が最優先**。他は全部「入れ替えない」という結論を動かさないが、2 だけは動かしうる。

## 8. サイズ削減の調査項目(**UIAPduino へ還元する前提**)

§2 の「ほぼ 1,920 B」は upstream README の記述で、**実測値ではない**。§4 で唯一残った動機(entry 方式の同時搭載)は「**あと何 byte あれば載るか**」に還元されるので、**まず測り、次に削る**。新 BL を作るのではなく、**削減が効いたら UIAPduino / upstream に還元する**のが狙い。

なお手元の checkout(`/home/mt/dev_wch/rv003usb`)の origin は **`YuukiUmeta-UIAP/rv003usb`= UIAPduino fork** で、`usb_config.h` の PID も `0xB803`。**調査対象がそのまま手元にある**。

### 8.1 既に適用済みの最適化(提案しても無駄なもの)

削減案を出す前に、**upstream が既にやっていること**を押さえる。ここを重複提案すると信用を落とす。

| 分類 | 適用済みの内容 | 出典 |
|---|---|---|
| コンパイルフラグ | **`-Os -flto -ffunction-sections -fdata-sections -msmall-data-limit=8 -fno-tree-loop-distribute-patterns`**、`-nostdlib` | ch32fun `ch32fun.mk` |
| アーキ | **`-march=rv32ec -mabi=ilp32e`**(16 レジスタ + 圧縮命令) | 同上 |
| リンク | **`-Wl,--gc-sections`**、`-lgcc` | 同上 |
| 計測 | **`-Wl,--print-memory-usage -Wl,-Map=$(TARGET).map`**(= **map は既に出ている**) | 同上 |
| startup | **`USE_TINY_BOOT`** — 手書き asm の最小 startup。`csrw mtvec, 3` でベクタ表を番地 0 に置き、**未使用のベクタスロット(`0x04`–`0x4F`)に startup コード自体を詰め込んでいる**。`0x50` が `EXTI7_0_IRQHandler` の 1 word | `rv003usb.S` |
| USB stack | **`RV003USB_OPTIMIZE_FLASH 1`**(`.h`/`.c`/`.S` の 5 箇所で分岐) | `usb_config.h` / stack |
| 除外 | `SYSTEM_C:=`(ch32fun の system.c を外す)、`FUNCONF_USE_DEBUGPRINTF 0` | Makefile / funconfig.h |
| libc / libgcc | **`memcpy` / `memset` / `strlen` / `printf` の呼び出しが無い**。C 側に **乗除算が無い**(シフトのみ)ので `__mulsi3` / `__divsi3` を引かない | grep で確認 |
| 命令選択 | asm 側で圧縮命令を明示(`c.sw` / `c.li` / `c.addi` / `c.j` / `c.nop`) | `rv003usb.S` |
| 属性 | `boot_usercode` に `noreturn`(「**2–4 byte 節約できる**」とコメント) | `bootloader.c` |

→ **「`-Os` にしましょう」「`--gc-sections` を」の類は全部済み。** 残っているのは下の A–E。

### 8.2 まず測る(実機不要。**ビルドするだけ**)

| # | 測ること | 手段 | なぜ最初か |
|---|---|---|---|
| **A1** | **関数別サイズと残り byte** | `.map` と `--print-memory-usage` は**既に有効**。ビルドして読むだけ | **これ無しに A2 以降は判断できない**。「大きい関数」の順位が削減の優先順になる |
| **A2** | **`0x00`–`0x4F`(ベクタ領域)に startup が何 byte 使っているか** | map + 逆アセンブル | 余っていれば**死んだ隙間**。小関数を置ける |
| **A3** | **構成別の差分**(timeout のみ / button のみ / 両方 / `KEEP_PORT_CFG` 有無) | 構成を変えてビルドし直す | **§4 の唯一の動機が「あと何 byte」に確定する** |

**A1–A3 は実機も target も要らない。** [experiments/README.ja.md §3.1](../experiments/README.ja.md) の梯子で言えば最下段(実機なし)で答えが出る問いなので、上の段に持ち上げない。

### 8.3 削減の候補

見込みは**すべて推定**で、A1 の実測前は当てにしない。「還元」列は upstream(cnlohr)へ出せるか、fork 固有かの区別。

#### B. フラグ level(リスク低)

| # | 案 | 見込み | リスク | 還元 |
|---|---|---|---|---|
| **B1** | **`-Oz`**(GCC 12+)を `-Os` の代わりに | 数十 byte(C 部分の数 %) | ほぼ無し。**ISR 本体は asm なので timing に影響しない** | **upstream**(ch32fun.mk のオプション追加) |
| **B2** | **`-msave-restore`** — prologue/epilogue を libgcc の `__riscv_save_N`/`__riscv_restore_N` 呼び出しに置換 | 関数数に比例。十〜数十 byte | **C 側の呼出 latency が増える**。USB の C 部分が 40 サイクル制約に触れないか要確認 | upstream(要検証つき) |
| **B3** | **新しい GCC(13/14)でビルド** | 不定(数十 byte 動くことがある) | 無し(ただし asm の `.option arch, +zicsr` 分岐は既に GCC>10 対応済み) | **報告のみで価値がある** |
| **B4** | LTO のインライン判断を絞る(`-finline-limit` / `--param max-inline-insns-*` の掃引) | 不定 | 掃引結果が構成依存 | upstream(数値の提案) |

**B4 は掃引なので自動化できる**。この repo の実験の型([README.ja.md §7](../experiments/README.ja.md) の「1 関数の中でループする」)にそのまま乗る。

#### C. リンカ / レイアウト level

| # | 案 | 見込み | リスク | 還元 |
|---|---|---|---|---|
| **C1** | **BL 専用 ld から未使用セクションを削る** — `.preinit_array` / `.init_array` / `.fini_array` / `.ctors` / `.dtors` / `.fini` は `-nostdlib` + C++ 無しで**中身が空**だが、`KEEP` が付いていて各々に `. = ALIGN(4)` がある。**RV32EC は 2 byte 命令なので、境界ごとに最大 2 byte 捨てている**可能性 | 境界 6–8 箇所 × 最大 2 B = **10–20 byte** | 低(BL 専用 ld なので app の ld に影響しない) | **upstream**(BL の ld のみ) |
| **C2** | `.text` 内の関数順序を alignment padding が最小になるよう並べる | 数〜十数 byte | LTO が並べ替えるので効果が読みにくい | upstream(効果が出れば) |
| **C3** | `.boot_firmware` / `_boot_firmware_xor` の配置の padding 回収 | 数 byte | secret の XOR 計算に依存するので慎重に | upstream |

**C1 が「カリカリ」の本命**。空セクションの `ALIGN(4)` は誰も疑わない場所で、しかも **A1 の map にそのまま出る**(セクション先頭番地の飛びを見れば分かる)。

#### D. USB descriptor / protocol level(**効きが大きい可能性**)

| # | 案 | 見込み | リスク | 還元 |
|---|---|---|---|---|
| **D1** | **string descriptor の短縮** — USB string は **UTF-16LE = 1 文字 2 byte**。manufacturer / product / serial の 3 つがある。product を短くする、serial index を `0` にする | **数十 byte**(16 文字の product を削れば 34 B) | ⚠ [ecosystem §4.2](ecosystem-any-hardware.ja.md) の「**serial string に UID を載せて個体識別**」と衝突する。**BL では個体識別の必要性が低い**ので許容できるかの判断が要る | **fork 固有**(UIAPduino の patch が触った箇所そのもの) |
| **D2** | **EP1 IN を省けるか** — BL の protocol は **control transfer だけ**(`SET_REPORT` / `GET_REPORT`)。`ENDPOINTS 2`(EP0 + 1)を **1** にできれば、endpoint descriptor 7 B + endpoint 処理コード + buffer が消える | **当たれば最大**(数十〜百 byte + RAM) | ⚠⚠ **HID class は interrupt IN endpoint を持つのが通例で、Windows が列挙を拒否する可能性がある**。**3 OS での受容性を確認する実機実験が必須** | **upstream の設計判断**(大きいので相談案件) |
| **D3** | HID report descriptor の最小化(feature report だけなので不要な usage / collection を削る) | 十数 byte | host 側の report 解釈が変わる。3 host 実装(minichlink / webflasher / WebLink)で確認 | upstream |
| **D4** | config / interface descriptor の共有・圧縮 | 数〜十数 byte | 低 | upstream |

**D2 が唯一「実機と 3 OS が要る」項目**。他は全部ビルドだけで判定できる。

#### E. コード level(効くが PR 規模が大きい)

| # | 案 | 見込み | リスク | 還元 |
|---|---|---|---|---|
| **E1** | rv003usb.c の残り ~250 行のうち **descriptor dispatch を asm に落とす** | 不定(A1 で大きければ狙う) | 保守性が落ちる。upstream が嫌がる可能性 | upstream(要相談) |
| **E2** | `boot_usercode` の `asmDelay(1000000)` の見直し | 数 byte | D− を LOW にしてから host が切断を認識するまでの待ちなので、**短くすると再列挙に失敗しうる** | upstream |
| **E3** | **entry 判定の共通化** — timeout / button / host 検出で重複している GPIO 設定を 1 本化 | 十〜数十 byte | 3 方式の組合せテストが要る | **upstream**。**§4 の唯一の動機に対する直接の手段** |

### 8.4 進め方

1. **A1 → A2 → A3**(ビルドのみ)。ここで「残り byte」と「両方載せるのに足りない byte」が数字になる。
2. **C1 → B1 → B3**(リスク低・還元しやすい順)。A3 の不足分が埋まるか見る。
3. 埋まらなければ **D1**(fork 固有なので UIAPduino 側で即入れられる)。
4. まだ足りなければ **E3**、それでも足りなければ **D2**(実機 + 3 OS)。
5. **どの段で足りたかを記録して、その内容を UIAPduino / upstream に出す**。

**2 の時点で「実は 200 byte 空いていた」なら §4 の結論(entry 同時搭載は容量で無理)が覆る。** 逆に **1 で「残り 4 byte」だったら、この節の残りは全部やらなくていい**。だから A1 が最優先。

## 9. 参照

- software USB の物理・timing・BL の位置づけ: [../protocols/software-usb.ja.md](../protocols/software-usb.ja.md)
- **HID scratchpad BL の protocol と stub 一覧**(本メモの根拠の中心): [../protocols/custom-bootloader.ja.md §2b](../protocols/custom-bootloader.ja.md)
- **BOOT 領域の番地・サイズ・jump 遅延・切替レジスタ・app 側 updater**: [../protocols/custom-bootloader.ja.md §2a](../protocols/custom-bootloader.ja.md)
- BL 設計空間(entry 方式・trial boot・内蔵ライタ): [bootloader-design-space.ja.md](bootloader-design-space.ja.md)
- PID 体系と役割の申告: [builtin-probe-and-self-update.ja.md](builtin-probe-and-self-update.ja.md)
- 「知識を host に置く」という同型の設計: [../protocols/dmi-bridge.ja.md §0](../protocols/dmi-bridge.ja.md)
- 一次ソース: [rv003usb](https://github.com/cnlohr/rv003usb)(`README.md` / `bootloader/README.md` / `bootloader/bootloader.c` / `bootloader/ch32v003fun-usb-bootloader.ld` / `demo_terminal/`)、[ch32_user_bootloader_flasher](https://github.com/monte-monte/ch32_user_bootloader_flasher)
