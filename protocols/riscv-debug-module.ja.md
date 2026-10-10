# RISC-V Debug Module(DMI 上の操作)

[pc-to-link.ja.md](pc-to-link.ja.md) の `DmiOp`(cmd `0x08`)で DM レジスタを read/write できるようになった上で、その上に立つ **RISC-V Debug Module** の操作。これは **RISC-V External Debug Support(Debug Spec)準拠のベンダ非依存な層**で、WCH 固有ではない(他社 RISC-V にも概ね通じる)。CH32 実機(V003/V103/V203/V307/X035)で確認。状態: 大半 verified。

## DMI トランザクション(下地)

`DmiOp` 1 回 = DM レジスタ 1 個の read/write。`[addr(1B), data(be32), op(1B)]` を送り、`[addr, data(be32), status]` が返る(op: 1=read/2=write、status: 0=success/2=failed/3=busy、busy は再試行)。以下は「DM レジスタ addr にどんな値を書く/読むか」を並べたもの。

## DM レジスタ番地

| 名前 | addr | 用途 |
|---|---|---|
| DMDATA0 | `0x04` | abstract データ 0 / mailbox |
| DMDATA1 | `0x05` | abstract データ 1 / mailbox 上位 |
| DMCONTROL | `0x10` | halt/resume 要求、dmactive、hartsel |
| DMSTATUS | `0x11` | halted/running 状態 |
| HARTINFO | `0x12` | 選択した hart の abstract access 情報 |
| DMABSTRACTCS | `0x16` | abstract command 状態(busy/cmderr) |
| DMCOMMAND | `0x17` | abstract command 発行 |
| DMPROGBUF0 | `0x20` | program buffer word 0 |
| DMPROGBUF1 | `0x21` | program buffer word 1 |

## 基本操作

以下の DMCONTROL 固定値は **hartsel 0** の操作。H417 の hartsel 1 の選択・CSR 読み出しの確認範囲は次節を参照し、この表の verified を H417 の両コア制御に拡張しない。

| 操作 | 手順 | 状態 |
|---|---|---|
| **halt** | DMCONTROL=`0x80000001`(haltreq+dmactive)→ DMSTATUS の all/any-halted を待つ → DMCONTROL=`0x00000001`(haltreq クリア) | verified |
| **resume** | DMCONTROL=`0x40000001`(resumereq+dmactive)。**直後に ~10ms sleep が要る**(quirk) | verified |
| **read_reg**(GPR/CSR/PC) | DMDATA0=0 → DMCOMMAND=`0x00220000 \| regno`(GPR=`0x1000+n`, PC=dpc=`0x7b1`)→ ABSTRACTCS busy 待ち → DMDATA0 読み | verified |
| **write_reg** | DMDATA0=value → DMCOMMAND=`0x00230000 \| regno` → busy 待ち | verified |
| **step**(1 命令) | dcsr(CSR `0x7b0`)の step(bit2)を立てて write_reg → resume → 再 halt を待つ → step クリア | verified(V203 で PC 前進を確認) |
| **read_mem32** | PROGBUF0=`0x0002a303`(`lw x6,0(x5)`)・PROGBUF1=`0x00100073`(`ebreak`)→ DMDATA0=addr → cmderr クリア → DMCOMMAND=`0x00271005`(x5←data0 + postexec)→ abstract 待ち → DMCOMMAND=`0x00221006`(data0←x6)→ abstract 待ち → DMDATA0 読み | verified |
| **write_mem32** | PROGBUF0=`0x0072a023`(`sw x7,0(x5)`)・PROGBUF1=`0x00100073`(`ebreak`)→ DMDATA0=addr → cmderr クリア → DMCOMMAND=`0x00231005`(x5←data0)→ 待ち → DMDATA0=data → cmderr クリア → DMCOMMAND=`0x00271007`(x7←data0 + postexec で `sw`)→ 待ち | verified |
| **write_mem16** | write_mem32 と同手順で PROGBUF0 のみ `0x00729023`(`sh x7,0(x5)`)。**V103 の標準 flash に必須**(16bit store ごとに FLASH controller が latch。`sw` では不可)→ [pc-to-link.ja.md](pc-to-link.ja.md) §6 | verified |

- **DMABSTRACTCS**: busy=bit12、cmderr=bits[10:8](書き戻しでクリア)。
- **DMSTATUS**: allrunning=bit11 / anyrunning=bit10 / allhalted=bit9 / anyhalted=bit8。
- **DMCOMMAND(abstract register access)の読み方**: `0x0027xxxx`=transfer+postexec、`0x0023xxxx`=write(transfer, write bit)、`0x0022xxxx`=read(transfer)。下位 16bit が regno(GPR=`0x1000+n`: x5=`0x1005`, x6=`0x1006`, x7=`0x1007`)。read_mem/write_mem は「addr/data を DMDATA0 経由で x5/x7 に載せ、progbuf の lw/sw/sh を postexec」で成立する。
- **任意長 write/read**(`write_mem`/`read_mem`)は word 単位で write_mem32/read_mem32 を回し、端の word は read-modify-write で byte 粒度を保つ。

## H417 の単一コア選択と CSR 読み出し

**verified の範囲**: CH32H417QEU6(chip ID `0x4170053d`、family `0xc6`)と WCH-LinkE(serial `49808F06CE30`、USB `1a86:8010`、FW **2.22** / raw `02 16`)、SWIO → PB9、speed low での **hartsel 0 / 1 の選択・DMCONTROL readback・下表の CSR 読み出し**。2026-10-10 に ch32rv 側で取得した独立した 2 attach セッションの USB capture と結果を収載した。[証拠と照合方法](../captures/fixtures/ch32h417-core-select-2026-10-10/README.ja.md)。既存 CLI の `--core 1` 対応を示すものではない。

[RISC-V Debug Module 一次仕様](https://docs.riscv.org/reference/debug/v1.0/debug_module.html)では、単一 hart は `hasel=0` と `hartsel` で選ぶ。`hartsello`(hartsel 下位 10 bit)は DMCONTROL bits[25:16]、`hartselhi`(上位 10 bit)は bits[15:6]。abstract command の busy 中は hartsel を変更しない。**今回の測定は index 0 / 1 のみ**で、HARTSELLEN・全 index・Hart Array Mask は探索していない。hart index と mhartid の一致もこの 2 index に限定する。

実測手順:

1. DeviceLock を取得し、DetachChip → ProbeInfo → SetSpeed(low) → AttachChip。対象 ID を照合し、コア 0 を halt して基準値を読む。
2. 直前の abstract command 完了後、DMI `0x10` に `dmactive | (hart << 16)` を書く。hartsel 1 は `0x00010001`、hartsel 0 は `0x00000001`。DMCONTROL と DMSTATUS を読む。
3. halted を確認し、abstract register access(`DMCOMMAND = 0x00220000 | csr`)で下表の CSR を読む。各セッションで `1 → 0 → 1 → 0` を選び直す。
4. 最後に DMCONTROL を `0x00000001` に戻して DetachChip。再 attach したセッションでも反復する。

| 観測 | hartsel 0 | hartsel 1 |
|---|---|---|
| DMCONTROL (`0x10`) | `0x00000001` | `0x00010001` |
| DMSTATUS (`0x11`) | session 1: `0x00400382`、session 2: `0x004c0382` | `0x004c0382` |
| HARTINFO (`0x12`) | `0x00212340` | `0x00212340` |
| mhartid (`CSR 0xf14`) | `0x00000000` | `0x00000001` |
| marchid (`CSR 0xf12`) | `0xdc68d866` | `0xdc68d8ae` |
| mimpid (`CSR 0xf13`) | `0xdc688002` | `0xdc688001` |
| misa (`CSR 0x301`) | `0x40901127` | `0x40901127` |
| dpc (`CSR 0x7b1`) | `0x201003b0` | `0x00000000` |

両コアは選択直後から anyhalted / allhalted が立っていた。コア 1 宛の haltreq は発行しておらず、その効果は未実測。コア 1 の dpc が 0 の理由も未確認。DMSTATUS の上位状態 bit はセッション間で変化し、ackhavereset は発行していないので、**DMSTATUS 全 word をコア識別値にしない**。選択の確認には DMCONTROL readback と mhartid / marchid / mimpid を使う。

**実装上の要件(制御の実測とは別)**: 選択した hartsel を状態として保持し、halt / resume / reset / ack と要求クリア時を含む全 DMCONTROL 書き込みに反映する。上の基本操作のコア 0 固定値をそのまま書くと選択が 0 に戻る。ch32rv の現行実装に対する依頼書もこの点を指摘しており、CLI の拒否解除だけでは足りない。

**未確認**: 選択コアの halt / resume / step / reset、DetachChip・再 attach 時の両コア状態と選択保持、他 probe firmware・他 H41x、物理 SWIO capture。測定コードは Flash・option byte・一般メモリの書き込み、erase、reset を行っていない(DMCONTROL と abstract CSR 読み出し用 DM 作業レジスタは書く)。LinkE 内部の attach / detach 副作用は USB capture から保証しない。残る調査は [coverage の H417 項目](../coverage.ja.md#h417-core-control)に整理する。

## breakpoint の土台

- **ebreak を halt にする**: dcsr(`0x7b0`)の ebreakm(bit15)/ebreaks(bit13)/ebreaku(bit12)を立てる。これで各特権 mode の `ebreak` が例外 trap でなく Debug Mode 突入(halt)になる。**SW breakpoint に必須**(未設定だと `continue` で止まらず暴走)。verified(V203、gdb `continue` が breakpoint で停止)。
- **HW trigger 数の動的検出**: tselect(`0x7a0`)に index を write → read-back で存在確認 → mcontrol を tdata1(`0x7a1`)へ write → read-back で**定着するか**を検査(type field bits[31:28]=2)。**有無は misa/core 世代と無関係で動的検出が必須**。

実測(5 core):

| core | family | misa | marchid | HW trigger 数 |
|---|---|---|---|---|
| CH32V307 | 0x06 | `0x40901125` | `…d881` | **4** |
| CH32X035 | 0x0d | `0x40901105` | `…d883` | **4** |
| CH32V203 | 0x05 | `0x40901105` | `…d882` | **0** |
| CH32V003 | 0x09 | `0x40800014` | `…d841` | **0** |
| CH32V103 | 0x01 | `0x40101105` | `0` | **0** |

- **SW breakpoint**: 対象番地を `ebreak`(4B `0x00100073`)/ `c.ebreak`(2B `0x9002`)で上書きし read-back で着弾確認。着弾しても上記 dcsr.ebreak* を立てていないと trap して halt しない。SW(Z0)要求の break は「RAM patch → 空き HW trigger → flash SW breakpoint([pc-to-link.ja.md](pc-to-link.ja.md) §6 の直接 FLASH controller で page 書換)」の順にフォールバック。
- **flash SW breakpoint の要点**: code は低位 alias(`0x0000_0000`)で走るが、**FLASH controller には物理 flash 番地(`0x0800_0000+off`)を渡す**(alias 番地で erase/program すると効かない)。read は alias/物理どちらでも鏡。

## semihosting(RISC-V)

target が halt 状態で host に syscall を頼む機構。マジック命令列で識別する:

- 命令列: `slli`(`0x01f01013`)/ `ebreak`(`0x00100073`)/ `srai`(`0x40705013`)。PC がこの列にかかって halt したら semihosting call。
- syscall 番号(a0): `SYS_WRITE0`=`0x04`(NUL 終端文字列出力)/ `SYS_WRITEC`=`0x03`(1 文字)/ `SYS_EXIT`=`0x18` / `SYS_EXIT_EXTENDED`=`0x20`。exit の a1 は `ADP_Stopped_ApplicationExit`=`0x20026`。
- 引数は a1(GPR)経由でメモリブロックを指す。host が read_mem で読む。

## 参照

- RISC-V External Debug Support(Debug Spec)— DM/abstract command/program buffer の一次仕様
- wlink `dmi.rs`(手順の転記元)/ probe-rs / RINS
- DMI を運ぶ下の層: [pc-to-link.ja.md](pc-to-link.ja.md)
