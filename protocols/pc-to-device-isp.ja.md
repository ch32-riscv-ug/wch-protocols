<a id="pc-to-device-isp"></a>

# PC ↔ target(factory ISP / シリアル書き込み)

status([語彙](../README.md#status)): **USB 経路は CH32X035F8U6(BTVER 2.60)で `verified`**(ch32rv の実機往復。書いた内容を RVSWD の読出しで照合。replay fixture は ch32rv の [isp-info-x035](https://github.com/ch32-riscv-ug/ch32rv/blob/v0.24.0/tests/fixtures/replay/isp-info-x035.ndjson) / [isp-flash-x035](https://github.com/ch32-riscv-ug/ch32rv/blob/v0.24.0/tests/fixtures/replay/isp-flash-x035.ndjson) / [isp-config-set-x035](https://github.com/ch32-riscv-ug/ch32rv/blob/v0.24.0/tests/fixtures/replay/isp-config-set-x035.ndjson))。他の系列と UART 経路は `attested`(minichlink / wchisp / chprog の 3 実装一致)。層は L3(ISP protocol)。経路 ② — WCH-Link を使わず、チップに元から入っている**書き込み専用 bootloader(system bootloader / factory ISP)**を PC から直接叩く。RISC-V debug(DMI)は無く、flash 焼き・option/config 設定・verify だけ。

同じ ISP protocol が **2 つの物理経路**で使える:

- **USB**: チップの USB を PC に直結。CH32X035(BTVER 2.60)の factory ISP は **`1a86:55e0`**(bcdDevice `26.00`、class `0xff`、bulk `0x02` OUT / `0x82` IN の 1 組、64 B、string descriptor なし)。minichlink は VID `0x4348` と `0x1a86` の両方を探す。他の系列がどちらの VID を名乗るかは未測定。
- **UART(シリアル)**: チップの特定 USART に PC の TTL シリアルをつなぐ。**WCH の EVT / AN でこの手順が提示されている**。

<a id="enter-bootloader"></a>

## 1. bootloader に入る

app を bypass して system bootloader を起動させる:

- 起動元の選び方は **2 系統**ある(各 RM §1.3 "Boot Configuration" / FLASH 章から転記、attested):

| 系列 | 選択方式 | 条件 | 備考 |
|---|---|---|---|
| **V103 / V20x / V30x / V31x / L103 / V407**(/ V205 / H417 は同族と見られる、未確認) | **BOOT0 / BOOT1 ピン**(reset・電源投入時にラッチ) | `BOOT0=0` → user flash / **`BOOT0=1, BOOT1=0` → system memory(factory ISP)** / `BOOT0=1, BOOT1=1` → SRAM | system memory から起動すると `0x00000000` に alias。STM32 と同じ作法 |
| **V003 / V00X** | **ソフトウェア選択のみ**(BOOT ピン無し)。`FLASH_STATR` bit14 `BOOT_MODE`(1 = BOOT 領域 / 0 = user)を `BOOT_MODEKEYR`(`0x40022028`)解錠後に書き、software reset | RM の memory map に「software configuration に依存」と明記 | **app が協力しないと factory ISP に入れない**(V003 の `START_MODE` 問題の正体) |
| **X033 / X035** | **電源投入時の PC17(USB D+)= High**、または上と同じ `BOOT_MODE` + software reset | CH32X035 DS V2.3 ピン表 注 8:「PC17 is the BOOT detection pin. Upon power-up, PC17 is high, causing the chip to enter the BOOT area.」 | **app の協力なしで入れる**(BOOT0 ピンの代わりが PC17)。PC17 を抵抗で 3.3 V へ引く button を持つ板がある(WeAct CH32X035 CoreBoard V1.0 の DOWNLOAD KEY: VDD33 → 5.1 kΩ → PC17、回路図で確認)。button を離す時機の条件は未実測。bit13 `BOOT_STATUS`・bit12 `BOOT_AVA` も持つ |
| **X305 / X315** | option byte の `START_MD`(既定 1: 電源投入時に BOOT 領域から起動)と `USBFSDLEN` / `USARTDLEN`(既定 1: BOOT の「keyless download」を有効)。ソフトウェアでは `BOOT_MODE` + software reset | CH32X315 RM の option byte 表 | 既定値のままなら **button 無しで電源投入時に factory ISP が応答する**と読めるが、待ち時間・条件は未実測 |
| M030 | (BOOT 領域無し) | — | factory ISP 自体が無い |

切替レジスタの詳細と自作 BL からの使い方: [custom-bootloader.ja.md](custom-bootloader.ja.md) §2a。

<a id="x035-entry"></a>

**CH32X035 で実測した入り方**(CH32X035F8U6 / C8T6、BTVER 2.60。F8U6 は WeAct CH32X035 CoreBoard、debug chip ID `0x035e0611`。debug 線は OEP probe(ESP32-P4)の RVSWD と WCH-LinkE fw 2.22):

| 方法 | 結果 |
|---|---|
| debug 線で `FLASH_KEYR` と `FLASH_BOOT_MODEKEYR`(`0x40022028`)にそれぞれ `0x45670123`, `0xCDEF89AB` → `FLASH_STATR` に bit14(`BOOT_MODE`)を足して書く → system reset | **入る**。reset 後 0.5 s 以内に `1a86:55e0` が列挙される。`STATR` は `0xF000`、PC は `0x680` 前後(BOOT 領域) |
| 同じ手順を、hart が flash の中身を実行している状態で、別々の attach に分けて行う | 中身が正規の app なら入る。flash に実行できないデータ(試験用の pattern)が入っていると入れない: reset のたびにそのデータが命令として走って鍵を触り、FLASH をロックする。以後 `FLASH_CTLR` の LOCK を外せない |
| reset vector で hart を止めてから、上の手順を 1 回の attach で行う | flash の中身によらず入る |
| 同じ手順を WCH-LinkE(fw 2.22)で: DMI の reset-halt → 鍵 → `BOOT_MODE` → LinkE の soft reset | 入る(`STATR` `0xF000`、PC `0x6c0`)。USB 未接続のままだと約 10 s で app へ戻る |
| app から(EVT の `SystemReset_StartMode(Start_Mode_BOOT)` と同じ操作。ArduinoCore-CH32RV の DFU_DETACH / 1200 bps touch) | 入る。DETACH から `55e0` の出現まで 0.36〜0.8 s |

`FLASH_STATR` の上位 4 bit の観測値: app 実行中 `0xB000`、鍵を外した後 `0x3000`、`BOOT_MODE` を書いた後 `0x5000`、bootloader 実行中 `0xF000`。PC17(BOOT キー)で入る方法と、キーを離す時機は未測定。

**終わり方と時間**(X035、BTVER 2.60):

- `IspEnd(01)` を送って**応答を読む**と、約 200 ms で bootloader は `BOOT_MODE` を解除して app へ reset する(`STATR` が `0xB000` に戻る)。
- **応答を読まずに終えると、bootloader は応答の送信待ちのまま残る**。その後の session では Identify には答えるが、ReadConfig の OUT が stall する。
- 最後の通信から**約 60 s** 何も来ないと、bootloader は `BOOT_MODE` を解除して app へ抜ける。現れてから**一度も**話しかけられないと、約 9.3〜9.7 s で抜ける。
- bootloader 実行中に debug 線で hart を halt したままにすると、USB の OUT がすべて timeout する(USB は CPU が処理しているため)。

<a id="x035-read-protect"></a>

**読み出し保護(RDPR)中は factory bootloader が走らない**(`verified`、CH32X035F8U6、BTVER 2.60、debug 線は OEP probe の RVSWD、2026-10-14): WriteConfig で RDPR/nRDPR = `00 ff` を書くと option bytes にすぐ反映され、reset 後に `FLASH_OBR` の bit1(RDPRT)が立つ(`FLASH_OBR` = `0x03ffff7e`)。この状態では RVSWD の flash 読み出しが fault(`0x03`)になる。`BOOT_MODE` を立てて reset すると、`STATR` は `0xF000` になるが PC は `0x0`(`ra` = `0x684`)で止まり、ISP device は現れない。確かめた入り方は 2 通り: (1) debug 線で `BOOT_MODE` を立てて reset、(2) debugger の電源を落とし(OEP probe の hub port を off)、X035 の USB port だけ電源を入れ直してから app に DFU_DETACH(app は `BOOT_MODE` を立てて reset するが、bootloader は同じく PC `0x0` で止まり一度も列挙されない)。ISP 経由で保護を外すことはできないので、解除は debug 線で行う(code flash は消える)。BOOT キーで入った場合は未確認。

- **保護中の app は数秒で止まる**(`verified`、同じ範囲): debugger なしで電源を入れると app は起動するが、約 4〜5 s で USB を切って止まった。debugger(OEP probe)が起動して約 1 s で止まった例もある(probe が起動時に線を触るためと見られる。推定)。保護を外すと同じ image は止まらない。止まる原因は未調査。

<a id="weact-x035-power"></a>

**板の事情: WeAct CH32X035 CoreBoard の電源**(この板に固有、chip の性質ではない): この板は USB と debug 線の両方から給電される。OEP probe をつないだ構成では、X035 の USB port だけ電源を切っても電源投入 reset にならず、OEP probe の port も切ると reset される(uhubctl で port 電源を切った。hub `1a86:8094` は port ごとの電源制御(PPPS)を持つ)。電源投入 reset を前提にする試験(PC17 の BOOT 判定、保護中の起動)では、debug 線側の給電も切る。
- bootloader は **USB と UART の両方で応答を待つ**(chip による)。どちらか使う方につなぐ。

<a id="factory-bootloader"></a>

### factory bootloader の実体(系列差・entry の強さ)

- **CH32V003**: factory bootloader は `0x1FFFF000` からの **1,920 byte** system 領域にあり、UART **115200 bps** で動く。**外部 BOOT ピンだけでなく application が `START_MODE` を設定して reset する必要がある** → app が完全に壊れると入口として弱い(復旧は debug probe 経由が確実)。詳細は [CH32V003 factory bootloader missing manual](https://github.com/basilhussain/ch32v003-bootloader-docs)。V00X では system 領域サイズや一部 package の UART remap が異なる。
- **USB 内蔵系列**: factory ISP は wchisp / WCHISPTool から使えるが、「USB peripheral がある」ことと「その SKU の factory bootloader が USB 経路を公開する」ことは**別**。正確な対応は **exact SKU × BOOT pin/option byte × bootloader version** で管理する。
- **CH32M030**: 工場 ISP を持たず、外部 debug または導入済み custom IAP を使う。

<a id="framing"></a>

## 2. transport ごとの framing

ISP のコマンド本体(§3)は共通で、**外側の枠だけ transport で違う**:

| transport | request | response | 備考 |
|---|---|---|---|
| **USB**(`1A86:55E0`、minichlink は `4348:55E0` も探す) | bulk EP `0x02` OUT に `cmd(1) len(2 LE) payload…` を素で載せる | bulk EP `0x82` IN に `cmd(1) 00 len(2 LE) payload…` | sync 無し。**string descriptor / serial を持たない**(識別は topology / BTVER)。timeout は minichlink 5 s。`verified`(X035) |
| UART | `0x57 0xAB` sync + `cmd(1) len(2 LE) payload…` + `checksum(1)` | `0x55 0xAA` sync + 同形 + checksum | checksum は byte の和。先頭で auto-baud(wchisp)。`attested`(実機未確認) |

- host は **VID `0x4348` または `0x1A86`、PID `0x55E0`** を探す(minichlink、ch32rv `crates/isp` の `ISP_IDS`。実機で列挙を見たのは X035 の `1a86:55e0` だけ)。**WCH-Link の IAP mode(`4348:55e0`)と WCH IAP(EVT の app 内 BL、`1a86:55e0`)も同じ `55E0`** を名乗る([pc-to-link.ja.md](pc-to-link.ja.md) §10b / [wch-iap.ja.md](wch-iap.ja.md) §2)。X035 の factory ISP と WCH IAP は VID も同じなので、**VID では区別できない**。BTVER / chip 種別、および「`0xA1` に答えるか」で判別する。

<a id="commands"></a>

## 3. コマンド一覧(WCH bootloader、`0xAx` 系)

status: `attested`(3 実装一致: minichlink `pgm-wch-isp.c`(MIT)/ wchisp / wagiminator `chprog.py`)。`0xA1`〜`0xA8` と `0xA2` は X035(BTVER 2.60)の USB で `verified`(実測の byte は下の「X035 の実測」)。`P[n]` は response の payload(ヘッダ 4 byte の後)の n 番目。

| cmd | 名称 | request(cmd len payload) | response | 備考 |
|---|---|---|---|---|
| `0xA1` | **Identify** | `A1 12 00` + `chip_id device_type` + `"MCU ISP & WCH.CN"`(16 B) | 6 B: `A1 00 02 00 chip_id device_type` | 送る `chip_id/device_type` は任意(minichlink は `52 11` 固定)。**返ってきた `P[0]=chip_id, P[1]=device_type`** で系列確定。`chip_type = chip_id \| device_type<<8` |
| `0xA7` | **ReadConfig** | `A7 02 00 1F 00`(mask `0x001F` = 全部) | 30 B: `A7 00 1A 00` + `P[0..1]` + **`P[2..13]` config 12 B** + **`P[14..17]` BTVER** + **`P[18..25]` UID 8 B** | BTVER は `P[15].P[16]P[17]`(例 `02 03 01` → 2.31)。UID が XOR key の材料 |
| `0xA3` | **IspKey** | `A3 1E 00` + **30 B の seed** | `A3 00 02 00 chk 00` | device は `seed + UID` から 8 B の XOR key を作る。**`P[0]=chk` は key 8 B の和(byte)**。host が同じ計算で key を持てば一致を検証できる(§4) |
| `0xA4` | **Erase** | `A4 04 00 n_lo n_hi 00 00` | `A4 00 02 00 status 00` | **`n` = 消去 sector 数(1 KB/sector)= ceil(len/1024)、最小 8**。`status=0` で成功。全消去は `n = flash_size/1024` |
| `0xA5` | **Program** | `A5 3D 00` + `addr(4 LE)` + `pad(1)` + **`data 56 B ^ key[k % 8]`** | `A5 00 02 00 status 00` | `addr` は **flash 先頭を 0 とするオフセット**(`0x08000000` → `0`)。`pad` = 残り長の下位 byte。image は **256 B 境界まで `0xFF` で埋めて**送る。`status ∈ {0x00, 0xFE, 0xF5}` を成功扱い |
| `0xA6` | Verify | Program と同形(データを XOR して送る) | 同。一致 `00`、不一致は非 0 | minichlink は未実装(wchisp は実装) |
| `0xA8` | **WriteConfig** | `A8 0E 00 07 00` + **config 12 B** | `A8 00 02 00 status 00` | mask `0x0007`。ReadConfig で得た 12 B を書き換えて送る。X035 では約 1 ms で応答し、直後の ReadConfig と option bytes(`0x1FFFF800`)に反映済み(reset 不要) |
| `0xA2` | **IspEnd** | `A2 01 00 01` | `A2 00 02 00 00 00` | `01` = reset して user code へ。**応答を読むまで reset しない**(X035。§1) |
| `0xA9` / `0xAA` / `0xAB` | DataErase / DataProgram / DataRead | — | — | data flash(EEPROM 相当。CH55x 系)|
| `0xC3` / `0xC4` | WriteOTP / ReadOTP | — | — | |
| `0xC5` | SetBaud | — | — | **UART 経路の baud 変更** |

典型フロー: `Identify → ReadConfig(UID)→ IspKey → Erase → Program × N → (Verify × N)→ IspEnd`。

<a id="x035-bytes"></a>

**X035 の実測**(CH32X035F8U6、BTVER 2.60、USB):

| cmd | request | response | 備考 |
|---|---|---|---|
| `0xA1` Identify | `a1 12 00` + `00 00` + `"MCU ISP & WCH.CN"` | `a1 00 02 00 5e 23` | chip `0x5e`(debug chip id の 2 byte 目と一致)、type `0x23` = CH32X03x |
| `0xA7` ReadConfig | `a7 02 00 1f 00` | `a7 00 1a 00` + `1f 00` + config 12 B + BTVER 4 B + UID 8 B | 先頭 2 B は送った mask(`1f 00`)の返し |
| `0xA3` IspKey | `a3 1e 00` + 30 B の 0 | `a3 00 02 00 c6 00` | `c6` = key 8 B の和(§4) |
| `0xA4` Erase | `a4 04 00` + sector 数(4 B LE) | `a4 00 02 00 00 00` | 8 sector(8 KiB)で約 25 ms。全 62 sector も受理 |
| `0xA5` Program | `a5 3d 00` + offset(4 B LE)+ `00` + 56 B(key で XOR) | `a5 00 02 00 00 00` | 最後に data 0 B の Program(offset = 末尾)を送り、これも `00` |
| `0xA8` WriteConfig | `a8 0e 00 07 00` + config 12 B | `a8 00 02 00 00 00` | DATA0/1、USER(IWDG / STOP / STANDBY / RST_MODE)、WRPR0 を書いて戻した。RDPR は §1 |
| `0xA2` IspEnd | `a2 01 00 01` | `a2 00 02 00 00 00` | |

UID(ReadConfig の最後の 8 B)は ESIG の `0x1FFFF7E8` から読んだ 8 B と一致した。

<a id="read-config"></a>

### 3.1 ReadConfig の config 12 B(CH32V 系)

`P[2..13]` = ReadConfig の rbuff[6..17]。minichlink の表示と STM32 互換 option byte の並びから:

| P | 内容(CH32V 系) | 備考 |
|---|---|---|
| `P[2]` `P[3]` | **RDPR / nRDPR**: `A5 5A` = **読出保護 無効**、それ以外(例 `00 00`)= 有効 | minichlink: `rbuff[6]==0xA5` → "disabled" |
| `P[4]` | **USER**: bit0 `IWDG_SW`、bit1 `STOP_RST`(1=無効)、bit2 `STANDBY_RST`(1=無効)、X035: bits[4:3]=`11` で **reset pin を GPIO 化** | `P[5]` は補数 |
| `P[6]` / `P[8]` | **DATA0 / DATA1** | `P[7]` / `P[9]` は補数 |
| `P[10..13]` | **WRPR0..3**(write protect、4 B。**補数 byte は含まない**) | X035 で実測: option bytes(`0x1FFFF800`)が `a5 5a 1f e0 ff 00 ff 00 \| ff 00 ff 00 ff 00 ff 00` のとき、config 12 B は `a5 5a 1f e0 ff 00 ff 00 ff ff ff ff`。minichlink は `rbuff[16],[15],[14],[13]` の順で表示する |

読出保護の解除(minichlink `-u`)は `P[2]=A5, P[3]=5A, WRPR=FF FF FF FF` を書く → **chip が user 領域を全消去**する(RM の記述どおり)。有効化は `P[2]=P[3]=00`。

<a id="flash-size"></a>

### 3.2 Identify から flash size を決める(minichlink の表)

`chip_id`(`P[0]`)で SKU 内の flash 容量が変わる系列がある:

| 系列 | 判定 |
|---|---|
| V103 | `chip_id == 0x32` → 32 KB、他 → 64 KB |
| V203 | `0x33/35/36/37` → 32 KB、`0x30/31/32/3A/3B/3E` → 64 KB、他 → 128 KB |
| V303 | `0x32/0x33` → 128、他 → 128 KB(表のまま) |
| X035 / L103 | `(chip_id & 0xF) == 7` → 48、他 → 64 KB |

CH5xx 系(`0x70..0x93`)は別 protocol 分岐(読出保護の位置が `P[10] bit7`、CH570 は `P[12]==0x3A`)。

<a id="xor-key"></a>

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

<a id="open-questions"></a>

## 5. 未解読 / 要 capture

- X035 以外の系列の USB の実 byte(特に VID が `4348` か `1a86` か、ReadConfig の `P[0..1]`、IspEnd の振る舞い)。
- **UART 経路**の framing(`57 AB` + checksum)と auto-baud、`0xC5` SetBaud の引数、各チップの bootloader USART のピン — wchisp からの転記のみ。EVT / AN の「Serial」モード手順と突き合わせる。
- Erase の `n` に上限(flash 容量超え)を与えたときの挙動、部分消去が実際に効くか(minichlink は「全消去のみ実装」と注記)。
- chip 系列別の **BOOT pin / option byte による bootloader entry 条件**(RM の "Boot configuration")。X033 / X035 は DS の PC17 で確定(§1)、X305 / X315 の keyless download は実機未確認。V003 は `START_MODE` の設定が要る([wch-iap.ja.md](wch-iap.ja.md) §1 世代 A の切替レジスタと同じ `FLASH->STATR` bit14)。
- data flash(`0xA9`〜`0xAB`)、OTP(`0xC3` / `0xC4`)。
- **IAP(In-Application Programming)は別 protocol**: EVT の `0xAA 0x55` sync + `0x80..0x84` → [wch-iap.ja.md](wch-iap.ja.md)。

<a id="investigation"></a>

## 6. 調査の入口

- WCH 純正 **WCHISPTool**(USB モード / Serial モード)の capture。
- **EVT / AN**: system bootloader の入り方・UART ピン・手順が提示されている(RISC-V CH32V/CH32X の EVT パッケージ、WCH ISP のアプリケーションノート)。
- 先行実装: `wchisp`(Rust、GPL-2.0)/ minichlink `pgm-wch-isp.c`(MIT)/ ch552tool 系。
- 実装: [ch32rv](https://github.com/ch32-riscv-ug/ch32rv) の `isp`(X035 の USB 経路を実機で往復。UART 経路は `57 ab` / `55 aa` + byte 和で実装したが実機未確認)。
