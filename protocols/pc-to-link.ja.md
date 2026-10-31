<a id="pc-to-link"></a>

# PC ↔ WCH-Link(USB protocol)

PC と WCH-Link/WCH-LinkE 間の USB bulk protocol。層の位置づけは L2 転送(USB bulk)+ L3(WCH-Link コマンド)。DMI の中身(RISC-V Debug Module の使い方)は [riscv-debug-module.ja.md](riscv-debug-module.ja.md)。

status の語は [README の status 語彙](../README.md#status) のとおり。多くの項目は先行実装(wlink / probe-rs / minichlink / RINS / board-identify / WCH OpenOCD)から転記し、実機で確認したものを `verified` にしている。実機の範囲は特に断りがなければ WCH-LinkE fw 2.22(variant `0x12`)と旧 WCH-Link(CH549、fw 2.12)。

<a id="usb-ids"></a>

## 1. USB 識別

| モード | VID:PID | 構成 | status |
|---|---|---|---|
| RISC-V mode | `1a86:8010` | vendor bulk(MI_00)+ CDC serial | `verified`(LinkE 2.22、CH549 2.12) |
| RISC-V mode(第 2 PID) | `1a86:8011` | 同上。WCH-DAPLink の firmware image が持つ PID(§10b.4) | `attested`(ch32-device-data `read_link_version.py`、probe-rs) |
| ARM/DAP mode | `1a86:8012` | CMSIS-DAP + CDC。GetProbeInfo(`81 0d 01 01`)は EP `0x02` OUT / `0x83` IN で同じ frame が通る | `attested`(`read_link_version.py` の実測、probe-rs) |
| IAP mode | `4348:55e0` | **interface は class `0xff` / subclass `0x80` / protocol `0x55`、bulk EP 4 本(`0x01`/`0x81`/`0x02`/`0x82`)、いずれも 64 B、100 mA**。**string descriptor を持たない**(product/serial とも無し) | `verified`(LinkE。構成は §10b の Windows capture の config descriptor、VID:PID は Linux 側の列挙記録) |

- product string は `"WCH-Link"` または `"WCH_Link"`(probe-rs は両方を受ける)。
- IAP mode は probe の MCU(LinkE = CH32V305)の bootloader で、factory ISP と同じ `55e0` を名乗る。CH32X035 の factory ISP は `1a86:55e0`([pc-to-device-isp](pc-to-device-isp.ja.md) §1)で、VID だけでは区別できない系列もある。BTVER / chip 種別と、`0xA1` Identify に答えるかで判別する。

<a id="endpoints"></a>

## 2. Endpoint と転送

| 用途 | EP | status |
|---|---|---|
| command OUT / IN | `0x01` / `0x81` | `verified`(LinkE 2.22、CH549 2.12) |
| data(flash raw)OUT / IN | `0x02` / `0x82` | `verified`(LinkE 2.22、CH549 2.12。flash 書込みと高速 read で使う) |

- command 経路は frame 化(§3)。data 経路は **frame 化されない生バイト**(§5)。minichlink は data の OUT に `0x02`、IN に `0x81` を使う記述があるが、実機の data IN は `0x82`。
- data EP の bulk IN は、名目の長さが短くても **1 packet(64 B)を受けられる buffer で読む**。bulk IN の要求は packet の大きさに切り上げられるので、4 byte の buffer で読むと「64 byte 来たら失敗」になり、そのデータを失う。
- timeout: probe-rs は 100 ms 固定。command EP は 500 ms で足りる。data EP は §5b と §10b の長い転送に合わせて秒単位で取る。

<a id="frame"></a>

## 3. フレーム形式(command EP)

```
host → probe:  0x81 | cmd | len | payload...
probe → host:  0x82 | cmd | len | payload...   (成功)
probe → host:  0x81 | reason | len | ...       (エラー。観測した形)
```

- `len` は payload のバイト数。**payload の先頭 byte が sub-command として働くコマンドが多い**(`81 0d 01 13` の `13`)。sub の後ろに余分な byte を付けても probe は len を見て黙って無視し、sub だけで処理する(§4 の [GetProbeInfo(payload に余分な byte)](#cmd-get-probe-info-extra-bytes))。
- 状態: `attested`(wlink / minichlink / probe-rs / RINS 一致)。成功の形は全コマンドで `verified`。
- **エラー応答**: 先頭が `0x81` の応答を返すことがある。観測した例は target 無しの AttachChip と、stub 経路の部分書込み拒否の `81 55 01 02`(byte 1 = reason `0x55`)。reason の体系は `todo`。ch32rv は「応答の先頭が `0x81` ならエラー、byte 1 が reason」として扱っている。
- 応答の cmd byte だけで成否を判定すると、sub の違う応答(例: RstOut のつもりで GetProbeInfo の応答)を取り違える。応答の len と payload の形も確かめる。

<a id="commands"></a>

## 4. コマンド一覧

表は [protocols/data/wchlink-commands.csv](data/wchlink-commands.csv) から [tools/gen_command_tables.py](../tools/gen_command_tables.py) で作る(手で編集しない)。**request frame は frame 全体**で書き、`{…}` は可変の field。列の意味は [protocols/data/README.ja.md](data/README.ja.md)。同じコマンドを別の probe / fw で測った結果は行を分ける。

<!-- BEGIN generated:wchlink-commands section=4 -->
| request frame(host → probe) | 名前 | 応答 | status | 受理 / 効果 | 範囲(probe fw · chip) | 根拠 | 説明 |
|---|---|---|---|---|---|---|---|
| `81 0d 01 01` | <a id="cmd-get-probe-info"></a>**GetProbeInfo** | `82 0d 04 {fw_major} {fw_minor} {variant} {fw_mode}` | `verified` | yes / yes | LinkE 2.22 | usb-capture; wlink; probe-rs; minichlink | variant: 1=CH549 / 2,0x12=LinkE / 3=LinkS / 4=DAPLink / 5,0x85=LinkW。fw_mode: 0=RISC-V / 1=ARM。実測: LinkE `02 16 12 00`(2.22)、CH549 `02 0c 01 00`(2.12) |
| `81 0d 01 01` | GetProbeInfo | `82 0d 04 02 0c 01 00` | `verified` | yes / yes | CH549 Link 2.12 | usb-capture; ch32rv replay target-info-v103 |  |
| `81 0d 02 01 13` | <a id="cmd-get-probe-info-extra-bytes"></a>**GetProbeInfo(payload に余分な byte)** | `82 0d 04 02 16 12 00` | `verified` | yes / yes | LinkE 2.22 | usb-capture | probe は payload の先頭 byte を sub と読み、残りを無視して GetProbeInfo として答える。RstOut のつもりでこの frame を送ると RST は動かず、続く DmiOp の read が busy(data `ffffffff`)になる |
| `81 0d 02 01 13` | GetProbeInfo(payload に余分な byte) | `82 0d 04 02 0c 01 00` | `verified` | yes / yes | CH549 Link 2.12 | usb-capture |  |
| `81 0d 01 02` | <a id="cmd-attach-chip"></a>**AttachChip** | `82 0d 05 {family} {chip_id:be32}` | `verified` | yes / yes | LinkE 2.22 · V003 V103 V203 V205 V307 V407 X035 X315 L103 M030 H417 V006 | usb-capture; wire-capture; wlink; probe-rs; board-identify | target が無いと 4 byte の応答、または `81 55 01 xx`。接続の間 hart を halt し、target の clock を組み直して戻さない(§11) |
| `81 0d 01 02` | AttachChip | `82 0d 05 {family} {chip_id:be32}` | `verified` | yes / yes | CH549 Link 2.12 · V103 | usb-capture |  |
| `81 0d 01 03` | <a id="cmd-redetect-chip"></a>**RedetectChip** | `82 0d 01 03` | `verified` | yes / yes | LinkE 2.22 · L103 | wire-capture; board-identify | target を reset せずに probe に把握し直させる。線上では FLASH_KEYR / MODEKEYR に鍵を書く 4 件だけで clock に触らない。RedetectChip だけの状態では続く DmiOp は target に届かない(応答はデータ 0・状態 0) |
| `81 0d 01 ff` | <a id="cmd-detach-chip"></a>**DetachChip** | `82 0d 01 ff` | `verified` | yes / yes | LinkE 2.22 · L103 V203 V003 X035 | usb-capture; wire-capture | 線上は `W dmcontrol 0x40000001` → `R dmstatus` → `W dmcontrol 0x40000000`(dmactive = 0 で DM を reset) 別名: wlink=OptEnd |
| `81 0d 01 09` | <a id="cmd-set-power-3v3"></a>**SetPower 3V3 on** | `82 0d 01 …` | `attested` | yes / unknown | LinkE 2.22 | impl-agree; minichlink pgm-wch-linke.c; wlink | 3.3 V 出力を入れる |
| `81 0d 01 0a` | SetPower 3V3 off | — | `refuted` | yes / no | LinkE 2.22 | adc; E167 | 受理されるが 3V3 は 3.24 V のまま(target なしで ADC 測定) |
| `81 0d 01 09` | SetPower 3V3 on | — | `attested` | yes / unknown | CH549 Link 2.12 · V103(一体型ボード) | impl-agree | 受理される |
| `81 0d 01 0a` | SetPower 3V3 off | — | `refuted` | yes / no | CH549 Link 2.12 · V103(一体型ボード) | hw-log | 受理されるが target は走り続ける |
| `81 0d 01 0b` | <a id="cmd-set-power-5v"></a>**SetPower 5V on** | `82 0d 01 …` | `attested` | yes / unknown | LinkE 2.22 | impl-agree; minichlink pgm-wch-linke.c; wlink | 5 V 出力を入れる |
| `81 0d 01 0c` | SetPower 5V off | — | `attested` | yes / unknown | LinkE 2.22 | impl-agree; minichlink; wlink; E167 | 受理される。5V の線は未測定(3V3 は 3.24 V のまま) |
| `81 0d 01 0b` | SetPower 5V on | — | `attested` | yes / unknown | CH549 Link 2.12 · V103(一体型ボード) | impl-agree | 受理される |
| `81 0d 01 0c` | SetPower 5V off | — | `refuted` | yes / no | CH549 Link 2.12 · V103(一体型ボード) | hw-log | 受理されるが target は走り続ける |
| `81 11 01 05` | <a id="cmd-chip-info"></a>**ChipInfo** | (frame なしの生 20 byte) | `verified` | yes / yes | LinkE 2.22 · V203 V205 V307 H417 M030 | usb-capture; board-identify; wlink | `[0:2]` 不明 / `[2:4]` flash KiB(BE)/ `[4:12]` UID / `[12:16]` 解釈未確立 / `[16:20]` chip_id(BE)。UID と `[12:16]` の読み方は §9 |
| `81 01 01 01` | <a id="cmd-check-flash-protection"></a>**CheckFlashProtection / UnprotectFlash** | — | `conflict` | unknown / unknown | — | impl-agree; probe-rs; wlink | sub `0x02` = UnprotectFlash。cmd `0x01` は §5 の SetWriteMemoryRegion と同じ番号で、実装は読出し保護に cmd `0x06` を使う。写し間違いの疑い |
| `81 06 01 01` | <a id="cmd-check-read-protect"></a>**CheckReadProtect** | `82 06 01 {01=保護 / 02=非保護}` | `verified` | yes / yes | LinkE 2.22 · V203 L103 | hw-log; wlink; probe-rs |  |
| `81 06 01 02` | <a id="cmd-unprotect"></a>**Unprotect(読出し保護の解除)** | — | `verified` | yes / yes | LinkE 2.22 · V203 | hw-log; wlink; probe-rs | option page を消して読出し保護を外す。チップは code flash を全消去する |
| `81 0b 01 01` | <a id="cmd-soft-reset"></a>**soft reset(Reset sub 0x01)** | — | `verified` | yes / yes | LinkE 2.22 · L103 V203 V003 | wire-capture; hw-log; wlink; probe-rs | 線上は haltreq ×2 → PFIC_CFGR `0xE000E048` ← `0xBEEF0080`(SYSRST)。haltreq を立てたまま reset するので hart は reset vector で止まって出てくる |
| `81 0b 01 01` | soft reset(Reset sub 0x01) | — | `verified` | yes / yes | CH549 Link 2.12 · V103 | hw-log | reset 後に hart は走り出していた(線は未取得) |
| `81 0b 01 02` | <a id="cmd-chip-reset"></a>**ChipReset(Reset sub 0x02)** | — | `conflict` | unknown / unknown | — | source; WCH OpenOCD wlinke.c L1057-1060 | 転記が 2 通りある(`81 0b 02` と、len を補った `81 0b 01 02`)。原文の再確認が要る 別名: openocd=wlink_chip_reset |
| `81 0c 02 {family} {speed}` | <a id="cmd-set-speed"></a>**SetSpeed** | `82 0c 01 01` | `verified` | yes / yes | LinkE 2.22 · L103 V203 V003 | usb-capture; wire-capture; wlink; probe-rs; E163 | speed: high=`01` / medium=`02` / low=`03`(逆順)。attach 前は family に `01` を送る。範囲外の値も拒否されず応答は常に `82 0c 01 01`。線上の速さは link-to-target §5 |
| `81 0d 01 04` | <a id="cmd-get-rom-ram"></a>**GetRomRam** | `82 0d 04 …` | `single-source` | unknown / unknown | — | source; WCH OpenOCD wlinke.c L758-772 | CODE / RAM 分割の照会。応答 4 byte の `[3]` が構成番号で、意味は chip_type ごとに変わる 別名: openocd=wlink_getromram |
| `81 0d 01 13` | <a id="cmd-rst-out"></a>**RstOut on** | `82 0d 01 00` | `single-source` | yes / unknown | LinkE 2.22 | source; usb-capture; WCH OpenOCD wlinke.c L1394-1400; ch32rv 03e4e44 | RST の線を low に駆動する(配線していれば target の NRST)。受理は実測、線の電気的な効果は未測定 別名: openocd=wlink_rstout |
| `81 0d 01 14` | RstOut off | `82 0d 01 00` | `single-source` | yes / unknown | LinkE 2.22 | source; usb-capture; WCH OpenOCD wlinke.c L1394-1400; ch32rv 03e4e44 | RST の線を離す 別名: openocd=wlink_rstout |
| `81 0d 01 13` | RstOut on | `82 0d 01 ff` | `refuted` | no / no | CH549 Link 2.12 | usb-capture; ch32rv 03e4e44 | `ff` = 対応していない |
| `81 0e 01 01` | <a id="cmd-disable-debug"></a>**DisableDebug** | — | `single-source` | unknown / unknown | — | source; WCH OpenOCD wlinke.c L1781-1786 | target の debug 機能を落とす 別名: openocd=wlink_disabledebug |
| — | <a id="cmd-arm-version"></a>**ArmVersion** | — | `todo` | unknown / unknown | — | source | 名前だけがあり、byte 列は source にも無い 別名: openocd=wlink_armversion |
| `81 ff 01 41` | <a id="cmd-mode-to-dap"></a>**モード切替 RISC-V → DAP** | (なし。probe は PID 8012 で再列挙) | `verified` | yes / yes | LinkE 2.22 | hw-log; wlink; cjacker/wchlinke-mode-switch | 通常の command EP へ送る |
| `81 ff 01 52` | <a id="cmd-mode-to-riscv"></a>**モード切替 DAP → RISC-V** | (なし。probe は PID 8010 で再列挙) | `verified` | yes / yes | LinkE 2.22 | hw-log; wlink; cjacker/wchlinke-mode-switch | DAP device(PID 8012)の interface 0 の OUT EP `0x02` へ送る |
| `81 08 06 {addr} {data:be32} {op}` | <a id="cmd-dmi-op"></a>**DmiOp** | `82 08 06 {addr} {data:be32} {status}` | `verified` | yes / yes | LinkE 2.22 · V003 V103 V203 V307 X035 L103 H417 | usb-capture; wire-capture; wlink; probe-rs; RINS | op: 0 nop / 1 read / 2 write。status: 0 success / 2 failed / 3 busy(再試行)。1 件が線上の DMI 1 frame になる |
<!-- END generated:wchlink-commands section=4 -->

- **GetProbeInfo の応答 payload** = `[fw_major, fw_minor, variant, fw_mode]`。variant: 1=CH549 / 2,0x12=LinkE / 3=LinkS / 4=DAPLink / 5,0x85=LinkW。fw_mode: 0=RISC-V / 1=ARM(RV/ARM で firmware が分かれているのは CH549 だけ)。版の読み方は §10。
- **AttachChip の応答 payload** = `[family, chip_id_be32]`。family は §9。chip_id の `[7:4]` は silicon revision。
- **RstOut** は、配線していれば target の NRST を low に引く(connect-under-reset に使う)。LinkE 2.22 は `82 0d 01 00` で受理するが、線が動くかは未測定。CH549 Link 2.12 は `ff`(非対応)を返す。一体型の CH549 Link + CH32V103 ボードでは、Link の RST で chip は reset されない。
- **SetPower**: どちらの probe も受理するが、測った範囲では電源は切れなかった。LinkE 2.22 は `0a` の後も 3V3 が 3.24 V のまま([E167](../experiments/e167_linke_3v3_output/README.ja.md))。CH549 Link 2.12 は `0a`/`0c` の後も一体型ボードの V103 が走り続ける。
- **DmiOp** は 1 件が線上の DMI 1 frame になる(LinkE 2.22 + L103 の short 形式で、間に LinkE 自身のアクセスは入らない)。ただし線上の形式(short / long / burst)を選ぶのは LinkE で、AttachChip・flash・高速 read などの他のコマンドでは LinkE が自分の DMI 列を流す([link-to-target.ja.md](link-to-target.ja.md) §3)。`[addr, data_be32, op]` は RVSWD の short frame の addr7 + data32 と、DMI の op を byte に詰めたもの。その先の DM レジスタ操作は [riscv-debug-module.ja.md](riscv-debug-module.ja.md)。
- **SetSpeed** は attach の前に family `0x01` で 1 回送れば、flash・read・DMI の速さに効く。attach の後に実 family で送り直しても効く。SDI print の enable だけは、実 family での送り直しが要る(§8)。

<a id="h417-core-select"></a>

### 4a. H417 のコア選択を運ぶ DmiOp

**`verified` の範囲**: WCH-LinkE FW 2.22(serial `49808F06CE30`、`1a86:8010`) → CH32H417QEU6(`0x4170053d` / family `0xc6`)、SWIO → PB9、speed low。専用 vendor コマンドは無く、`DmiOp` で標準 DMCONTROL の hartsel を書く。同一 attach 内で `1 → 0 → 1 → 0`、独立した 2 セッションで確認。[capture と構造化結果](../captures/fixtures/ch32h417-core-select-2026-10-10/README.ja.md)。

| 操作 | command OUT (`0x01`) | command IN (`0x81`) |
|---|---|---|
| hartsel 1 を選ぶ | `81 08 06 10 00 01 00 01 02` | `82 08 06 10 00 01 00 01 00` |
| hartsel 0 を選ぶ | `81 08 06 10 00 00 00 01 02` | `82 08 06 10 00 00 00 01 00` |
| DMCONTROL を read | `81 08 06 10 00 00 00 00 01` | hartsel 1: `82 08 06 10 00 01 00 01 00` / hartsel 0: `82 08 06 10 00 00 00 01 00` |

末尾の要求 byte は op(`02`=write、`01`=read)、応答 byte は status(`00`=success)。GetProbeInfo は `82 0d 04 02 16 02 00`、AttachChip は `82 0d 05 c6 41 70 05 3d`。選択したコア 1 の halt / resume / step(haltreq `0x80010001`)と CSR 読出しは [riscv-debug-module](riscv-debug-module.ja.md#h417-core-select) を参照。DetachChip・再 attach で選択が保たれるか、高速 bulk read(§5b)が hartsel を保つかは未測定。

<a id="flash"></a>

## 5. flash 書き込み経路

**データ転送は command EP でなく data EP `0x02`/`0x82` を使う**。frame 化されず、生バイトを data_packet_size 単位(最終 packet は `0xff` pad)で送る。手順は EraseFlash(必要なら)→ AttachChip → SetWriteMemoryRegion → WriteFlashOP + stub → 確認 → WriteFlash + data → End → soft reset。

<!-- BEGIN generated:wchlink-commands section=5 -->
| request frame(host → probe) | 名前 | 応答 | status | 受理 / 効果 | 範囲(probe fw · chip) | 根拠 | 説明 |
|---|---|---|---|---|---|---|---|
| `81 02 01 01` | <a id="cmd-erase-flash"></a>**EraseFlash(chip 全体)** | — | `verified` | yes / yes | LinkE 2.22 · L103 V203 V003 | usb-capture; wire-capture; wlink | LinkE 自身が MER で消す(stub を使わない)。APB1PCENR = 0 のまま終わる。後に AttachChip を送る |
| `81 01 08 {addr:be32} {len:be32}` | <a id="cmd-set-write-memory-region"></a>**SetWriteMemoryRegion** | — | `verified` | yes / yes | LinkE 2.22 · V003 V103 V203 V307 X035 L103 | usb-capture; wlink |  |
| `81 02 01 05` | <a id="cmd-write-flash-op"></a>**WriteFlashOP(stub を送る)** | — | `verified` | yes / yes | LinkE 2.22 · V003 V103 V203 V307 X035 L103 | usb-capture; wire-capture; wlink | 直後に data EP へ family の stub を data packet 単位で送る |
| `81 02 01 07` | <a id="cmd-write-flash-confirm"></a>**確認** | `82 02 01 07` | `verified` | yes / yes | LinkE 2.22 · V003 V103 V203 V307 X035 L103 | usb-capture; wlink |  |
| `81 02 01 02` | <a id="cmd-write-flash"></a>**WriteFlash** | — | `verified` | yes / yes | LinkE 2.22 · V003 V103 V203 V307 X035 L103 | usb-capture; wlink | data EP へ write pack ごとに送り、各 pack の後に data EP から ack(`41 01 01 04`、byte 3 = `04` で成功)を 1 packet で読む |
| `81 02 01 08` | <a id="cmd-write-flash-end"></a>**End** | — | `verified` | yes / yes | LinkE 2.22 · V003 V103 V203 V307 X035 L103 | usb-capture; wlink |  |
<!-- END generated:wchlink-commands section=5 -->

family 別パラメータ(実機確認。code flash 先頭は共通 `0x08000000`):

| family | byte | 線 | stub | data packet | write pack |
|---|---|---|---|---|---|
| V003 / CH641 | `0x09` / `0x49` | **1 線 SWIO** | CH32V003 | 64 | 1024 |
| V103 | `0x01` | 2 線 | CH32V103 | 128 | 4096 |
| V20x / V30x | `0x05` / `0x06` | 2 線 | CH32V307 | 256 | 4096 |
| X035 / CH643 | `0x0d` / `0x0c` | 2 線 | CH643 | 256 | 4096 |
| L103 | `0x0e` | 2 線 | CH643(wlink の `CH32L103` は `CH643` と byte 単位で同じ blob) | 256 | 4096 |

- **stub**: 上表の名前は **wlink `src/flash_op.rs`**(元は WCH EVT の flash ルーチン)の blob で、`CH32V307` = 446 B。wlink の blob は実質 4 本で 9 family byte を覆い、RV32EC に収まるのは V003 版だけ(他は x28〜x30 を使う)。minichlink の LinkE 用 loader(`linke-flashloader-v1..v4` = 512 / 512 / 1536 / 1280 B)は**別の blob** で、同じ family に 2 系統がある([bootloader-survey の `stubs.csv`](../references/data/bootloader-survey/stubs.csv))。WCH OpenOCD の loader 18 種との照合は済んでいない。
- **LinkE は host が送った stub をそのまま走らせる**(LinkE 2.22 + L103 の線): target RAM `0x20000000` に置かれる 512 B は、送った stub(CH643、488 B)に data packet 境界までの `0xff` を足したものと byte 単位で一致する。呼び出し規約: `sp = 0x20002800`、`dcsr = 0x90c3`(ebreakm + ebreaku、prv = M)、`mstatus = 0`、a0 = コマンド(1 = 解除、8 = 書込。bit0 解除 / bit1 一括消去 / bit2 ページ消去 / bit3 書込 / bit4 確認に見える)、a1 = 番地、a2 = 長さ、データバッファ `0x20001000`。完了は `c.ebreak` で止まり(CH643 版の stub では先頭から `+0x158`。[wlink-CH643.asm](../references/data/bootloader-survey/stub_disasm/wlink-CH643.asm) の 0x158 の `9002`)、LinkE が dpc と a0(0 = 成功)を読む。stub は書込み中に IWDG を `0xAAAA` で reload する。この規約を守れば自前の stub も走らせられる(LinkE が ebreak の番地を確かめているかは未確認)。
- **stub 経路は部分書き込み不可**: chip erase 無しに mid-flash の 1 page を書くと probe が `81 55 01 02`(reason `0x55`)で拒否する。stub 経路は **full-region programming 専用**(chip erase 後、region = 全 image)。任意 page は §6 の直接 FLASH controller 経路を使う。
- **1 セッションで効く書込みは 1 回だけ**(LinkE 2.22 + CH32V307): 2 回目以降の SetWriteMemoryRegion … End は、stub の送信も Program の各 sub も data の ack(`41 01 01 04`)も正常に返るのに、何も書かない(detach/attach や soft reset を挟んでも同じ)。host は 1 回の書込みに連続した region を 1 つだけ渡し、隙間は `0xff` で埋める。ELF の `.data` の初期値は `.text` の後ろの別 segment で届くので、これを守らないと image の末尾が黙って欠ける。
- **1 線 SWIO と 2 線 RVSWD の差は USB protocol 層に現れない**: attach/DMI/flash のコマンドは同一で、配線差は LinkE firmware が吸収する。ただし 1 線 target は LinkE/LinkW のみ(旧 CH549 Link 不可)。
- **線上で LinkE がすること**(LinkE 2.22 + L103): WriteFlash の前後で RCC の周辺 clock enable(`0x40021014`〜`0x40021020`)と SysTick CTLR(`0xE000F000`)を 0 にし、戻さない。EraseFlash は stub を使わず LinkE 自身が MER で消し、APB1PCENR = 0 のまま終わる(system reset なし)。最後に soft reset を送れば既定値に戻る(§11)。
- **family 別の capability**(probe-rs 由来、`attested`):
  - **特殊消去(power-off / RST erase、§7)非対応** = `0x02` `0x03` `0x07` `0x0b`(CH56x/57x/58x/59x の BLE 系)。それ以外は対応。
  - **flash protect 系コマンド(`0x01`/`0x06`)対応** = `0x01` `0x05` `0x06` `0x09` `0x0c` `0x0d` `0x0e` `0x49` `0x4e` `0x86` `0xc6`(V103 / V20x / V30x / V003 / CH643 / X035 / L103 / CH641 / V00X / V317 / H4)。

<a id="fast-read"></a>

### 5b. 高速バルク memory read(write 経路の対)

**word 単位 DMI read の 2 桁高速化**。data EP から生バイトで流れてくる読み出し経路で、書込側(§5)の鏡。attach 済みが前提で、**flash / system / RAM のどの読める番地でも使える**。

<!-- BEGIN generated:wchlink-commands section=5b -->
| request frame(host → probe) | 名前 | 応答 | status | 受理 / 効果 | 範囲(probe fw · chip) | 根拠 | 説明 |
|---|---|---|---|---|---|---|---|
| `81 03 08 {addr:be32} {len:be32}` | <a id="cmd-set-read-memory-region"></a>**SetReadMemoryRegion** | — | `verified` | yes / yes | LinkE 2.22 · V003 V103 V203 V307 L103 | usb-capture | len は 4 の倍数 |
| `81 02 01 0c` | <a id="cmd-read-memory"></a>**ReadMemory** | — | `verified` | yes / yes | LinkE 2.22 · V003 V103 V203 V307 L103 | usb-capture; wire-capture | data EP `0x82` から len byte を読む。word ごとに byte が反転している |
| `81 02 01 0c` | ReadMemory | — | `verified` | yes / yes | CH549 Link 2.12 · V103 | usb-capture |  |
<!-- END generated:wchlink-commands section=5b -->

続けて data EP `0x82` から `len` バイトを読み切る。

- **返る 32bit word は byte 反転している**。4 byte ごとに `[0]↔[3]` / `[1]↔[2]` を入れ替えると LE に戻る。これを忘れると「読めてはいるが値が違う」形で壊れる。
- **開始番地は word 境界にする**。境界に無い番地をそのまま渡すと化ける。読みたい範囲を word 境界の窓に広げて読み、端を切り落とす。
- **1 要求の長さは転送時間で縛る**。probe が流す速さは LinkE 約 56 KB/s、CH549 約 11 KB/s(usbipd 越し。LinkE は 64 KiB 1.17 s・128 KiB 2.32 s、CH549 は 32 KiB 2.93 s)。host の timeout で要求を取り消しても **probe は最後まで送り続け、残りが次の data EP の読みに現れる**(dump がちょうど 64 byte ずれる、flash の ack の読みが image の byte を掴む、など)。1 要求を 16 KiB 程度に区切れば、最も遅い CH549 でも 1 転送が約 1.5 s に収まる。
- **線上の実体**(LinkE 2.22): 2 線の L103 / V203 では abstract memory access(`command 0x0228xxxx`、post-increment)+ abstractauto による 585 clock / 15 語の burst。1 線の V003 では program buffer の routine を `command 0x00040000`(postexec のみ)で実行する。**読んだ後に ABSTRACTCS を一度も読まない**ので、アクセスが失敗しても USB 側には現れない。CH32L103 の未マップ番地(例 `0x60000000`)は fault を出さずに 0 を返すので、そもそも失敗の合図が無い。
- **実測**(WCH-LinkE + usbipd): 32 KiB の read が word 単位 DMI の >120 s から 0.71 s(~45 KiB/s)に、4 KiB の readback verify が ~15 s から 0.6 s になる。**遅いリンクほど効く**(usbipd、Windows の CH375 ioctl 経路)。V003 / V103 / V203 / V307 / L103 と CH549 Link でバイト一致(endian 含む)を確認。

<a id="flash-controller"></a>

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
- **始める前に CTLR のモード bit を下ろす**: FTER を立ててから CTLR=0 に戻すまでの間で書込みが止まると(host の kill など)、FLASH_CTLR に `0x00020000`(FTER)が残る。CH32V003 ではこの状態が reset まで続き、その間は **WCH の stub 経路(§5)も 1 page 目しか書かない**(chip erase を含め、続く書込みが `0x08000040` から先で照合に失敗する)。target 側の状態なので probe を替えても直らない。書込み・消去の前に CTLR の FTER / FTPG / PGSTART などを下ろしておく。(LinkE、CH32V003)
- **page program は 3 方式**(消去済み前提。unlock 後)。どの family がどれかは `ch32-device-data` の `index/flash_program_method.csv`(`mode` / `program_commit` / `program_buffer_load_bits`)にある。
  - **PgStart 方式(V20x/V30x, page 256)**: CTLR=FTPG → 4B ずつ write_mem32(各 word 後 WRBUSY 待ち)→ CTLR=FTPG\|PGSTART → STATR BUSY 待ち → CTLR=0 → lock。
  - **Buffered 方式(V003/CH641 page 64, X035/CH643/L103 page 256)**: CTLR=FTPG → CTLR=FTPG\|BUFRST → BUSY 待ち → 各 word: write_mem32 → CTLR=FTPG\|BUFLOAD → BUSY 待ち → 全 word 後: FLASH_ADDR=addr → CTLR=FTPG\|STRT → BUSY 待ち → CTLR=0 → lock。X035/CH643 を PgStart 方式で書くと、program は何もせず erase だけが通る(erase は全 family 共通なので)。
  - **V103 標準 halfword 方式(erase 128 / program 標準)**: fast buffer でなく 16bit halfword(`sh`=`write_mem16`)で書く。**各 erase/program 後に未文書の commit 副作用が必須**: `*(0x40022034) = *((addr & ~3) ^ MASK)`(無いと無反応。実測)。**同じ副作用を CH32M030 も持つ**(SDK 全 12 series 走査で V103 10 箇所 / M030 8 箇所、他 10 series は 0)。**XOR マスクは series で違う — V103 = `0x1000` / M030 = `0x100`**。PG も commit も page で 1 回にまとめても EVT の手順と同じ結果になる。
- **buffer load の幅がそのまま方式を決める**(EVT の `FLASH_BufLoad` の引数。DB の `program_buffer_load_bits`): 32 bit = V003 / V006 / V205 / X035 / L103(DMI から Buffered で書ける)、64 bit = M030、128 bit = V103。**1 word ずつしか書けない DMI writer は、buffer load が 1 word より広い family で page を壊す**(V103 で実際に壊れた)。
- **flash SW breakpoint の番地**: code は低位 alias(`0x0000_0000`)で走るが、FLASH controller には物理番地(`0x0800_0000` + offset)を渡す。alias の番地で erase/program しても効かない。

実機検証: V20x/V30x(PgStart)✓ / V003/CH641(Buffered)✓ / X035/CH643(Buffered)✓ / L103(Buffered)✓(256 B page の surgical erase — 前後の page 無傷、program/verify 往復)/ V103(標準)✓。

- **消去済みセルの読み出し値は family で違う** — **チップ自身の特性**で、probe の都合ではない(RM に明文あり。例 `CH32FV2x_V3xRM`「擦除成功后，字读- 0xe339e339」。独立実装 wlink の dump とも一致)。系統 **A** = `0xFFFFFFFF`(V003 / V103 / V205 / V006 / X035 / L103 / M030)、系統 **B** = `0xe339e339`(V20x / V30x / V407 / X315 / H417。byte 列は `39 e3 39 e3`)。→ **erase 成否は read 値でなく STATR(BUSY クリア + WPRERR 無し)で判定**する。消去済みの page を read-modify-write する機能(部分書込の保存等)は、系統 B では blank と実データを区別できないので family で分ける。**値の一次ソースは `ch32-device-data` の [`index/flash_geometry.csv`](https://github.com/ch32-riscv-ug/ch32-device-data/blob/main/index/flash_geometry.csv)**(`erased_read_word` = RM 原文、`blank_check_word` = word 幅に正規化した比較用の値)。系統の意味と bootloader での使われ方は [bootloader-survey.ja.md](../references/bootloader-survey.ja.md) §2.3。

<a id="option-bytes"></a>

### 6b. option byte の書き込み(同じ経路の応用)

option bytes は通常の page と手順が違う(専用の unlock と OPTPG/OPTER)。**STM32F1 系の配置**を踏襲する family で成立する。

| reg | 番地 | 用途 |
|---|---|---|
| FLASH_OBKEYR | `0x40022008` | KEY1,KEY2 で **OPTWRE**(option 書込許可)を立てる。STM32F1 の OPTKEYR 相当 |
| FLASH_CTLR | `0x40022010` | bit4 **OPTPG** / bit5 **OPTER** / bit9 **OPTWRE**(+ §6 の STRT) |

手順(`verified`、L103 ほか):

1. **unlock**: KEYR に KEY1,KEY2 → **OBKEYR に KEY1,KEY2** → MODEKEYR に KEY1,KEY2(無害)。`CTLR & OPTWRE == 0` なら失敗として止める。
2. **option 全消去**: CTLR=`OPTER|OPTWRE` → CTLR=`OPTER|OPTWRE|STRT` → BUSY 待ち(WPRERR で中止)。**書込前に必ず消す**。その前の書込みが失敗して STATR に WRPRTERR が残っていると、ここで WPRERR に見える。始める前に STATR と CTLR をきれいにする(CH32V203)。
3. **8 halfword を書く**: 各 halfword ごとに CTLR=`OPTPG|OPTWRE` → CTLR=`OPTPG|OPTWRE|STRT` → `write_mem16(OB_BASE + i*2, value)` → BUSY 待ち。
4. CTLR=0 で OPTPG/OPTWRE を落とす。反映は **system reset 後**。

- **RDPR(halfword 0)を最初に書く**。手順 2 で保護が消えた状態が最短で済む。
- 16 byte は **値 + 補数**の 8 組(`RDPR/nRDPR`、`USER/nUSER`、`DATA0/1`、`WRPR0..3`)。補数は書き手の責任(`0xFF ^ value`)。
- **`OB_BASE` は family で違う**: 多くは `0x1FFFF800` だが **CH32M030 は `0x1FFFF300`**。全 family 共通と決め打つと M030 で別番地を叩く(`ch32-device-data` の `index/register_map.csv` が family 別の番地を持つ。`block=OB` の行)。
- RM は書込み手順を 2 系統に分ける(`half-word (OBPG)` = V003/V103/V20x/V30x/V407/X315/H417、`fast page (FTPG)` = L103/M030/V006/V205/X035)。ただし FTPG に分類される L103 でも上の OBPG の手順が実機で動くので、分類だけで手順は決まらない。
- **`RDPR` を `0xA5`(保護解除)にする書込は、チップ側で flash 全消去を誘発する**。読み出し保護の解除 = 中身を捨てること、という保護仕様そのもの。復旧手順(unbrick)はこれを利用する。
- **読み出し保護中の flash read は、その family の消去パターンを返す**(CH32V203: `0x08000000` を読むと `39 e3 39 e3`)。保護しただけで中身は消えていない(解除時に消える)。ChipInfo の `[12:16]` や power-off erase の後に同じ値が見えるのも同じ理由。**保護中でも option bytes は読める**(`RDPR=0xff` を含む 16 byte がそのまま返る)ので、保護を外す前に USER などを保存できる。
- **CH32V103 は option 領域を reset まで書込前の像で読み返す**。検証は reset を挟んでから行う。他の family では直後の read-back に反映される。
- 実機検証(L103): 現在値の round-trip 書込で不変・RDPR 維持・flash 無傷、USER の 1 bit 変更(`0xff`→`0xfd`、補数 `00`→`02`)が read-back に反映。

<a id="special-erase"></a>

## 7. 特殊消去(SWD ピン共用 target の復旧)

「Clear All Code Flash」(WCH-LinkUtility の Target タブ)相当。SWDIO/SWCLK を GPIO 等に使って通常の attach ができなくなった target を消す。**attach しない**。

<!-- BEGIN generated:wchlink-commands section=7 -->
| request frame(host → probe) | 名前 | 応答 | status | 受理 / 効果 | 範囲(probe fw · chip) | 根拠 | 説明 |
|---|---|---|---|---|---|---|---|
| `81 0d 02 0f {family}` | <a id="cmd-erase-code-flash-power-off"></a>**EraseCodeFlash by power off** | `82 0d 01 {0f=消去した / 00=消去していない}` | `verified` | yes / yes | LinkE 2.22 · X035 | usb-capture; wire-capture; adc; wlink; probe-rs; E165; E167; E168 | 3V3 は切らない。RST に約 4 ms の low pulse を入れながら halt を取り、MER で消す。`00` は約 2.1 s で、halt を取れず消していない 別名: minichlink=unbrick(コメントアウト、family 09) |
| `81 0d 02 08 {family}` | <a id="cmd-erase-code-flash-rst"></a>**EraseCodeFlash by RST pin** | — | `attested` | unknown / unknown | — | impl-agree; wlink; probe-rs | NRST の配線が要る |
<!-- END generated:wchlink-commands section=7 -->

先に SetSpeed(§4)を送る。

- **名前に反して、LinkE 2.22 は 3V3 を切らない**([E167](../experiments/e167_linke_3v3_output/README.ja.md)): target なしで 3V3 は 3.0 V 以上のまま、RST が約 43 ms で 1 になり、0.6〜2.35 s に low の pulse 67 回(幅約 4 ms、間隔約 30 ms)、約 2.45 s で 0 に戻る。その間 SWDIO / SWCLK も動く。RST 未配線の X035 では、RAM `0x20003000` の目印 256 byte がどの場合も残り、電源は一度も落ちない([E168](../experiments/e168_linke_ram_marker_reset/README.ja.md))。
- **応答**(LinkE 2.22 + CH32X035): `82 0d 01 0f` = 消去した(約 0.2 s)、`82 0d 01 00` = halt を取れず消去していない(約 2.1 s)。正常な target は 1 回目で `0f`。attach で止まった X035(§11)では 1 回目が `00` になることが多く、その回で target は(電源を落とさずに)reset され、2 回目で `0f` になる。他の family の応答の意味は記録していない。
- **minichlink の「公式 unbrick」**(`LEUnbrick`、frame `81 0d 01 0f 09`。len が 1 なのに payload が 2 byte)は「Official unbrick unreliable on x-series devices.」のコメント付きでコメントアウトされている。`single-source`(minichlink `pgm-wch-linke.c` のコメント。X 系列での不安定さは自前では見ていない)。
- power-off erase 後の flash debug-read は `0xe339e339` の繰り返し(**wlink dump も同値**なので chip の挙動そのもの。§6b の保護中の読み出しと同じ値)。この状態でも **通常 flash を実行すれば即復旧**する(実機確認)。

<a id="monitor"></a>

## 8. 実行時 I/O(monitor)

| 経路 | 機構 | status |
|---|---|---|
| SerialDMDATA | host が DMI で DMDATA0(`0x04`)/DMDATA1(`0x05`)を polling。target→host frame: data0 低 byte=`0x80\|(count+4)`、上位 3B+data1=payload。ACK は data0 に host 入力(bit7 クリア)を書く。**core は running のまま**(attach の後に resume が要る) | `verified`(LinkE 2.22、V203 で連続受信) |
| SDI print | 下の表の enable を送ると、LinkE が DMDATA0 を約 29 µs ごとに読み、文字を CDC へ forward する([link-to-target](link-to-target.ja.md) §5)。LinkE 専用 | `verified`(LinkE 2.22、V203 / L103) |
| UART bridge | probe の CDC port を読むだけ(物理 UART 配線が要る)。flash の直後に CDC の配送が止まることがあり、port を開き直すと戻る | `verified`(LinkE、ArduinoCore-CH32RV の実機ログ) |

<!-- BEGIN generated:wchlink-commands section=8 -->
| request frame(host → probe) | 名前 | 応答 | status | 受理 / 効果 | 範囲(probe fw · chip) | 根拠 | 説明 |
|---|---|---|---|---|---|---|---|
| `81 0d 02 ee 00` | <a id="cmd-sdi-print-enable"></a>**SDI print enable** | `82 0d 01 {00=成功 / ff=非対応}` | `verified` | yes / yes | LinkE 2.22 · V203 L103 | usb-capture; wlink | flag は直感と逆(`00` = enable)。AttachChip の後に実 family で SetSpeed を送り直してから送る |
| `81 0d 02 ee 01` | <a id="cmd-sdi-print-disable"></a>**SDI print disable** | `82 0d 01 {00=成功 / ff=非対応}` | `verified` | yes / yes | LinkE 2.22 · V203 L103 | usb-capture; wlink |  |
<!-- END generated:wchlink-commands section=8 -->

- **SDI enable 手順**: GetProbeInfo → SetSpeed(family=`0x01`)→ AttachChip → **SetSpeed(実 family)** → **`81 0d 02 ee 00`**。詰まりやすい点は 2 つ: enable の flag が逆(`ee 01` は disable)、AttachChip の後に実 family で SetSpeed を送り直す(LinkE が forward する DM data の番地がこれで決まると見られる)。wlink の `sdi-print enable` の usbmon capture と一致。enable の後は vendor interface を解放してよい。
- **CDC の DTR**: CDC を開くときに DTR を assert すると、LinkE は SDI の forward を 1 行で止める。SDI を読むときは DTR を触らない開き方をする(UART bridge は DTR を立てても影響しない)。

<a id="chip-identification"></a>

## 9. AttachChip 応答と chip 識別

応答に family byte + 32bit chip ID。probe-rs はこれを mask(概ね `0xffffff0f`)で照合する([7:4] は silicon revision で don't-care)。

| byte | family | core | status |
|---|---|---|---|
| `0x01` | CH32V103 | V3A | `verified`(LinkE 2.22 / CH549 2.12、chip ID `0x2500410f`) |
| `0x02` | CH57x | V3A | `attested`(probe-rs) |
| `0x03` | CH56x | V3A | `attested`(probe-rs) |
| `0x04` | CH32F10x | Cortex-M3 | `attested`(probe-rs) |
| `0x05` | CH32V20x | V4B/V4C | `verified`(V203、`0x20310500`) |
| `0x06` | CH32V30x | V4C/V4F | `verified`(V307、`0x30700528`) |
| `0x07` | CH58x | V4A | `attested`(probe-rs) |
| `0x09` | CH32V003 | V2A | `verified`(V003、`0x00300500`) |
| `0x0A` | CH8571 | (undoc) | `attested`(probe-rs) |
| `0x0B` | CH59x | V4C | `attested`(probe-rs) |
| `0x0C` | CH643 | V4C | `attested`(probe-rs) |
| `0x0D` | CH32X035 | V4C | `verified`(X035、`0x03510601`) |
| `0x0E` | CH32L103 | V4C | `verified`(L103、`0x10310710`) |
| `0x49` | CH641 | V2A | `attested`(probe-rs) |
| `0x4E` | CH32V00X | V2C | `verified`(V006 QFN32、`0x00600620`) |
| `0x86` | CH32V317 | V4F | `attested`(probe-rs) |
| `0x8B` | CH570/572 | V3C | `attested`(probe-rs) |
| `0x8E` | CH32M030 | — | `verified`(M030、`0x03bb0810`) |
| `0xA6` | CH32V407 | — | `verified`(V407、`0x46710011`) |
| `0xC6` | CH32H4 | V4F | `verified`(H417、`0x4170053d`) |
| `0xCE` | CH32V205 | — | `verified`(V205RCT6、`0x20510510`) |
| `0xE6` | CH32X315 | — | `verified`(X315MCU6、`0x31500000`) |

`verified` の行はすべて WCH-LinkE fw 2.22 の AttachChip 応答(ch32rv の replay fixture と `docs/data-requests/measured/` の記録)。V467 / X305 / M103 の family byte は未測定。

<a id="chip-id-vs-memory"></a>

**AttachChip の chip ID とメモリの device ID は同じ値**(`verified`、WCH-LinkE fw 2.22、V103 だけ CH549 Link fw 2.12。ch32rv の実機で AttachChip の応答と `read` の値を比べた): V003 / V103 / V203 / V307 / L103 / X035(2026-09-01、[記録](https://github.com/ch32-riscv-ug/ch32rv/blob/v0.24.0/docs/data-requests/measured/device-ids-2026-09-01.md))、V205 / X315(2026-10-08)、V407 / H417 / M030(2026-10-10)の 11 chip で完全一致。メモリは little-endian で読み u32 にして比べる。番地は V003 = `0x1FFFF7C4`、V103 = `0x1FFFF884`、M030 = `0x1FFFF384`、他(V20x / V30x / L103 / X035 / V205 / X315 / V407 / H417)= `0x1FFFF704`。V006 と、family byte が未測定の V467 / X305 / M103 は比べていない([coverage](../coverage.ja.md#chip-id-vs-memory))。

**ChipInfo の UID と `[12:16]`**:

- ChipInfo `[4:12]` の UID は、ESIG(`0x1FFFF7E8` からの 8 byte)を **32 bit word ごとに byte を逆順**にしたもの(V205: ChipInfo `0fb8abcd77d2bc4d`、ESIG のメモリ `cd ab b8 0f 4d bc d2 77`)。USB serial との照合などで UID を比べるときは順序を揃える。
- `[12:16]` は「protection flags」と書かれてきたが、実測値は V203 `e339e339`、V205 `ffffffff` で、その chip の消去パターン(§6)と同じ。ESIG の UID の 3 語目(`0x1FFFF7F0`)の値と見られる(AttachChip の DMI 列は最後にこの番地を読む。[link-to-target](link-to-target.ja.md) §5)。解釈は未確定。

<a id="firmware-version"></a>

## 10. firmware 版

| 項目 | 内容 | 状態 |
|---|---|---|
| 取得 | GetProbeInfo 応答の v_major / v_minor(raw byte) | `verified`(LinkE raw `0216`→2.22/v42、CH549 raw `020c`→2.12/v32) |
| 表記の三重性 | raw `02 0c` = 正規化 `2.12` = WCH 表示 `v32`(`major*10+minor`) | `attested` |
| 既知不良版 | **2.11(v31): download --reset 後に target が走らない**。2.12 で解消 | `verified`(ArduinoCore-CH32RV の実機ログ) |
| SDI print 要件 | firmware 2.10 以降 | `single-source`(wlink README) |

- **firmware image と版の対応**: WCH が配る WCH-Link 系の firmware image の sha256 と版(wcfg の版、WCH の表示 `vNN`)の表は `ch32-device-data` の [`index/link_firmware.csv`](https://github.com/ch32-riscv-ug/ch32-device-data/blob/main/index/link_firmware.csv)(10 行、binary は置かず指紋と取得元だけ)。手元の image を照合するときはこれを引く。
- 版の比較は正規化した値(`major*10+minor`)で行う。probe-rs は `v_major != 2 && v_minor < 7` と書いていて、major が 2 の版を素通りさせる。

<a id="iap"></a>

## 10b. probe firmware の更新(IAP)

status: `verified`(LinkE、独立した 2 経路)。

1. **WCH-LinkUtility V3.00**(FileVersion/ProductVersion とも `3.0.0.0`)が WCH-LinkE を **2.12 → 2.22** に更新した **USBPcap**(Windows)capture。
2. **ch32rv `probe firmware update`**(Linux/WSL2 + usbipd)による自前実装での更新。**2.22 ⇔ 2.13 を計 5 回**往復し、転送列は 1 と frame 単位で一致(seq 番号まで同じ)。中断・復旧も実測(§10b.6)。

probe 自身の firmware を書き換える経路。**target とは無関係**で、probe が USB device として別の identity に再列挙してから行う。

<a id="iap-flow"></a>

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

- **`81 0f 01 01` = IAP entry**([コマンド表](#cmd-iap-entry))。応答は返らず、probe はそのまま bootloader へ落ちる。
- **時間の実測**(109,544 B の image。Windows = 純正 [fixture](../captures/fixtures/linke-iap-update-fw212-to-222.ndjson)、Linux = ch32rv [fixture](../captures/fixtures/linke-iap-update-fw213-to-222-linux.ndjson)):

  | 事象 | Windows 純正 | Linux(usbipd 経由) |
  |---|---|---|
  | IAP entry 送信 → **IAP device が再列挙** | **1.92 s** | ~5 s(usbipd の再 attach 込み) |
  | → 最初のデータ転送 | さらに 1.66 s(計 3.58 s) | (同上に含む) |
  | **書込 pass** | 5.72 s | 6.29 s |
  | **照合 pass** | 1.03 s | 1.65 s |
  | 開始 → 終了 | **6.76 s** | 7.95 s |
  | 最後のデータ転送 → **通常 mode で再列挙** | **1.06 s** | ~4 s(同上) |

  **書込と照合の速度は別物**(書込 ≒19 KiB/s、照合 ≒104 KiB/s)。差は書込 pass の stall(§10b.2)で、「両 pass の合計 ÷ 総時間」で均すと実態を隠す。

  host は **device の消失と再出現を待つ**必要がある。「entry 後 N ms」のような固定待ちではなく、再列挙の検出で進める。**戻ってきた直後は CDC が先に enumerate される窓があり、最初の open が `busy` で失敗しうる**(§11 の attach 直後のレースと同じ。1 秒間隔の retry で回避)。
- **版は 2 か所に出る**: USB の `bcdDevice` は **BCD**(`0x0212` = 2.12 / `0x0222` = 2.22)、GetProbeInfo 応答は **binary**(`02 0c` / `02 16`)。同じ版の別表現なので、どちらで判定してもよいが混ぜない(§10)。

<!-- BEGIN generated:wchlink-commands section=10b -->
| request frame(host → probe) | 名前 | 応答 | status | 受理 / 効果 | 範囲(probe fw · chip) | 根拠 | 説明 |
|---|---|---|---|---|---|---|---|
| `81 0f 01 01` | <a id="cmd-iap-entry"></a>**IAP entry** | (なし。probe は IAP mode で再列挙) | `verified` | yes / yes | LinkE 2.12 2.13 2.22 | usb-capture; WCH-LinkUtility V3.00 capture | §10b。送った時点の probe の fw(2.12 / 2.13 / 2.22)で確認 |
<!-- END generated:wchlink-commands section=10b -->

<a id="iap-frame"></a>

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

- **`off` は累積 offset の下位 16 bit**で、64 KB ごとに巻き上がる(実測: 最終 `off=0xabbc`+44 = `0xabe8`、上位込みで 109,544)。**線上に上位 bit は流れない**ので、上位を保つのは probe 側(あるいは単純に順次追記しているだけ)。→ host は **offset 0 から 60 byte ずつ厳密に順送り**するしかなく、**seek も途中再開もできない**。
- **`len` の意味が command で揺れている**: `0x80`/`0x82` は len=60 で `off` の 2 byte を数えない(転送 64 B = 4 B header + 60 B)のに、`0x81`/`0x83` は len=`0x02` でその 2 byte を数えている。「`0x02` は長さでなく sub-command」という読みも同じく成立し、capture だけでは決着しない。**汎用の framer を書くなら、この 2 系統を別扱いにする**こと。
- IAP mode の interface は **class `0xff` / subclass `0x80` / protocol `0x55`、bulk EP 4 本**(§1)。**更新に使うのは `0x02`/`0x82` の 1 組だけ**で、`0x01`/`0x81` は使われない。
- 最終 packet だけ `len` が端数(実測 `0x2c` = 44)。
- **書込 pass と照合 pass で同じ全長を 2 回送る。** 実測はどちらも **109,544 byte** で、`WCH-LinkUtility/Firmware_Link/FIRMWARE_CH32V305.bin` と**完全一致**(両 capture で全 byte 照合済み)。
- 転送数は **書込 1,826 回 + 照合 1,826 回**(60 B × 1,825 + 端数 44 B)。**ack は 3,653 個すべて `0000`**(例外なし。両 capture)。
- **書込 pass は 64 packet(3,840 B)ごとに ~170 ms 止まる。** ack 遅延の中央値 0.4 ms に対し、**第 64・128・…・1792 packet だけが ~170 ms**(28 回)。probe が 3,840 B 溜めてから焼いていると読める。**Windows と Linux で packet 番号まで一致する**ので、driver でなく probe 側の挙動。
  - **含意: ack の timeout は 170 ms より十分大きく取る**(probe-rs 既定の 100 ms 固定では 64 転送ごとに必ず失敗する。ch32rv は 3 s)。
  - 書込 pass の端数(最後の stall 以降の 34 packet = 2,024 B)は、**照合 pass の最初の frame で flush される**(そこだけ ~170 ms)。つまり `0x82` は単なる比較ではなく、少なくとも保留分の書込を確定させる。
- **開始(`81 02 0000`)の ack は ~10 ms**。107 KB の一括消去には短すぎるので、**消去は書込 pass 中の stall 側**にあると見るのが自然(直接の証拠ではない)。
- 実バイトの抜粋は [linke-iap-update-fw212-to-222.ndjson](../captures/fixtures/linke-iap-update-fw212-to-222.ndjson)(Windows 純正)と [linke-iap-update-fw213-to-222-linux.ndjson](../captures/fixtures/linke-iap-update-fw213-to-222-linux.ndjson)(ch32rv・同じ image)。**同じ image なら seq 番号まで一致する**。

<a id="iap-image"></a>

### 10b.3 firmware image の構成

WCH-LinkUtility の `Firmware_Link/` に平文で入っている。**全ファイルが `<probe>_APP_IAP.bin` = bootloader + `FIRMWARE_<MCU>.bin` の対**になっており、bootloader 部を除いた残りが app image と**バイト完全一致**する(V3.00 と 1 つ前の版の 2 セットで確認)。

**入手先は中国語サイトのみ**: [www.wch.cn/downloads/WCH-LinkUtility_ZIP.html](https://www.wch.cn/downloads/WCH-LinkUtility_ZIP.html)。英語サイト(`wch-ic.com`)の同名ページは開いても内容が出ない(WCH-Link の User Manual は英語サイトにあるが、Utility 本体は見当たらない)。英語マニュアル `WCH-LinkUserManual-EN.pdf` は**この ZIP の `Doc/` に同梱**されている。

> **罠**: WCH のサイトは SPA(`/js/app.js` が API から中身を取る)で、`/downloads/<slug>.html` は**存在しない slug でも HTTP 200 と同一の 4,305 byte を返す**(実測: 実在ページ・デタラメな slug・当該ページの 3 つが md5 まで一致)。**HTTP ステータスやサイズで存在判定をしてはいけない。** 判定はブラウザで描画するか API を直接叩く必要がある。

| APP_IAP(外部書込機用) | BL | app(**IAP で流すのはこちら**) | probe | wcfg key |
|---|---:|---|---|---|
| `WCH-LinkE-APP-IAP.bin` 117,736 | `0x2000` | `FIRMWARE_CH32V305.bin` 109,544 | WCH-LinkE(CH32V305) | `CH32V307Ver` |
| `WCH-LinkW-APP-IAP.bin` 122,456 | `0x2000` | `FIRMWARE_CH32V208.bin` 114,264 | WCH-LinkW(CH32V208) | `CH32V208Ver` |
| `WCH-DAPLink_APP_IAP.bin` 36,292 | `0x2000` | `FIRMWARE_CH32V203.bin` 28,100 | WCH-DAPLink(CH32V203) | `CH32V203Ver` |
| `WCH-Link_APP_IAP_RV.bin` 45,784 | **`0xC00`** | `FIRMWARE_CH549.bin` 42,712 | 旧 WCH-Link RISC-V(CH549) | `CH549Ver_RV` |
| `WCH-Link_APP_IAP_ARM.bin` 27,734 | **`0xC00`** | `FIRMWARE_DAP_CH549.bin` 24,662 | 旧 WCH-Link ARM/DAP(CH549) | `CH549Ver_ARM` |

- **BL サイズは family 依存**: CH32V 系は 8 KB(`0x2000`、app は `0x08002000`)、CH549 系は 3 KB(`0xC00`)。「差が `0x2000`」は CH32V 系限定の話。
- **版は image 自身に埋まっている**: app image 内の **USB device descriptor の `bcdDevice`**(BCD)がそのまま版。probe に挿さずに版が読める。
- `wchlink.wcfg` の `Ver` 値は WCH 内部の通し番号で **`minor = Ver − 20`**。2 セットの `Ver` と、対応する image の `bcdDevice` が**全 5 機種で一致**した(下表)ので、この対応は verified。

  | key | 旧セットの Ver / image の bcdDevice | V3.00 の Ver / bcdDevice | 対象 |
  |---|---|---|---|
  | `CH32V307Ver` | 33 / `0x0213` | **42 / `0x0222`** | WCH-LinkE |
  | `CH32V208Ver` | 33 / `0x0213` | 34 / `0x0214` | WCH-LinkW |
  | `CH32V203Ver` | 32 / `0x0212` | 32 / `0x0212` | WCH-DAPLink |
  | `CH549Ver_RV` | 32 / `0x0212` | 32 / `0x0212` | 旧 WCH-Link(RISC-V) |
  | `CH549Ver_ARM` | 31 / `0x0211` | 31 / `0x0211` | 旧 WCH-Link(ARM) |

  実機の `ch32rv probe list` が出す `v42` はこの Ver 値そのもの。`firmware_version.txt` は旧セット `v34` → V3.00 `v40` と単調増加するが**どの probe の Ver とも一致しない**ので、**Firmware_Link パッケージ自体の版**と読むのが妥当。

> **罠**: `WCH-LinkUtility.exe` の版は resource 上 `3.0.0.0`(= **V3.00**)だが、binary 内には `WCH-LinkUtility V2.50` という**更新し忘れの文字列**も残っている。版の判定は resource(FileVersion / ProductVersion)を見る。

<a id="iap-image-contents"></a>

### 10b.4 firmware から確認できること

`FIRMWARE_CH32V305.bin` は RISC-V の生イメージ(先頭 `6f 10 c0 1a` = `jal`)。debug 文字列はほぼ無いが:

- **USB device descriptor が平文で 2 つ入っている**(18 byte の `12 01 …`)。CH32V 系の probe firmware は **1 つの image が RISC-V mode と DAP mode の両方の descriptor を持つ**:

  | image | 埋まっている PID | 意味 |
  |---|---|---|
  | `FIRMWARE_CH32V305` / `CH32V208` | `1a86:8010` + `1a86:8012` | LinkE / LinkW は 1 つの firmware で RV/ARM 両対応(mode 切替は §4) |
  | `FIRMWARE_CH32V203` | `1a86:8011` + `1a86:8012` | **§1 の「第 2 PID `8011`」の出どころは WCH-DAPLink** |
  | `FIRMWARE_CH549` / `FIRMWARE_DAP_CH549` | `8010` のみ / `8012` のみ | CH549 だけ RV/ARM が別 firmware(§4 と整合) |

  ここから **版(`bcdDevice`)と mode 別 PID が probe 無しで読める**。一方 **product string は全機種 `"WCH-Link"`** なので、**image から probe 型番は判別できない**(LinkE 用と LinkW 用を取り違えても image 側の情報だけでは弾けない)。

- **FLASH 解錠鍵 `0x45670123` / `0xCDEF89AB` を組み立てる命令列がある**(`lui s1,0xcdef9` + `addi a1,s1,-1621` 等、offset `0xba30` 付近)。FLASH controller base `0x40022000` の参照も同領域に集中。
- SDI print が無効なときの案内文 `"Please check the SDI, and Enable this function through the upper computer software"` が平文で入っている([serial-and-print.ja.md](serial-and-print.ja.md) §3 の SDI 経路)。

> 定数は RISC-V の `lui`+`addi` で組み立てられるため、**リテラル検索では見つからない**。上位 20 bit を `lui` の即値として走査する必要がある。

<a id="iap-host-notes"></a>

### 10b.5 実装に向けた注意(host tool を書く人へ)

**純正以外でこの経路を実装しているのは、確認できる範囲で [ch32rv](https://github.com/ch32-riscv-ug/ch32rv) の `probe firmware update` だけ**(本書の capture から実装し、実機で往復検証)。minichlink は IAP mode を検出するが更新はしない(`pgm-wch-linke.c` の `found_programmer_in_iap` は「IAP に嵌まっている」と気づくためだけ)。probe-rs / wlink にも無い。**Linux / macOS から probe を更新する手段は、ここ以外に事実上存在しない**。

<a id="iap-image-source"></a>

#### image の入手

**image は再配布せず、利用者に WCH 純正から取ってもらう。** 配布元は WCH の [WCH-LinkUtility 配布ページ](https://www.wch.cn/downloads/WCH-LinkUtility_ZIP.html)(**中国語サイトのみ**。英語サイトには見当たらない → §10b.3)。ZIP を展開した `WCH-LinkUtility/Firmware_Link/` に §10b.3 の image が平文で入っている。MounRiver Studio / WCH の IDE 同梱版にも同じものがある。tool 側は **既知の sha256 と照合する**形にしておくとよい([fixture](../captures/fixtures/linke-iap-update-fw213-to-222-linux.ndjson) の `_trim.image_sha256`)。

<a id="iap-paths"></a>

#### 経路は 3 つある

| 用途 | 手順 |
|---|---|
| **更新** | 通常 mode → `81 0f 01 01`(§10b.1)→ 再列挙待ち → §10b.2 の書込 |
| **救出**(IAP に嵌まっている) | **既に IAP mode なので entry 不要**。§10b.2 の書込だけ |
| **脱出**(app は無事だが IAP に入ってしまった) | **終了 frame `83 02 0000` を 1 つ送るだけ**。image は要らない(§10b.6) |

<a id="iap-image-checks"></a>

#### 渡された image をどう検査するか

**最も起きやすい事故は `*_APP_IAP.bin`(BL + app)を IAP に流すこと** — app 領域に bootloader を書くことになる。

| 検査 | 判定 |
|---|---|
| **BL + app か** | BL 末尾は `0xff` padding なので、**`image[bl-64 .. bl]` が全 `0xff` かつ `image[bl]` が `0xff` でない**なら BL 付き(`bl` = `0x2000` / `0xC00`)。表に依存しない構造判定 |
| RISC-V か | 先頭が `0x6f`(`jal`)。CH549 系の 8051 image(`0x02` = LJMP)を CH32V 系 probe に流すのを防ぐ |
| 版 | image 内 device descriptor の `bcdDevice`(§10b.4) |
| 型番 | **image からは判別できない**(§10b.4)。警告に留め、正しいファイル名を案内する |

- **先頭 2 byte で BL 付きを弾こうとしてはいけない**: `WCH-LinkE-APP-IAP.bin` も `6f 10` で始まる(**弾きたい当のファイルが通ってしまう**)。判定は上表の padding 構造で行う。
- サイズ照合を使うなら「既知の APP_IAP サイズなら拒否」の形にする。手元に 1 ファイルしかない場面で「差が `0x2000`」は計算できない。

<a id="iap-design"></a>

#### 設計の要点

- **明示コマンドにする。** 自動フロー(attach や flash の途中)に混ぜない。
- **`0000` 以外の応答はすべて即中断。** 異常系の応答形式が未確認のため。
- **ack の timeout を 170 ms より十分大きく**(§10b.2 の stall)。100 ms 固定は必ず失敗する。
- **転送は offset 0 から順送りのみ**(§10b.2)。途中再開の実装は無駄になる — やり直しは常に頭から。
- **書込 pass と照合 pass の両方を送る**(純正と同じ手順を崩さない)。
- **再列挙は固定待ちにしない。** device の消失と再出現をポーリングし、**戻り直後の open 失敗は retry する**(§10b.1)。
- **entry したら終了 frame まで到達する経路にし、脱出コマンドを別に用意する**(§10b.6)。

<a id="iap-unknowns"></a>

#### 未確認(実装前に埋めたい)

| 項目 | 何が困るか | 測り方 |
|---|---|---|
| **異常時の応答** | capture には `0000` しか出てこない。エラーの形が不明 | 異常を意図的に起こす必要があり、安全に測りにくい。当面は「`0000` 以外は失敗」で運用 |
| `81 02 0000` が消去を含むか | 部分書込後の状態を語れない | ack ~10 ms は全消去には短すぎる(§10b.2)。断定には別手段が要る |
| **壊れた app のまま終了 frame を送ったら** | 「壊れた app へ jump して二度と IAP に入れない」なら、そこだけは復旧不能になる | **probe を本当に失いうる唯一の操作**なので未実施。実施するなら捨てても構う個体で |

<a id="iap-interrupt-recovery"></a>

### 10b.6 中断と復旧(実測)

**IAP entry は不揮発**。3 段階で確認した(WCH-LinkE 実機):

| 試験 | 電源再投入後 |
|---|---|
| entry だけ送って停止(**書込ゼロ・app 無傷**) | **IAP mode のまま** |
| 書込 pass の **63%(57,600 / 90,868 B)で SIGKILL** | **IAP mode のまま** |
| IAP mode の device に **終了 frame `83 02 0000` を 1 つ**送る | **通常 mode へ復帰**(入っている app で起動。実測 6 s) |

- BL は「IAP に留まれ」を**不揮発に記録**しており、app が完全に無傷でも電源再投入では戻らない。**IAP に入れたら片道**で、抜くのは終了 frame か更新の完走だけ。
- 裏返して、**中断がどの時点で起きても probe は IAP に残る = 常に再試行できる**(entry 不要でそのまま書き直す)。実際、63% で殺した後の電源再投入 → `probe firmware update` の再実行だけで完全復旧した。
- **終了 frame は単独で送れる**(開始も書込も不要)。誤って IAP に入れた個体を **image 無しで**戻せる。

<a id="quirks"></a>

## 11. quirk(実測)

| quirk | 内容 |
|---|---|
| DMI NOP | addr=0/val=0 の nop が直前の read 結果を返す前提のハックが probe-rs にある(`single-source`) |
| resume 後 sleep | DMI write `0x10=0x40000001`(resume)後に ~10ms sleep が要る(probe-rs、`single-source`) |
| attach 直後のレース | 挿抜直後は CDC が vendor interface より先に enumerate され、その窓で開くと失敗。1 秒間隔 3 回 retry で回避(ArduinoCore-CH32RV の実測) |
| 大 image で固まる | 十数 KB(16.7 KB)の書込中に bulk timeout → probe 無応答化 → USB 再接続でのみ復旧(`USBDEVFS_RESET` 不可。ArduinoCore-CH32RV の実測)。§5b の「取り消された転送の残りが次の読みに出る」と同じ原因の可能性がある(未確認) |
| **LinkE の壊れ読み値** | family byte は正しいまま chip ID/UUID が同一 word の繰り返しに。再 attach でも電源断でも直らない(**probe 側の状態**)。復旧は RedetectChip(`81 0d 01 03`)+ detach + 再 attach。ChipInfo 応答全体が同一 word 繰り返しかで検出(board-identify の実測) |
| **CH549 で長い read が次の読みを汚す** | §5b。1 要求の転送が host の timeout を越えると、残りが次の data EP の読みに出る。CH549 は流す速さが遅い(約 11 KB/s)ので 64 KiB の 1 要求で必ず起きる。flash の照合の読みや ack の読みに混ざると、偽の verify-mismatch や書込みの abort に見える。16 KiB 程度の窓に分ければ起きない |
| **V103 attach quirk**(`verified`、CH32V103R8T6、ch32rv の実機) | AttachChip が生きた GPR `s1`/`x9` を chip id で上書きし復元しない(dscratch にも保存されない)。attach 直後に x9 = chip id(`0x2500410f`)。resume 後 program が s1 を使う瞬間 fault する: 症状は trap せず `0x110` の default handler(`c.j .` の無限ループ)へ飛ぶこと、mcause = 4(load-address-misaligned)、mepc = fault した `lw a5,4(s1)`。**halt → resume だけでも壊れる**(breakpoint・step を使わなくても)。V103 固有で、V003 / V203 / X035 は無事。**attach 後に soft-reset** してアプリにレジスタを作り直させると回避できる(soft reset 後は x9 が正しい RAM の pointer に戻り、halt / resume が通る)。単一 step、resumereq のクリア、prefetch の flush は関係しなかった |
| **CH549 2.12 + V103: halt と option byte の読出しの後の chip erase** | EraseFlash が timeout し、Link の endpoint が抜き差しするまで stall したままになる。erase の前に attach し直すと通る |
| attach の掴み | AttachChip は target core を掴む。セッション終了時は必ず DetachChip(失敗経路含む) |
| **AttachChip の clock 組み直し**(LinkE 2.22。L103/V203/X035 は線上、L103/V203/V307 はアプリ側でも実測。V003 では起きない) | AttachChip(`81 0d 01 02`、応答まで約 55 ms)の間、LinkE は **hart を haltreq で止めた状態で**、firmware が自分で RCC を書き換える。USB 上にはこの操作は現れない。流れ: HSI へ切替 → PLL off → 系統ごとの処理 → PLL を**系統ごとの決まった倍率**で起動 → SW = PLL。**元の clock は戻さない**。<br>・L103: reset 直後の CFGR0 `0`(HSI)が `0x001c040a` になり、FLASH_ACTLR は決め打ちで `0x1`<br>・V203: CFGR0 `0x0028000a` が `0x0034040a` になり、RCC_CFGR2(`0x4002102C`)は読まずに `0` を書く<br>・V307: CFGR0 `0x0028000a` が `0x0038040a` になる(アプリ側の観測のみ、線は未取得)<br>・X035: PLL は使わず、CFGR0 を `0`(AHB /1)、FLASH_ACTLR を `0` → `0x10` → `0x2` にする(09-11 fixture で CFGR0 `0x50` → `0`)。core の既定の clock はこれと同じなので、既定のままなら変化は見えない<br>このため、**attach → resume すると sketch は自分で設定したのと違う clock で動き続ける**。reset すれば既定値に戻る。起動時の clock で baud を決めた UART は、接続の直後から読めなくなる(E162: L103 はループが約 6 倍、V203/V307 は約 1.3 倍に速くなった)。host がこの変化を退避・復元する方法は無い(元の値は AttachChip の中で上書きされる)。<br>L103 と X035 は同じ接続の中で FLASH_CTLR = `0x8080`(LOCK\|FLOCK)と FLASH_STATR = `0xB020` を書く(V203 と V003 は書かない)。STATR の bit 12/13/15 は L103 では予約で、書く意図は不明。3 target とも ESIG(`0x1FFFF7E0`/`E8`/`EC`/`F0`)を読み、DATA0 には最後に読んだ `0x1FFFF7F0` の語が残る。<br>**V003 では RCC に一切触れない**(chip ID・CSR `0x7C0`・ESIG だけ。E162 でもアプリ側で変化なし)。<br>CSR `0x7C0` ← `0x300`(全 target): QingKe V3/V5 manual の `dbgmcu_0`。EVT の header では、V20x / V003 の bit 8/9 は IWDG_STOP / WWDG_STOP(halt 中は watchdog を止める)。L103 の header では bit の割当てが違う(IWDG=bit0、WWDG=bit1)ので、L103 での意味は未確認。<br>接続の DMI 列の全体(long 形式の問い合わせ、非標準番地 `0x7c`〜`0x7f`、resume まで)は [link-to-target](link-to-target.ja.md) §5。<br>注意: **同じ probe で前後を読んでもこの副作用は見えない**(1 回目の読み出し自体が接続なので、すでに書き換わった後を読む)。基準値は reset 直後の最初の接続の線上か、target 自身の報告から取る。<br>出典: [2026-09-25 fixture](../captures/fixtures/wire-linke-p4-2026-09-25/README.ja.md)「低速区間と L103 / V203 の比較」、[09-11 fixture](../captures/fixtures/wire-flash-v003-x035-2026-09-11/README.ja.md)(X035)、[E162](../experiments/e162_linke_attach_clock_uart/README.ja.md)(アプリ側の観測)。3 target とも `captures/tools/rvswd.py` / `swio.py` で独立に復号した(L103 `extra/l103/clock_read_rcc_0`、V203 `extra/v203/clock_read_rcc_0` と `v203-100mhz/clock_target_info`、V003 `extra/v003/clock_read_rcc_0`) |
| <a id="x035-hpre-stop"></a>**HPRE の bit 3 が 1 の X035 に接続すると target が止まる**(LinkE 2.22、X035C8T6、E162 / E164 / E165 で再現) | CFGR0 の HPRE が `1xxx`(CFGR0 bit 7 = 1。`1000` = /2、`1001` = /4)の状態で動いている X035 に AttachChip を送ると、AttachChip は成功し chip ID も正しいが、直後の ChipInfo の ESIG がすべて 0 になり、target が止まる。以後は no target。**分周比ではなく bit の値で決まる**(同じ /2 でも `0001` は止まらない。`0101` = /6 も止まらない)。速度設定にもよらない。L103(CFGR0 `0x80`)では起きない。option byte は変わらない。<br>仕組み(線上、[E165](../experiments/e165_linke_x035_v103_wire/README.ja.md)): LinkE は読んだ CFGR0 に bit 6 を立てて書き戻す(`0x50` → `0x50`、`0x90` → `0xd0`)。そのため `1xxx` は `11xx`(/32〜/256)になって HCLK が大きく落ち、以後の DMI が化けて FLASH_ACTLR ← `0xffffffff`、CFGR0 ← `0x80000000` を書き込む。attach の前に CFGR0 を読むこと自体が attach なので、事前には判定できない。<br>復旧: 特殊消去 `81 0d 02 0f 0d`(§7)を応答が `82 0d 01 0f` になるまで繰り返す。止まった状態の 1 回目は多くの場合 `00`(halt を取れず消していない)で、**その 0f の実行自体が、電源を落とさずに target を reset する**(AttachChip・RedetectChip・DetachChip・時間の経過では戻らない。[E169](../experiments/e169_x035_reset_trigger/README.ja.md))。次の 0f で halt が取れて MER で消え、続く AttachChip の中で LinkE が CFGR0 ← `0`、FLASH_ACTLR ← `2` を書いて戻る。flash は消える。1 回目の `00` の直後は flash を消さないまま AttachChip が通り hart は halt している(DMSTATUS `0x000c0382`、havereset あり)。消さずに直す経路の候補だが未使用。<br>止まった状態の特殊消去の中(E165、E168): 3V3 は切れない。約 120 ms 線が静かな後、haltreq(DMCONTROL ← `0x80000001` と DMSTATUS の読出し)を約 5.6 ms の塊で約 11 ms 間隔に送り、halt が取れたら FLASH_CTLR ← `0x8080`、STATR ← `0xB020`、KEYR / MODEKEYR の解錠、MER による全体消去を 2 回行い、OBR / WPR と `0x08000400` を読む。<br>reset の出どころ(どの線の動きか)は未特定([E170](../experiments/e170_x035_wake_burst_reset/README.ja.md)): OEP の RVSWD bit-bang では、正常な X035 は 100 clock の wake burst でも SWDIO の長い high / low でも reset されなかった。CFGR0 `0xd0` / ACTLR `0x83` で HCLK を落とした状態を作ると、DMSTATUS は SWCLK の half 2 µs 以上(約 250 kHz 以下)で安定、1 µs で時々化け、0.5 µs 以下で化けた。LinkE の接続時の区間(約 475 kHz、half 約 1.05 µs)はちょうどこの境目にある。LinkE はこれより遅い速さを選べない(E163)。<br>E162 のときは USER が `0x07`(RST_MODE `00` = PA21 が外部 reset)にもなっていて、特殊消去の直後の窓で §6b の手順により `0x1f` に戻した。化けた書込みが FLASH の register に当たった可能性が高い(推定)。<br>回避: X035 のアプリは HPRE に `0xxx` の符号化を使う |
| **flash 書込み時の周辺 clock 停止**(LinkE 2.22、L103 実測) | 書込み(§5)の経路で、APB1PCENR = 0 にしてから KEYR/MODEKEYR を解錠し、**MER(chip 全体消去)**を行う。stub を置く前に AHB/APB2/APB1 の PCENR と SysTick CTLR を 0 にし、**戻さない**。最後の PFIC SYSRST(`0xE000E048` ← `0xBEEF0080`)で既定値に戻る。**EraseFlash だけで終えると SYSRST が無いので、APB1 が止まったまま終わる** |
| **止まった page erase の残り**(CH32V003) | §6。FLASH_CTLR に FTER(`0x00020000`)が残ると、reset まで stub 経路も 1 page 目しか書かない |

<a id="open-questions"></a>

## 12. 未解読(todo)

- **エラー応答 frame の体系**(§3)。観測したのは `81 55 01 xx` の形だけ。
- `wlink_armversion`(名前だけで、byte 列は source にも無い)。
- **ChipReset の frame**(`81 0b 02` と `81 0b 01 02` の 2 通りの転記)と、cmd `0x01` の CheckFlashProtection / UnprotectFlash が実在するか(§4 の `conflict` の行)。WCH OpenOCD `wlinke.c` の原文での再確認が要る。
- **RstOut の電気的な効果**(LinkE 2.22 は受理する。RST の線を NRST につないだ target で測る)。
- GetRomRam / DisableDebug の実機での振る舞い。
- **IAP(§10b)の残り**: 異常時の応答形式、`81 02 0000` が消去を含むか、**壊れた app のまま終了 frame を送った場合**(§10b.5)。
- ChipInfo `[0:2]` と `[12:16]` の意味(§9)。
- stub の 2 系統(wlink の blob と minichlink の LinkE loader)と、WCH OpenOCD の loader 18 種の照合(§5)。

<a id="references"></a>

## 参照

- [wlink protocol.md](https://github.com/ch32-rs/wlink/blob/main/protocol.md) / [RINS: WCH-Link](https://perigoso.github.io/rins/wch-link/index.html) / minichlink `pgm-wch-linke.c` / probe-rs `probe/wlink/` / WCH OpenOCD `riscv-openocd-wch` の `src/jtag/drivers/wlinke.c`(版は記録していない)
- DMI の先: [riscv-debug-module.ja.md](riscv-debug-module.ja.md)
- 実装: [ch32rv](https://github.com/ch32-riscv-ug/ch32rv)(`crates/wchlink`)
