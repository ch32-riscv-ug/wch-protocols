# WCH IAP(EVT 同梱の app 内 bootloader)— UART / USB からの書込 protocol

状態: **attested(WCH 公式 EVT ソース 12 シリーズ分から転記。実装可)**。自前 capture は未 → `verified` 化には WCHMcuIAP_WinAPP.exe の実 capture が要る。層は L3(IAP protocol)+ L4(target 側 flash 手順)。

WCH の各シリーズ EVT に入っている `IAP/`(`USB_UART` / `UART_USB_IAP` / `USART_IAP`)は、**ユーザーが自分で焼く bootloader**(IAP)と、それに対応した APP の 2 プロジェクト構成。一度 IAP を焼けば、以後は WCH-Link 無しで UART または USB から APP を更新できる。factory ISP([pc-to-device-isp.ja.md](pc-to-device-isp.ja.md)、`0xAx` 系)とは**全くの別 protocol**。host 側は EVT 同梱の `WCHMcuIAP_WinAPP.exe`(Windows)のみで、他 OS の実装は存在しない — 本書はそれを書けるようにするための仕様。

## 0. 3 つの世代がある(ここを混ぜると実装が壊れる)

同じ `0x80..0x84` のコマンドでも、**IAP の置き場・entry フラグの極性・UART sync・USB ID が世代で違う**。

| 世代 | シリーズ | IAP の置き場 | APP の配置 | CheckNum の意味 | UART sync | USB |
|---|---|---|---|---|---|---|
| **A: BOOT 領域常駐** | V003 / V00X(V006 系) | **BOOT 領域**(V003: 1,920 B @`0x1FFFF000`、V00X: 3,328 B @`0x1FFF0000`)。link は `ORIGIN=0` | **`0x08000000` から user flash 全部** | **「APP が有効」**の印(あれば APP へ) | `AA 55` | 無し(UART のみ) |
| **B: user flash 先頭(V4 系)** | V20x / V205 / V307 / V407 / X035 / X315 / L103 / M030 / H417 | user flash 先頭 `0x08000000`〜(20 KB、H417 は 24 KB) | **`0x08005000`**(H417 `0x08006000`)。APP の ld は `ORIGIN=0x00005000` | **「IAP に留まれ」**の要求(あれば IAP に留まる)| `AA 55` | `1A86:55E0`(vendor bulk) |
| **C: 旧式** | V103 | user flash 先頭 | `0x08005000` | **フラグ無し**(pin のみ) | **`57 AB`** | **`4348:55E0`**(CH372 driver 要) |

**極性が A と B で逆**なのが最大の罠。A では END が CheckNum を**書き**、APP からの JUMP_IAP が CheckNum を**消す**。B では END が CheckNum を**消し**、APP からの JUMP_IAP が CheckNum を**書く**。

## 1. entry(IAP に留まるか APP へ行くか)

IAP 起動直後の判定。`UPGRADE_MODE` で command 方式 / IO 方式を選ぶ(既定 command)。

```
世代 A(V003 / V00X):
    if (*(u32*)FLASH_Base != 0xFFFFFFFF && *(u32*)CalAddr == CheckNum) → APP
    else → IAP に留まる
世代 B(V4 系):
    if (*(u32*)FLASH_Base != 0xFFFFFFFF && *(u32*)CalAddr != CheckNum) → APP
    else → IAP に留まる
世代 C(V103)/ IO 方式:
    if (Pxn が LOW(GND)) → APP      ※ 内部 pull-up。浮かせば IAP、GND で APP
    else → IAP に留まる
```

- `FLASH_Base` の先頭 word が `0xFFFFFFFF`(APP 未書込)なら常に IAP。
- `CheckNum = 0x5AA55AA5`。`CalAddr` は **user flash 最終 page の最後の word**(下表)。APP はこの page を使わないこと。
- IO 方式の pin: **PA0**(V4 系・V103、13 例)、**PC0**(V003 / V00X)、**PB4**(M030)。10 回読んで 7 回以上 LOW なら APP。
- **APP から IAP へ戻る**: APP が `CMD_JUMP_IAP`(`0x84`)を受けたら、世代 A は `CalAddr` の page を消去、世代 B は `CalAddr` の page を読み出し・当該 word だけ CheckNum に差し替えて再書込(`Program_Buf_Modify` → erase → fast program)。その後 APP 側が `NVIC_SystemReset()`。

### APP への制御移行

| 世代 | 方法 |
|---|---|
| A | `FLASH->BOOT_MODEKEYR = 0x45670123, 0xCDEF89AB; FLASH->STATR bit14(MODE) = 0(user); NVIC_SystemReset()` — **software reset で BOOT 領域から user 領域へ切替**([custom-bootloader.ja.md](custom-bootloader.ja.md) §2a) |
| B | USB / USART / AFIO clock を無効化 → `NVIC_EnableIRQ(Software_IRQn); NVIC_SetPendingIRQ(Software_IRQn)`。**`SW_Handler` が APP 先頭へ jump する**。⚠ この `SW_Handler` は MounRiver のプロジェクトテンプレート側 startup(`.cproject` が参照する `startup_ch32x035_3.3v.S` / `startup_ch32v20x_D6.S` 等)にあり、**EVT 同梱の共有 startup では既定の無限ループ**。自作 BL では「周辺 de-init → `FLASH_Base` へ jump(先頭は `jal _start`)」を自前で書く |
| C | 同 B(`Software_IRQn`)。加えて USB を `RB_UC_RESET_SIE` で止め、GPIOA/B・USART3 を DeInit |

## 2. シリーズ別パラメータ(EVT から転記)

| series | 世代 | IAP 領域 | `FLASH_Base`(APP) | `CalAddr` | program 単位 / erase | UART | pin(TX/RX) | baud | USB peripheral | USB VID:PID |
|---|:--:|---|---|---|---|---|---|---|---|---|
| CH32V003 | A | BOOT 1,920 B @`0x1FFFF000` | `0x08000000` | `0x08003FFC` | **64 B** / ERASE で**全消去** | USART1 | PD5/PD6 | 460800(`BRR=0x34`@24 MHz HSI、`0x68`@48 MHz) | — | — |
| CH32V00X(V006) | A | BOOT 3,328 B @`0x1FFF0000` | `0x08000000` | `0x08003FFC`(16K−4 のまま) | 256 B / 全消去 | USART1 | PD5/PD6 | 460800(同上) | — | — |
| CH32V103 | C | user 先頭 | `0x08005000`(APP ld) | **無し** | 128 B / page | **USART3** | PB10/PB11 | **57600** | USBFS(旧 API) | **`4348:55E0`** |
| CH32V20x | B | user 先頭 20 KB | `0x08005000` | `0x08037FFC`(224K−4) | 256 B / page | USART3 | PB10/PB11 | 460800 | USBFS(`User/`)/ USBD(`CONFIG/`) | `1A86:55E0` / USBD 側は `4348:55E0` |
| CH32V205 | B | 〃 | `0x08005000` | `0x0803FFFC`(256K−4) | 256 B / page | USART2 | PA2/PA3 | 460800 | USBFS + USBHS | `1A86:55E0`(HID 版 `1A86:FE17`) |
| CH32V307 | B | 〃 | `0x08005000` | `0x08077FFC`(480K−4) | 256 B / page | USART3 | PB10/PB11 | 460800 | USBFS + USBHS | `1A86:55E0` |
| CH32V407 | B | 〃 | `0x08005000` | `0x080F7FFC`(992K−4) | 256 B / **4 KB erase**(10 KB buffer) | USART2 | PA2/PA3 | 460800 | USBHS | `1A86:55E0`(HID 版 `FE17`) |
| CH32X035 | B | 〃 | `0x08005000` | `0x0800F7FC`(62K−4) | 256 B / page | USART2 | PA2/PA3 | 460800 | USBFS | `1A86:55E0` |
| CH32X315 | B | 〃 | `0x08005000` | `0x08037FFC` | 256 B / 4 KB erase | USART2 | **PA4/PA5** | 460800 | USBHS | `1A86:55E0`(HID 版 `FE17`) |
| CH32L103 | B | 〃 | `0x08005000` | `0x0800FFFC`(64K−4) | 256 B / page | USART2 | PA2/PA3 | 460800 | USBFS | `1A86:55E0`(HID 版 `FE17`) |
| CH32M030 | B | 〃 | `0x08005000` | `0x0800FFFC` | **128 B** / page | USART1 | **PC0/PC1** | 460800 | USBFS | `1A86:55E0` |
| CH32H417 | B | user 先頭 **24 KB** | **`0x08006000`** | `0x08077FFC` | 256 B / 4 KB erase(10 KB buffer) | USART1 | **PB6/PB7** | 460800 | USBFS + USBHS(V3F / V5F 別 core) | `1A86:55E0`(HID 版 `FE17`) |

- USB descriptor は全世代 **vendor class(`FF/80/55`)、EP2 OUT/IN、64 B**、`bcdDevice = 0x0100`。`usb_desc.c` 系(V205/V407/L103/X315/H417)は HID descriptor(`PID 0xFE17`)も併載しているが、IAP 経路は vendor 側。
- **`1A86:55E0` は factory ISP の `4348:55E0` と PID が同じ**。host は VID で区別できるが、[pc-to-device-isp.ja.md](pc-to-device-isp.ja.md) §2 の「LinkE IAP mode も同 ID」問題と同族なので、正体判定は必ず protocol で行う(IAP に `0xA1` Identify を送っても応答しない、で見分けられる)。
- UART は **8N1、フロー制御なし**。世代 A は `BRR` 直書きなので**システムクロックを変えると baud がずれる**(24 MHz HSI 前提)。

## 3. frame

### 3.1 UART(世代 A / B)

```
request:
  AA 55 | Cmd | Len | [addr(4) — ERASE / VERIFY のみ] | [data(Len) — PROM / VERIFY のみ] | sum_lo | sum_hi | 55 AA
response(END 以外):
  AA 55 | 00 | status | 55 AA          status: 00 = OK / 01 = ERROR
```

- `sum` = **Cmd + Len + addr の 4 byte + data の各 byte の 16 bit 和**、little-endian。
- 末尾 sync は先頭の**反転順**(`55 AA`)。1 byte でも合わなければ frame ごと無視(応答なし)。
- **VERIFY は addr(4) と data(Len) の両方**を持つ。ERASE は addr のみ、PROM は data のみ。
- **END には応答が無い**(device は即 APP へ jump する準備に入る)。
- `Len` は最大 64(V003 は `data[64]`)。実用上 host は 56〜60 byte 単位で送る。

### 3.2 UART(世代 C: V103)

```
request:  57 AB | Cmd | Len | Rev(2) | data(Len) | sum(1?)     ※ isp_cmd = {Cmd, Len, Rev[2], data[60]}
response: 00 | status                                          ※ sync 無し
```

frame の末尾処理は EVT の該当 `UART_Rx_Deal` を実機で確認すること(本書は構造体定義と送信側から転記)。

### 3.3 USB(世代 B / C)

**EP2 OUT の 64 B packet に `isp_cmd` をそのまま載せる**。sync も checksum も無い(USB が framing を担う)。

```
PROM:            Cmd | Len | data[≤62]
ERASE / VERIFY:  Cmd | Len | addr[4] | data[≤56]
END / JUMP_IAP:  Cmd | Len
response(EP2 IN、END 以外): 00 | status     (2 byte)
```

世代 C(V103)は `Cmd | Len | Rev[2] | data[60]` の 4 byte ヘッダ。

## 4. コマンドの意味(device 側の実装から)

| cmd | 名 | 世代 A(V003/V00X) | 世代 B(V4 系)| 世代 C(V103) |
|---|---|---|---|---|
| `0x80` | **PROM** | data を内部 buffer に貯め、**page(64/256 B)たまるごとに** fast program → `Program_addr += page` | 同じ。**page ごとに erase してから program**(256 B、V4x7/X315/H417 は 4 KB ごとに erase して 256 B × 16 を program) | 同じ(128 B) |
| `0x81` | **ERASE** | **`FLASH_EraseAllPages()` = user flash 全消去**(addr は無視) | **`FLASH_Unlock_Fast()` のみ**(消去は PROM 内で page ごと。addr は無視) | unlock のみ |
| `0x82` | **VERIFY** | 初回 VERIFY で **PROM の残り buffer を 0xFF pad して書き切る**(flush)。以後 `Verify_addr` から順次比較 | 同じ | 同じ |
| `0x83` | **END** | `CalAddr` の page を消去して **CheckNum を書く**(APP 有効化)→ lock → `End_Flag` | `CalAddr` の page を**消去**(要求クリア)→ lock → `End_Flag` | lock のみ |
| `0x84` | **JUMP_IAP** | IAP 側では no-op(SUCCESS)。**APP 側**が受けたら CalAddr page を消去 → reset | IAP 側では no-op。**APP 側**が CheckNum を書いて reset | (無し) |

**実装上の要点**:

1. **addr フィールドは device 側で使われない。** PROM は `FLASH_Base` から順次、VERIFY も `Verify_addr` を順次進める。host は**先頭から連続して**送ること(飛び地の書込は不可)。
2. **PROM の最後の端数は VERIFY を送るまで flash に書かれない。** image が page の倍数でない場合、host が VERIFY を送らないと末尾が欠ける。WCHMcuIAP は常に VERIFY を全域で行う。
3. 世代 A の ERASE は **全消去**なので、END で書く CheckNum も毎回消える → END が毎回書き直す設計。世代 B は page 単位消去なので CalAddr page は PROM 範囲外なら残る。
4. 応答は各 frame ごとに 1 つ。**END だけ応答なし**。device は `End_Flag` を見て 10 ms 後に APP へ。
5. `Len` が 0 の PROM は無害(何も貯まらない)。

### 典型的な host シーケンス

```
[APP が動いているとき]  JUMP_IAP(0x84) → APP が reset → IAP mode で再列挙(USB)/ 再起動(UART)
ERASE(0x81, addr=0)                      → AA 55 00 00 55 AA
PROM(0x80, data 56B) × N                 → 各 AA 55 00 00 55 AA
VERIFY(0x82, addr, data 56B) × N         → 各 AA 55 00 00 55 AA(不一致なら 00 01)
END(0x83)                                → 応答なし。~10 ms 後に APP 起動
```

WCHMcuIAP(V1.50 以降)は `.hex` / `.bin` 両対応、UART 既定 460800(変更可)。USB は driver 不要(vendor bulk。V103 の `4348:55E0` のみ CH372 driver)。

## 5. APP 側に要る実装(IAP 対応 APP の契約)

- **link origin を `FLASH_Base`(`0x00005000`)にする**(世代 A は `0`)。vector table は APP 先頭。
- **`CalAddr` の page を使わない**(最終 page。データ保存に流用しない)。
- IAP へ戻す経路として、UART / USB の受信で `0x84` を受けたら §1 の手順で flag を操作して `NVIC_SystemReset()`。EVT の APP サンプルはこの受信部(`RecData_Deal` の `CMD_JUMP_IAP` 分岐)だけを持つ。
- 世代 A では APP 側からの reset に `SystemReset_StartMode(Start_Mode_BOOT)`(`STATR` bit14 = 1)を併用して **BOOT 領域から再起動**させる。

## 6. 派生 IAP(同じ EVT にある別経路)— 何ができるか

| 例 | transport | 形式 | 配置 |
|---|---|---|---|
| **HOST_IAP**(V103/V20x/V205/V307/V407/X035/X315/L103/M030/H417) | target が **USB host** になり USB メモリを読む | ルートの **`/APP.BIN`** を読み `0x08006000` へ書き、flag を書いて APP へ(FAT12/16/32) | PC 不要の現場更新 |
| **ETH_IAP**(V20x/V307) | TCP **port 1000**(WCHNET) | file header `{ "WCHNET\0\0"(8B), fileLen(u32) = bin + 512, checkSum(u32) }` + 512 B info + bin。**BIM 40 KB @`0x08000000` / USER 108 KB @`0x0800A000` / BACKUP 108 KB @`0x08025000` / flag @`0x0803FF00`**、`IMAGE_FLAG_UPDATE = 0x57434820`("WCH ")。BACKUP に受けてから USER へ copy(A/B 型) | 遠隔更新。手順書は「WCHNET IAP Upgrade Solution Tutorial」 |
| **BLE OTA**(V20x `OnlyUpdateApp_IAP` / `BackupUpgrade_IAP`) | BLE GATT(OTAprofile) | **同じ `0x80..0x84`**(`0x84` は **CMD_IAP_INFO**)。`IMAGE_IAP` 16 KB @`0x08000000` / `IMAGE_A` 240 KB @`0x08004000` / OTA flag @`0x08077000`。erase は `{cmd, len, addr(2), block_num(2)}`、block 4 KB / page 256、`IAP_LEN=247` | Backup 型は A/B 2 image |

→ 「WCH の IAP コマンド体系(`0x80..0x84`)は transport を替えて再利用されている」。自作 BL でこの体系を踏襲すれば、WCHMcuIAP 互換の host が流用できる可能性がある(要 capture)。

## 7. verified 化に要る capture / 実機確認

1. **WCHMcuIAP_WinAPP.exe の UART capture**(460800)で §3.1 の frame と §4 の順序(ERASE→PROM→VERIFY→END、VERIFY の addr 値、END 後の待ち)を照合。
2. 同 USB capture(`1A86:55E0`、EP2)で §3.3 と 64 B packet の埋め方(`Len` の実値)を確認。
3. 世代 C(V103)の UART 末尾(checksum の有無)を実機で確認。
4. 世代 B の `SW_Handler` の実体(MRS テンプレート startup)を入手して jump 手順を確定。
5. 世代 A で ERASE 後に END が書く CheckNum と、APP 側 JUMP_IAP の page 消去が **同じ page** を指すこと(`CalAddr & 0xFFFFFFC0` = V003 の 64 B 境界)を実機確認。

## 参照

- 一次資料: 各 EVT `EXAM/IAP/*/…_IAP/User/{iap.h,iap.c,main.c}`、`…_APP/User/iap.c`、`*_IAP使用说明.pdf`(V1.1)、`WCHMcuIAP_WinAPP.exe`。
- factory ISP との区別・`55E0` の衝突: [pc-to-device-isp.ja.md](pc-to-device-isp.ja.md)
- BOOT 領域(世代 A の置き場)・boot mode 切替レジスタ・BootAsUser: [custom-bootloader.ja.md](custom-bootloader.ja.md) §2a
- USART printf / SDI print(print 側): [serial-and-print.ja.md](serial-and-print.ja.md)
- 自作 BL の設計空間: [../references/bootloader-design-space.ja.md](../references/bootloader-design-space.ja.md)
