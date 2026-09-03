# RISC-V Debug Module(DMI 上の操作)

[pc-to-link.ja.md](pc-to-link.ja.md) の `DmiOp`(cmd `0x08`)で DM レジスタを read/write できるようになった上で、その上に立つ **RISC-V Debug Module** の操作。これは **RISC-V External Debug Support(Debug Spec)準拠のベンダ非依存な層**で、WCH 固有ではない(他社 RISC-V にも概ね通じる)。CH32 実機(V003/V103/V203/V307/X035)で確認。状態: 大半 verified。

## DMI トランザクション(下地)

`DmiOp` 1 回 = DM レジスタ 1 個の read/write。`[addr(1B), data(be32), op(1B)]` を送り、`[addr, data(be32), status]` が返る(op: 1=read/2=write、status: 0=success/2=failed/3=busy、busy は再試行)。以下は「DM レジスタ addr にどんな値を書く/読むか」を並べたもの。

## DM レジスタ番地

| 名前 | addr | 用途 |
|---|---|---|
| DMDATA0 | `0x04` | abstract データ 0 / mailbox |
| DMDATA1 | `0x05` | abstract データ 1 / mailbox 上位 |
| DMCONTROL | `0x10` | halt/resume 要求、dmactive |
| DMSTATUS | `0x11` | halted/running 状態 |
| DMABSTRACTCS | `0x16` | abstract command 状態(busy/cmderr) |
| DMCOMMAND | `0x17` | abstract command 発行 |
| DMPROGBUF0 | `0x20` | program buffer word 0 |
| DMPROGBUF1 | `0x21` | program buffer word 1 |

## 基本操作

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
