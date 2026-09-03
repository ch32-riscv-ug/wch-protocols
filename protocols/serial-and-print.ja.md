# シリアル系まとめ(target 側の I/O と on-chip IAP)

状態: **attested(WCH 公式 EVT ソースから転記)**。自前 capture / 実機で `verified` 化が要る。層は L3/L4(target firmware 側)。

「シリアル経由の方法」は複数あり、**書き込み(programming)と実行時出力(print)**、さらに **transport(USB / UART / debug 線)**が絡む。ここは各シリーズの WCH 公式 **EVT**(`EVT/EXAM/...`)のサンプルから転記した target 側の事実をまとめる。host 側(PC がどう読むか)は [pc-to-link.ja.md](pc-to-link.ja.md) §monitor / [pc-to-device-isp.ja.md](pc-to-device-isp.ja.md) / [riscv-debug-module.ja.md](riscv-debug-module.ja.md) §semihosting。

## 0. 3 つの「シリアル書き込み」protocol は別物

混同しやすいので最初に区別する:

| 名称 | bootloader | protocol | transport | PC ツール | 本書での扱い |
|---|---|---|---|---|---|
| **factory ISP** | chip 内蔵 system bootloader(工場書込済み) | `0xAx` 系 + UID XOR key | USB `4348:55e0` / UART | WCHISPTool | [pc-to-device-isp.ja.md](pc-to-device-isp.ja.md) |
| **WCH IAP(EVT)** | ユーザが焼く app 内 bootloader(EVT `IAP/USB_UART` サンプル) | `0xAA 0x55` sync + `0x80..0x84` | USB / UART 両対応 | WCHMcuIAP_WinAPP.exe(EVT 同梱) | §1(本書) |
| **custom bootloader** | ユーザ自作(DFU/UF2/HID 等) | 各自 | USB/UART | dfu-util 等 | [dap.ja.md](dap.ja.md) 隣の boot 経路(todo) |

**factory ISP と WCH IAP は全くの別プロトコル**。ISP は工場 bootloader(BOOT ピンで入る)、IAP は自分で焼いた bootloader(領域 `0x08000000`〜、APP は後方)。

## 1. WCH IAP demo protocol(EVT `IAP/USB_UART`)

target に IAP bootloader を焼いておくと、以降は WCH-Link 無しで USB か UART から app を更新できる。EVT の `CH32X035_IAP`(bootloader)+ `CH32X035_APP`(通常 app)の 2 プロジェクト構成。**同一 bootloader が USB(USBFS device)と UART を同時に待ち受ける**。

### メモリ配置(X035 例)

- IAP bootloader: `0x08000000`〜(先頭)。
- APP: `FLASH_Base = 0x08005000`(= bootloader 20KB ぶん後方)。app 側もこの番地でリンクする。
- **entry 判定**(bootloader 起動時):
  - `*(0x08005000) == 0xFFFFFFFF`(app 未書込)→ IAP に留まる。
  - `*(CalAddr) == CheckNum`(app が「次は IAP に入れ」フラグを書いた)→ IAP に留まる。`CalAddr = 0x0800F800 - 4`、`CheckNum = 0x5AA55AA5`。
  - どちらでもない → APP へ jump。
- app 側から IAP に戻すには `CalAddr` に `CheckNum` を書いて reset(コマンド `CMD_JUMP_IAP` 相当)。

### コマンド

| cmd | 値 | 意味 |
|---|---|---|
| CMD_IAP_PROM | `0x80` | program(受信データを flash へ。`FLASH_ErasePage_Fast` 後に書く) |
| CMD_IAP_ERASE | `0x81` | erase(4 byte アドレス指定) |
| CMD_IAP_VERIFY | `0x82` | verify(4 byte アドレス + データ) |
| CMD_IAP_END | `0x83` | 終了(`CalAddr` page を消して app へ) |
| CMD_JUMP_IAP | `0x84` | app から IAP へ入る要求 |

応答ステータス: `0x00` success / `0x01` error / `0x02` end。

### UART フレーム(USART2 @ 460800 baud、X035 例)

**request**:

```
0xAA 0x55 | Cmd(1) | Len(1) | [ERASE/VERIFY: addr 4B] | [PROM/VERIFY: data Len B] | checksum_lo checksum_hi | 0x55 0xAA
```

- sync head: `0xAA 0x55`(先頭)/ `0x55 0xAA`(末尾、反転)。
- checksum: **Cmd + Len + addr + data の総和(16bit)を LE**(lo, hi)で付ける。
- ERASE/VERIFY は 4 byte アドレスを伴う。PROM/VERIFY は Len byte のデータを伴う。

**response**(END 以外):

```
0xAA 0x55 | 0x00 | status(0x00 ok / 0x01 err) | 0x55 0xAA
```

- USB 経由も同じコマンド体系(USBFS device の endpoint に載る)。UART は USART2・**460800 baud** が既定(EVT の `USART2_CFG(460800)`)。
- **series 差**: V003/V006 は USB を持たないため IAP サンプルは `V00x_APP`(UART 系)。V103/M030 は `UART_USB_IAP`、V20x/V205/V307/V407/L103/X035/X315/H417 は `USB_UART`(§4 表)。

## 2. USART printf(物理 UART への printf)

EVT 共通の `SRC/Debug/debug.c`。`USART_Printf_Init(baud)` で TX を初期化し、`_write` を retarget して `printf` が UART へ出る。**単なる UART TX** なので、PC 側は任意のシリアル端末か WCH-Link の CDC bridge([pc-to-link.ja.md](pc-to-link.ja.md) §monitor の uart)で読む。

- 既定 baud: **115200**(EVT main の `USART_Printf_Init(115200)`)。
- 既定は `DEBUG = DEBUG_UART1`(USART1)。`DEBUG_UART2`/`3` に切替可。
- **TX ピンはシリアーズで違う**(§4 表)。V003/V006 は remap 前提。

## 3. SDI printf(WCH-Link 経由・物理 UART 不要)

EVT の `SDI_Printf` サンプル。**target が debug data レジスタ(memory-mapped)へ書き、WCH-LinkE がそれを吸って CDC に出す**。物理 UART 配線不要で printf できる WCH 独自機構。これは host 側 [pc-to-link.ja.md](pc-to-link.ja.md) §monitor の **SerialDMDATA(dmdata)/ SDI** の target 側そのもの。

- 前提: **WCH-LinkE のみ**(無印 CH549 Link 不可)。WCH-LinkUtility **1.8 以降**。保護機能とは併用不可(EVT 注記)。
- 有効化 `SDI_Printf_Enable()`: `*DEBUG_DATA0 = 0` を書くだけ(mailbox クリア)。
- **target 側 `_write` の郵便受け方式**(EVT `debug.c`):
  - `*DEBUG_DATA0 != 0` の間は待つ(host が前フレーム未消費)。
  - 1 フレーム最大 **7 byte**: `DATA1 = buf[3..7]`(4B)、`DATA0 = count | buf[0]<<8 | buf[1]<<16 | buf[2]<<24`(**低 byte = 長さ(≤7)、上位 3 byte = 先頭 3 文字**)。
  - host は DATA0 低 byte ≠ 0 を見て count + 7 byte を取り出し、`DATA0 = 0` を書いて ACK。
- **DEBUG_DATA0/1 のアドレスは core 世代で違う**(§4 表)。DMI から見た DMDATA0/DMDATA1(`0x04`/`0x05`)と同じ郵便受けの、target 側 memory-mapped view。

## 4. 対応シリーズ表(EVT 実測)

| series | core | SDI_Printf | SDI DATA0/DATA1 | USART printf 既定 TX | IAP サンプル |
|---|---|---|---|---|---|
| CH32V003 | V2A | ✓ | `0xE00000F4`/`0xE00000F8` | USART1 **PD5** | V00x_APP(UART) |
| CH32V006 | V2C | ✓ | `0xE00000F4`/`0xE00000F8` | USART1 **PD5**(remap) | V00X_APP(UART) |
| CH32V103 | V3A | ✓ | `0xE0000380`/`0xE0000384` | USART1 **PA9** | UART_USB_IAP |
| CH32V20x | V4B/V4C | ✓ | `0xE0000380`/`0xE0000384` | USART1 **PA9** | USB_UART |
| CH32V205 | V4C | ✓ | `0xE0000340`/`0xE0000344` | USART1 **PA9** | USB_UART |
| CH32V307 | V4F | ✓ | `0xE0000380`/`0xE0000384` | USART1 **PA9** | USB_UART |
| CH32V407 | V4F | ✓ | `0xE0000340`/`0xE0000344` | USART1 **PA9** | USB_UART |
| CH32L103 | V4C | ✓ | `0xE0000380`/`0xE0000384` | USART1 **PA9** | USB_UART |
| CH32X035 | V4C | ✓ | `0xE0000380`/`0xE0000384` | USART1 **PB10** | USB_UART |
| CH32X315 | V4C | ✓ | `0xE0000340`/`0xE0000344` | USART1 **PA11** | USB_UART |
| CH32M030 | — | ✓ | `0xE0000340`/`0xE0000344` | USART1 **PC1**(remap) | UART_USB_IAP |
| CH32H417 | V4F | **✗**(SDI_Printf サンプル無し) | — | USART1 | USB_UART |

- **SDI DATA アドレスの 2 系統**: V2 系(V003/V006)は `0xE00000F4`、V4 系でも `0xE0000380`(V20x/V307/X035/L103/V103)と `0xE0000340`(V205/V407/X315/M030)に分かれる。debug module の data レジスタ配置差。
- USART は `DEBUG_UART2`/`3` に変更可・remap 可。上表は EVT 既定(`DEBUG_UART1`)の TX ピン。

## 4b. print 経路の分類(「SDI print」は狭義に WCH 公式方式だけ)

同じ debug 線を使っても protocol が違う。混同しないための一覧:

| 名称 | target→host 経路 | 必要な target code | 公式 SDI 互換 |
|---|---|---|:--:|
| **WCH SDI virtual serial** | SWIO/RVSWD → LinkE firmware → USB COM | EVT の SDI printf 系(§3) | ○ |
| minichlink terminal / semihost | debug register/memory handshake → probe → minichlink `-T` | ch32fun 系 debug printf/semihost | ×(同じ線・別 protocol) |
| SEGGER RTT | target RAM ring buffer → debug memory read → probe | RTT control block/library | × |
| physical UART | target UART TX/RX → probe UART bridge → USB CDC | UART driver | ×(debug 線を使わない) |
| probe log console | probe firmware の log → USB CDC/UART | target 側不要 | ×(target print ではない) |

「SDI print 対応」は狭義には WCH 公式方式(DMDATA 郵便受け)だけを指す。minichlink terminal は同じ線を使う**別 protocol**。

## 5. host 側で読む(既存 doc へ)

- **物理 UART printf** → WCH-Link CDC bridge か任意の端末: [pc-to-link.ja.md](pc-to-link.ja.md) §monitor(uart)。
- **SDI printf** → DMI で DMDATA0/1 を polling(dmdata、任意 probe)/ LinkE の SDI forward(sdi、LinkE 専用): [pc-to-link.ja.md](pc-to-link.ja.md) §monitor。有効化バイト列 `81 0d 02 ee 00` は verified。
- **semihosting**(debug 経由の printf 相当): [riscv-debug-module.ja.md](riscv-debug-module.ja.md) §semihosting。
- **RTT**(RAM リングバッファ): host 側 SerialRTT。

## 6. 要 capture(verified 化)

- WCH IAP: WCHMcuIAP_WinAPP.exe の USB / UART 各 capture で、USB 側フレーム(endpoint・枠)と PROM のアドレス進行を確定。
- SDI printf: dmdata polling の実往復と EVT `_write` の対応をバイトで突き合わせ(host 側は一部 verified、target 郵便受け方式は本書で確定)。
- 各シリーズの USART/IAP ピン・baud は EVT 既定。基板ごとの実配線は個別確認。

## 参照

- 各シリーズ EVT: `EVT/EXAM/SRC/Debug/debug.c`(USART/SDI printf)、`EVT/EXAM/IAP/USB_UART`(IAP)、`EVT/EXAM/SDI_Printf`。
- host 側: [pc-to-link.ja.md](pc-to-link.ja.md) / [pc-to-device-isp.ja.md](pc-to-device-isp.ja.md) / [riscv-debug-module.ja.md](riscv-debug-module.ja.md)。
