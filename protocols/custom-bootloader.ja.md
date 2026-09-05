# custom bootloader(経路 ③: DFU / UF2 / UART / HID / OTA)

状態: **reference / attested**(実装事例は多数。§2a の BOOT 領域は RM から転記、§2b の HID bootloader protocol は rv003usb / ch32fun / minichlink のソースから byte 単位で転記 → **実装可**。自前 capture 未)。層は L3(各 bootloader protocol)。経路 ③ — user が自分で焼く bootloader。factory ISP([pc-to-device-isp.ja.md](pc-to-device-isp.ja.md))や WCH IAP([wch-iap.ja.md](wch-iap.ja.md))とは別で、DFU/UF2/UART/HID/network など transport が多彩。

## 1. bootloader の種類(格納場所で分ける)

| 種類 | 格納場所 | 導入者 | 主目的 | 消去事故耐性 |
|---|---|---|---|---|
| factory ISP | system/BOOT 領域 | 工場出荷時 | USB/UART から code flash 書込 | code flash 消去後も残るが entry 条件依存 |
| custom system bootloader | 書換可 system/BOOT 領域 | 最初に debug probe 等で導入 | factory ISP 置換、user flash 全量確保 | system 領域を壊すと probe 救出要 |
| custom user-flash bootloader | code flash 先頭等 | debug probe/factory ISP で導入 | DFU/UF2/UART/OTA | mass erase で消える。APP の link offset 要 |
| application IAP | application 内 | app と同時 | 稼働中に自己更新 | app 破損時は入れない。recovery stub 併用推奨 |

## 2. 実装事例(OSS / WCH 公式)

| 実装 | target / transport | 配置・image 条件 | host | 言語 | 主な機能・注意 |
|---|---|---|---|---|---|
| WCH 公式 EVT `USART_IAP` | V003/V00X、UART | **BOOT 領域常駐**(1,920 / 3,328 B)、APP は `0x08000000` 全域 | WCHMcuIAP | C | 全消去 → program → verify → CheckNum 書込。→ [wch-iap.ja.md](wch-iap.ja.md) 世代 A |
| WCH 公式 EVT `UART_USB_IAP` / `USB_UART` | V103 / V20x / V205 / V307 / V407 / X035 / X315 / L103 / M030 / H417 | user flash 先頭 20 KB(H417 24 KB)、APP `0x08005000`(H417 `0x08006000`) | WCHMcuIAP | C | UART 460800 + USB `1A86:55E0`(V103 は `4348:55E0`・57600)。→ [wch-iap.ja.md](wch-iap.ja.md) 世代 B / C |
| WCH 公式 EVT `ETH_IAP` | V20x/V307 Ethernet | BIM 40 KB / USER 108 KB @`0x0800A000` / BACKUP 108 KB @`0x08025000`(A/B) | TCP 1000、`WCHNET` header | C | network IAP。→ [wch-iap.ja.md](wch-iap.ja.md) §6。製品には認証・rollback 追加要 |
| WCH 公式 EVT `HOST_IAP` | V103〜H417 の USB host | target が USB メモリの **`/APP.BIN`** を読み `0x08006000` へ | PC 不要 | C | 現場更新。→ [wch-iap.ja.md](wch-iap.ja.md) §6。媒体 image の真正性確認は別途 |
| [rv003usb bootloader](https://github.com/cnlohr/rv003usb/tree/master/bootloader) | V003、GPIO **software USB** low-speed **HID** | **1,916 B + 4 B(secret)= 1,920 B の BOOT 領域**。scratchpad は RAM `0x20000100` | minichlink(`-c 0x1209b003`) | C | **driver 不要**。`1209:B003`(UIAPduino fork は `B803`)。timeout(75 ms 単位。**upstream 既定 67 ≈ 5 s**、UIAPduino fork は 7 ≈ 0.5 s)/ button / host 検出。**protocol は §2b**。自己更新はしない(→ `ch32_user_bootloader_flasher`) |
| `rv003usb/bootloader_v006` | V006 系、software USB | V00X 向け別実装 | minichlink | C | 旧 V003 版と別。README 整備途上 |
| [ch32fun `examples_usb/bootloader`](https://github.com/cnlohr/ch32fun/tree/master/examples_usb/bootloader) | X035/CH5xx(V20x/V30x は stub 追加で可、V103 は BOOT 2 分割のため不可)、**hardware USB(USBFS)** | **BOOT 領域 3,328 B**(X035)。scratchpad **6,144 + 128 B**(RAM) | minichlink | C | rv003usb BL の移植。`1209:B003`、feature report `0xA8`(7 B)/`0xAA`(127 B)/`0xAB..0xB0`(〜6,272 B)。**同じ scratchpad protocol(§2b)**。非破壊書込、任意番地 read、`funRebootToBootloader` |
| (host)[rv003usb-webflasher](https://github.com/SadaleNet/rv003usb-webflasher) / [WebLink_USB](https://github.com/monte-monte/WebLink_USB) | rv003usb BL を持つ V003(/V006)| — | **ブラウザ(WebHID、Chromium 系)** | JS | driver 不要・インストール不要で V003 を書ける。**§2b の JS 実装**。debug 不可、BL が焼かれている前提 |
| [tinyboot](https://github.com/OpenServoCore/tinyboot) | V003/V00X/V103、UART・1 線 UART・**RS-485** | system/user flash mode | Rust `tinyboot` CLI | Rust | **CRC16**・info・retry・trial boot/confirm。transport 拡張可 |
| [wch-uf2](https://github.com/ArcaneNibble/wch-uf2) | CH32V2xx の USBD | 先頭 **4 KiB** 予約、APP `0x08001000` | OS の MSC + **UF2 copy** | C | double reset、flash/RAM download。V3xx 非対応、hardcoded 値の family 化要 |
| [Swindle CH32V3x DFU BL](https://github.com/mean00/swindle_bootloader_ch32v3x) | CH32V3x hardware USB | 先頭 **16 KiB** 予約(実 ~6 KiB)、APP `0x4000` | `dfu-util` | C | RAM marker/button/invalid CRC で DFU。12-byte header + **CRC32** |
| [PlumBL](https://github.com/HaiMianBBao/PlumBL) | CH32V30x ほか、CherryUSB **DFU/U2F** | user flash 予約 | `dfu-util`/U2F tool | C | multi-platform port 例 |

## 2a. BOOT 領域(system memory)— 自作 BL を「user flash を 1 byte も食わずに」置ける場所

状態: **attested(各 RM の Flash Memory Organization + EVT `FLASH/BootAsUser` から転記)**。

factory ISP が入っている information block は、**多くの系列でユーザーが上書きできる**(WCH 公式 EVT `BootAsUser` が「BOOT 区域を user 区域として使う」手順を配布している)。ここに自作 BL を置くと APP は user flash 全部を使える(WCH IAP 世代 A、rv003usb / ch32fun BL がこの構成)。

| family | BOOT 領域(番地) | size | user flash の fast program 単位 | 書込保護単位 | 備考 |
|---|---|---:|---:|---:|---|
| **CH32V003 / CH641** | `0x1FFFF000`–`0x1FFFF77F` | **1,920 B**(2K−128) | 64 B | 1 KB | BootAsUser 対象。**BOOT↔user の関数 jump に ~1 µs の遅延**(頻繁に呼ぶ関数を置かない) |
| **CH32V00X**(V002/4/5/6/7) | `0x1FFF0000`–`0x1FFF0CFF` | **3,328 B**(3K+256) | 256 B | 2 KB | BootAsUser 対象。jump 遅延なし |
| **CH32X035 / X033** | `0x1FFF0000`–`0x1FFF0CFF` | **3,328 B** | 256 B | 2 KB | BootAsUser 対象(WCH-LinkUtility **V2.40+**)。ch32fun BL が収まる |
| CH32L103 | `0x1FFF0000`–`0x1FFF0CFF` | 3,328 B | 256 B | 2 KB | |
| CH32V103 | `0x1FFFF000`–`0x1FFFF7FF` **+** `0x1FFFF900`–`0x1FFFFFFF` | 2 KB + 1,792 B(**2 分割**、間に option bytes と vendor word) | 128 B | 4 KB | 分割のため ch32fun BL は「非対応見込み」 |
| CH32V20x / V30x / V31x | `0x1FFF8000`–`0x1FFFEFFF` | **28 KB** | 256 B(標準 erase 4 KB、fast 256 B) | 4 KB | 大きい。128 KB 超えは FLASH clock 制約あり(RM) |
| CH32V407 / X315 | `0x1FFF8000`–`0x1FFFEFFF` | 28 KB | 256 B | — | IAP sample は 4 KB erase |
| CH32H417 | `0x1FFF0000`–`0x1FFFDFFF`(56 KB)or `–0x1FFF6FFF`(28 KB) | 56 / 28 KB(構成 2 種) | 256 B | — | |
| CH32M030 | (information store 512 B のみ) | **無し** | 128 B | — | → factory ISP を持たない([pc-to-device-isp.ja.md](pc-to-device-isp.ja.md) §1) |

### BOOT 領域に置く方法(EVT `BootAsUser` の手順)

1. linker に `BFLASH` 領域を足す(`ORIGIN = 0x1FFFF000, LENGTH = 1920` / `0x1FFF0000, 3328`)。**IAP を丸ごと置くなら `FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1920` と書く**(WCH IAP 世代 A の Link.ld。BOOT 領域は実行時 `0x00000000` にエイリアスされる)。
2. BOOT 側に置く関数へ `__attribute__((section(".Bcode")))`。startup に `.bxx` セクション。
3. **書込は debug probe(WCH-LinkUtility ≥ V1.80、X035 は ≥ V2.40)で、download address を `0x1FFFF000` / `0x1FFF0000` に指定、hex のみ**。factory ISP や自分自身からは書けない(ISP won't work — ch32fun README も同旨)。
4. EVT の注記「BOOT FLASH は user code から消去できない」は **BootAsUser(BOOT 領域に置いたコードがデータ領域として使えない)の文脈**。**`BOOT_MODEKEYR` を解錠すれば user code から BOOT 領域を erase / program できる**(V003 で実証: [`ch32_user_bootloader_flasher`](https://github.com/monte-monte/ch32_user_bootloader_flasher) は `KEYR` + `BOOT_MODEKEYR` + `MODEKEYR` の 3 組を解錠したあと、通常の fast `PAGE_ER` / `PAGE_PG`(64 B)を `0x1FFFF000` に対して実行する)。→ **BL の自己更新は「app 側 updater」の形で可能**。同ツールは書く前に **user flash 末尾 `0x08003800` に 1,920 B の backup** を取り、失敗時に復元する。V00X / X035 / X315 も同じレジスタを持つが未検証。
5. **上書きすると factory ISP は失われる**。戻すには `0x1FFFF000` に元の Boot code を焼き直す(WCH の配布物か GitHub 上の dump)。

### boot mode の切替レジスタ(BOOT 領域 ↔ user 領域)

`FLASH_STATR`(`0x4002200C`)の bit14 **`BOOT_MODE`**(1 = 次の software reset で BOOT 領域、0 = user 領域)。書くには **`FLASH_BOOT_MODEKEYR`(`0x40022028`)に `0x45670123`, `0xCDEF89AB`** を書いて解錠する(bit15 `BOOT_LOCK` が 1 だと不可)。X035/V00X には bit13 **`BOOT_STATUS`**(いま BOOT 領域から走っているか、RO)と bit12 `BOOT_AVA` がある。

```c
// user 領域へ(rv003usb / ch32fun BL の boot_usercode、EVT の SystemReset_StartMode(Start_Mode_USER))
FLASH->BOOT_MODEKEYR = 0x45670123; FLASH->BOOT_MODEKEYR = 0xCDEF89AB;
FLASH->STATR = 0;                 // bit14 = 0
PFIC->SCTLR = 1u << 31;           // system reset(= NVIC_SystemReset)

// app から BL へ(ch32fun funRebootToBootloader / EVT の Start_Mode_BOOT)
FLASH->BOOT_MODEKEYR = 0x45670123; FLASH->BOOT_MODEKEYR = 0xCDEF89AB;
FLASH->STATR = 0x4000;            // bit14 = 1
RCC->RSTSCKR |= 0x1000000;        // reset flag クリア(RMVF)
PFIC->CFGR = 0xBEEF0080;          // system reset(key 付き)
```

- **起動元の選択方式は系列で 2 系統**: V103 / V2x / V3x / L103 / V407 は **BOOT0/BOOT1 ピン**、**V003 / V00X / X035 / X315 は上記レジスタによるソフトウェア選択のみ**(BOOT ピン無し)。→ [pc-to-device-isp.ja.md](pc-to-device-isp.ja.md) §1 の表。後者では **app が協力しないと factory ISP にも自作 BL にも入れない**ので、[../references/bootloader-design-space.ja.md](../references/bootloader-design-space.ja.md) §1 の entry 設計(窓・RAM magic・reset 原因)がそのまま必要になる。
- **BL 側の「留まるか」判定に reset 原因を使う**: rv003usb BL は `RCC->RSTSCKR & (1<<26)`(power-on reset)でのみ BL に留まり、それ以外は即 user code。`SOFT_REBOOT_TO_BOOTLOADER` を有効にすると `RSTSCKR == 0x10000000`(software reset のみ)を「app からの要求」と解釈する。
- minichlink は **`FLASH_STATR` bit13 で「いま BOOT 領域で走っているか」**を判定する(`B003DetermineIfInBoot`)。
- V003 で BOOT 領域から起動させるには option byte 側の設定も要る(rv003usb `configurebootloader`: `OBKEYR`/`KEYR`/`MODEKEYR` 解錠 → `OPTER` → 再書込)。**出荷状態の多くは BOOT 起動になっている**が、要確認。

## 2b. HID scratchpad bootloader の protocol(rv003usb / ch32fun、host = minichlink)

状態: **attested(BL 側 = rv003usb `bootloader.c` / ch32fun `examples_usb/bootloader/bootloader.c`、host 側 = minichlink `pgm-b003fun.c`。同一作者の対実装なので独立実装ではない)**。

BL は **HID の feature report だけ**で動く(class request `SET_REPORT 0x21/0x09` = 書込、`GET_REPORT 0xA1/0x01` = 読出)。コマンドは無く、**host が RISC-V の機械語(stub)を scratchpad に送り、BL がそれを実行する**。BL 本体は小さいまま、機能は host 側の stub で増える。

### USB

| 項目 | rv003usb(V003、software USB) | ch32fun(HW USB) |
|---|---|---|
| VID:PID | **`1209:B003`**(UIAPduino fork は `B803`、V006 版 `B806`) | `1209:B003` |
| app mode の PID(rv003usb demo)| `1209:D003` | — |
| EP0 | 8 B(low-speed) | 64 B |
| feature report ID | **`0xAA`**: 127 B(+ID = 128 B) | **`0xA8`**: 7 B / **`0xAA`**: 127 / **`0xAB`**: 1,024+127 / **`0xAC`**: 2,048+127 / **`0xAD`**: 3,072+127 / **`0xAE`**: 4,095(Windows/macOS の上限)/ **`0xAF`**: 5,120+127 / **`0xB0`**: 6,144+127 B(ID = `0xAA + ⌈size/1024⌉`。host は `0xAD`/`0xB0` を GET して実サイズを探る) |
| scratchpad | RAM `0x20000100`、**128 B**。`runwordpad` = `0x20000180` | RAM `.scratchpad`、**6,144 + 128 B** |

### host 実装(3 つ)

| host | 言語 / API | 対象 PID | 備考 |
|---|---|---|---|
| minichlink `pgm-b003fun.c`(cnlohr) | C / hidapi | `1209:B003`(`-c` で変更) | 参照実装。stub 群を持つ |
| [rv003usb-webflasher](https://github.com/SadaleNet/rv003usb-webflasher)(SadaleNet) | **JS / WebHID** | `1209:B803` | MIT(`minichlink-minimal/` は GPL)。**sector ごとに read-back 照合し、差分だけ書く**。V006 は「たぶん」 |
| [WebLink_USB](https://github.com/monte-monte/WebLink_USB)(monte-monte) | **JS / WebHID** | `1209:B803`(BL)/ `1209:D803`(app) | flasher + terminal。`ch32_user_bootloader_flasher` の UI にも使う |

**同じ protocol を C と JS の別作者が実装して動いている**ので、protocol の形は「独立実装 2 つ以上の一致」を満たす(byte の verified 化には capture が要る)。**ブラウザ側は 2 実装とも UIAPduino fork の PID(`B803`)を既定にしている** — upstream `B003` の BL を使うなら filter を変える。

### scratchpad の構造と実行

```
report:  [ID] [AA 00 00 00] [stub code …] [args …] [data …] … [CD AB 34 12]
                ^scratchpad[0..3]              ^末尾 4 B = 0x1234ABCD で「実行せよ」
```

1. host が `SET_REPORT` で scratchpad を埋める。**最後の 4 B が `0x1234ABCD`** なら BL は `runwordpad` を正にして実行を予約(rv003usb は次の IN packet を待ってから、ch32fun は `runwordpad = 100`)。
2. BL のメインループが `runwordpad` を減らし、0 で **`scratchpad + 4` を関数として呼ぶ**: `void stub(uint32_t *scratchpad, volatile int32_t *runwordpad)`。
3. stub は結果を scratchpad に書き、**完了印として `scratchpad[0..3] = 0xFFFFFFFF`** を書いて `ret`。
4. host は `GET_REPORT` を繰り返し、**report byte[1] == 0xFF** で完了と判定(byte[0] は report ID)。
5. `runwordpad` の意味: **負 = 起動までのカウントダウン**(timeout)、**0 = 何もしない**(halt)、**正 = N 周後に stub 実行**。

### minichlink の stub 一覧(何ができるか)

| stub | 引数(scratchpad 内 offset) | 用途 |
|---|---|---|
| `halt_wait_blob` | — | `runwordpad = 0`(timeout 停止)。**接続直後に必ず送る** |
| `byte/half/word_wise_read_blob` | `@52` addr, `@56` len → 結果 `@60..` | 任意番地 read(整列で使い分け) |
| `byte/half/word_wise_write_blob` | `@52` addr, `@56` len, `@60..` data | RAM / 周辺レジスタ write(flash は下記) |
| `write64_flash` | `@52` addr, `@56` `0x4002200C`(STATR), `@60..` 64 B | V003 の 64 B fast program(host が事前に `CTLR = FTPG`, `FTPG\|BUFRST` を write) |
| `write_block_bin` / `_v20x_v30x` | `@76` addr, `@80` STATR, `@84` `sector_size \| len<<16`, 以後 data | page ループの program(割込み禁止版)。V20x/V30x 用は BUSY/WRBUSY 待ちが違う |
| `erase_block_bin` | addr, STATR, `sector_size \| len<<16` | page erase ループ |
| `run_app_blob`(V003) | — | **BOOT 領域末尾の secret(`0x1FFFF77C`)から `boot_usercode` の位置を読んで jump**。無ければ §2a の切替シーケンスを直接実行 |
| `run_app_new_blob`(HW USB) | — | `lw a3,-4(a0); jr a3` — scratchpad 直前に置かれた `boot_usercode_address` へ jump |
| ch5xx 系 stubs | — | CH5xx の flash controller(`0x40001800`)用 |

- **chip 判別**は stub の read で `0x1FFFF7C4`(V2/V3/X03x/L103 の chip id)/ `0x1FFFF704`(V00X 系)/ `0x1FFFF884`(V103)/ `0x40001041`(CH5xx)を読む。読出保護は `FLASH_OBR`(`0x4002201C` bit1)と `FLASH_WPR`(`0x40022020`)で判定。
- **app から BL へ戻す hook**(rv003usb demo、PID `D003`): app の HID に feature report **`FD 12 34 AA BB CC DD`**(ID `0xFD`)を送ると app が §2a のシーケンスで BL へ再起動する。minichlink は `B003` が無ければ `D003` を探してこれを送る。
- rv003usb BL の flash 配置: `FLASH LENGTH = 1916`、`SECRET @0x77C LENGTH 4`(`boot_usercode` の offset を XOR で保存)。**GPIO pin や機能の組合せでサイズが 1,920 B を超える**ため、timeout / button / host 検出は択一に近い。

## 3. transport 比較

| transport | driver/host 依存 | 速度目安 | 長所 | 短所 |
|---|---|---|---|---|
| software USB HID | OS 標準 HID | USB low-speed | V003 でも USB 端子だけで更新 | pin/clock/割込制約、signal 品質 |
| hardware USB vendor/HID | HID なら driver 不要 | FS/HS | WCH sample/minichlink 互換を作りやすい | 独自 host protocol 保守 |
| USB DFU | `dfu-util` 等 | FS/HS | 標準 protocol、CLI 既存 | Arduino 側で image/offset 管理 |
| USB UF2 MSC | OS の file copy | FS | UX 最良、追加 driver 不要 | MSC emulation 複雑、copy 完了判定・大 image 再起動注意 |
| UART | USB-UART/serial | 115.2 kbps〜 | USB なしでも移植しやすい | port/baud/reset 配線が board 依存 |
| 1 線 UART / RS-485 | transceiver/half-duplex | 配線依存 | 長距離・multi-drop・既設 bus | node address、衝突回避、DE timing |
| Ethernet/Wi-Fi/BLE | network stack | 可変 | 遠隔更新 | 認証・暗号・再送・rollback 必須 |
| USB host/SD/SPI flash | 外部媒体 | 可変 | PC なし更新 | 媒体破損・image 選択・電源断対策 |

## 4. bootloader 実装に共通の仕様項目

新規に設計/解読するとき繰り返し現れる:

- **image header**: magic / format version / target / load address / 長さ / version / hash。
- **CRC**(破損検出、例 CRC16/CRC32)と **署名**(作成元確認)は役割が別。
- **更新失敗対策**: bootloader 常駐 / A/B slot / download slot からの copy / trial boot / watchdog / app の `confirm()`。
- **boot entry**: button / double reset / RAM magic / application command / 無効 image 検出 / BOOT pin / option byte。
- **user-flash bootloader** は bootloader 領域 + metadata + APP offset のぶん app 容量が減る。
- **app への jump 時**: vector table/interrupt・clock・USB pull-up・peripheral・stack・`.data/.bss` の状態。
- **識別情報**: USB=VID/PID・serial・DFU alt setting・UF2 family ID / UART=port・node ID。

## 5. 未解読 / 要調査

- WCH IAP(UART / USB)は [wch-iap.ja.md](wch-iap.ja.md) で **12 シリーズ分 byte 確定**(EVT 転記)。残るは WCHMcuIAP の実 capture。
- §2b の HID scratchpad protocol を **USB capture で verified 化**(feature report の実バイト、`0x1234ABCD` の位置、完了印 `0xFF`)。
- wch-uf2 / Swindle DFU / PlumBL / tinyboot の header・CRC・entry の実バイト(手元にソース無し。転記待ち)。
- V003 の option byte で BOOT 起動を選ぶ手順(rv003usb `configurebootloader` の `OPTER` の意味)を RM と突き合わせる。
- V20x/V30x の 28 KB BOOT 領域に自作 BL を置く実例(ch32fun は「stub 追加で可」と注記、未検証)。

## 参照

- WCH IAP(UART 実測済み): [serial-and-print.ja.md](serial-and-print.ja.md) §1
- factory ISP との区別: [pc-to-device-isp.ja.md](pc-to-device-isp.ja.md)
- host ツール・probe 一覧: [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md)
- **自分で BL を設計するときの設計空間**(entry 方式・能力・chip 別制約・BL↔Core↔host 契約・内蔵ライタ MCU): [../references/bootloader-design-space.ja.md](../references/bootloader-design-space.ja.md)
