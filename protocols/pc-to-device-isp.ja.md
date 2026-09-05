# PC ↔ target(factory ISP / シリアル書き込み)

状態: **attested(3 実装一致、USB 経路は byte 単位で転記済み・実装可)**。自前 capture 未 → `verified` 化が要る。ch32rv の isp crate は骨組みのみ。層は L3(ISP protocol)。経路 ② — WCH-Link を使わず、チップに元から入っている**書き込み専用 bootloader(system bootloader / factory ISP)**を PC から直接叩く。RISC-V debug(DMI)は無く、flash 焼き・option/config 設定・verify だけ。

同じ ISP protocol が **2 つの物理経路**で使える:

- **USB**: チップの USB(`4348:55e0`)を PC に直結。
- **UART(シリアル)**: チップの特定 USART に PC の TTL シリアルをつなぐ。**WCH の EVT / AN でこの手順が提示されている**。

## 1. bootloader に入る

app を bypass して system bootloader を起動させる:

- **BOOT ピン操作 + reset/電源投入**: BOOT0(と一部 chip の BOOT1)を規定レベルにして reset すると、user flash でなく system bootloader から起動する。ピンと論理は chip 系列ごとに違う(EVT / データシートの "System Memory / Boot configuration" 参照)。
- bootloader は **USB と UART の両方で応答を待つ**(chip による)。どちらか使う方につなぐ。

### factory bootloader の実体(系列差・entry の強さ)

- **CH32V003**: factory bootloader は `0x1FFFF000` からの **1,920 byte** system 領域にあり、UART **115200 bps** で動く。**外部 BOOT ピンだけでなく application が `START_MODE` を設定して reset する必要がある** → app が完全に壊れると入口として弱い(復旧は debug probe 経由が確実)。詳細は [CH32V003 factory bootloader missing manual](https://github.com/basilhussain/ch32v003-bootloader-docs)。V00X では system 領域サイズや一部 package の UART remap が異なる。
- **USB 内蔵系列**: factory ISP は wchisp / WCHISPTool から使えるが、「USB peripheral がある」ことと「その SKU の factory bootloader が USB 経路を公開する」ことは**別**。正確な対応は **exact SKU × BOOT pin/option byte × bootloader version** で管理する。
- **CH32M030**: 工場 ISP を持たず、外部 debug または導入済み custom IAP を使う。

## 2. transport ごとの framing

ISP のコマンド本体(§3)は共通で、**外側の枠だけ transport で違う**:

| transport | request | response | 備考 |
|---|---|---|---|
| **USB**(`4348:55E0` / `1A86:55E0`) | bulk EP `0x02` OUT に `cmd(1) len(2 LE) payload…` を素で載せる | bulk EP `0x82` IN に `cmd(1) 00 len(2 LE) payload…` | sync 無し。**string descriptor / serial を持たない**(識別は topology / BTVER)。timeout は minichlink 5 s |
| UART | `0x57 0xAB` sync + `cmd(1) len(2 LE) payload…` + `checksum(1)` | 同形 | checksum は payload の和(byte)。先頭で auto-baud(wchisp。**本書は USB のみ byte 確認**) |

- host は **VID `0x4348` または `0x1A86`、PID `0x55E0`** を探す(minichlink)。**WCH-Link の IAP mode と WCH IAP(EVT の app 内 BL)も同じ `55E0`** を名乗る([pc-to-link.ja.md](pc-to-link.ja.md) §10b / [wch-iap.ja.md](wch-iap.ja.md) §2)。BTVER / chip 種別、および「`0xA1` に答えるか」で判別する。

## 3. コマンド一覧(WCH bootloader、`0xAx` 系)

状態: **attested(3 実装一致: minichlink `pgm-wch-isp.c`(MIT)/ wchisp / wagiminator `chprog.py`)**。以下の byte は minichlink の実装をそのまま転記したもので、**USB 経路**。`P[n]` は response の payload(ヘッダ 4 byte の後)の n 番目。

| cmd | 名称 | request(cmd len payload) | response | 備考 |
|---|---|---|---|---|
| `0xA1` | **Identify** | `A1 12 00` + `chip_id device_type` + `"MCU ISP & WCH.CN"`(16 B) | 6 B: `A1 00 02 00 chip_id device_type` | 送る `chip_id/device_type` は任意(minichlink は `52 11` 固定)。**返ってきた `P[0]=chip_id, P[1]=device_type`** で系列確定。`chip_type = chip_id \| device_type<<8` |
| `0xA7` | **ReadConfig** | `A7 02 00 1F 00`(mask `0x001F` = 全部) | 30 B: `A7 00 1A 00` + `P[0..1]` + **`P[2..13]` config 12 B** + **`P[14..17]` BTVER** + **`P[18..25]` UID 8 B** | BTVER は `P[15].P[16]P[17]`(例 `02 03 01` → 2.31)。UID が XOR key の材料 |
| `0xA3` | **IspKey** | `A3 1E 00` + **30 B の seed** | `A3 00 02 00 chk 00` | device は `seed + UID` から 8 B の XOR key を作る。**`P[0]=chk` は key 8 B の和(byte)**。host が同じ計算で key を持てば一致を検証できる(§4) |
| `0xA4` | **Erase** | `A4 04 00 n_lo n_hi 00 00` | `A4 00 02 00 status 00` | **`n` = 消去 sector 数(1 KB/sector)= ceil(len/1024)、最小 8**。`status=0` で成功。全消去は `n = flash_size/1024` |
| `0xA5` | **Program** | `A5 3D 00` + `addr(4 LE)` + `pad(1)` + **`data 56 B ^ key[k % 8]`** | `A5 00 02 00 status 00` | `addr` は **flash 先頭を 0 とするオフセット**(`0x08000000` → `0`)。`pad` = 残り長の下位 byte。image は **256 B 境界まで `0xFF` で埋めて**送る。`status ∈ {0x00, 0xFE, 0xF5}` を成功扱い |
| `0xA6` | Verify | Program と同形(データを XOR して送る) | 同 | minichlink は未実装(wchisp は実装) |
| `0xA8` | **WriteConfig** | `A8 0E 00 07 00` + **config 12 B** | `A8 00 02 00 status 00` | mask `0x0007`。ReadConfig で得た 12 B を書き換えて送る |
| `0xA2` | **IspEnd** | `A2 01 00 01` | (応答を待たず reset) | `01` = reset して user code へ |
| `0xA9` / `0xAA` / `0xAB` | DataErase / DataProgram / DataRead | — | — | data flash(EEPROM 相当。CH55x 系)|
| `0xC3` / `0xC4` | WriteOTP / ReadOTP | — | — | |
| `0xC5` | SetBaud | — | — | **UART 経路の baud 変更** |

典型フロー: `Identify → ReadConfig(UID)→ IspKey → Erase → Program × N → (Verify × N)→ IspEnd`。

### 3.1 ReadConfig の config 12 B(CH32V 系)

`P[2..13]` = ReadConfig の rbuff[6..17]。minichlink の表示と STM32 互換 option byte の並びから:

| P | 内容(CH32V 系) | 備考 |
|---|---|---|
| `P[2]` `P[3]` | **RDPR / nRDPR**: `A5 5A` = **読出保護 無効**、それ以外(例 `00 00`)= 有効 | minichlink: `rbuff[6]==0xA5` → "disabled" |
| `P[4]` | **USER**: bit0 `IWDG_SW`、bit1 `STOP_RST`(1=無効)、bit2 `STANDBY_RST`(1=無効)、X035: bits[4:3]=`11` で **reset pin を GPIO 化** | `P[5]` は補数 |
| `P[6]` / `P[8]` | **DATA0 / DATA1** | `P[7]` / `P[9]` は補数 |
| `P[10..13]` | **WRPR**(write protect、4 B) | minichlink は `rbuff[16],[15],[14],[13]` の順で表示(= `P[12..9]`)。**補数 byte の有無と WRPR の位置は 1 byte ずれの可能性があり capture で確定** |

読出保護の解除(minichlink `-u`)は `P[2]=A5, P[3]=5A, WRPR=FF FF FF FF` を書く → **chip が user 領域を全消去**する(RM の記述どおり)。有効化は `P[2]=P[3]=00`。

### 3.2 Identify から flash size を決める(minichlink の表)

`chip_id`(`P[0]`)で SKU 内の flash 容量が変わる系列がある:

| 系列 | 判定 |
|---|---|
| V103 | `chip_id == 0x32` → 32 KB、他 → 64 KB |
| V203 | `0x33/35/36/37` → 32 KB、`0x30/31/32/3A/3B/3E` → 64 KB、他 → 128 KB |
| V303 | `0x32/0x33` → 128、他 → 128 KB(表のまま) |
| X035 / L103 | `(chip_id & 0xF) == 7` → 48、他 → 64 KB |

CH5xx 系(`0x70..0x93`)は別 protocol 分岐(読出保護の位置が `P[10] bit7`、CH570 は `P[12]==0x3A`)。

## 4. XOR 難読化 key(byte 確定・USB 経路)

Program / Verify のデータは **UID 由来の 8 B key で byte ごとに XOR** される。

```
sum   = Σ UID[0..7]  (mod 256)
key[0..6] = sum
key[7]    = sum + chip_id      (chip_id = Identify の P[0]、mod 256)
```

- **minichlink は IspKey の seed 30 B を全部 `0x00` で送る**ため、key はこの形に退化する(device 側の一般式は `seed` の内容で変わる。wchisp は乱数 seed を送り、対応する導出を host 側で行う)。**seed を 0 にするのが実装上の最短経路**で、3 実装とも動作報告あり。
- 検証: IspKey の応答 `P[0]` が `Σ key[0..7] (mod 256)` に一致すれば key は正しい。
- 適用: 56 B chunk の各 byte `data[i] ^ key[i % 8]`(chunk 先頭で `i=0`)。image の余りは `0xFF` を XOR して埋める。

CH55x 系は device_type が違うため `key[7]` の値が変わるだけで式は同じ(minichlink は共通実装)。

## 5. 未解読 / 要 capture

- **USB の実バイトを自前 capture で `verified` 化**(WCHISPTool の USB モードを USBPcap / usbmon で観察。特に ReadConfig の `P[0..1]`、config 12 B の補数位置、Program の `pad` byte の実値)。
- **UART 経路**の framing(`57 AB` + checksum)と auto-baud、`0xC5` SetBaud の引数 — wchisp からの転記のみ。EVT / AN の「Serial」モード手順と突き合わせる。
- Erase の `n` に上限(flash 容量超え)を与えたときの挙動、部分消去が実際に効くか(minichlink は「全消去のみ実装」と注記)。
- chip 系列別の **BOOT pin / option byte による bootloader entry 条件**(RM の "Boot configuration")。V003 は `START_MODE` の設定が要る([wch-iap.ja.md](wch-iap.ja.md) §1 世代 A の切替レジスタと同じ `FLASH->STATR` bit14)。
- **IAP(In-Application Programming)は別 protocol**: EVT の `0xAA 0x55` sync + `0x80..0x84` → [wch-iap.ja.md](wch-iap.ja.md)。

## 6. 調査の入口

- WCH 純正 **WCHISPTool**(USB モード / Serial モード)の capture。
- **EVT / AN**: system bootloader の入り方・UART ピン・手順が提示されている(RISC-V CH32V/CH32X の EVT パッケージ、WCH ISP のアプリケーションノート)。
- 先行実装: `wchisp`(Rust、GPL-2.0)/ minichlink `pgm-wch-isp.c`(MIT)/ ch552tool 系。
- capture で Identify → Erase → Program → Verify の 1 往復を記録 → 本書を `verified` へ昇格。
