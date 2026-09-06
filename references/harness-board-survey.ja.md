# board 別の到達範囲 — 1 target 専有の計測 probe をどこまで広げられるか

状態: **調査メモ**(公式資料からの転記 + 自 repo の実測。設計判断は含むが実装・実測は未)。対象: **RP2040 / RP2350 / ESP32-S3 / CH32X035 / CH32X033**。構想側は [dut-harness-design.ja.md](dut-harness-design.ja.md)、probe パターンの共存は [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md)。

## 0. 結論(先に)

1. **能力を 8 軸に分解すると board 差がきれいに説明できる**(§1)。「どの board が優れているか」ではなく「**どの軸を諦めるか**」の問題になる。
2. **最大の発見: CH32X035 / X033 には PIOC がある。** RM 第 22 章 —「単一クロックサイクルの専用命令セット RISC コアを system clock で動かし、**2K 命令の program ROM**・49 SFR・PWM タイマを持ち、**2 本の I/O ピンのプロトコル制御**を行う」。WCH は **Single_Wire / IIC(master と slave 両方)/ UART / NEC** の ASM 例を EVT に同梱している。→ **X03x は「SWIO/RVSWD の phy が hardware になる最安の probe」**。ただし **2 ピンしか無いので LA にはならない**。
3. **ESP32-S3 は「深さ」だけ桁違い。** LCD_CAM の DVP 入力(8/16 bit、**40 MHz 以下**)+ GDMA + **PSRAM(GDMA は外部 RAM にアクセス可)**で、RP2040 の 264 KB に対し **MB 級のバッファ**が取れる。しかも **GPIO matrix なのでキャプチャ ch のピン連番が要らない**。代わりにアナログが弱い(ADC 連続 83.3 kSa/s、**DAC 無し**)。
4. **RP2040 は全軸が中庸で、かつ「16ch 連番キャプチャ + hardware 周辺エミュ + 4ch ADC」が同時に成立する唯一の選択肢**。RP2040-Zero が最安・最安定という判断は妥当。
5. **RP2350 は上位互換だが errata E9(入力ピンが約 2.1–2.2 V でラッチする)が LA 用途に直撃する。** 全 ch が Hi-Z 入力になる構成そのものが該当条件で、**PIO プログラムからは回避策(入力バッファの都度 enable)が使えない**。外部 pull-down(8.2 kΩ 以下)か、修正 revision の確認が済むまでは **RP2040 を主にする**。
6. **Pico(無印)は RP2040-Zero より「窓」が広い**。GP0–GP22 が連番で出ているので **23ch** 取れる(Zero は 16ch)。代わりに ADC が 3ch(GP29 が VSYS 監視で埋まる)。**Zero = 小さく安い / Pico = 窓が広い**の使い分け。
7. **1 台に全部載せない道もある**(§5)。S3 を深いキャプチャ、RP2040 を probe + エミュに分け、trigger 線 1 本で時間軸を結ぶ。**「深さ」と「アナログ + hardware エミュ」は別 chip の得意分野**なので、分業は不利ではない。

## 1. 能力の 8 軸

| 軸 | 何で決まるか | 単位 |
|---|---|---|
| **A. 線 phy の決定論** | SWIO/RVSWD を「ばらつかせずに」出せるか | 分解能 [ns] |
| **B. 並列キャプチャ** | 何 ch を同時に、どのレートで、ピン連番は要るか | ch / MSa/s |
| **C. 深さ** | バッファ(内部 RAM / 外部 RAM) | byte → 時間 |
| **D. 周辺エミュ** | SPI slave / I2C slave(**複数アドレス**)/ UART が hardware で足りるか | 個数 |
| **E. アナログ** | ADC の ch とレート、出力(DAC)、アナログトリガ | ch / kSa/s |
| **F. host 経路** | USB の種別と実効帯域、Wi-Fi、driver の要否 | MB/s |
| **G. 電気** | target が 5 V のとき直結できるか | V |
| **H. 入手性** | 価格・安定性・フットプリント | — |

### 総括表

| | RP2040 | RP2350 | ESP32-S3 | CH32X035 | CH32X033 |
|---|---|---|---|---|---|
| **A** phy | **PIO 8 ns**@125 MHz(2 blk × 4 SM、命令 32/blk) | **PIO 8 ns**@150 MHz(**3 blk × 4 SM**) | **RMT 12.5 ns**(自 repo 実測 E006)/ DEDIC_GPIO | **PIOC 20.8 ns**@48 MHz 単一サイクル・2K 命令・**2 ピン専用** | 同じ(PIOC あり) |
| **B** キャプチャ | PIO `in pins,N`。**連番必須**。16–23ch @ 数十 MSa/s | 同じ + SM 余裕。**E9 が直撃** | **LCD_CAM DVP 8/16bit ≤40 MHz + GDMA**。**連番不要**(GPIO matrix) | 無し。TIM+DMA で GPIO を舐める程度 | 同じ |
| **C** 深さ | 264 KB | **520 KB** | **PSRAM 8 MB 級**(GDMA が外部 RAM 可) | 20 KB(PIOC コード領域を除く) | 同じ |
| **D** エミュ | UART×2 / **SPI×2(PL022 slave)** / I2C×2(slave 1 アドレス) | 同じ | UART×3 / SPI2,SPI3 slave / I2C×2 slave | **SPI×1 / I2C×1 / USART×4**、+ **PIOC で I2C slave**(WCH 例あり) | 同じ(USART×4) |
| **E** アナログ | ADC 4ch・**合計 500 kSa/s**・DAC 無し(PWM 代用) | ADC 4ch(B パッケージは 8ch) | ADC 連続 **83.3 kSa/s**・**DAC 無し** | **12bit ADC 10–14ch + OPA/PGA 2 + CMP 3(TIM2 直トリガ)** | 同じ(CMP 2) |
| **F** host | USB FS(~1 MB/s) | USB FS | USB OTG FS + USB-Serial-JTAG + **Wi-Fi/BLE** | USB FS device(大パッケージは host も) | USB FS device(**PD/host 無し**) |
| **G** 電気 | 3.3 V。**5 V 非トレラント** | 同じ | 3.3 V | **VDD 2–5.5 V** → **5 V target 直結可** | 同じ |
| **H** 入手性 | **最安・最安定**(Zero) | Zero 同形あり | 実績多い。手元に常設 2 枚 | **極安**($1 未満級) | 同じ |

**読み方**: A は全 board で足りている(SWIO の 290 ns に対し最悪でも 20.8 ns 分解能)。**差が出るのは B・C・E・G**。

## 2. 軸ごとの詳細

### 2.1 A. 線 phy — どの board でも足りる

自 repo の実測で SWIO の短パルスは **290 ns**、長パルスは **890 ns**([E006](../experiments/e006_tool_pulse_capture/README.ja.md))。必要な分解能はこの 1/10 程度あれば十分。

| board | 手段 | 分解能 | 290 ns が何 tick か | 根拠 |
|---|---|---:|---:|---|
| RP2040/RP2350 | PIO | 8 ns(@125 MHz) | 36 | PIO は 1 命令 1 サイクル |
| ESP32-S3 | RMT | **12.5 ns** | 23 | **実測**([E006](../experiments/e006_tool_pulse_capture/README.ja.md)。ばらつき観測されず) |
| CH32X035/X033 | **PIOC** | 20.8 ns(@48 MHz) | 14 | RM 第 22 章(単一サイクル・system clock) |
| (参考)CPU ポーリング | ソフト | — | — | **S3 の CPU ループは 2 線で 100 kbps が上限**([E005](../experiments/e005_tool_clocked_capture/README.ja.md)) |

**要点は「CPU ループでは足りないが、どの board にも専用機構がある」**。E005 が示したとおり CPU ポーリングは 100 kbps で頭打ちになるので、phy は必ず PIO / RMT / PIOC のどれかに載せる。

#### PIOC(CH32X035/X033)— WCH 版の PIO

RM 第 22 章の記述:

> 単一クロックサイクルの専用 lean 命令セット RISC コアを system clock で動作させ、**2K 命令のプログラム ROM**、**49 個の SFR**、PWM タイマ/カウンタを持ち、**2 本の I/O ピン**のプロトコル制御をサポートする。

EVT から読み取れる実体([`SRC/Peripheral/inc/PIOC_SFR.h`](https://github.com/openwch/ch32x035) と `EXAM/PIOC/`):

| 項目 | 内容 |
|---|---|
| コード領域 | `PIOC_SRAM_BASE = SRAM_BASE + 0x4000`(SRAM の窓に載せる。**SRAM 20 KB のうち上位が取られる前提で見積もる — 要確認**) |
| IO | **IO0 / IO1 の 2 本のみ**。既定 `IO0=PC18 / IO1=PC19`、remap で `IO0=PC7 / IO1=PC19`(`AFIO_PCFR1.PIOC_RM`) |
| 方向・pull | `R8_PORT_DIR` / `R8_PORT_IO`。**`RB_PORT_IN_XOR`(IO0 XOR IO1)と `RB_PORT_XOR0/1`(出力 XOR 入力)を直接読める** |
| bit 符号化支援 | `R8_BIT_CYCLE`(**encode bit cycle**)+ `RB_BIT_TX_O0` / `RB_BIT_RX_I0`(**decoded bit data**) |
| タイマ | `TMR0`(timer / PWM 切替、周波数選択 3 bit) |
| master 連携 | `R8_CTRL_RD` / `R8_CTRL_WR` / `R8_DATA_EXCH` + `RB_DATA_SW_MR` / `RB_DATA_MW_SR` の**ハンドシェイクフラグ**、32 個の `DATA_REG`、**PIOC 専用割込ベクタ**(#47) |
| 起床 | `RB_EN_LEVEL0/1` で **IO のレベル変化で起床**できる |
| 命令 | `CLRA / MOVA / MOVL / AND / XORL / ADDL / CALL / RET / JMP / JNZ / NOP` …(EVT の `PIOC_INC.ASM`)。アセンブラと `.BAT` 同梱 |
| WCH 提供例 | `PIOC_Single_Wire`(498 B)/ `PIOC_IIC`(980 B、**master と slave 両方**)/ `PIOC_UART`(1650 B)/ `PIOC_NEC`(374 B)/ `1_Wire`(RGB、1142 B) |

**「2 ピンのプロトコル制御」は SWIO(1 線)と RVSWD(2 線)にそのまま一致する。** `BIT_CYCLE` によるビット周期符号化と `BIT_RX_I0` の復号は、**パルス幅で 0/1 を表す SWIO のための機構に見える**。X03x は「WCH 自身が 1 線/2 線のために用意した engine を持つ chip」であり、[link-to-target](../protocols/link-to-target.ja.md) の phy を最も素直に実装できる可能性がある。

**ただし 2 ピン。** PIOC を debug 線に使えば、同じ PIOC で I2C slave を演じることはできない(排他)。エミュは hardware 周辺(SPI/I2C/USART)側に回す必要がある(§2.4)。

### 2.2 B. 並列キャプチャ — ここで board が分かれる

| board | 機構 | ch | レート | ピン連番 |
|---|---|---|---|:--:|
| RP2040 | PIO `in pins,N` + DMA | 最大 32(実際は空きピン数) | sysclk まで(数十 MSa/s 実用) | **必須** |
| RP2350 | 同じ(SM に余裕) | 同じ | 同じ | **必須** |
| **ESP32-S3** | **LCD_CAM の DVP 入力 + GDMA** | **8 / 16** | **40 MHz 以下** | **不要**(GPIO matrix) |
| CH32X035/X033 | TIM トリガの DMA で GPIO ポートを読む | 8–16 | 1–4 MSa/s 程度(**要実測**) | ポート単位(PA/PB/PC の連番) |

- **RP2040/RP2350 の「連番必須」が board 選定を支配する**。`in pins,N` は IN base から連番 N 本なので、board が連番でピンを出していないと窓が作れない(§3)。
- **ESP32-S3 の GPIO matrix は連番制約が無い**のが大きい。ピン配置の自由度が要る用途(既存の治具に合わせる、PSRAM で潰れたピンを避ける)では圧倒的に楽。
- **X03x は「キャプチャ」と呼べる水準には届かない**。I2C 400 kHz や UART をストリームで拾って decode するのが上限で、波形観測は無理。

### 2.3 C. 深さ — ESP32-S3 だけが別クラス

16ch = 2 B/sample として:

| board | バッファ | 100 MSa/s | 20 MSa/s | 1 MSa/s |
|---|---|---|---|---|
| RP2040 | 264 KB(実用 ~200 KB) | ~1 ms | ~5 ms | ~100 ms |
| RP2350 | 520 KB(実用 ~450 KB) | ~2 ms | ~11 ms | ~225 ms |
| **ESP32-S3 + 8 MB PSRAM** | **8 MB** | (帯域が先に律速) | **~200 ms** | ~4 s |
| CH32X035/X033 | 20 KB 未満 | — | — | ~7 ms |

**S3 の制約は容量ではなく PSRAM 帯域**。16ch @ 40 MSa/s = 80 MB/s は octal PSRAM でも厳しく、20 MSa/s(40 MB/s)程度が現実的な線と見る(**要実測**)。加えて **octal PSRAM 搭載モジュールは GPIO33–37 が潰れる**(quad なら空くが帯域は落ちる)ので、**深さとピン数のトレードオフが PSRAM の種類で決まる**。

### 2.4 D. 周辺エミュ — hardware slave の数で決まる

| board | UART | SPI slave | I2C slave | 複数アドレス I2C |
|---|---|---|---|---|
| RP2040/RP2350 | 2 | **2**(PL022 の slave mode) | 2(**各 1 アドレス**) | PIO で書く / 2 ブロックを外部結線 |
| ESP32-S3 | 3 | 2(SPI2/SPI3) | 2 | ソフト(PIO 相当が無い) |
| **CH32X035** | **4** | 1 | 1 | **PIOC で I2C slave**(WCH 例あり。ただし PIOC は debug 線と排他) |
| CH32X033 | 4 | 1 | 1 | 同じ |

- **RP2040 の強みは「既定ピン割当で UART0/I2C1/SPI0/UART1 が同時に衝突しない」**点(→ [dut-harness-design §3.2](dut-harness-design.ja.md))。エミュを全部 hardware に載せられるので、PIO を丸ごとキャプチャに使える。
- **X03x は SPI と I2C が 1 本ずつ**。SD エミュ(SPI slave)+ センサ群(I2C slave)+ target USART を同時にやるなら、PIOC を I2C slave に使い、**debug 線は CPU bit-bang に落とす**という逆の配分になる。どちらを hardware に置くかの選択が生じる。

### 2.5 E. アナログ — CH32X03x が最も強い

| board | ADC | 連続レート | 出力 | アナログトリガ |
|---|---|---|---|---|
| RP2040 | 4ch 12bit | **合計 500 kSa/s**(DNL に既知の癖) | DAC 無し → PWM+RC | ソフト判定 |
| RP2350 | 4ch(B は 8ch) | 同程度 | 同じ | ソフト判定 |
| ESP32-S3 | 2×SAR 12bit | **83.3 kSa/s**(soc_caps) | **DAC 無し**(ESP32 無印/S2 にはある) | ソフト判定 |
| **CH32X035** | **12bit 10–14ch** | 要確認 | DAC 無し → PWM | **OPA/PGA 2 群(可変ゲイン)+ CMP 3 群。CMP は TIM2 を直接トリガできる** |
| CH32X033 | 同じ | 要確認 | 同じ | CMP 2 群 |

**X03x の OPA/PGA + CMP は本物の利点**。電流シャント → PGA でゲイン → ADC が chip 内で完結し、しかも **CMP が TIM2 を直接叩ける = hardware アナログトリガ**が組める。RP2040/S3 にはこれが無く、外付け INA18x が必要になる。

一方 **ESP32-S3 のアナログは弱い**(83.3 kSa/s、DAC 無し)。深いデジタルキャプチャと引き換えに、電流プロファイルは粗くなる。

### 2.6 F. host 経路

| board | 経路 | 実効帯域 | driver | 備考 |
|---|---|---|---|---|
| RP2040/RP2350 | USB FS(CDC/HID/vendor) | ~1 MB/s | 標準 bind | 連続キャプチャは §2.3 の圧縮前提 |
| ESP32-S3 | USB OTG FS / USB-Serial-JTAG(`303A:1001`)/ **Wi-Fi** | ~1 MB/s / LAN | 標準 | **Wi-Fi があるのは S3 だけ** → 遠隔ベンチ・全ブラウザ対応(WebSocket) |
| CH32X035 | USB FS device | ~1 MB/s | 標準 | **software USB ではなく hardware USB**(V003 との決定的な差) |
| CH32X033 | USB FS device | 同じ | 標準 | PD/host 無し |

⚠ **ESP32-S3 の GPIO19/20 は USB(D−/D+)**。手元の常設 v2 はこの 2 本を peer 間結線に使っている([E003](../experiments/e003_smoke_peer/README.ja.md))ので、**その配線のままでは native USB も USB-Serial-JTAG も使えない**(E002 が CH340 経由だったのと整合)。S3 で USB を使う構成に移るなら 19/20 を空ける必要がある。

### 2.7 G. 電気 — 5 V target なら CH32X03x だけ

CH32X035/X033 は **VDD 2–5.5 V**。V003(3.3/5.0 V 動作)や 5 V 系の target を**レベル変換なしで直結できる**のはこの 2 つだけ。RP2040/RP2350/ESP32-S3 は 3.3 V で 5 V 非トレラントなので、[dmi-bridge §8.2](../protocols/dmi-bridge.ja.md) の「5 V board は SWIO のみ(open-drain だから直列抵抗で足りる)」の制約がそのままかかる。

### 2.8 RP2350 の errata E9 — LA 用途では致命的になりうる

**RP2350-E9**: GPIO を入力にして出力バッファを無効にすると、ピンが GND に戻らず **約 2.1–2.2 V でラッチする**。当初は内部 pull-down 使用時の問題とされたが、**内部 pull-down を使っていなくても起きる**ようデータシート要件が更新された。

- 回避策 1: **読む直前だけ入力バッファを有効化する** → **PIO プログラムからは使えない**(まさにキャプチャ用途が該当)。
- 回避策 2: **外部 pull-down を 8.2 kΩ 以下**にする(100 kΩ → 4.7 kΩ で解消の報告あり)。
- **16ch 全部が Hi-Z 入力になるロジアナ構成は、この errata の直撃コース**。

→ **RP2350 を採るなら、キャプチャ ch 全部に外部 pull-down を入れる前提で基板を設計する**。それが嫌なら RP2040。修正 revision の状況は購入時に要確認。

## 3. board 別の到達範囲

### 3.1 RP2040-Zero — 基準

| | 値 |
|---|---|
| 連番窓 | **GP0–GP15 = 16ch** |
| ADC | **GP26–29 = 4ch** |
| その他 | GP16 = WS2812、GP17–25 = 裏面パッド |
| 到達点 | 16ch キャプチャ + hw エミュ全部 + 4ch アナログ + PIO 8 SM を phy に回して余る |

割当案は [dut-harness-design §3.3](dut-harness-design.ja.md)。**「最小フットプリントで全軸が成立する」構成**。castellated なのでキャリア基板に直接載せられ、治具の量産に向く。

### 3.2 Raspberry Pi Pico(無印)— 窓が広い

| | 値 |
|---|---|
| 連番窓 | **GP0–GP22 = 23ch**(GP23=SMPS PS / GP24=VBUS sense / GP25=LED は内部) |
| ADC | GP26–28 = **3ch**(GP29 = ADC3 は VSYS/3 監視で埋まる) |
| その他 | **SWD ヘッダ 3 pin**(probe 自身のデバッグができる) |

**Zero より 7ch 広い**。[dut-harness-design §3.3](dut-harness-design.ja.md) の割当で足りなかった「第 2 SPI CS・第 2 I2C・観測予備」を全部入れられ、debug 線を窓の中に置いたまま app 側 16ch を確保できる。

**推奨用途**: 開発機。ピンが余るので配線を試行錯誤でき、SWD ヘッダで firmware 自体をデバッグできる。固まったら Zero に移す。

### 3.3 Pico W / Pico 2 / Pico 2 W / RP2350-Zero

| board | 連番窓 | ADC | 差分 |
|---|---|---|---|
| Pico W | GP0–GP22 = 23ch | GP26–28 = 3ch(ADC3 は CYW43) | **Wi-Fi**(GP23/24/25/29 が CYW43 で埋まる)→ 遠隔ベンチ・WebSocket |
| Pico 2 | GP0–GP22 = 23ch | 3ch | **RAM 520 KB / PIO 3 blk**。**E9** |
| Pico 2 W | 同じ | 3ch | 上記 + Wi-Fi。**E9** |
| RP2350-Zero | GP0–GP15 = 16ch | GP26–29 = 4ch | Zero と同形で RAM/PIO 倍。**E9** |

**RP2350 系の判断**: 命令メモリ(32/blk × 3)と RAM(520 KB)が欲しくなったときの逃げ道として押さえておく価値はあるが、**E9 の外部 pull-down 対策が前提**(§2.8)。

### 3.4 ESP32-S3 — 深さと Wi-Fi、ただしピンに注意

| | 値 |
|---|---|
| キャプチャ | **LCD_CAM DVP 8/16bit ≤40 MHz + GDMA**。**ピン連番不要** |
| 深さ | **PSRAM 8 MB 級**(帯域が律速。20 MSa/s 前後が現実線・要実測) |
| phy | **RMT 12.5 ns**(実測済み)/ DEDIC_GPIO |
| エミュ | UART×3、SPI2/SPI3 slave、I2C×2 slave |
| アナログ | **83.3 kSa/s、DAC 無し** — 弱い |
| host | USB OTG FS / USB-Serial-JTAG / **Wi-Fi** |
| 注意 | **GPIO19/20 = USB**。octal PSRAM なら **GPIO33–37 が潰れる** |

**到達点**: 「深いキャプチャ」と「遠隔」は S3 でしか出せない。**アナログと hardware エミュの多さは RP2040 に劣る**。

**手元の常設 v1/v2(S3 2 枚、GPIO19↔19 / 20↔20 直結)で今できること**:

- **キャプチャ側の実験は今日始められる**。LCD_CAM を DVP 入力として叩けるか、GDMA が PSRAM に落ちるか、実効レートは幾らか — これは target 無しで測れる(片方が信号源、片方がキャプチャ)。
- **phy 側は E005/E006 で既に測ってある**(送信 12.5 ns / 受信は CPU ループで 100 kbps 上限)。E005 の未決「速くしたい場合の手 (c) SPI slave で受ける」は、**LCD_CAM でも代替できる**可能性がある → 候補 `tool-fast-capture` の選択肢が 1 つ増える。
- **ただし 19/20 の結線は USB を塞ぐ**(§2.6)。USB transport の実験に移る前に配線を見直す判断が要る。

### 3.5 CH32X035 / CH32X033 — 最安・5 V・PIOC、ただし LA は無理

| | X035 | X033 |
|---|---|---|
| core / clock | QingKe V4C / 48 MHz | 同じ |
| Flash / SRAM | 62 K / 20 K | 同じ |
| GPIO | 11–60(パッケージ次第。LQFP48 で 46) | 18(TSSOP20) |
| **PIOC** | **あり**(2 ピン、2K 命令、48 MHz 単一サイクル) | **あり** |
| USB | FS device(大パッケージは **host** も)+ **USB PD/Type-C** | FS device のみ(**PD/host 無し**) |
| アナログ | 12bit ADC 10–14ch、**OPA/PGA 2、CMP 3(TIM2 直トリガ)** | ADC 10+1、OPA 2、**CMP 2** |
| DMA | **8ch**(TIMx/ADC/USART/I2C/SPI 対応) | 同じ |
| VDD | **2–5.5 V** | 同じ |

**到達点(現実的な線)**:

| 機能 | 可否 |
|---|---|
| DMI ブリッジ(flash / debug) | **◎** — PIOC が 1 線/2 線専用機構。hardware USB なので V003 の software USB 問題も無い |
| target USART の橋渡し | **◎** — USART が 4 本 |
| SPI slave(SD エミュ) | **○** — hardware SPI 1 本 |
| I2C slave(センサ 1 個) | **○** — hardware I2C 1 本。**複数アドレスは PIOC が要る = debug 線と排他** |
| 電流・電圧の観測 | **◎** — PGA + CMP で chip 内完結、hardware アナログトリガ |
| **並列キャプチャ(LA)** | **✗** — 専用機構が無く RAM も 20 KB。低速ストリーム decode が上限 |
| **5 V target 直結** | **◎** — この 2 つだけ |

→ **X03x の立ち位置は「LA を諦めた最安の bench probe」**。flash + monitor + 1 バス分のエミュ + アナログ観測までは届き、**5 V target と極安が要る場面では他に選択肢が無い**。加えて「CH32 が CH32 を書く」という bootstrap 上の意味がある([ecosystem-any-hardware](ecosystem-any-hardware.ja.md) の連鎖 bootstrap)。

### 3.6 参考: 対象外にした board

| board | 理由 |
|---|---|
| CH32V003 | software USB(low-speed HID、timing 制約)。**PIOC が無い**。初回書込に別 programmer が要る |
| Tiny2040 / XIAO RP2040 等 | 出ているピンが 11–12 本で窓が作れない |
| Arduino Uno(AVR) | 5 V SWIO の bootstrap 専用。キャプチャもエミュも成立しない([dmi-bridge §8.2](../protocols/dmi-bridge.ja.md)) |

## 4. 用途別の推奨

| 欲しいもの | 推奨 | 理由 |
|---|---|---|
| **標準構成・治具に量産** | **RP2040-Zero** | 最安・最安定・castellated。16ch + hw エミュ + 4ch ADC が同時成立 |
| **開発機(配線を試す)** | **Pico(無印)** | 23ch 窓 + SWD ヘッダ。固まったら Zero に移植 |
| **深い波形が要る / 遠隔** | **ESP32-S3** | PSRAM で MB 級、Wi-Fi。ピン連番不要 |
| **5 V target / 極安 / 数を揃える** | **CH32X035 or X033** | PIOC + hardware USB + 2–5.5 V。LA は諦める |
| **命令メモリ / RAM が足りなくなったら** | RP2350-Zero / Pico 2 | 同形で倍。**E9 対策の外部 pull-down が前提** |
| **X035 と X033 の選択** | 迷ったら **X033**(安い)。**USB PD か USB host が要るなら X035** | 差は PD/Type-C と host、CMP の数だけ |

## 5. 1 台に載せるか、2 台で分業するか

「深さ(S3)」と「アナログ + hardware エミュ(RP2040 / X03x)」は**別 chip の得意分野**なので、分業も選択肢になる。

```
        ┌──────────────┐  trigger 線(1 本)  ┌──────────────┐
host ───┤ RP2040-Zero  ├──────────────────────┤  ESP32-S3    │
        │ probe + エミュ│                      │ 深いキャプチャ│
        │ + アナログ    │                      │ + Wi-Fi      │
        └──────┬───────┘                      └──────┬───────┘
               └────────── target(1 枚)──────────────┘
```

- **時間軸は trigger 線 1 本で結ぶ**。両方が同じエッジを自分の時計で記録すれば、host 側で 2 つの時間軸を突き合わせられる(オフセットと drift を較正すればよい)。
- 得: 各軸で最良を取れる。**S3 の GPIO19/20 問題やアナログの弱さを回避できる**。
- 損: host が **2 台を同時に扱う**必要がある(= [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md) の話)。配線と設営が重くなる。

**1 台で足りるなら 1 台**。分業は「S3 の深さがどうしても要る」ときの手段として持っておく。

## 6. 未決(実測で決めること)

| # | 問い | 対象 board | 今日測れるか |
|---|---|---|:--:|
| 1 | **LCD_CAM を DVP 入力として叩き、GDMA で PSRAM に落とせるか。実効レートは** | ESP32-S3 | **○**(常設 v2 で target 不要) |
| 2 | **PIOC に SWIO / RVSWD を書けるか。`BIT_CYCLE` 支援はパルス幅符号化に使えるか** | X035/X033 | 実物次第 |
| 3 | PIOC 使用時に app が使える SRAM は幾らか | X035/X033 | 実物次第 |
| 4 | PIO I2C slave の多アドレス + clock stretch(命令数と最悪遅延) | RP2040 | 実物次第 |
| 5 | PL022 slave の実上限(SD の速度に間に合うか) | RP2040 | 実物次第 |
| 6 | X03x の TIM+DMA GPIO キャプチャの実効レートと深さ | X035/X033 | 実物次第 |
| 7 | E9 の影響と外部 pull-down の必要値 | RP2350 | 実物次第 |
| 8 | PSRAM(octal/quad)とキャプチャレートの実トレードオフ | ESP32-S3 | **○** |
| 9 | 2 台分業時の時間軸較正(オフセットと drift) | S3 + RP2040 | 両方揃えば |

**1 と 8 は target が要らないので、手元の常設 v2 だけで今日始められる**。[experiments/README.ja.md §3.1](../experiments/README.ja.md) の「機材が用意できることを確認してから採番する」に従うなら、この 2 つが最初の候補。

## 7. 参照

- 構想側(何をやりたいか・ピン割当・family 衝突表): [dut-harness-design.ja.md](dut-harness-design.ja.md)
- probe パターンの共存(書込のみ / 複数 / +LA): [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md)
- 汎用 probe と transport の比較: [generic-probe-design.ja.md](generic-probe-design.ja.md)
- protocol(caps で能力を申告する仕組み): [../protocols/dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md)
- 自 repo の実測: [E005 クロック同期キャプチャ](../experiments/e005_tool_clocked_capture/README.ja.md) / [E006 パルス幅生成・測定](../experiments/e006_tool_pulse_capture/README.ja.md) / [E003 peer 対の結線](../experiments/e003_smoke_peer/README.ja.md)
- 一次資料: CH32X035 RM 第 22 章(PIOC)/ CH32X035DS0(型番表)/ EVT `EXAM/PIOC/`(ASM 例)/ [RP2350 errata E9(Raspberry Pi 公式フォーラム)](https://forums.raspberrypi.com/viewtopic.php?t=375631) / [ESP32-S3 datasheet](https://cdn-shop.adafruit.com/product-files/5477/esp32-s3_datasheet_en.pdf) / [ESP-IDF ADC 連続モード](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/adc/adc_continuous.html) / [esp-hal lcd_cam::cam](https://docs.espressif.com/projects/rust/esp-hal/1.0.0-rc.1/esp32s3/esp_hal/lcd_cam/cam/index.html)
