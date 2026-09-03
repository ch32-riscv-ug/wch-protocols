# PC ↔ target(factory ISP / シリアル書き込み)

状態: **attested**(先行実装は一致するが自前 capture 未 → `verified` 化が要る。ch32rv の isp crate は骨組みのみ)。層は L3(ISP protocol)。経路 ② — WCH-Link を使わず、チップに元から入っている**書き込み専用 bootloader(system bootloader / factory ISP)**を PC から直接叩く。RISC-V debug(DMI)は無く、flash 焼き・option/config 設定・verify だけ。

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

| transport | 枠 | 備考 |
|---|---|---|
| USB(`4348:55e0`) | bulk EP `0x02` out / `0x82` in に `cmd(1) len(2 LE) payload...` を素で載せる | sync prefix 無し。USB serial を持たない(識別は topology / BTVER) |
| UART | `0x57 0xAB` sync + `cmd(1) len(2 LE) payload...` + `checksum(1)` | checksum は payload の和(byte)。auto-baud(先頭で同期) |

- UART の USART・baud・配線は EVT / AN が chip ごとに提示する(WCHISPTool の "Serial" モード相当)。
- USB の `4348:55e0` は **WCH-Link IAP mode と同 VID:PID**。BTVER / chip 種別で判別し、LinkE 個体は probe firmware 更新へ回す。

## 3. コマンド一覧(WCH bootloader、`0xAx` 系。attested)

先行実装(wchisp / minichlink `pgm-wch-isp.c` / ch55x 系ツール)が一致。**byte 単位は自前 capture で `verified` 化が要る**。

| cmd | 名称 | payload / 応答 |
|---|---|---|
| `0xA1` | Identify | payload = chip_id + device_type + `"MCU ISP & WCH.CN"`。応答に chip signature(2B)。→ chip 系列を確定 |
| `0xA7` | ReadConfig | 応答に **BTVER(bootloader 版)/ chip UID(8B)/ option・config bytes**。UID は XOR key(§4)の材料 |
| `0xA3` | IspKey | host が乱数 buffer を送る → device が UID + buffer から XOR key を生成、応答に検証 byte。以降の Program/Verify がこの key で難読化される |
| `0xA4` | Erase | payload = 消去 sector 数(1KB/sector)。全消去 or 範囲 |
| `0xA5` | Program(Write) | payload = addr(LE32)+ data(最大 ~56B、**§4 の key で XOR**)。逐次 chunk |
| `0xA6` | Verify | Program と同形式で読み戻し照合 |
| `0xA8` | WriteConfig | option / config bytes 書き込み |
| `0xA2` | IspEnd | セッション終了(reset して user flash から実行 等) |

典型フロー: `Identify → ReadConfig(UID 取得)→ IspKey → Erase → Program(chunk 反復)→ Verify → IspEnd`。

## 4. XOR 難読化 key(attested・要 capture)

Program/Verify のデータは **chip UID から導く XOR key で難読化**される(平文では焼けない):

- ReadConfig(`0xA7`)で 8B の UID を得る。
- IspKey(`0xA3`)で host 乱数と UID から key を確定(chip 系列で算法が違う。CH32V と CH55x で別)。
- 応答の検証 byte が一致すれば key 正当。以降 Program/Verify の data を key で XOR。

**算法の byte 単位は要 capture**(実装ごとに細部差の報告あり)。ここを詰めるのが ISP を `verified` にする山場。

## 5. 未解読 / 要調査

- USB/UART 双方の**実バイトを自前 capture で確定**(WCHISPTool の USB・Serial 各モードを usbmon / ロジアナで観察)。
- XOR key 生成算法の chip 系列差(CH32V / CH32X / CH55x)。
- Erase の sector 数エンコードと最大 flash 範囲、Program chunk サイズの chip 差。
- UART の chip 別 USART ピン・baud 一覧(EVT / AN から転記 → 実機確認)。
- **IAP(In-Application Programming)は factory ISP とは別 protocol**: user が焼く app 内 bootloader。WCH の EVT サンプルは `0xAA 0x55` sync + `0x80..0x84` コマンド(この `0xAx` ISP とは無関係)。→ [serial-and-print.ja.md](serial-and-print.ja.md) §1 に EVT からの実測を記載済み。

## 6. 調査の入口

- WCH 純正 **WCHISPTool**(USB モード / Serial モード)の capture。
- **EVT / AN**: system bootloader の入り方・UART ピン・手順が提示されている(RISC-V CH32V/CH32X の EVT パッケージ、WCH ISP のアプリケーションノート)。
- 先行実装: `wchisp`(Rust、GPL-2.0)/ minichlink `pgm-wch-isp.c`(MIT)/ ch552tool 系。
- capture で Identify → Erase → Program → Verify の 1 往復を記録 → 本書を `verified` へ昇格。
