# 上級ガイド

[overview.ja.md](overview.ja.md) の層モデルを前提に、(A) 各層を深掘りし、(B) 未知のプロトコルを**どう解読して検証するか**(方法論)、(C) 実装で踏む落とし穴、をまとめる。新しい領域を調べるときの手引き。

## A. 層モデルの深掘り

### A-1. なぜ「WCH 固有の殻 + RISC-V 標準の中身」なのか

RISC-V は **Debug Spec**(External Debug Support)で、デバッガがチップを触る標準を定めている。中心概念:

- **DM(Debug Module)**: チップ内のデバッグ制御ブロック。halt/resume、レジスタ/メモリアクセスを提供する。DMCONTROL/DMSTATUS/abstract command 等のレジスタ群を持つ。
- **DTM(Debug Transport Module)** と **DMI(Debug Module Interface)**: 外部デバッガが DM のレジスタを read/write するための**汎用バス**。「アドレス + データ + op(read/write)」の 1 トランザクションが単位。

標準が決めているのは「DMI で DM レジスタを読み書きできること」まで。**DMI を物理的にどう運ぶか**(JTAG か、独自 2 線か、独自 1 線か)はベンダ裁量。WCH はここを独自化した:

- **WCH-Link ↔ target の線**(SWIO 1 線 / RVSWD 2 線)= WCH 独自の DMI 物理転送。→ [link-to-target.ja.md](../protocols/link-to-target.ja.md)(未解読)。
- **PC ↔ WCH-Link の USB**= その DMI トランザクションを USB に載せる WCH 独自プロトコル。`DmiOp`(cmd `0x08`)が 1 DMI トランザクション = `[addr, data_be32, op]` を運ぶ。→ [pc-to-link.ja.md](../protocols/pc-to-link.ja.md)(verified)。

だから **PC 側から見ると、DM レジスタへの read/write を USB frame で送るだけ**で RISC-V 標準のデバッグが全部できる。DM の使い方(abstract command でレジスタを読む、program buffer でメモリを読む 等)は [riscv-debug-module.ja.md](../protocols/riscv-debug-module.ja.md) にまとめた。これは**ベンダ非依存**なので、他社 RISC-V にも通じる知識。

### A-2. 2 系統の USB endpoint

WCH-Link は制御用とデータ用で口を分ける:

- **command EP `0x01`/`0x81`**: frame 化(`0x81|cmd|len|payload`)。attach、DMI、電源、設定など**ほぼ全部**ここ。
- **data EP `0x02`/`0x82`**: flash 本体データの bulk。**frame 化されない生バイト**を data_packet_size 単位で流す。firmware stub と firmware image の転送に使う。

この分離は「小さな制御コマンドは frame で正確に、大きな flash データは frame オーバーヘッド無しで速く」という設計。

### A-3. flash 書き込みの 2 経路

flash に焼く方法が 2 つあり、用途が違う(詳細 [pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §flash):

1. **stub 経路(全消去 + 全書き)**: RAM に family 別の loader stub を送って走らせ、data EP から image を流す。速いが **chip 全体 or region 全体専用**。mid-flash の 1 page だけ書こうとすると probe が `0x55` で拒否する。
2. **直接 FLASH controller 経路(page 単位)**: halt した hart の program buffer 経由で、memory-mapped な FLASH controller(`0x4002_2000`)を DMI で直接叩く。**任意の 1 page** を消去/書き込みできる。gdb の flash breakpoint や option byte 書き換えの土台。family ごとに手順(PgStart 方式 / Buffered 方式 / V103 の標準 halfword 方式)が違う。

### A-4. 実行時 I/O(target → PC の print)

デバッグ中に target が文字を出す手段が複数ある(層が違う):

- **semihosting**: RISC-V の `ebreak` + マジック命令列で host に syscall を頼む(SYS_WRITE0 等)。**DM の halt を使う**アプリ層機構。
- **SerialDMDATA / SDI print**: DM の data レジスタ(DMDATA0/1)を郵便受けにして、core を **走らせたまま** host が polling で吸い出す。LinkE 専用の SDI enable が要る。
- **RTT**: RAM 上の制御ブロックを探して read/write オフセットを読み、halt して吸い出す(SEGGER 由来)。
- **UART bridge**: WCH-Link の CDC シリアルを読むだけ(物理 UART 配線が要る)。

### A-5. host 側のドライバ層(全 USB 経路に共通)

L2 の下、PC 側には「OS のドライバが device を握り、ツールがどの API で届くか」という層がある。probe(pc-to-link)・ISP USB・DAP の**全 USB 経路に共通**して効く、プロトコルの手前の関門。

- **汎用ドライバ**(WinUSB / libusb / usbfs)なら `nusb`/`libusb` が直接 bulk 転送できる。
- **ベンダ専用ドライバ**(WCH の CH375 系)だと汎用 API では開けず、ベンダ DLL か IOCTL 直叩きが要る。
- **Windows は WCH-Link への経路が 2 系統**(WinUSB / WCH 純正)で排他。純正のまま 64bit で喋る道が CH375 IOCTL 直叩き(`ch32rv-usb-wch-win` として部品化)。IOCTL 定数・GUID・pipe マッピングまで [pc-usb-driver.ja.md](../protocols/pc-usb-driver.ja.md) にある。

新しい経路(ISP USB / DAP)を実装するときも、まずこの層でその device をどのドライバが握るかを確認する。

## B. reverse engineering の方法論

このプロジェクトの鉄則(原設計案 §3): **先行実装は「仕様書として読む」だけ**。wlink / minichlink / probe-rs / RINS / WCH OpenOCD の記述は出発点。**自分の実機 capture で裏を取ってから `verified`** にし、裏の取れないものは実装しない。

### B-1. capture の取り方

- **Linux**: `usbmon`(`/sys/kernel/debug/usb/usbmon/`)または Wireshark で USB bulk を丸ごと記録。
- **ツール内蔵**: ch32rv は `--capture <file>` で自分の USB 往復を **NDJSON** に記録する。1 行 = 1 転送: `{seq, t_us, chan(cmd/data), dir(in/out), len, ok, data(hex)}` + 先頭に `_meta`/`_device` 行。詳細と参照 fixture は [captures/README.ja.md](../captures/README.ja.md)。
- **突き合わせ**: 同じ操作(list→attach→flash→reset)を wlink / probe-rs / WCH 純正ツールでも記録し、**バイト列が一致するか**で裏を取る。firmware 版(2.11/2.12/2.15 等)ごとに差が出ることがあるので版も記録する。

### B-2. replay で検証を固定化する

capture した NDJSON は **replay**(オフライン再生)できる: 記録された USB 応答を per-(chan,dir) の FIFO として返し、実機なしでツールのパーサ/状態機械を回す。これで:

- 実機がなくてもパース・デコードの回帰テストになる。
- ツールが**記録と同じ順序でコマンドを出しているか**(divergence)を検出できる。flash のように決定的な経路は replay がバイト一致する。

`verified` の根拠として「この capture を replay して一致」を残せる。fixture は commit して回帰テストに使う。

### B-3. status の規律

各プロトコル項目に status を付ける(この repo 共通語彙):

- `verified` — **自前の実機 capture で確認**。最上位。根拠(どの capture / 実機)を併記する。
- `attested` — 複数の独立した先行実装が一致。実機未確認。
- `single-source` — 単一実装のみに存在。
- `conflict` — 実装間で矛盾。**要 capture で解決**。
- `todo` — 存在の証拠(関数名・文字列)のみ。中身不明。

**裏が取れるまで実機に流す実装をしない**。特に破壊的操作(erase、option byte、power-off erase)は capture で受理と結果を両方確認してから。

## C. 実装で踏む落とし穴(実測)

新しくツールを書く人が同じ穴に落ちないよう、実機で確認した罠を挙げる(詳細と family 差は [pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §quirk)。

- **消去済みセルの debug read 値が family で違う**: V20x/V30x は `0xe339e339`(LinkE の placeholder。実セルは 0xff)、X035/V003 は素直に `0xff`。→ **erase 成否は read 値でなく FLASH controller の STATR で判定**する。
- **LinkE の壊れ読み値**: 別ツールを使った後などに、family byte は正しいまま **chip ID と UUID が同一 word の繰り返し**になることがある。再 attach でも target 電源断でも直らない(**probe 側の状態**)。復旧は RedetectChip(`0x0d 0x03`)+ detach + 再 attach。
- **速い bulk read 直後の stale データ**(CH549 で実測): stub 実行直後の高速 bulk read が **program 前の古い flash 像**(0xff やゴミ)を返すことがある。→ 照合は権威ある DMI 読みで再確認する。偽の verify-mismatch の原因。
- **CH32V103 の attach quirk**: AttachChip が生きた GPR `s1`/`x9` を chip id で上書きし復元しない。resume 後に program が s1 を使う瞬間 fault する(V103 固有)。→ **attach 後に soft-reset** してレジスタを再構築させる。
- **大 image で probe が固まる**: 十数 KB の書込中に bulk timeout → probe 無応答化 → USB 再接続でのみ復旧(`USBDEVFS_RESET` 不可)。chunk 化とタイムアウト設計で回避。
- **firmware 2.11(v31)は download --reset 後に走らない**(2.12 で解消)。版チェックは**正規化値**(`major*10+minor`)で比較(probe-rs は `v_major != 2 && v_minor < 7` の比較ミスがある — 真似しない)。
- **1 線 SWIO と 2 線 RVSWD の差は USB protocol 層に現れない**: attach/DMI/flash のコマンドは同一で、配線差は LinkE firmware が吸収する。ただし 1 線 target は LinkE/LinkW のみ(旧 CH549 Link は不可)。
- **Windows のドライバ 2 系統(排他)**: WCH-Link の vendor interface は WinUSB(Zadig / 自動 bind)か WCH 純正ドライバ(`WCHLinkW64.SYS`)のどちらかが握り、**両立しない**。WinUSB 前提の `nusb`/`libusb` は純正ドライバ下だと「incompatible driver」で開けない。純正のまま喋るには CH375 系の IOCTL を直接叩く(Zadig 不要)。「列挙できるのに開けない」の主因。→ [pc-usb-driver.ja.md](../protocols/pc-usb-driver.ja.md)。

## 参照

- RISC-V External Debug Support(Debug Spec)— DM/DMI/abstract command/program buffer の一次仕様
- [wlink protocol.md](https://github.com/ch32-rs/wlink/blob/main/protocol.md) / [RINS: WCH-Link](https://perigoso.github.io/rins/wch-link/index.html) / minichlink `pgm-wch-linke.c` / probe-rs `probe/wlink/`
- 各層の実仕様: [protocols/](../protocols/)
