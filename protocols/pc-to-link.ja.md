# PC ↔ WCH-Link(USB protocol)

PC と WCH-Link/WCH-LinkE 間の USB bulk protocol。この repo で最も裏取りが進んでいる領域(大半 `verified` = 実機 capture 済み)。層の位置づけは L2 転送(USB bulk)+ L3(WCH-Link コマンド)。DMI の中身(RISC-V Debug Module の使い方)は [riscv-debug-module.ja.md](riscv-debug-module.ja.md)。

status 語彙: `verified`(自前 capture)/ `attested`(複数実装一致)/ `single-source` / `conflict` / `todo`。多くの項目は先行実装(wlink / probe-rs / minichlink / RINS / board-identify)から転記し、実機で確認して昇格した。

## 1. USB 識別

| モード | VID:PID | 構成 | 状態 |
|---|---|---|---|
| RISC-V mode | `1a86:8010` | vendor bulk(MI_00)+ CDC serial | verified(実機 2 台) |
| RISC-V mode(第 2 PID) | `1a86:8011` | 同上 | attested |
| ARM/DAP mode | `1a86:8012` | CMSIS-DAP + CDC | attested |
| IAP mode | `4348:55e0` | WCH factory ISP と同一の bulk 構成。**interface は class `0xff` / subclass `0x80` / protocol `0x55`、bulk EP 4 本(`0x01`/`0x81`/`0x02`/`0x82`)、いずれも 64 B、100 mA** | **構成は verified**(§10b の capture の config descriptor)/ **VID:PID は attested**(その capture には device descriptor が含まれず未確認) |

## 2. Endpoint と転送

| 用途 | EP | 状態 |
|---|---|---|
| command OUT / IN | `0x01` / `0x81` | **verified**(LinkE FW2.22・CH549 Link FW2.12 の実機 2 台で確認) |
| data(flash raw)OUT / IN | `0x02` / `0x82` | **verified**(flash 経路で使用を確認) |

- command 経路は frame 化(§3)。data 経路は **frame 化されない生バイト**(§5 flash)。
- timeout: probe-rs は 100ms 固定、本実装は 500ms で安定。

## 3. フレーム形式(command EP)

```
host → probe:  0x81 | cmd | len | payload...
probe → host:  0x82 | cmd | len | payload...   (成功)
```

- `len` は payload のバイト数。
- 状態: attested(wlink / minichlink / probe-rs / RINS 一致)。
- **エラー応答**: 先頭 byte や error code 体系は **todo**。target 無し attach では `0x55`(reason)を伴うエラー応答が観測される(§4 AttachChip、§5 部分書き込み拒否)。

## 4. コマンド一覧

payload は「cmd の後」を示す。応答が生バイト(frame 無し)の場合は明記する。

| cmd | sub/payload | 意味 | 状態 |
|---|---|---|---|
| `0x0d` | `0x01` | **GetProbeInfo**。応答 payload = `[fw_major, fw_minor, variant, fw_mode]`(4B)。variant: 1=CH549 / 2,0x12=LinkE / 3=LinkS / 4=DAPLink / 5,0x85=LinkW。fw_mode: 0=RISC-V / 1=ARM(RV/ARM 別 firmware は CH549 のみ) | **verified**(LinkE variant 0x12・raw `02 16`=2.22、CH549 variant 1・raw `02 0c`=2.12) |
| `0x0d` | `0x02` | **AttachChip**。応答 payload = `[family, chip_id_be32]`(5B)。target 無しは 4B 応答 or reason `0x55` エラー | **verified**(V203→family `0x05`/id `0x20310500`、V103→`0x01`/`0x2500410f`、V003→`0x09`/`0x00300500`) |
| `0x0d` | `0x03` | **RedetectChip**。target を **reset せずに** probe に把握し直させる。壊れ読み値(§7)の復旧に使う | attested |
| `0x0d` | `0xff` | **DetachChip(OptEnd)**。掴んだ core の解放 + セッション前の状態クリア | **verified** |
| `0x0d` | `0x01 0x09`/`0x0a` | 3.3V 出力 on/off(`81 0d 01 09` / `0a`) | attested |
| `0x0d` | `0x01 0x0b`/`0x0c` | 5V 出力 on/off | attested |
| `0x11` | `0x05` | **ChipInfo**。応答は **frame 無しの生 20B**: `[0:2]?` / `flash_kb(be16, [2:4])` / `UUID([4:12])` / `protection flags([12:16], 解釈未確立)` / `chip_id([16:20])`。UUID 全 0/全 ff は未応答 | **verified**(V203→flash 64KiB・UUID `b661abcd1e91bc63`。UUID は独立読取と一致) |
| `0x01` | `0x01` / `0x02` | CheckFlashProtection / UnprotectFlash | attested |
| `0x06` | `0x01` / `0x02` | CheckReadProtect(1=保護/2=非保護)/ Unprotect | verified |
| `0x0b` | — | Reset(target) | attested |
| `0x0b` | `0x01` | soft reset して実行 | verified |
| `0x0c` | `[family, speed]` | **SetSpeed**。attach 前は family 不明のため `0x01` を送る。speed は high=`0x01` / medium=`0x02` / low=`0x03`(**逆順注意**) | **verified** |
| `0x08` | `[addr, data_be32, op]`(6B) | **DmiOp**。op=0 nop / 1 read / 2 write。応答 6B `[addr, data_be32, status]`(status=0 success / 2 failed / 3 busy)。busy は再試行 | **verified**(DM 経由で全 GPR・PC・flash/RAM を読み wlink dump とバイト一致) |

DmiOp が RISC-V Debug Module への窓口。その先の DM レジスタ操作は [riscv-debug-module.ja.md](riscv-debug-module.ja.md)。**この `[addr, data_be32, op]` は RVSWD 線上フレーム(addr7+data32+op2)を byte 詰めしたもの**で、WCH-Link は透過ブリッジ(→ [link-to-target.ja.md](link-to-target.ja.md) §3)。

## 5. flash 書き込み経路

**データ転送は command EP でなく data EP `0x02`/`0x82` を使う**。frame 化されず、生バイトを data_packet_size 単位(最終 packet は `0xff` pad)で送る。

| cmd | sub/payload | 意味 | 状態 |
|---|---|---|---|
| `0x02` | `0x01` | EraseFlash(chip 全体)→ 後に AttachChip | verified |
| `0x01` | `addr_be32 len_be32` | **SetWriteMemoryRegion** | verified |
| `0x02` | `0x05` | **WriteFlashOP** → 直後に data EP へ flash stub を送る | verified |
| `0x02` | `0x07` | 確認(応答 payload[0]=`0x07`) | verified |
| `0x02` | `0x02` | **WriteFlash** → data EP へ write_pack_size(4096)ごとに chunk 送信。各 chunk 後に data EP から 4B ack を読む(`41 01 01 04`、byte3=`0x04` で成功) | verified |
| `0x02` | `0x08` | End | verified |
| `0x0b` | `0x01` | soft reset して実行 | verified |

family 別パラメータ(実機確認。code flash 先頭は共通 `0x08000000`):

| family | byte | 線 | stub | data packet | write pack |
|---|---|---|---|---|---|
| V003 / CH641 | `0x09` / `0x49` | **1 線 SWIO** | CH32V003 | 64 | 1024 |
| V103 | `0x01` | 2 線 | CH32V103 | 128 | 4096 |
| V20x / V30x | `0x05` / `0x06` | 2 線 | CH32V307 | 256 | 4096 |

- **stub 経路は部分書き込み不可**: chip erase 無しに mid-flash の 1 page を書くと probe が `81 55 01 02`(reason `0x55`)で拒否する。stub 経路は **full-region programming 専用**(chip erase 後、region = 全 image)。任意 page は §6 の直接 FLASH controller 経路を使う。
- **1 線 SWIO と 2 線 RVSWD の差は USB protocol 層に現れない**: attach/DMI/flash のコマンドは同一で、配線差は LinkE firmware が吸収する。ただし 1 線 target は LinkE/LinkW のみ(旧 CH549 Link 不可)。

## 6. 直接 FLASH controller 経路(DMI 経由・page 単位)

任意の 1 page を消去/書き込みする経路。**halt した hart の program buffer で、memory-mapped FLASH controller(`0x4002_2000`)を DMI で直接叩く**(read_mem32/write_mem32 は [riscv-debug-module.ja.md](riscv-debug-module.ja.md))。stub 不要=probe 側の `0x55` 拒否を回避。gdb flash breakpoint と option byte 書き込みの土台。

| reg | 番地 | 用途 |
|---|---|---|
| FLASH_KEYR | `0x40022004` | KEY1=`0x45670123`, KEY2=`0xCDEF89AB` で LOCK 解除 |
| FLASH_STATR | `0x4002200C` | bit0 BUSY / bit1 WRBUSY / bit4 WPRERR |
| FLASH_CTLR | `0x40022010` | bit6 STRT / bit7 LOCK / bit15 FLOCK / bit16 FTPG / bit17 FTER / bit18 BUFLOAD / bit19 BUFRST / bit21 PGSTART |
| FLASH_ADDR | `0x40022014` | 消去/プログラム page アドレス |
| FLASH_MODEKEYR | `0x40022024` | KEY1,KEY2 で FLOCK(fast mode)解除 |

- **unlock**: `CTLR & (LOCK|FLOCK) == 0` ならスキップ。else KEYR に KEY1,KEY2 → MODEKEYR に KEY1,KEY2。
- **page erase(全 family 共通)**: unlock → CTLR=FTER → FLASH_ADDR=addr → CTLR=FTER\|STRT → STATR BUSY クリア待ち → CTLR=0 → STATR 書き戻し(EOP クリア)→ lock。WPRERR で write-protect エラー。
- **page program は 3 方式**(消去済み前提。unlock 後):
  - **PgStart 方式(V20x/V30x, page 256)**: CTLR=FTPG → 4B ずつ write_mem32(各 word 後 WRBUSY 待ち)→ CTLR=FTPG\|PGSTART → STATR BUSY 待ち → CTLR=0 → lock。
  - **Buffered 方式(V003/CH641 page 64, X035/CH643/L103 page 256)**: CTLR=FTPG → CTLR=FTPG\|BUFRST → BUSY 待ち → 各 word: write_mem32 → CTLR=FTPG\|BUFLOAD → BUSY 待ち → 全 word 後: FLASH_ADDR=addr → CTLR=FTPG\|STRT → BUSY 待ち → CTLR=0 → lock。
  - **V103 標準 halfword 方式(erase 128 / program 標準)**: fast buffer でなく 16bit halfword(`sh`=`write_mem16`)で書く。**各 erase/program 後に未文書の commit 副作用が必須**: `*(0x40022034) = *((addr & ~3) ^ 0x1000)`(無いと無反応。実測)。高速化のため PG も commit も page で 1 回にまとめて EVT 手順と等価を確認。

実機検証: V20x/V30x(PgStart)✓ / V003/CH641(Buffered)✓ / X035/CH643(Buffered)✓ / L103(Buffered)attested / V103(標準)✓。

- **消去済みセルの debug read 値は family で違う**: V20x/V30x は `0xe339e339`(LinkE placeholder。実セルは 0xff)、X035/V003 は素直に `0xff`。→ **erase 成否は read 値でなく STATR(BUSY クリア + WPRERR 無し)で判定**する。

## 7. 特殊消去(SWD ピン共用 target の復旧)

「Clear All Code Flash」相当。SWDIO/SWCLK を GPIO 等に使うと通常 attach ができなくなる target を、電源/RST で再起動し、app が pin を再構成する前の boot 窓で消去する。**attach しない**。

| cmd | payload | 意味 | 状態 |
|---|---|---|---|
| `0x0c` | `family speed` | SetSpeed(先に必要) | verified |
| `0x0d` | `0x0f family` | EraseCodeFlash By Power off。probe が target を電源再投入(**LinkE/LinkW のみ**、probe 給電が条件) | verified(受理を実機確認) |
| `0x0d` | `0x08 family` | EraseCodeFlash By RST pin。NRST 配線が要る | attested |

- power-off erase 後の flash debug-read は `0xe339e339` の繰り返し(**wlink dump も同値**なので chip の挙動そのもの)。この状態でも **通常 flash を実行すれば即復旧**する(実機確認)。

## 8. 実行時 I/O(monitor)

| 経路 | 機構 | 状態 |
|---|---|---|
| SerialDMDATA | host が DMI で DMDATA0(`0x04`)/DMDATA1(`0x05`)を polling。target→host frame: data0 低 byte=`0x80\|(count+4)`、上位 3B+data1=payload。ACK は data0 に host 入力(bit7 クリア)を書く。**core は running のまま** | **verified**(V203 で連続受信) |
| SDI enable/disable | **enable=`81 0d 02 ee 00`、disable=`ee 01`**(フラグは直感と逆)。応答 payload[0]=`0x00` 成功/`0xff` 非対応。LinkE 専用 | **verified** |
| UART bridge | probe の CDC port を読むだけ(物理 UART 配線が要る) | 実装済み(未実機) |

- **SDI enable 手順**: GetProbeInfo → SetSpeed(family=`0x01` placeholder)→ AttachChip → **SetSpeed(実 family)** → **SDI enable = `81 0d 02 ee 00`**。詰まりやすい点: enable のフラグが逆(`ee 01` は disable)、AttachChip 後に実 family で SetSpeed 再送が要る。

## 9. AttachChip 応答と chip 識別

応答に family byte + 32bit chip ID。probe-rs はこれを mask(概ね `0xffffff0f`)で照合する([7:4] は silicon revision で don't-care)。

family byte(probe-rs より転記。状態: attested):

| byte | family | core |  | byte | family | core |
|---|---|---|---|---|---|---|
| `0x01` | CH32V103 | V3A |  | `0x0B` | CH59x | V4C |
| `0x02` | CH57x | V3A |  | `0x0C` | CH643 | V4C |
| `0x03` | CH56x | V3A |  | `0x0D` | CH32X035 | V4C |
| `0x04` | CH32F10x | Cortex-M3 |  | `0x0E` | CH32L103 | V4C |
| `0x05` | CH32V20x | V4B/V4C |  | `0x49` | CH641 | V2A |
| `0x06` | CH32V30x | V4C/V4F |  | `0x4E` | CH32V00X | V2C |
| `0x07` | CH58x | V4A |  | `0x86` | CH32V317 | V4F |
| `0x09` | CH32V003 | V2A |  | `0x8B` | CH570/572 | V3C |
| `0x0A` | CH8571 | (undoc) |  | `0xC6` | CH32H4 | V4F |

- gap series(V205/V407/V467/X305/X315/M030/M103)の family byte は未確定(既存に相乗りか新値か。要実機 attach)。

## 10. firmware 版

| 項目 | 内容 | 状態 |
|---|---|---|
| 取得 | GetProbeInfo 応答の v_major / v_minor(raw byte) | verified(LinkE raw `0216`→2.22/v42、CH549 raw `020c`→2.12/v32) |
| 表記の三重性 | raw `02 0c` = 正規化 `2.12` = WCH 表示 `v32`(`major*10+minor`) | attested |
| 既知不良版 | **2.11(v31): download --reset 後に target が走らない**。2.12 で解消 | verified |
| SDI print 要件 | firmware 2.10 以降 | single-source |
| 版比較の罠 | probe-rs は `v_major != 2 && v_minor < 7` の比較ミス。**正規化値で比較**すること | 教訓 |

## 10b. probe firmware の更新(IAP)

状態: **verified**(**WCH-LinkUtility V3.00**(FileVersion/ProductVersion とも `3.0.0.0`)で WCH-LinkE の firmware を **2.12 → 2.22** に更新した際の **USBPcap**(Windows)capture。手順・byte・総量まで確認)。

probe 自身の firmware を書き換える経路。**target とは無関係**で、probe が USB device として別の identity に再列挙してから行う。

### 10b.1 全体の流れ

```
① 通常 mode(1a86:8010、bcdDevice=旧版)
     host → 81 0d 01 01              GetProbeInfo(§4)
     probe → 82 0d 04 02 0c 12 00    2.12 / variant 0x12=LinkE / RISC-V
     host → 81 0f 01 01              ★ IAP entry(応答なし。probe は即再起動)
② IAP mode(別 device として再列挙)      ← §10b.2
     書込 pass → 照合 pass → 終了
③ 通常 mode(1a86:8010、bcdDevice=新版)
```

- **`81 0f 01 01` = IAP entry。** 応答は返らず、probe はそのまま bootloader へ落ちる(§12 の todo だったもの)。
- **時間の実測**(1 回の更新、[fixture](../captures/fixtures/linke-iap-update-fw212-to-222.ndjson) と同じ run):

  | 事象 | 直前からの間隔 |
  |---|---|
  | IAP entry 送信 → **IAP device が再列挙** | **1.92 s** |
  | → **最初のデータ転送** | さらに 1.66 s(entry から計 3.58 s) |
  | 書込 pass + 照合 pass(2 × 109,544 B) | **6.76 s**(≒ 32 KiB/s) |
  | 最後のデータ転送 → **通常 mode で再列挙** | **1.06 s** |

  host は **device の消失と再出現を待つ**必要がある。「entry 後 N ms」のような固定待ちではなく、再列挙の検出で進める。
- **版は 2 か所に出る**: USB の `bcdDevice` は **BCD**(`0x0212` = 2.12 / `0x0222` = 2.22)、GetProbeInfo 応答は **binary**(`02 0c` / `02 16`)。同じ版の別表現なので、どちらで判定してもよいが混ぜない(§10)。

### 10b.2 IAP mode の frame

data EP **`0x02` OUT / `0x82` IN**(bulk)。command EP は使わない。

```
host  → cmd | len | off_lo | off_hi | data...
probe → 00 00                                (毎回この 2 byte)
```

| cmd | len | 意味 |
|---|---|---|
| `0x81` | `0x02` | **開始**(payload `00 00`)。以降の書込に備える |
| `0x80` | `0x3c` | **書込**。`off` から 60 byte |
| `0x82` | `0x3c` | **照合**。同じ範囲・同じ内容をもう一度送る(2 pass 目) |
| `0x83` | `0x02` | **終了**(payload `00 00`)。probe が app へ jump |

- **`off` は 16 bit little-endian で、64 KB を超えると巻き上がる。** host が上位を管理する(実測: 最終 `off=0xabbc`+44 = `0xabe8`、上位込みで 109,544)。
- IAP mode の interface は **class `0xff` / subclass `0x80` / protocol `0x55`、bulk EP 4 本**(§1)。**更新に使うのは `0x02`/`0x82` の 1 組だけ**で、`0x01`/`0x81` は使われない。
- 最終 packet だけ `len` が端数(実測 `0x2c` = 44)。
- **書込 pass と照合 pass で同じ全長を 2 回送る。** 実測はどちらも **109,544 byte** で、`WCH-LinkUtility/Firmware_Link/FIRMWARE_CH32V305.bin` と**完全一致**(先頭 60 byte も一致)。
- 転送数は **書込 1,826 回 + 照合 1,826 回**(60 B × 1,825 + 端数 44 B)。
- 実バイトの抜粋は [captures/fixtures/linke-iap-update-fw212-to-222.ndjson](../captures/fixtures/linke-iap-update-fw212-to-222.ndjson)。

### 10b.3 firmware image の構成

WCH-LinkUtility **V3.00** の `Firmware_Link/` に平文で入っている(以下のサイズ・一致はこの版で確認)。

| ファイル | 中身 |
|---|---|
| `WCH-LinkE-APP-IAP.bin`(117,736 B) | **8 KB の IAP bootloader + app**。app 部は offset `0x2000` から `FIRMWARE_CH32V305.bin` と**バイト完全一致** |
| `FIRMWARE_CH32V305.bin`(109,544 B) | **app のみ。IAP で転送されるのはこちら** |
| `wchlink.wcfg` | **各 probe の版対応表**(下記) |

- app の配置は **`0x08002000`**(flash 先頭 8 KB が IAP bootloader)。
- `wchlink.wcfg` の `Ver` 値は **WCH 内部の通し番号**で、`minor = Ver − 20`:

  | key | Ver | = 版 | 対象 |
  |---|---:|---|---|
  | `CH32V307Ver` | 42 | **2.22** | WCH-LinkE(CH32V305 実装) |
  | `CH32V208Ver` | 34 | 2.14 | WCH-LinkW |
  | `CH32V203Ver` | 32 | 2.12 | |
  | `CH549Ver_RV` | 32 | 2.12 | 旧 WCH-Link(RISC-V) |
  | `CH549Ver_ARM` | 31 | 2.11 | 旧 WCH-Link(ARM) |

  実機の `ch32rv probe list` が出す `v42` はこの Ver 値そのもの。`firmware_version.txt` は `v40` で、この表のどの値とも一致しない(用途不明)。

> **罠**: `WCH-LinkUtility.exe` の版は resource 上 `3.0.0.0`(= **V3.00**)だが、binary 内には `WCH-LinkUtility V2.50` という**更新し忘れの文字列**も残っている。版の判定は resource(FileVersion / ProductVersion)を見る。

### 10b.4 firmware から確認できること

`FIRMWARE_CH32V305.bin` は RISC-V の生イメージ(先頭 `6f 10 c0 1a` = `jal`)。debug 文字列はほぼ無いが:

- **FLASH 解錠鍵 `0x45670123` / `0xCDEF89AB` を組み立てる命令列がある**(`lui s1,0xcdef9` + `addi a1,s1,-1621` 等、offset `0xba30` 付近)。FLASH controller base `0x40022000` の参照も同領域に集中。
- SDI print が無効なときの案内文 `"Please check the SDI, and Enable this function through the upper computer software"` が平文で入っている([serial-and-print.ja.md](serial-and-print.ja.md) §3 の SDI 経路)。

> 定数は RISC-V の `lui`+`addi` で組み立てられるため、**リテラル検索では見つからない**。上位 20 bit を `lui` の即値として走査する必要がある。

## 11. quirk(実測)

| quirk | 内容 |
|---|---|
| DMI NOP | addr=0/val=0 の nop が直前の read 結果を返す前提のハックが probe-rs にある |
| resume 後 sleep | DMI write `0x10=0x40000001`(resume)後に ~10ms sleep が要る |
| attach 直後のレース | 挿抜直後は CDC が vendor interface より先に enumerate され、その窓で開くと失敗。1 秒間隔 3 回 retry で回避 |
| 大 image で固まる | 十数 KB の書込中に bulk timeout → probe 無応答化 → USB 再接続でのみ復旧(`USBDEVFS_RESET` 不可) |
| **LinkE の壊れ読み値** | family byte は正しいまま chip ID/UUID が同一 word の繰り返しに。再 attach でも電源断でも直らない(**probe 側の状態**)。復旧は RedetectChip(`0x0d 0x03`)+ detach + 再 attach。ChipInfo 応答全体が同一 word 繰り返しかで検出 |
| **CH549 の stale fast-read** | stub 実行直後の高速 bulk read が program 前の古い flash 像(0xff/ゴミ)を返すことがある。照合は権威ある DMI 読みで再確認。偽 verify-mismatch の原因 |
| **V103 attach quirk** | AttachChip が生きた GPR `s1`/`x9` を chip id で上書きし復元しない → resume 後 program が s1 を使う瞬間 fault(V103 固有)。**attach 後に soft-reset** で回避 |
| attach の掴み | AttachChip は target core を掴む。セッション終了時は必ず DetachChip(失敗経路含む) |

## 12. 未解読(todo)

`wlink_disabledebug`、`wlink_getromram`(CODE/RAM split)、`wlink_rstout`、`wlink_chip_reset`、`wlink_armversion`、mode 切替(RV↔ARM: `81 ff 01 41`/`81 ff 01 52` の記述あり)。frame エラー応答の体系。→ 先行実装から転記 → capture で verified 化。

~~IAP entry~~ → **§10b で verified**(`81 0f 01 01`)。

## 参照

- [wlink protocol.md](https://github.com/ch32-rs/wlink/blob/main/protocol.md) / [RINS: WCH-Link](https://perigoso.github.io/rins/wch-link/index.html) / minichlink `pgm-wch-linke.c` / probe-rs `probe/wlink/`
- DMI の先: [riscv-debug-module.ja.md](riscv-debug-module.ja.md)
