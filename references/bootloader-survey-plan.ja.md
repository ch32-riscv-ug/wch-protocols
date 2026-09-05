# bootloader 横断調査 — 調査設計(何を・どの軸で・どんな形式で残すか)

状態: **plan**(調査の設計のみ。§8 の数値は予備調査で実ファイルから直接読んだ分だけを載せた `single-source`〜`attested`。本調査はこれから)。

対象読者: この調査を実行する人(自分/将来の自分/別の agent)。
目的: **「WCH の CH32 系で bootloader を作るとき、差はどの軸から生まれるのか」**を、後から別の軸で切り直せる形の**生データ**として残す。結論より先に**データセットの形**を決めるのがこの文書の役目。

---

## 0. 答えるべき問い(この調査のゴール)

| # | 問い | 決着の付け方 |
|---|---|---|
| **Q1** | bootloader の差は **series 軸**で説明できるのか、それとも **core / flash 世代 / transport / 電圧** など別軸か | 全 project の属性を 1 行 1 project の表にし、各列で分散を見る。軸ごとに「同値クラス」がいくつできるかを数える |
| **Q2** | **単一ソース + `#if` で全 series をカバーする bootloader** は作れるか。作れるなら「共通コア」と「切替が必要な点」の境界はどこか | 差分項目を「定数で吸収できる / 関数差替で吸収できる / 構造が違う」の 3 段階に分類する |
| **Q3** | **V003 系だけが特殊**という直感は正しいか。特殊なら何が特殊か(1,920 B、RV32EC、BOOT 領域常駐、jump 方式…) | V003 を他と分けたときに何列が説明できるかを数える。V006(V00X)との差も分ける |
| **Q4** | **BL と対で使う「拡張可能な stub」**(host が機械語を送り込んで target で実行させる方式)は、どこまで series 差を stub 側に押し出せるか | stub の引数レイアウト・呼出規約・完了印を byte 単位で表にして、series 差が stub 本体だけに収まるか見る |
| **Q5** | 容量(BOOT 領域 1,920 / 3,328 / 28 KB、user flash 20 KB 予約)の中に、どの機能セットまで入るか | 機能 × 概算サイズの表を作る。既存実装の実測サイズを根拠にする |
| **Q6** | WCH 公式 IAP の protocol は本当に「3 世代 12 series」で足りるのか。世代内の非互換は無いか | 全 series の frame/command を byte 単位で並べ、世代内の差(sync head, CalAddr 極性, blank pattern など)を洗う |

| **Q7** | BL / stub は **C と asm のどちらで書くべきか**。言語差が小さいなら保守性で C、大きいなら asm。asm にすると機能を足す余地が増えるのか | §9 の仮説 H1〜H4 を P3.5 で検証。判定基準は §9.5 |

`Q2` の答えが「作れる」なら次は実装、「作れない」なら**どこで分けるのが最小の分割か**を出す。どちらでも次の一手が決まるようにする。

---

## 1. 調査対象(インベントリ)

**参照表記**: `<repo>/<repo 内パス>` と書く。`<repo>` は下の対応表の GitHub リポジトリを指す(**ローカルの clone 位置は書かない**)。数は予備調査で存在を確認済みのもの。

| `<repo>` | GitHub | 内容 |
|---|---|---|
| `CH32V003` … `CH32H417`(12) | [`ch32-riscv-ug/<repo>`](https://github.com/ch32-riscv-ug/) | WCH 公式 EVT のミラー(`EVT/EXAM/…`)+ datasheet / RM |
| `ch32-device-data` | [`ch32-riscv-ug/ch32-device-data`](https://github.com/ch32-riscv-ug/ch32-device-data) | RM/DS から起こした構造化データ(§1e) |
| `ch32fun` | [`cnlohr/ch32fun`](https://github.com/cnlohr/ch32fun) | ch32fun 本体 + `minichlink/`(host + stub 群) |
| `rv003usb` | [`cnlohr/rv003usb`](https://github.com/cnlohr/rv003usb) — **本調査が読むのは fork** [`YuukiUmeta-UIAP/rv003usb`](https://github.com/YuukiUmeta-UIAP/rv003usb) | software USB + V003 用 bootloader |
| `ch32_user_bootloader_flasher` | [`YuukiUmeta-UIAP/ch32_user_bootloader_flasher`](https://github.com/YuukiUmeta-UIAP/ch32_user_bootloader_flasher) | app 側から BOOT 領域を書き換える updater |
| `UIAP-Devices` | [`YuukiUmeta-UIAP/UIAP-Devices`](https://github.com/YuukiUmeta-UIAP/UIAP-Devices) | UIAPduino の基板 |

### 1a. WCH 公式 EVT の IAP — bootloader 本体(**主対象**, 12 project)

| # | series | path | IAP `.ld` | APP `.ld` |
|---|---|---|:---:|:---:|
| 1 | CH32V003 | `CH32V003/EVT/EXAM/USART_IAP/CH32V003_IAP` | ✓ 1920 | — |
| 2 | CH32V00X(V006) | `CH32V006/EVT/EXAM/USART_IAP/CH32V00X_IAP` | ✓ 3328 | — |
| 3 | CH32V103 | `CH32V103/EVT/EXAM/IAP/UART_USB_IAP/CH32V103_IAP` | ✗ | ✓ |
| 4 | CH32V205 | `CH32V205/EVT/EXAM/IAP/USB_UART/CH32V205_IAP` | ✓ 20K | ✓ |
| 5 | CH32V20x | `CH32V20x/EVT/EXAM/IAP/USB_UART/CH32V20x_IAP` | ✗ | ✓ |
| 6 | CH32V30x | `CH32V307/EVT/EXAM/IAP/USB_UART/CHV30x_IAP` | ✗ | ✓ |
| 7 | CH32V407 | `CH32V407/EVT/EXAM/IAP/USB_UART/CH32V407_IAP` | ✓ 20K | ✓ |
| 8 | CH32X035 | `CH32X035/EVT/EXAM/IAP/USB_UART/CH32X035_IAP` | ✗ | ✓ |
| 9 | CH32X315 | `CH32X315/EVT/EXAM/IAP/USB_UART/CH32X315_IAP` | △ 192K | ✓ |
| 10 | CH32L103 | `CH32L103/EVT/EXAM/IAP/USB_UART/CH32L103_IAP` | ✗ | ✓ |
| 11 | CH32M030 | `CH32M030/EVT/EXAM/IAP/UART_USB_IAP/CH32M030_IAP` | ✗ | ✓ |
| 12 | CH32H417 | `CH32H417/EVT/EXAM/IAP/USB_UART/CH32H417_IAP`(**V3F / V5F の 2 core 分**) | ✓ 24K(V3F)/ △ 128K(V5F) | ✓×2 |

**対になる APP 側も同数取る**(BL 予約サイズの根拠になるため)。

`.ld` は ✓ = 実体があり **series 既定より狭い**(サイズを縛っている)、△ = 実体はあるが **series 既定と同値**(縛っていない)、✗ = 実体が無い、— = 不要(既定のままでよい)。→ 分布の意味は §8.8、扱いは §10 R1。

### 1b. EVT のその他 IAP 系(**副対象**, 比較用)

| 種別 | 数 | path 例 | なぜ見るか |
|---|---:|---|---|
| `ETH_IAP` | 2 | `CH32V20x/EVT/EXAM/ETH/ETH_IAP`, `CH32V307/EVT/EXAM/ETH/ETH_IAP` | **A/B slot + BIM** という別アーキテクチャ。Q5 の上限側 |
| `HOST_IAP`(USB host が `/APP.BIN` を読む) | 13 | `CH32{V103,V205×2,V20x,V307×2,V407,X035,X315,L103,M030,H417×2}/…/HOST_IAP` | PC 不要更新。**transport 軸の極端値** |
| BLE IAP / OTA | 3 + 1 | `CH32V20x/EVT/EXAM/BLE/{BackupUpgrade_IAP,OnlyUpdateApp_IAP,BackupUpgrade_OTA}`, `…/MESH/adv_vendor_self_provision_IAP` | 無線・ライブラリ同居時の制約 |
| `BootAsUser`(BOOT 領域を user 領域として使う手順) | 3 | `CH32{V003,V006,X035}/EVT/EXAM/FLASH/BootAsUser` | **自作 BL を BOOT 領域に置く根拠**。§2a の一次資料 |

### 1c. OSS custom bootloader(**主対象**)

| project | path | target | transport |
|---|---|---|---|
| rv003usb bootloader | `rv003usb` : `bootloader/` (`bootloader.c` 15,043 B, `ch32v003fun-usb-bootloader.ld` 3,541 B) | V003 | software USB HID |
| ch32fun USB bootloader | `ch32fun` : `examples_usb/bootloader/` (`bootloader.c` 8,267 B, `ch32x035-usb-bootloader.ld` 3,269 B, `ch570-usb-bootloader.ld` 3,511 B) | X035 / CH5xx | hardware USB HID |
| ch32_user_bootloader_flasher | `ch32_user_bootloader_flasher` : 直下 (`flasher.c` 9,480 B, `v003_flash.c` 4,406 B, `swio.h` 4,098 B, `binary_addition.S` 213 B) | V003 の BOOT 領域を **app 側から**書換 | — |
| UIAPduino(fork の PID/timeout 差) | `UIAP-Devices` : `UIAPduino/pro-micro/{ch32v003,ch32v006}` | V003 / V006 | 回路のみ(firmware は rv003usb 由来) |

未取得のため**転記待ち**: `wch-uf2`, Swindle CH32V3x DFU BL, PlumBL, tinyboot(→ §10)。

### 1d. BL と対で使う「拡張可能な stub」(**主対象**。BL 単体では意味を成さないので同格に扱う)

| stub 群 | path | 形態 | 個数(予備確認) |
|---|---|---|---:|
| minichlink b003 stub(HID scratchpad BL 用) | `ch32fun` : `minichlink/pgm-b003fun.c`(60,459 B) | C 配列の RISC-V 機械語 | `byte/half/word_wise_read`(3), `word/half/byte_wise_write`(3), `write64_flash`, `halt_wait`, `run_app`, `run_app_new` = **10 blob** |
| 同 page ループ版 | 同上 | C 配列 | `write_block_bin`, `write_block_bin_v20x_v30x`, `erase_block_bin` = **3 blob**(**V20x/V30x で別実装**という重要な差)。inline blob は合計 **13** |
| 外出し stub(`.S` → header 生成) | `ch32fun` : `minichlink/stubs/b003/*.S` + `*.h` | asm source + 生成 header | `ch5xx_flash_{addr,begin,end,erase,in,open,out,read_byte,read_word,wait,write_block}`(11), `ch5xx_write_safe`, `erase_block`, `write_block`, `write_block_v20x` = **15 本**(うち ch5xx 系 12、汎用 3) |
| stub の原典 asm | `ch32fun` : `misc/attic/rv003usb_bootloader_stubs_for_minichlink/{erase_block,write_block}.asm` + `.sh` | asm + 生成 script | **2** |
| **WCH-Link 内蔵の flash loader blob**(probe が target RAM に載せる WCH 純正 stub) | `ch32fun` : `minichlink/pgm-wch-linke.c` の `bootloader_v1..v4` | 生 byte 列 | **4**(v1 = V20x / 512 B, v2 = V20x+V30x / 512 B, v3 = CH58x·59x·570 / 1,536 B, v4 = CH57x) |

> **観点**: 上 3 群は「BL が小さいまま、能力は host が送る stub で増える」方式。最下段の WCH 純正 blob は「probe が RAM に置く flash writer」。**どちらも『series 差を stub 側に押し出す』設計**であり、Q4 の中心。

### 1f. EVT の構成メタデータ(**予備調査で追加発見。全 project にある**)

ソースコードの外に、機械可読な構成情報が 3 種類ある。**これを取らないと memory map と書込先が復元できない**。

| ファイル | 場所 | 取れる情報 | なぜ効くか |
|---|---|---|---|
| `EVT/EXAM/SRC/Ld/Link.ld` | **12 series 全部に存在**(H417 は `Ld/V3F/` `Ld/V5F/` の 2 本) | series 既定の `MEMORY`(flash 全域 / RAM 全域) | **flash/RAM の総量の一次データ**。さらに**コメントアウトされた品種別・分割別の構成**が同居している(§8.7) |
| `<project>/.template` | **全 project に存在** | `Series` / `MCU`(実型番) / `Mcu Type` / `Address`(書込先) / `Target Path`(出力が `.bin` か `.hex` か) / `SDIPrintf` / ROM・SRAM サイズを含む `Description` | project ↔ 実チップの結び付け。**`.bin`/`.hex` の別は書込手順の差**に直結 |
| `<project>/.cproject` | 全 project | linker script の参照パス | `Ld/` 欠落の検出に使う |

> **罠**: V003 / V00X の `USART_IAP` は BOOT 領域常駐(`Link.ld` が `ORIGIN=0x00000000, LENGTH=1920 / 3328`、jump が `SystemReset_StartMode(Start_Mode_USER)` = `BOOT_MODEKEYR` 解錠 + `FLASH_STATR` bit14 クリア)なのに、`.template` の `Address` は **`0x08000000` のまま**。`FLASH/BootAsUser` も同じく `Address=0x08000000` だが `Link.ld` には `BFLASH (rx) : ORIGIN = 0x1ffff000, LENGTH = 1920` がある。**`.template` の `Address` を BL の配置先として信用してはいけない** → `conflict` 行として記録する。

### 1e. 突き合わせ用の chip 素性(既存データの再利用。**新規に作らない**)

`ch32-device-data` の CSV を join する:

- `index/parts.csv`(104 行) — `flash_bytes`, `sram_bytes`, `vdd_min`, `vdd_max`, `clock_max`, `usb`
- `evidence/operating_conditions.csv`(2,797 行) — **電圧軸の一次データ**(`series,symbol,parameter,condition,min,typ,max,unit`)
- `evidence/memory_map.csv`(798 行) — 領域番地
- `index/capabilities.csv`(1,708 行) — 周辺の有無
- `evidence/evt_examples.csv`(1,605 行) — EVT sample の全数(対象漏れ検出に使う)

join key は `series` / `family`。**このリポジトリ側では chip 素性を再入力せず、`series` 列だけ持って参照する**。

---

## 2. 分析軸(この軸で切り直せるようにデータを持つ)

| 軸 | 具体値の例 | 差が出ると予想する根拠(予備調査で確認済み) |
|---|---|---|
| **A. series / family** | V003, V00X, V103, V20x, V205, V30x, V407, X035, X315, L103, M030, H417 | 既定の切り口。ただし §8 の通り**series では説明できない差**がある |
| **B. CPU core** | QingKe V2A/V2C(RV32EC)/ V3A(RV32IMAC)/ V4B,V4F / V5F、**H417 は V3F+V5F の 2 core** | H417 だけ IAP project が core ごとに 2 本ある。RV32EC は register 数と ABI が違う |
| **C. flash 書込世代** | 手書き 64 B fast(V003)/ `FLASH_ROM_WRITE` 256 B(V00X)/ `FLASH_ProgramPage_Fast` 256 B(V2x 以降)/ 128 B(V103) | **`flash.c` の実装が 4 種類**に割れている(§8) |
| **D. BL の置き場所** | BOOT 領域常駐(V003 1,920 B / V00X 3,328 B)/ user flash 先頭 20 KB / 24 KB(H417) | `Link.ld` の `ORIGIN`/`LENGTH` に直接出る |
| **E. transport** | UART only / UART+USBFS / UART+USBHS / USB host / Ethernet / BLE / software USB HID | project 構成ファイルで判別可 |
| **F. BL への entry / 抜け方** | `BOOT_MODE` register + reset / Software IRQ / GPIO / RAM magic / power-on reset 判定 | **V003·V00X と他で jump 方式が別物**(§8) |
| **G. 電圧・電源** | vdd_min/max、X035 の `PWR_VDD_SupplyVoltage()` 依存の USB 初期化 | X035 の `USBFS_Device_Init()` だけ**電圧引数を取る**(§8) |
| **H. protocol 世代** | 世代 A(UART, `AA 55`)/ B(V103, `57 AB`)/ C(UART+USB, `AA 55`) | sync head と struct レイアウトが割れている(§8) |
| **I. 容量クラス** | flash 16 KB〜1 MB、RAM 2 KB〜1 MB(H417) | `CalAddr` が series ごとに違うのは**単に flash 末尾**だから、という仮説の検証 |
| **J. 記述形態** | **C** / **asm(`.S`/`.asm`)** / **生バイト列(手書き hex 配列)** / **生成 header(`xxd -i` 出力)** | **同じ機能が 4 形態で並存**している(§8.9)。形態が違うと比較の粒度がずれるので、正規化方針が要る(§4.1) |

**再分析のため**: 上のどの軸も「1 project 1 行」の表の**列**として持つ。軸を後から足せるよう、生の `#define` は全部別テーブルに残す(§3.3)。

---

## 3. データスキーマ(これが本体)

出力先: `references/data/bootloader-survey/`(CSV)。
**CSV の書式は姉妹 repo `ch32-device-data` に合わせる** — 末尾 3 列を `#,confidence,basis` にして join / 突合ツールを共用する。`confidence` の語彙はこの repo の status 語彙(`verified` / `attested` / `single-source` / `conflict` / `todo`)を使い、`ch32-device-data` の `confirmed`/`reference` とは §5 の対応表で読み替える。

### 3.1 `projects.csv` — 1 行 1 project(分析の主キー)

```
project_id,kind,vendor,series,family,role,repo,path,doc_version,doc_date,
transport_primary,transport_secondary,resides_in,core,src_file_count,src_bytes,src_lines,
tmpl_series,tmpl_mcu,tmpl_mcu_type,tmpl_address,tmpl_target_path,tmpl_out_format,tmpl_sdi_printf,
ld_ref_path,ld_present,
#,confidence,basis
```

- `project_id`: `evt-v003-usart-iap`, `oss-rv003usb-bl`, `stub-minichlink-b003` のような安定 ID。**全テーブルの join key**
- `kind`: `evt-iap` / `evt-eth-iap` / `evt-host-iap` / `evt-ble-iap` / `evt-bootasuser` / `oss-bl` / `host-stub` / `evt-app`
- `role`: `bl` / `app` / `host`
- `resides_in`: `boot-region` / `user-flash-head` / `application` / `host`
- `doc_version` / `doc_date`: EVT ヘッダコメントの `Version : V1.0.1` / `Date : 2025/01/09` を**そのまま**(EVT 世代の追跡に効く)
- `tmpl_*`: `.template` の値を**加工せずそのまま**(§1f)。`tmpl_address` は書込先の**申告値**であって真値ではない(V003/V00X は不一致)。`tmpl_out_format` は `bin` / `hex`
- `ld_ref_path` / `ld_present`: `.cproject` が指す linker script のパスと、その実体の有無(§10 R1)

### 3.2 `memory_map.csv` — linker script の生値

```
project_id,region,origin_expr,origin_bytes,length_expr,length_bytes,constrains_size,
source_file,source_line,#,confidence,basis
```

- `*_expr` は **`64K-24K` のような式のまま**残す(劣化させない)。`*_bytes` は評価後の 10 進。両方持つ
- `region`: `FLASH` / `RAM` / `RAM_CODE` / `RAM_LOAD` / `BFLASH`
- `constrains_size`: `1` = series 既定より狭い(実際に縛っている)/ `0` = 既定と同値で無制約 / 空 = `.ld` 自体が無い。→ §8.8

### 3.2b `series_memory.csv` — series 既定の memory map と**品種別 variant**

出典は `EVT/EXAM/SRC/Ld/Link.ld`(§1f)。**コメントアウトされた構成も 1 行として取る** — これが「統一 BL が跨がねばならない構成の全集合」になる。

```
series,variant_label,parts,active,flash_origin,flash_length_expr,flash_length_bytes,
ram_origin_expr,ram_length_expr,ram_length_bytes,extra_regions,source_file,source_line,
#,confidence,basis
```

- `active`: `1` = 有効な行 / `0` = **コメントアウトされた代替構成**(捨てない)
- `variant_label` / `parts`: ld 中のコメントの文言をそのまま(`CH32V20x_D6 - CH32V203F6-CH32V203G6-CH32V203C6` など)
- `extra_regions`: H417 の `RAM_CODE` / `RAM_LOAD`、BootAsUser の `BFLASH` を `;` 区切りで
- 想定行数: **20〜30**(V307 だけで 6 構成、V20x 3、V006 3、V407 2)

### 3.3 `constants.csv` — **全 `#define` の生ダンプ**(再分析の生命線)

```
project_id,file,line,name,value_expr,value_num,category,
#,confidence,basis
```

- **フィルタしない**。`User/*.h` と `User/*.c` の全 `#define` を機械的に落とす
- `category` は後付けの分類ラベル(`protocol` / `address` / `size` / `timing` / `usb` / `pin` / `clock` / `misc`)で、**分類を間違えても `value_expr` が残っているので再分類できる**
- 想定行数: 1 project あたり 20〜80 → 全体で **1,500〜3,000 行**

### 3.4 `protocol.csv` — frame と command

```
project_id,transport,sync1,sync2,header_layout,cmd_name,cmd_byte,payload_max,
resp_ok,resp_err,endianness,checksum,source_file,source_line,#,confidence,basis
```

`header_layout` は `Cmd:u8,Len:u8,data[64]` のように**構造体宣言をそのまま文字列化**して入れる。

### 3.5 `entry_exit.csv` — BL に入る条件 / 抜ける方法(Q2 の核心)

```
project_id,phase,mechanism,expr,polarity,gpio_port,gpio_pin,marker_addr,marker_value,
blank_pattern,exit_method,deinit_steps,source_file,source_line,#,confidence,basis
```

- `phase`: `enter` / `stay` / `exit`
- `polarity`: `==` / `!=`(**V003 系と他で反転している**。§8。ここを列にしないと差が消える)
- `deinit_steps`: jump 前に落とす周辺を `;` 区切りで列挙(**移植時の落とし穴が全部ここに出る**)

### 3.6 `flash_ops.csv` — flash 操作の粒度と API

```
project_id,op,granularity_bytes,api_or_inline,register_seq,wait_flag,align_mask,
source_file,source_line,#,confidence,basis
```

`register_seq` は `CTLR|=0x10000;CTLR|=0x80000;wait STATR&1;…` のように**レジスタ操作列をそのまま**。

### 3.7 `usb.csv` / 3.8 `clock_uart.csv`

```
usb.csv:       project_id,controller,vid,pid,class,subclass,protocol,ep0_size,ep_in,ep_out,
               packet_size,vendor_or_hid,source_file,source_line,#,confidence,basis
clock_uart.csv:project_id,sysclk_hz,clock_source,uart_port,baud,brr_value,printf_baud,
               source_file,source_line,#,confidence,basis
```

### 3.9 `stubs.csv` + `stub_args.csv` — 拡張可能な stub(Q4)

```
stubs.csv:     stub_id,host_tool,repo,path,line,name,target_family,blob_bytes,purpose,
               entry_convention,completion_marker,trigger_word,
               source_form,is_generated,generated_from,disasm_path,equiv_group,reg_set,rv32ec_safe,
               #,confidence,basis
stub_args.csv: stub_id,arg_name,scratchpad_offset,width_bytes,direction,meaning,
               #,confidence,basis
```

- `source_form`: `c` / `asm` / `hex-array`(手書き)/ `generated-header`(`xxd -i` 出力)/ `raw-byte-string`(`\x..` 連結)
- `is_generated` / `generated_from`: `.h` は `.S` からの**生成物**なので `1` + 元の `stub_id`。**生成物は集計から除外**しないと二重計上になる
- `disasm_path`: 逆アセンブル結果のパス(§4.1)。`hex-array` / `raw-byte-string` は**必須**
- `equiv_group`: 同一機能の別形態を束ねる ID(§3.12)
- `reg_set` / `rv32ec_safe`: 使用レジスタ集合と、それが **x0–x15 に収まるか**(§9.3 の H3 判定)
- `blob_bytes`: 実バイト数(`sizeof` 相当)。**stub 予算の議論に直接使う**
- `entry_convention`: `void stub(uint32_t *scratchpad, volatile int32_t *runwordpad)` のような呼出規約
- **blob 本体の 16 進も `stubs_hex/<stub_id>.hex` に別ファイルで保存**(CSV に入れると壊れるため。逆アセンブルは後からできる)

### 3.12 `reg_ops.csv` — **言語をまたぐ比較の共通座標系**(§4.1 の中核)

C も asm も hex も、CH32 の flash / USB 操作は最終的に **MMIO への読み書き列**に落ちる。ここを比較の単位にすれば記述形態に依らず並べられる。

```
impl_id,seq,op,reg_name,reg_addr,value_expr,value_num,mask,wait_on,wait_bit,loop_count,
source_form,source_file,source_line,#,confidence,basis
```

- `impl_id`: `projects.csv` の `project_id` または `stubs.csv` の `stub_id`
- `seq`: 実行順(0 始まり)。**順序が仕様の一部**なので必ず持つ
- `op`: `write` / `read` / `wait` / `set-bits` / `clear-bits` / `loop-begin` / `loop-end`
- `reg_name` / `reg_addr`: `FLASH_CTLR` / `0x40022010` のように**名前とアドレスの両方**(C は名前、hex はアドレスしか出ないため)
- `wait_on` / `wait_bit`: `FLASH_STATR` / `BSY` のような完了待ちの対象
- `loop_count`: `16`(V003 の 64 B = 4 B × 16)のような繰り返し回数。式のままでも可

### 3.13 `equiv_groups.csv` — 同一機能の別形態を束ねる

```
equiv_group,label,impl_id,source_form,role,notes,#,confidence,basis
```

- 例: `eg-v003-fastprog-64` に **EVT `flash.c` の `CH32_IAP_Program`(C)/ `v003_flash.c` の `flash_write`(C)/ `write64_flash`(hex 48 B)/ `write_block.asm`(asm)+ `write_block_bin`(hex)** の 5 実装がぶら下がる(§8.9)
- `role`: `reference`(基準に置く実装)/ `variant` / `generated`
- **この表があるおかげで「C と asm が本当にずれているか」を検証できる**。正規化手法そのものの検証セットになる

### 3.14 `files.csv` — 全解析ファイルの台帳(出典の再現性)

```
project_id,path,bytes,lines,sha256,role,#,confidence,basis
```

`sha256` があるので、EVT が更新されたとき**どのファイルが変わったか**が機械判定できる。

### 3.15 `findings.csv` — 分析結果(所見)を**データとして**持つ

```
finding_id,axis,claim,scope,evidence_project_ids,evidence_refs,counterexample,
#,confidence,basis
```

所見を散文だけに置かず行にしておくと、軸を切り替えたときに「この所見はまだ生きているか」を機械的に再点検できる。

---

## 4. 抽出方法と再現性

1. **抽出は script 化**して `references/data/bootloader-survey/extract.py`(または `.sh`)に置く。手打ちした行は `basis` に `manual` と書いて区別する。
2. 各行は必ず `source_file` + `source_line` を持つ。**行番号まで持たない行は作らない**(後から原典に戻れなくなる)。
3. `basis` の書式は `ch32-device-data` に倣い、**repo 名から始める**: `evt:CH32V003/EVT/EXAM/USART_IAP/CH32V003_IAP/User/iap.h(L31)` / `oss:rv003usb/bootloader/bootloader.c(L120)` / `rm:CH32V00XRM.PDF(p.34)`。
4. **パスは repo 相対で記録し、ローカルの clone 位置(絶対パス)は CSV にもドキュメントにも書かない**。`repo` 列 + `path` 列に分け、`repo` は §1 の対応表の名前(`CH32V003` / `ch32fun` / …)を使う。
5. 数値は**式と評価値の両方**を残す(`64K-24K` と `40960`)。片方だけにすると劣化する。
6. 再実行して差分が出たら `files.csv` の `sha256` で原因を切り分ける。


### 4.1 言語・記述形態をまたぐ比較の方法(正規化方針)

同じ機能が **C / asm / 手書き hex / 生成 header** の 4 形態で並存している(§8.9)。そのまま並べると粒度がずれるので、次の方針を採る。

#### 結論: **逆コンパイルはしない。逆アセンブルはする。比較はレジスタ操作列で揃える。**

| やること | やらないこと | 理由 |
|---|---|---|
| **hex blob → 逆アセンブル**して `stub_disasm/<stub_id>.asm` に保存 | **C をビルドして逆アセンブルし asm と突き合わせる** | 逆アセンブルは**決定的**(バイト列 → 命令列は 1 対 1)で、情報が増えるだけ。逆に C → バイナリは**コンパイラ・最適化・ABI に依存**する。V003 は **RV32EC**、他は RV32IMAC で ABI が違い、同じ C から違うバイナリが出る。差が「設計の差」か「コンパイラの差」か分離できなくなる(EVT は MounRiver 前提でビルド環境の再現自体も重い) |
| **C は C のまま読む**(関数単位・行番号で記録) | C を asm に寄せる | 上に同じ |
| **asm は asm のまま読む**。ビルドで得た `.h` は `is_generated=1` で紐付けるだけ | 生成 `.h` を独立実装として数える | `.S` → `xxd -i` → `.h` の**生成物**。二重計上になる |

#### 共通座標系 = **MMIO レジスタ操作列**(`reg_ops.csv`、§3.12)

CH32 の flash 操作はすべて `FLASH->CTLR / ADDR / STATR / KEYR / MODEKEYR / BOOT_MODEKEYR` への読み書きに落ちる。**C でも asm でも逆アセンブル後の hex でも同じ行に落ちる**ので、ここを比較の単位にする。関数でも命令でもなく「操作」で揃えるのが要点。

```
C:    FLASH->CTLR = CR_BUF_RST | CR_PAGE_PG;
asm:  lui a3, %hi(0x00080000|0x00010000); c.sw a3, 4(a5);
hex:  0xb7,0x06,0x05,0x00, 0xd4,0xc3            (逆アセンブル経由)
  ↓ すべて同じ 1 行に正規化
write, FLASH_CTLR, 0x40022010, CR_BUF_RST|CR_PAGE_PG, 0x00090000, …
```

#### 三層で持つ(潰さない)

| 層 | 中身 | テーブル |
|---|---|---|
| **L2 実装層** | 生のまま。C は C、asm は asm、hex は hex + 逆アセンブル | `stubs.csv` / `flash_ops.csv` / `stubs_hex/` / `stub_disasm/` |
| **L1 意図層** | レジスタ操作列に正規化 | **`reg_ops.csv`** ← ここで比較する |
| **L0 対応層** | 同一機能の別形態を束ねる | **`equiv_groups.csv`** |

L1 は L2 から導出されるので、**L1 の正規化を間違えても L2 が残っていればやり直せる**。逆は成り立たないので L2 を必ず先に固める。

#### サイズ比較の扱い(**ここが一番ずれる**)

- **バイト数の比較は「ビルド済みバイト列がある実装」だけで行う**(hex blob、生成 `.h` の `*_bin_len`)。C しかない実装は `blob_bytes` を**空**にし、推定値を入れない
- どうしても C 実装のサイズが要るときは、**比較対象と同じ toolchain / 同じ `-march` / `-mabi` / `-O` でビルドしたときだけ**数え、`basis` に toolchain 文字列を丸ごと記録する。**別条件のビルド値と混ぜない**
- rv003usb BL の「1,920 B に収まるか」の議論はこの制約下でしか成立しない(§8.9)

#### 検証セット

**V003 の 64 B fast program が 4 形態 5 実装で存在する**(§8.9)。まずこの `equiv_group` で正規化手法を検証し、`reg_ops.csv` の行が一致することを確認してから他へ広げる。ここで一致しないなら正規化の定義が間違っている。

---

## 5. 証拠水準

| この repo | 意味 | この調査での使い方 |
|---|---|---|
| `verified` | 自前 capture で確認 | 本調査ではほぼ出ない(ソース読解のため)。実機で焼いて確認した項目のみ |
| `attested` | 複数の独立実装が一致 | 例: HID scratchpad protocol(C 版 + JS 版 2 種) |
| `single-source` | 単一実装のみ | **EVT 転記の大半はここ**。WCH 公式 1 実装しか無いので |
| `conflict` | 実装間で矛盾 | 例: §8 の VID(doc は `1A86:55E0`、V20x sample は `4348:55E0`) |
| `todo` | 存在の証拠のみ | 未取得の OSS BL |

`ch32-device-data` 側の語彙と混ぜないため、join するときは `confidence_src` 列を足して出自を明示する。

---

## 6. 成果物

```
references/
  bootloader-survey-plan.ja.md      ← この文書
  bootloader-survey.ja.md           ← 分析レポート(散文)。findings.csv と 1:1 対応
  data/bootloader-survey/
    README.ja.md                    ← 列定義・EVT_ROOT・再実行手順
    extract.py                      ← 抽出 script
    projects.csv  memory_map.csv  series_memory.csv  constants.csv  protocol.csv
    entry_exit.csv  flash_ops.csv  usb.csv  clock_uart.csv
    stubs.csv  stub_args.csv  reg_ops.csv  equiv_groups.csv
    files.csv  findings.csv
    stubs_hex/<stub_id>.hex         ← stub の生バイト(劣化なし)
    stub_disasm/<stub_id>.asm       ← 上を逆アセンブルしたもの(§4.1)
```

既存文書への還流先:
- `protocols/wch-iap.ja.md` — 世代 A/B/C の記述を実数で補強
- `protocols/custom-bootloader.ja.md` §2a/§2b — BOOT 領域表・stub 表の裏取り
- `references/bootloader-design-space.ja.md` — Q2/Q5 の結論を設計判断として反映

---

## 7. フェーズ

| Ph | 内容 | 出力 | 目安 |
|---|---|---|---|
| **P0** | 対象確定・`files.csv` 生成(sha256 まで) | `files.csv` | 済に近い(§1 が確定済み) |
| **P1** | 機械抽出: `constants.csv` / `memory_map.csv` / **`series_memory.csv`** / `clock_uart.csv` + `.template` を `projects.csv` へ | 4 表 | 短い。script 一発 |
| **P2** | 半自動: `protocol.csv` / `entry_exit.csv` / `flash_ops.csv` / `usb.csv` — grep で候補を出して目視確定 | 4 表 | ここが一番重い |
| **P3** | stub: `stubs.csv` / `stub_args.csv` / `stubs_hex/` / **`stub_disasm/`** — blob を逆アセンブルして引数レイアウト確定 | 2 表 + hex + asm | Q4 用 |
| **P3.5** | 正規化: **`equiv_groups.csv` → `reg_ops.csv`**。まず §8.9 の検証セット(64 B fast program 5 実装)で手法を検証してから全体へ | 2 表 | **軸 J の要。P4 の前提** |
| **P4** | join(`ch32-device-data`)と軸ごとの集計 → `findings.csv` | 1 表 | 分析本体 |
| **P5** | Q1〜Q6 に答えるレポート + 統一 BL の可否判断 | `bootloader-survey.ja.md` | 結論 |

P1〜P3 は独立なので並行可。**P4 を始める前に P1〜P3 の CSV を固める**(後から列を足すと分析をやり直すことになる)。

---

## 8. 予備調査で既に見えている差(実数。**すべて実ファイルから直読**)

この節は「軸が実在すること」の証拠。本調査ではこれを全 project に広げる。

### 8.1 BL の置き場所と大きさ — `Link.ld` の `MEMORY`

| project | FLASH ORIGIN | FLASH LENGTH | RAM |
|---|---|---:|---:|
| V003_IAP | `0x00000000` | **1920** | 2K |
| V00X_IAP(V006) | `0x00000000` | **3328** | 4K |
| V205_IAP | `0x00000000` | 20K | 32K |
| V407_IAP | `0x00000000` | 20K | (2 構成) |
| X315_IAP | `0x00000000` | **192K**(= flash 全体を宣言。他と流儀が違う) | 64K |
| H417_IAP **V3F** | `0x00000000` | 24K | RAM_CODE 24K @`0x20100000` |
| H417_IAP **V5F** | `0x00010000` | 128K | RAM_CODE 128K @`0x200A0000` |

対になる APP 側(BL 予約サイズの根拠):

| APP | ORIGIN | LENGTH |
|---|---|---:|
| X035_APP / L103_APP / M030_APP / V20x_APP | `0x00005000` | 42K / 44K / 64K / 64K |
| V205_APP / V307_APP / X315_APP / V407_APP | `0x00005000` | 236K / 228K / 172K / 576K-20K |
| H417_APP V3F | `0x00006000` | 64K-24K |

→ **軸 D は実在**。かつ「20 KB 予約」は 8 series で共通、H417 だけ 24 KB。

### 8.2 protocol 世代 — `iap.h` の生値

| series | sync1,sync2 | `FLASH_Base` | `CalAddr` | struct |
|---|---|---|---|---|
| V003 | `0xaa,0x55` | `0x08000000` | `0x08004000-4` | union(`other.buf[64+2]` ← **+2**) |
| V00X | `0xaa,0x55` | `0x08000000` | `0x08004000-4` | union(`other.buf[64+4]`) |
| **V103** | **`0x57,0xab`** | (無し) | (**無し**) | **struct**(`Cmd,Len,Rev[2],data[60]`) |
| V205 | `0xaa,0x55` | `0x08005000` | `0x08040000-4` | union + `program`/`verify` メンバ |
| V20x | `0xaa,0x55` | `0x08005000` | `0x08038000-4` | 同上 |
| V30x | `0xaa,0x55` | `0x08005000` | `0x08078000-4` | 同上 |
| V407 | `0xaa,0x55` | `0x08005000` | `0x080F8000-4` | 同上 |
| X035 | `0xaa,0x55` | `0x08005000` | `0x0800F800-4` | 同上 |
| X315 | `0xaa,0x55` | `0x08005000` | `0x08038000-4` | 同上 |
| L103 | `0xaa,0x55` | `0x08005000` | `0x08010000-4` | 同上 |
| M030 | `0xaa,0x55` | `0x08005000` | `0x08010000-4` | 同上 |
| H417 | `0xaa,0x55` | **`0x08006000`** | `0x08078000-4` | 同上 |

command は **12 series 全部同一**: `CMD_IAP_PROM 0x80` / `ERASE 0x81` / `VERIFY 0x82` / `END 0x83` / `JUMP_IAP 0x84`(V103 のみ `0x84` 無し)、`ERR_SUCCESS 0x00`(V103 は綴りが `ERR_SCUESS`)/ `ERR_ERROR 0x01` / `ERR_End 0x02`、`CheckNum 0x5aa55aa5`。

→ **V103 だけが完全に別世代**。他 11 series は command 共通で、**差は番地定数だけ**。Q2 に対して強い追い風。

### 8.3 entry / exit — `main.c`(**series では説明できない差**)

| 項目 | V003 / V00X | X035 / V205 / L103 / M030 | V20x / V30x / V407 / X315 |
|---|---|---|---|
| blank 判定値 | `0xFFFFFFFF` | `0xFFFFFFFF` | **`0xe339e339`** |
| `CalAddr` 判定の極性 | **`== CheckNum` で APP へ** | `!= CheckNum` で APP へ | `!= CheckNum` で APP へ |
| APP への jump | `RCC_ClearFlag(); SystemReset_StartMode(Start_Mode_USER); NVIC_SystemReset();` | `NVIC_EnableIRQ(Software_IRQn); NVIC_SetPendingIRQ(Software_IRQn);` | 同左 |
| IO entry pin | **PC0** | PA0(M030 は **PB4**) | PA0 |
| UART | USART1(BRR 直値 `0x34`@24MHz) | USART2(M030 は USART1) | USART3(V407/X315 は USART2) |
| baud | 460800 | 460800 | 460800 |
| watchdog | 無し | 無し | **`IWDG_ReloadCounter()` あり**(V30x/V407/X315) |

→ **極性の反転と blank pattern の違いは series 軸ではなく「EVT の世代」軸**。`#if` で吸収するとき、ここを定数化し損ねると起動しない。**軸 A では説明できない差の実例**。

### 8.4 flash 書込 — `flash.c` が 4 実装に割れる

| 実装 | 粒度 | series |
|---|---:|---|
| 手書き inline(`CTLR` の BUFRST/BUFLOAD/PG を直叩き、`adr &= 0xFFFFFFC0`、16 word ループ) | **64 B** | V003 |
| `FLASH_ROM_WRITE(adr, buf, 256)` | 256 B | V00X |
| `CH32_IAP_ERASE(Start,End)` + 128 B 単位 | 128 B | V103 |
| `FLASH_ProgramPage_Fast(adr, buf)` | 256 B | V205/V20x/V30x/V407/X035/X315/L103/M030/H417 |

→ **軸 C は実在**。かつ 9 series が同一 API に収束しているので、**差替が必要なのは V003 / V00X / V103 の 3 つだけ**。

### 8.5 電圧依存(軸 G の実例)

X035 だけ USB 初期化が電圧を引数に取る:

```c
USBFS_Device_Init(ENABLE, PWR_VDD_SupplyVoltage());   // CH32X035_IAP/User/main.c
```

他 series は `USBFS_Device_Init(ENABLE)` の 1 引数。→ **X035 系は VDD で USB PHY の設定が変わる**。統一 BL では「電圧を読む/読まない」を切り替える必要がある。

### 8.6 stub 側(軸 Q4 の実例)

- `pgm-b003fun.c` に `write_block_bin` と **`write_block_bin_v20x_v30x` が別に存在**(BUSY/WRBUSY 待ちの違い)→ **stub にも series 差が漏れている**
- WCH 純正 flash loader blob は **v1(V20x, 512 B)/ v2(V20x+V30x, 512 B)/ v3(CH58x·59x·570, 1,536 B)/ v4(CH57x)** の 4 本
- 外出し stub は **15 本の `.S`**(ch5xx 系 12 + `erase_block` / `write_block` / `write_block_v20x`)に分割され、生成 header が **minichlink 本体から `#include` される**(= stub を外部ファイル化して増やせる構造)。**同じ stub が inline 版と `.S` 版で二重管理**されている点も要確認

### 8.7 series 既定の memory map と**品種別 variant**(`EXAM/SRC/Ld/Link.ld`)

| series | 有効な構成 FLASH / RAM | ld 内にコメントアウトで同居する代替構成 |
|---|---|---|
| CH32V003 | 16K / 2K | — |
| CH32V006 | 62K / 8K(V006·V007·M007) | V002 = 16K/4K、V004·V005 = 32K/6K |
| CH32V103 | 64K / 20K | — |
| CH32V205 | 256K / 32K | — |
| CH32V20x | 64K / 20K(V203K8·C8·G8·F8) | V203F6·G6·C6 = 32K/10K、V203RB·V208x = **128K/64K, 144K/48K, 160K/32K** |
| CH32V307 | 288K / 32K | V305RB·FB·V303CB·RB = 128K/32K、V307VC 系 = **192K/128K, 224K/96K, 256K/64K, 288K/32K, 128K/192K** |
| CH32V407 | 576K / 136K-1K(RAM は `0x20000000+1024` 始まり) | **512K/200K** |
| CH32X035 | 62K / 20K | — |
| CH32X315 | 192K / 64K | — |
| CH32L103 | 64K / 20K | — |
| CH32M030 | 64K / 12K | — |
| CH32H417 **V3F** | 64K @`0x00000000` / RAM 448K-256 @`0x20110000+256` | `RAM_CODE` 64K @`0x20100000`、`RAM_LOAD` 256 B |
| CH32H417 **V5F** | 128K @**`0x00010000`** / RAM 256K-768 @`0x200C0000+768` | `RAM_CODE`(ITCM)128K @`0x200A0000`、`RAM_LOAD` 256 B |

→ **flash/RAM は品種で固定ではなく、V20x・V30x・V407 は option で分割を選ぶ**。統一 BL は「flash 末尾がどこか」を build 時定数で決め打ちできない可能性がある(現行 EVT IAP の `CalAddr` は決め打ち)。**Q5/Q6 に直結する重要な制約**なので、`series_memory.csv` に代替構成も残す(§3.2b)。

→ H417 V5F だけ **flash ORIGIN が `0x00010000`**(= `0x08010000`)。「BL は flash 先頭」という前提が崩れる唯一の例。

### 8.8 `.ld` の在り処が語ること — **BL のサイズ上限はほぼ強制されていない**

`.ld` は **「series 既定からズレる側の project にだけ」**置かれている。

| 群 | series | IAP `.ld` | APP `.ld` | ズレている側 |
|---|---|:---:|:---:|---|
| I | V003, V006 | **有**(1920 / 3328) | **無** | **IAP**。BOOT 領域に収める必要がある。APP は 0 番地から全域なので既定でよい |
| II | V103, V20x, V30x, X035, L103, M030 | **無** | 有 | **APP**(`ORIGIN=0x00005000`)。IAP は 0 番地からなので**既定でも build が通ってしまう** |
| III | V205, V407, X315, H417 | 有 | 有 | WCH が両方入れた |

さらに群 III の中身を見ると、実体があっても縛っていないものがある:

| IAP の `.ld` | FLASH LENGTH | series 既定 | 縛っているか |
|---|---|---|---|
| V003 / V006 | 1920 / 3328 | 16K / 62K | **縛る** |
| V205 / V407 | 20K / 20K | 256K / 576K | **縛る** |
| H417 V3F | 24K | 64K | **縛る** |
| X315 | 192K | 192K | **縛らない**(既定と同値) |
| H417 V5F | 128K @`0x00010000` | 128K @`0x00010000` | **縛らない**(既定と同値) |

→ **IAP のサイズ上限を EVT が実際に強制しているのは 12 project 中 5 つだけ**。残り 7 つは BL が予約枠(20 KB / 24 KB)を超えても build が通り、**APP 領域を静かに侵食する**。

→ したがって「20 KB 予約」の唯一の実効的な根拠は **APP 側 `Link.ld` の `ORIGIN=0x00005000`** であり、BL 側には無い。統一 BL を作るなら**サイズ上限は自前で linker assert として持ち込む必要がある**(EVT を真似ると抜ける)。`memory_map.csv` の `constrains_size` 列(§3.2)でこの区別を残す。

### 8.9 記述形態は 4 種類あり、**同じ機能が 4 形態で並存**している(軸 J)

| 形態 | 実例 | source of truth か |
|---|---|---|
| **C** | EVT 全 IAP(`iap.c`/`flash.c`/`main.c`)、`rv003usb` `bootloader.c`(15,043 B、一部 `asm volatile`)、`ch32fun` `bootloader.c`(8,267 B、同)、`ch32_user_bootloader_flasher` `v003_flash.c` | ✓ |
| **asm(`.S`/`.asm`)** | `ch32fun` : `minichlink/stubs/b003/*.S`(15 本)、`misc/attic/…/{erase_block,write_block}.asm`(2 本) | ✓ |
| **手書き hex 配列** | `ch32fun` : `minichlink/pgm-b003fun.c` の inline blob 13 本、`pgm-wch-linke.c` の `bootloader_v1..v4`(WCH 純正、`"\x93\x77…"` の文字列連結) | ✓(**元ソースが存在しない**) |
| **生成 header** | `stubs/b003/*.h`(`riscv*-gcc` → `objcopy -O binary` → `xxd -i`。`unsigned char erase_block_bin[]` + `..._len = 52`) | ✗ **生成物**。`.S` が元 |

補足として重要な点:

- **手書き hex には「元ソースが無い」**。逆アセンブルが唯一の読み方。ただし一部は per-instruction コメントが付いている(`run_app_blob` は `lui a1,0x1FFFF000` から全行にコメント)一方、`byte_wise_read_blob` などは 1 行コメントのみ
- **コメントアウトされた代替実装が hex の中に埋まっている**: `word_wise_write_blob` は「readback 無し(有効)/ readback 有り(コメント)」、`run_app_blob` は「old(コメント)/ new(有効)」。→ `stubs.csv` に `active` 相当の扱いが要る(`series_memory.csv` と同じ発想)
- **hex → asm の書き起こしが実在する**: `attic/write_block.asm` の冒頭に *"Based on original code from pgm-b003fun.c by cnlohr"*。つまり **hex(cnlohr)→ asm(monte-monte)** の系譜。`equiv_groups.csv` で束ねる対象

#### 検証セット: V003 の 64 B fast program が **4 形態 5 実装**

| # | 実装 | 形態 | 場所 |
|---|---|---|---|
| 1 | `CH32_IAP_Program` | C | `CH32V003/EVT/EXAM/USART_IAP/CH32V003_IAP/User/flash.c`(`CTLR` を直叩き、`adr &= 0xFFFFFFC0`、16 word ループ) |
| 2 | `flash_write` | C | `ch32_user_bootloader_flasher/v003_flash.c`(`CR_PAGE_PG` → `CR_BUF_RST\|CR_PAGE_PG` → `ADDR` → 16 回 `BUF_LOAD`) |
| 3 | `write64_flash` | 手書き hex(48 B) | `ch32fun/minichlink/pgm-b003fun.c` |
| 4 | `write_block.asm` | asm | `ch32fun/misc/attic/rv003usb_bootloader_stubs_for_minichlink/` |
| 5 | `write_block_bin` | 手書き hex | `ch32fun/minichlink/pgm-b003fun.c`(4 と同一機能) |

→ **同じレジスタ列(`CTLR` の `PAGE_PG` / `BUF_RST` / `BUF_LOAD` / `STATR.BSY` 待ち)を 5 通りに書いたもの**。§4.1 の正規化(`reg_ops.csv`)がこの 5 実装で一致するかを最初に確認する。

### 8.10 未解決の矛盾(`conflict` 候補)

1. `protocols/custom-bootloader.ja.md` は WCH IAP の USB を `1A86:55E0` と記すが、`CH32V20x_IAP/CONFIG/usb_desc.c` の device descriptor は **`0x4348` / `0x55E0`**。→ series ごとに VID が違う可能性。**`usb.csv` で 12 series 全部を確認する**
2. V003/V00X の `USART_IAP` は **BOOT 領域常駐**(`Link.ld` = `ORIGIN 0x00000000 / LENGTH 1920·3328`、jump = `SystemReset_StartMode(Start_Mode_USER)`。実装は `FLASH_Unlock()` → `BOOT_MODEKEYR = KEY1,KEY2` → `FLASH->STATR &= ~(1<<14)` → `FLASH_Lock()`、`Start_Mode_BOOT = 0x4000`)なのに、`.template` の `Address` は **`0x08000000`**。`FLASH/BootAsUser` も `Address=0x08000000` だが ld には `BFLASH (rx) : ORIGIN = 0x1ffff000, LENGTH = 1920` がある → **`.template` の `Address` は BL 配置先の根拠にならない**
3. 出力形式が割れる: V003/V00X の IAP は `Target Path=obj\*.bin`、他 10 project は `*.hex`。BOOT 領域書込は「hex のみ」という §2a の記述と整合するか要確認

---

## 9. 仮説 — **どの言語で書くべきか**(軸 J の設計判断)

「言語差が小さいなら保守性で C、差が大きいなら asm」「asm にすれば機能を足す余地が増えるのでは」という問いに対する、**予備調査の実測に基づく仮説**。本調査(P3.5)で検証する。

### 9.0 前提の訂正 — 既存実装は**すでに役割で言語を分けている**

「V003 の bit-bang USB は C で入っている」は**半分だけ正しい**。実物は **asm + C のハイブリッド**:

| ファイル | 言語 | サイズ | 担当 |
|---|---|---:|---|
| `rv003usb/rv003usb/rv003usb.S` | **asm** | 23,474 B | **サイクル精度の線上信号**(USB low-speed の bit-bang、IRQ ハンドラ)。`nx6p3delay` = 「6n+3 サイクル」のようにサイクル数を数えたマクロを持つ |
| `rv003usb/rv003usb/rv003usb.c` | C | 11,643 B | USB の state machine・descriptor・上位処理 |

→ **「C か asm か」は二者択一ではなく、既に層で分かれている**。仮説もその形で立てる。

### 9.1 仮説 H1 — **層ごとに言語が決まる。全体を一言語にする理由は無い**

| 層 | 仮説する言語 | 実測の根拠 |
|---|---|---|
| **L-a. サイクル精度の信号生成**(bit-bang USB / 1-wire) | **asm 必須** | `rv003usb.S`。C ではサイクル数を保証できない |
| **L-b. BL 本体**(state machine, USB protocol, entry/exit, flash 呼出) | **C** | `rv003usb/bootloader/bootloader.c` 15,043 B、`ch32fun/examples_usb/bootloader/bootloader.c` 8,267 B。**2 実装とも C で成立している** |
| **L-c. host が送り込む stub** | **asm(または hex)** | `.S` 17 本。理由は §9.3 |

### 9.2 仮説 H2 — **L-b を C で書くのは成立するが、V003 では予算ギリギリ**

C で書けている証拠と、C の代償の証拠が両方ある:

| 証拠 | 内容 |
|---|---|
| 成立している | rv003usb BL は C で **1,916 B + secret 4 B = 1,920 B** に収まっている。ch32fun BL は C で X035 の 3,328 B に収まっている |
| **予算ギリギリ** | `boot_usercode()` に `__attribute__((noreturn))` を付けたコメントが **"noreturn attribute saves 2-4 bytes"**。**単位バイトを数えて戦っている** |
| 同上 | `Delay_Ms` を使わず `asmDelay()`(`c.addi` + `bne` の 2 命令)を inline asm で自作。ch32fun 側のコメントは **"This saves space and helps to fit into a tight BOOT area on ch32x035"** |
| 同上 | 既存 doc の記述: rv003usb BL は「**GPIO pin や機能の組合せでサイズが 1,920 B を超える**ため、timeout / button / host 検出は択一に近い」 |

→ **H2: V003(1,920 B)では C は成立するが余裕が無い。V00X / X035(3,328 B)では余裕がある。** 検証は「同一機能セットで C 版と asm 版のサイズを測る」ではなく(§4.1 によりビルド条件依存)、**機能を 1 つ足したときに何バイト増えるかを実測する**方向で行う。

### 9.3 仮説 H3 — **stub は asm。ただし理由はサイズより「制約」**

`.S` 17 本を実測したところ、**全ファイルが x0–x15 のレジスタしか使っていない**(`a6/a7/t3-t6/s2-s11` の出現ゼロ)。一方 `stubs/b003/Makefile` は `-march=rv32imac_zicsr` でビルドしている。

→ **RV32EC(x0–x15 のみ)でも動くことを、toolchain ではなく人手の規律で保証している**。C にすると compiler が x16 以降を使い、V003 で動かなくなる(`-march=rv32ec` を強制すれば回避できるが、その場合 IMAC 側の最適化を捨てる)。

stub に asm を選ぶ理由(サイズ以外):

| 制約 | 内容 |
|---|---|
| **RV32EC 互換** | 上記。1 つの blob を V003(EC)と V20x/X035(IMAC)の両方に送る |
| **位置独立** | scratchpad(`0x20000100` など)に load して実行する。C の PIC は余計な命令を生む |
| **特殊な呼出規約** | `void stub(uint32_t *scratchpad, volatile int32_t *runwordpad)` を受け、**完了印 `scratchpad[0..3] = 0xFFFFFFFF` を自分で書いて `ret`**。prologue/epilogue が邪魔 |
| **絶対サイズが小さい** | `halt_wait_blob` **10 B**、`write64_flash` **48 B**、`erase_block_bin` **52 B**。この規模では C の関数フレームが相対的に重い |

### 9.4 仮説 H4(**本命**)— **「機能を足す余地」を asm で稼ぐのは筋が悪い。stub 側に出すのが正解**

これが scratchpad 方式そのものの設計思想であり、実測がそれを支持する:

| | BL 本体(flash、常駐) | stub(RAM の scratchpad) |
|---|---|---|
| rv003usb / V003 | **1,920 B**(固定・これ以上増やせない) | scratchpad **128 B** @ `0x20000100` |
| ch32fun / X035 | **3,328 B**(固定) | scratchpad **6,144 + 128 B** = **BL 本体の約 1.9 倍** |
| 機能 1 個のコスト | BL を作り直して焼き直す | **数十バイト**(`halt_wait` 10 B / `write64_flash` 48 B / `erase_block` 52 B)。**BL の予算を一切消費しない** |

→ **H4: BL 本体を asm 化して仮に 30 % 縮めても増える余地は数百バイト。一方 stub 方式なら BL 本体を固定したまま host 側で機能を無限に足せる。**「機能を足したい」が動機なら、選ぶべきは asm ではなく **stub を増やせる構造**。

系: 統一 BL の設計目標は「BL 本体を小さく保つ」ではなく **「BL 本体を小さく保ったまま、stub の表現力を上げる」**。具体的には scratchpad サイズ・呼出規約・完了印の 3 つを series 間で統一することが、言語選択より先に効く。

### 9.5 検証方法(P3.5 で行う)

| 仮説 | 判定基準 | 使うデータ |
|---|---|---|
| H1 | `reg_ops.csv` で、L-a に相当する操作が C 実装に存在しないことを確認 | `reg_ops.csv` / `equiv_groups.csv` |
| H2 | rv003usb BL の機能フラグ(timeout / button / host 検出)を 1 つずつ有効にしたときの増分バイト数。**同一 toolchain・同一 flags で測り、条件を `basis` に丸ごと記録**(§4.1) | ビルド実測 |
| H3 | `.S` 17 本 + inline blob 13 本を逆アセンブルし、**使用レジスタ集合が x0–x15 に収まるか**を機械判定 | `stub_disasm/` → `stubs.csv` に `reg_set` / `rv32ec_safe` 列を追加 |
| H4 | §8.9 の検証セット(64 B fast program 5 実装)で、**同一操作を BL 内 C / stub asm のどちらに置いたときのコスト差**を `reg_ops.csv` の行数とバイト数で比較 | `reg_ops.csv` / `stubs.csv` |

> **注**: H2 の測定は §4.1 の「C のサイズは同一ビルド条件でしか比較しない」に従う。**別条件のビルド値や asm 版との直接比較はしない**(compiler・`-march`・`-O` に支配されるため)。

---

## 10. 既知のデータ欠損・リスク

| # | 欠損/リスク | 影響 | 対処 |
|---|---|---|---|
| R1 | **IAP 側の `Ld/Link.ld` が 6 project で欠落**(V103, V20x, V30x, X035, L103, M030)。`.cproject` は `${workspace_loc:/${ProjName}/Ld/Link.ld}` = project ローカルを指すのに実体が無い。**EVT 共通の `EXAM/SRC/Ld/Link.ld` は series 既定(flash 全域)で代替にはならない**。さらに X315 / H417 V5F は `.ld` があっても既定と同値で**サイズを縛っていない**(§8.8) | BL 予約サイズの直接根拠が無いのが 6、実効的な上限が無いのが計 7 | 対になる APP 側 `Link.ld` の `ORIGIN=0x00005000`(H417 は `0x00006000`)から **20 KB / 24 KB** と逆算し、`.template` の ROM サイズで裏を取る。`memory_map.csv` は `confidence=single-source` + `basis=derived-from-app-ld`、`constrains_size` を空にして「欠落」と「無制約」を区別する。series 既定は `series_memory.csv`(§3.2b)に別行で持ち、混同を防ぐ |
| R2 | 未取得の OSS BL(`wch-uf2`, Swindle DFU, PlumBL, tinyboot) | transport 軸の比較が偏る | `projects.csv` に `confidence=todo` で行だけ作り、取得後に埋める |
| R3 | H417 は **V3F/V5F の 2 core で project が 2 本**。他 series と粒度が違う | 「1 series 1 行」が崩れる | `project_id` を core 込みにする(`evt-h417-v3f-iap`)。`projects.csv` に `core` 列を必須化 |
| R4 | `WCHMcuIAP_WinAPP.exe`(host 側)は binary のみ | protocol の host 挙動が未確認 | 本調査の対象外。capture は別途(`protocols/wch-iap.ja.md` の残課題) |
| R5 | EVT は更新される | 数値が陳腐化 | `files.csv` の `sha256` と `doc_version`/`doc_date` を必ず記録 |
| R6 | BLE/ETH IAP は library 同居で構成が大きい | P2 が膨らむ | **副対象**に格下げし、`projects.csv` と `memory_map.csv` だけ埋めて protocol 詳細は後回し |

---

## 11. 参照

- 既存の分析: [../protocols/wch-iap.ja.md](../protocols/wch-iap.ja.md)(世代 A/B/C)、[../protocols/custom-bootloader.ja.md](../protocols/custom-bootloader.ja.md)(BOOT 領域・HID scratchpad protocol・stub 一覧)
- 設計側: [bootloader-design-space.ja.md](bootloader-design-space.ja.md)(entry 方式・BL↔Core↔host 契約)
- chip 素性の join 元: [`ch32-riscv-ug/ch32-device-data`](https://github.com/ch32-riscv-ug/ch32-device-data)(`index/parts.csv`, `evidence/operating_conditions.csv`, `evidence/memory_map.csv`)
- 実装可否の現状: [../coverage.ja.md](../coverage.ja.md)
