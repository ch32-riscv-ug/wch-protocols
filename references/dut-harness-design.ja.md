# 1 target 専有の計測 probe(仮称 DUT harness)— ロジアナ + 周辺エミュを 1 台に同居させる

状態: **アイデアメモ**(実装・実測は未。数値はすべて概算で、**実測してから固める**)。名前も仮。

[dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md) の probe は **1 台で複数 target(lane)を相手にする**前提だった。本メモはその逆、**1 台を 1 target に専有させて、debug 線・周辺エミュレーション・ロジックキャプチャ・アナログを 1 つの時間軸に載せる**方向を検討する。target board は RP2040-Zero(同フットプリントの RP2350-Zero も候補)を想定し、LinkE の置き換えというより「LinkE では作れない開発環境」を狙う。

## 0. 結論(先に)

1. **RP2040-Zero は GP0–GP15 が連番でエッジに出ている**。PIO の `in pins, N`(base = GP0)がそのまま 16ch キャプチャになり、**ADC は窓の外(GP26–29)に固まっている**。「デジタル窓 = GP0-15 / アナログ = GP26-29」という分離が board 側から与えられる。
2. **エミュレーション側はほぼ全部ハードウェア周辺で足りる**。USB が host 側なので hw UART が 2 本空き、PL022 は slave mode を持ち、しかも既定ピン割当で **UART0=GP0/1・I2C1=GP2/3・SPI0=GP4-7・UART1=GP8/9 が同時に衝突なく成立**する。→ **PIO 8 SM と命令メモリ 32×2 が丸ごとキャプチャと debug phy に空く**。
3. **価値の中心はロジアナそのものではなく「同一時間軸」**。生波形・エミュ側の意味イベント・自分が出した DMI・target の print・電流を同じ時計で刻める。生波形が粗くても**意味イベントは推定でなく確定値**(エミュ側が知っているから)。
4. **見返りの本命は cross-domain trigger**。「I2C の reg 0x6B に 0x80 が書かれた瞬間に DMI で core を halt」。**バス事象を breakpoint にできる**のは、debug probe と周辺エミュが同じ 1 台だから。
5. **RVSWD/SWIO は窓に入れる方に分がある**。窓の上端(GP14/15)に置けば幅 14/16 で出し入れでき、他 bit の位置は動かない。**自分で出した信号を自分で見る**ことで [link-to-target.ja.md](../protocols/link-to-target.ja.md) の穴(SWIO のパルス幅閾値・RVSWD の STOP 波形)が埋まる。
6. **1 device 専有が正当化される最大の例が CH32V003**。20 pin に SWIO/USART/SPI/I2C を載せるとほぼ埋まり、そもそも複数 lane を張る余地がない(§6)。

## 1. なぜ 1 device 専有か(multi-lane との使い分け)

| | multi-lane probe([dmi-bridge](../protocols/dmi-bridge.ja.md)) | DUT harness(本メモ) |
|---|---|---|
| 相手 | 複数 target を並列に | **1 target を専有** |
| probe の責務 | DMI を運ぶだけ(chip を知らない) | DMI + **target の周辺を演じる** + 観測する |
| 効く場面 | 量産書込・多台 flash | **開発中の 1 枚を深く見る**・回帰試験・障害注入 |
| ピン | lane あたり 1–2 本 | **十数本を 1 target に集中** |
| 時間軸 | lane ごとに独立 | **全部 1 本** |

両立はしない。**resource(PIO SM・命令メモリ・DMA・ピン)を 1 target に集中投下するから成立する機能**なので、同じ firmware の別プロファイルとして排他に持つのが素直。

## 2. 何ができるようになるか

### 2.1 同一時間軸に載るもの

| 種別 | 出どころ | 精度 |
|---|---|---|
| (a) 生波形 | PIO キャプチャ(GP0–15) | サンプルレート依存(**粗くてよい**) |
| (b) **意味イベント** | エミュ側(「addr 0x68 の reg 0x3B を 6 byte read」) | **確定値**(自分が応答したから) |
| (c) DMI トランザクション | 自分が出した `dmi_read/write` | 確定値 |
| (d) target の print | SDI(DMDATA polling)/ USART([serial-and-print](../protocols/serial-and-print.ja.md)) | 確定値 |
| (e) アナログ | ADC(VDD 分圧・電流シャント) | ADC のサンプルレート依存 |

**(b) が確定値である**ことがこの構成の肝。普通のロジアナは波形から protocol を**復元**するので、サンプルレートが足りなければ decode に失敗する。エミュ側は自分が返した byte を知っているので、**波形が粗くても byte 列は正しい**。生波形は「いつ・どんな形で」を補うために持つ。

→ ユーザが言う「あらい精度でロジアナで拾える」は、この構成では**弱点にならない**。

### 2.2 cross-domain trigger(本命)

trigger の source と action を別ドメインから組める。

| source(何が起きたら) | action(何をする) |
|---|---|
| デジタルパターン/エッジ(GP0-15) | キャプチャ開始/停止 |
| **エミュバスの意味条件**(I2C addr+reg+value、SPI コマンド、UART パターン) | **DMI で core を halt** |
| DMI 側の条件(`autopoll_hit` 相当) | NRST パルス / 電源断 |
| ADC 閾値(電流スパイク・VDD 低下) | INT 線をアサート / 刺激シーケンス開始 |
| host からの手動 / 外部 trigger 入力 | 時刻マーク / trigger 出力 / host へ通知 |

「センサに変な値を書いた瞬間に core を止めて、レジスタとメモリを覗く」が 1 台でできる。**core に hardware watchpoint が無い周辺事象に breakpoint を張る**のと同じ効果で、これは LinkE + 外部ロジアナの組み合わせでは(手動同期を除いて)作れない。

### 2.3 障害注入

専有しているから「わざと壊す」ができる。

| 対象 | 注入できるもの |
|---|---|
| I2C | 指定 byte で NACK、clock stretch を T µs、SDA グリッチ、SCL 保持でバスハング |
| SPI | busy 応答、CRC 破壊、応答遅延、MISO 保持 |
| UART | framing error、break、byte 落とし |
| 電源 | trigger から T µs 後に VDD 切断(brown-out 試験) |
| リセット | バス事象に同期した NRST |

「SD カードが途中で応答しなくなったら firmware はどうなるか」を**再現可能に**作れる。

### 2.4 副産物

- **baud 自動判定**: target TX のエッジ間隔をキャプチャ側で測って baud を決める(host が掃引しなくてよい)。
- **SPI ディスプレイの中身が見える**: ST7789 等を演じて描画コマンドを解釈し、framebuffer を host に流せば、**物理的な液晶を繋がずに target の描画が見える**。
- **[link-to-target](../protocols/link-to-target.ja.md) の穴埋め**: §5 のとおり自己観測で SWIO/RVSWD の波形が取れる。

## 3. RP2040-Zero の実体とピン割当案

### 3.1 board の実ピン

| 場所 | ピン |
|---|---|
| エッジ 23 pin | **GP0–GP15**、**GP26–GP29**(ADC0–3)、5V(VSYS)、GND、3V3 |
| 基板上 | GP16 = 内蔵 WS2812 |
| 裏面はんだパッド(9) | GP17–GP25 |

出典: Waveshare wiki / TinyGo board 定義([§10](#10-参照))。**要現物確認**。RP2350-Zero も同フットプリントで ADC は同じ GP26–29。

### 3.2 RP2040 の既定ピン割当(関係する範囲)

| 周辺 | GP0-15 内で取れる組 |
|---|---|
| UART0 TX/RX | GP0/1、GP12/13 |
| UART1 TX/RX | GP4/5、GP8/9 |
| I2C0 SDA/SCL | GP0/1、GP4/5、GP8/9、GP12/13(偶数 = SDA) |
| I2C1 SDA/SCL | GP2/3、GP6/7、GP10/11、GP14/15 |
| SPI0 RX/CSn/SCK/TX | GP0-3、GP4-7 |
| SPI1 RX/CSn/SCK/TX | GP8-11、GP12-15 |

(RP2040 datasheet の GPIO function 表より。実装時に再確認)

**UART0=GP0/1・I2C1=GP2/3・SPI0=GP4-7・UART1=GP8/9 は互いに衝突しない**。これが §0-2 の根拠。

### 3.3 割当案

| GP | 役割 | 窓 | 実現 |
|---:|---|:--:|---|
| 0/1 | target USART ⇄(TX/RX 交差) | ○ | hw UART0 |
| 2/3 | I2C SDA/SCL(複数アドレス slave) | ○ | hw I2C1 または PIO |
| 4/5/6/7 | SPI MOSI / CS / SCK / MISO(slave) | ○ | hw SPI0(PL022 slave) |
| 8/9 | 第 2 UART(target USART2 / ISP) | ○ | hw UART1 |
| 10 | INT/DRDY(エミュ → target 割込) | ○ | GPIO |
| 11 | NRST | ○ | GPIO |
| 12/13 | 汎用観測 / 第 2 CS / trigger 出力 | ○ | 予備 |
| **14/15** | **SWDIO(SWIO) / SWCLK** | △ | 幅 16 のときだけ窓に入る(§5) |
| 16 | status(WS2812) | — | 基板上 |
| 26/27/28 | ADC: VDD 分圧 / 電流 / 汎用アナログ | — | ADC0-2 |
| 29 | PWM-DAC(アナログ刺激) | — | PWM + RC |
| 裏 17-25 | power-EN(FET)、trigger 入力、外付け DAC 用 master バス、mode strap | — | はんだパッド |

**窓に入れる優先順位**: (1) target が駆動する線 → (2) 双方向 → (3) Pico が駆動する線。**Pico が駆動したものは意味イベントとして既に正確に分かっている**ので、窓が足りなければ最初に落とす。この原則があると、ピンが足りない board へ移すときに何を捨てるかが自動的に決まる。

電源制御(power-EN)を裏面に逃がしているのはこの原則による。ただし「エミュが実際にピンを叩いた時刻」と「firmware がそう指示した時刻」がずれる可能性はあるので、ずれが問題になるなら窓へ戻す(要実測)。

### 3.4 電気

- RP2040 の GPIO は **5V トレラントではない**。target が 5V 動作(V003 は 3.3/5.0V)なら分圧かレベル変換が要る。[dmi-bridge §8.2](../protocols/dmi-bridge.ja.md) の「5V board は SWIO のみ」と同じ問題が、エミュ線にも全部かかる。
- target 向きの全線に直列抵抗(数百 Ω〜1 kΩ)を入れるのを既定にする。競合(両側が同時に駆動)で焼かないため。
- I2C は open-drain なので pull-up の所在(target 側 / Pico 側 / 両方)を申告できるようにしたい。

## 4. 資源の見積り(**すべて概算・要実測**)

### 4.1 PIO

- RP2040: **2 ブロック × 4 SM**。命令メモリは**ブロックあたり 32 命令を 4 SM で共有** → 詰まるのは SM 数ではなく**命令メモリ**。
- RP2350: 3 ブロック × 4 SM = 12 SM、RAM 520 KB。**同フットプリント(RP2350-Zero)で逃げられる**。
- `in pins, N` は IN base から連番 N 本。**入力マッピングは出力と独立** → phy SM が GP14/15 を駆動しながら、キャプチャ SM が同じピンを読める(§5 の前提)。

想定配置(§0-2 が成立する場合):

| ブロック | 用途 |
|---|---|
| PIO0 | キャプチャ(+ trigger / RLE)。命令の余裕を最も要る側に割く |
| PIO1 | RVSWD/SWIO phy、WS2812、(必要なら)PIO I2C slave |

**唯一の難所は「1 本の I2C に複数センサ」**。hw I2C は 1 ブロック 1 アドレスなので、複数アドレスを演じるには PIO I2C slave(アドレス byte で IRQ → clock stretch 中に CPU が ACK 可否を決める)が要る。逃げ道:

1. hw I2C0 と I2C1 を別ピンに出して**外部で結線** → 純ハードで 2 アドレス(予備ピンを 2 本食う)
2. PIO I2C slave を書く(命令数と最悪応答遅延が未知。**要実測**)
3. RP2350-Zero にする

### 4.2 キャプチャの深さと速度

16ch = 2 B/sample、8ch = 1 B/sample。RAM 264 KB(実際に使えるのは 200 KB 程度)。

| 構成 | 深さ / 帯域(概算) |
|---|---|
| 16ch @ 100 MSa/s | 約 1 ms |
| 16ch @ 10 MSa/s | 約 10 ms |
| 8ch @ 1 MSa/s | 約 200 ms |
| USB FS 実効 ~1 MB/s | 連続なら 16ch で ~500 kSa/s / 8ch で ~1 MSa/s(無圧縮) |

→ **burst(trigger + RAM 深掘り → 後で吸い上げ)と continuous(RLE/エッジ時刻の圧縮ストリーム)の 2 モード**が要る。I2C 400 kHz や UART は continuous で足り、SPI の速い側と SWIO の波形は burst。

圧縮とトリガ判定は **core1** に置き、core0 を USB とエミュの応答に空けるのが素直(逆でもよい。エミュの割込遅延が支配的なら要検討)。

### 4.3 時間軸

**未決の中で一番大事**。キャプチャの sample index が最も細かい原器だが、意味イベント・DMI・ADC は CPU 側の時計で刻む。RP2040 の timer は 1 µs 刻みなので、10 MSa/s 以上のキャプチャとは直接対応が取れない。

案: DMA の転送境界で「sample index ↔ µs timer」の対応点を打ち、その間は線形内挿する。誤差の実測が要る。

## 5. RVSWD/SWIO を窓に入れるか

3 案。

| 案 | 内容 | 得失 |
|---|---|---|
| **A(推奨)** | **窓の上端 GP14/15** に置き、幅 14/16 で出し入れ | 幅 14 なら debug 線だけ落ち、**他 bit の位置は動かない**。使うときだけ 16 に広げる。コストは実質ゼロ |
| B | 窓外(裏面パッド)へ | 16ch 全部をアプリ信号に使える。代わりに**自己観測ができない** |
| C | A + debug 専用の第 2 キャプチャ unit | app 窓 14ch を低速連続、debug 2ch を高速バーストで並走。SM・DMA・RAM を追加で食う |

**A に分がある**。理由は [link-to-target.ja.md](../protocols/link-to-target.ja.md) 末尾の未解決項目(**SWIO の LOW パルス幅の 0/1 閾値**、**RVSWD の STOP 波形・クロック周波数・トランザクション間アイドル**)が、**自分で出した信号を自分で見れば埋まる**から。この 1 台が「線層を attested → verified に上げる測定器」を兼ねる。

C は A の上位互換なので、仕様側は「**capture unit の数を caps が申告する**」形にしておけば、1 unit 実装と 2 unit 実装が同じ仕様で共存できる([dmi-bridge §0](../protocols/dmi-bridge.ja.md) の設計原則 2 と同じ扱い)。

## 6. エミュレーションの知識境界

[dmi-bridge](../protocols/dmi-bridge.ja.md) の設計原則 1 は「**probe は chip を知らない**」だった。エミュレーションはその原則をどう扱うかを決める必要がある。**「probe はセンサも知らない。器だけ持ち、中身は host が流し込む」**が原則の自然な延長。

| 層 | probe が持つ | host が持つ |
|---|---|---|
| 器 | register file、block device、byte stream、INT 線、遅延/NACK の注入点 | — |
| 中身 | — | センサの reg map、SD の image、応答スクリプト |

演じたいものの例:

| 対象 | 器 | 備考 |
|---|---|---|
| SD カード(SPI mode) | block device | CMD0/8/55/41/58/17/24、CRC7/16。**busy トークンで待たせられる**ので host backed が成立する |
| 25 系 SPI NOR | block device | 0x03/0x02/0xD8/0x9F。外部 flash から起動する target 用 |
| 24Cxx EEPROM | register file | clock stretch で待たせられる |
| 汎用センサ | register file + INT | reg 読出の振る舞い(固定 / 自動 increment / キューから pop)を host が指定 |
| SPI ディスプレイ | byte stream + 解釈 | §2.4 |

**host backed の可否は「待たせる手段があるか」で決まる**。SD は busy トークン、I2C は clock stretch で待てるので、512 B ブロックを USB 往復(1–2 ms)で取りに行っても target 側の timeout(数百 ms)に収まる。**UART は待たせる手段が無い**ので事前ロードが必要。

## 7. アナログ(ADC/DAC)

RP2040 に **DAC は無い**。ADC は 4ch(GP26–29)、round-robin 合計 500 kSa/s、DNL に既知の癖がある。

| ピン | 案 |
|---|---|
| GP26 (ADC0) | target VDD の分圧 → 立ち上がり・brown-out がキャプチャと同じ時間軸に載る |
| GP27 (ADC1) | 電流(シャント + INA18x)→ **「この転送のとき何 mA」**。低消費電力系(L103)で効く |
| GP28 (ADC2) | 汎用入力(target 自身の DAC 出力の観測など。§8 の V307/V407) |
| GP29 | **PWM + RC で疑似 DAC 出力**(8bit/488 kHz、10bit/122 kHz 目安)。サーミスタ/ポテンショの模擬 |

本物の分解能が要るなら外付け MCP4725(I2C)/ MCP4921(SPI)。ただし **Pico が I2C slave を演じている線は master に使えない**ので、外付け DAC 用の master バスは裏面パッド側に別に立てる。

ADC を 4ch 全部観測に回して DAC を全部外付けにする案もある。**どちらにするかは、電流測定に何 ch 要るか(ハイサイド/ローサイド、複数レール)を実測してから決める。**

## 8. CH32 family 側の配線と衝突

**生成物**。一次データは [`ch32-device-data`](https://github.com/openwch/ch32-device-data) の **`index/`**、スクリプトと出力は [`data/harness-wiring/`](data/harness-wiring/README.ja.md)。**手書きしない**。

`ch32-device-data` は 3 層に分かれていて(`docs/data-layout.ja.md`)、**引くのは `index/`**(`evidence/` から `build_index.py` が組み直し、`check_tables.py` が「索引の行は証拠に戻せる」ことを毎回検証している層)。初版は `evidence/` を読んでいて網羅も confidence も劣っていた(§8.3)。

| 項目 | 値 | 出典 / confidence |
|---|---|---|
| debug 線(1 線 / 2 線 / 両対応 + pad) | **27 series** | `index/debug_interfaces.csv` — **`confirmed`**(各 DS + WCH-Link User Manual、章・ページつき) |
| 役割 → pad の route | **5,243 組 / 103 型番** | `index/pinout.csv` — **24,828 行が `confirmed`**(pin table zh+en)。uart/spi/i2c/pioc/clock/analog/timer に絞って抽出 |
| pad の衝突 | **1,118 組**(うち **debug 絡み 47 組**) | 上記からの導出 |

**穴は無い。27 series すべてに uart/spi/i2c の route がある。**

### 8.1 debug 線と、その pad に来る他の役割

| series | wire | SWDIO/SWIO | SWCLK | debug pad と食い合う役割 |
|---|---|---|---|---|
| CH32H415 | **1/2 線** | PB9 | PB8 | **PB8**: I2C1.SCL, I2C4.SCL, PIOC.IO0, TIM10.CH3, TIM4.CH3, USART6.RX<br>**PB9**: I2C1.SDA, I2C4.SDA, I2C4.SMBA, PIOC.IO1, SPI2.NSS, TIM10.CH4, TIM4.CH4, USART6.TX |
| CH32H416 | **1/2 線** | PB9 | PB8 | **PB8**: I2C1.SCL, I2C4.SCL, PIOC.IO0, TIM10.CH3, TIM4.CH3, USART6.RX<br>**PB9**: I2C1.SDA, I2C4.SDA, I2C4.SMBA, PIOC.IO1, SPI2.NSS, TIM10.CH4, TIM4.CH4, USART6.TX |
| CH32H417 | **1/2 線** | PB9 | PB8 | **PB8**: I2C1.SCL, I2C4.SCL, PIOC.IO0, TIM10.CH3, TIM4.CH3, USART6.RX<br>**PB9**: I2C1.SDA, I2C4.SDA, I2C4.SMBA, PIOC.IO1, SPI2.NSS, TIM10.CH4, TIM4.CH4, USART6.TX |
| CH32L103 | rvswd | PA13 | PA14 | **PA13**: I2C1.SCL, TIM1.BKIN, TIM1.ETR, USART1.RTS<br>**PA14**: TIM1.CH1N, TIM1.CH3, USART1.CTS |
| CH32M007 | **1/2 線** | PD1 | PB3 | **PB3**: CMP1.P1, I2C1.SCL, SPI1.MISO, TIM1.BKIN, TIM2.CH2, USART1.RX, USART1.TX, USART2.RTS, USART2.RX<br>**PD1**: ADC1.IETR, I2C1.SCL, I2C1.SDA, OPA.P3, TIM1.CH3N, TIM1.CH4, USART1.RX, USART1.TX, USART2.RX |
| CH32M030 | **1/2 線** | PA3 | PA2 | **PA2**: ADC1.IN15, I2C1.SCL, SPI1.NSS, TIM3.CH1_ETR, USART1.CTS, USART1.RX, USART1.TX<br>**PA3**: ADC1.IN16, CMP3.P0, I2C1.SDA, TIM2.CH1_ETR, USART1.RX, USART1.TX |
| CH32M103 | rvswd | PA13 | PA14 | **PA13**: I2C1.SCL, TIM1.BKIN, TIM1.ETR, USART1.RTS<br>**PA14**: TIM1.CH1N, TIM1.CH3, USART1.CTS |
| CH32V002 | swio | PD1 | — | **PD1**: ADC1.IETR, I2C1.SCL, I2C1.SDA, TIM1.CH3N, TIM1.CH4, USART1.RX, USART1.TX |
| CH32V003 | swio | PD1 | — | **PD1**: ADC1.IETR, I2C1.SCL, TIM1.CH3N, USART1.RX |
| CH32V004 | swio | PD1 | — | **PD1**: ADC1.IETR, I2C1.SCL, I2C1.SDA, TIM1.CH3N, TIM1.CH4, USART1.RX, USART1.TX |
| CH32V005 | **1/2 線** | PD1 | PB3 | **PB3**: I2C1.SCL, SPI1.MISO, TIM1.BKIN, USART1.RX, USART1.TX, USART2.RTS, USART2.RX<br>**PD1**: ADC1.IETR, I2C1.SCL, I2C1.SDA, OPA.P3, TIM1.CH3N, TIM1.CH4, USART1.RX, USART1.TX, USART2.RX |
| CH32V006 | **1/2 線** | PD1 | PB3 | **PB3**: I2C1.SCL, SPI1.MISO, TIM1.BKIN, USART1.RX, USART1.TX, USART2.RTS, USART2.RX<br>**PD1**: ADC1.IETR, I2C1.SCL, I2C1.SDA, OPA.P3, TIM1.CH3N, TIM1.CH4, USART1.RX, USART1.TX, USART2.RX |
| CH32V007 | **1/2 線** | PD1 | PB3 | **PB3**: CMP1.P1, I2C1.SCL, SPI1.MISO, TIM1.BKIN, TIM2.CH2, USART1.RX, USART1.TX, USART2.RTS, USART2.RX<br>**PD1**: ADC1.IETR, I2C1.SCL, I2C1.SDA, OPA.P3, TIM1.CH3N, TIM1.CH4, USART1.RX, USART1.TX, USART2.RX |
| CH32V103 | rvswd | PA13 | PA14 | — |
| CH32V203 | rvswd | PA13 | PA14 | **PA13**: I2C1.SDA, TIM1.CH1N, TIM2.CH3, USART3.TX<br>**PA14**: I2C1.SCL, TIM1.CH2N, TIM2.CH4, USART3.RX, USART4.TX, USART8.TX |
| CH32V205 | **1/2 線** | PA13 | PA14 | **PA13**: I2C1.SDA, TIM1.CH1N, TIM2.CH3, USART3.TX<br>**PA14**: I2C1.SCL, TIM1.CH2N, TIM2.CH4, USART3.RX, USART4.TX, USART8.TX |
| CH32V208 | rvswd | PA13 | PA14 | — |
| CH32V303 | rvswd | PA13 | PA14 | **PA13**: TIM10.CH2N, TIM8.CH1N, USART3.TX<br>**PA14**: TIM10.CH3N, TIM8.CH2N, USART3.RX, USART8.TX |
| CH32V305 | rvswd | PA13 | PA14 | **PA13**: TIM10.CH2N, TIM8.CH1N, USART3.TX<br>**PA14**: TIM10.CH3N, TIM8.CH2N, USART3.RX, USART8.TX |
| CH32V307 | rvswd | PA13 | PA14 | **PA13**: TIM10.CH2N, TIM8.CH1N, USART3.TX<br>**PA14**: TIM10.CH3N, TIM8.CH2N, USART3.RX, USART8.TX |
| CH32V317 | rvswd | PA13 | PA14 | **PA13**: TIM10.CH2N, TIM8.CH1N, USART3.TX<br>**PA14**: TIM10.CH3N, TIM8.CH2N, USART3.RX, USART8.TX |
| CH32V407 | **1/2 線** | PA13 | PA14 | **PA13**: TIM8.CH1N, USART3.TX, USART4.CTS, USART6.CK<br>**PA14**: TIM8.CH2N, USART3.RX, USART6.CTS, USART8.TX |
| CH32V467 | **1/2 線** | PA13 | PA14 | **PA13**: TIM8.CH1N, USART3.TX, USART4.CTS, USART6.CK<br>**PA14**: TIM8.CH2N, USART3.RX, USART6.CTS, USART8.TX |
| CH32X033 | rvswd | PC18 | PC19 | **PC18**: I2C1.SCL, I2C1.SDA, PIOC.IO0, TIM1.ETR, TIM2.CH1N, TIM3.CH2, USART3.TX<br>**PC19**: CMP1.P0, I2C1.SCL, I2C1.SDA, PIOC.IO1, TIM2.CH1, TIM3.CH1, USART3.RX, USART4.RX |
| CH32X035 | rvswd | PC18 | PC19 | **PC18**: I2C1.SCL, I2C1.SDA, PIOC.IO0, TIM1.ETR, TIM2.CH1N, TIM3.CH2, USART3.TX<br>**PC19**: CMP1.P0, I2C1.SCL, I2C1.SDA, PIOC.IO1, TIM2.CH1, TIM3.CH1, USART3.RX, USART4.RX |
| CH32X305 | **1/2 線** | PA13 | PA14 | **PA13**: I2C1.SDA, SPI1.SCK, TIM1.CH1N, USART2.RX, USART3.TX<br>**PA14**: I2C1.SCL, SPI1.MOSI, TIM1.CH2N, USART2.TX, USART3.RX |
| CH32X315 | **1/2 線** | PA13 | PA14 | **PA13**: I2C1.SDA, SPI1.SCK, TIM1.CH1N, USART2.RX, USART3.TX<br>**PA14**: I2C1.SCL, SPI1.MOSI, TIM1.CH2N, USART2.TX, USART3.RX |

### 8.2 harness としての読み方

1. **1/2 線を選べることが「使える pad を選べる」ことになる**(13 series が両対応: H415, H416, H417, M007, M030, V005, V006, V007, V205, V407, V467, X305, X315)。**1 線のみは  だけ**。
   例: **V005/V006/V007/M007 は 2 線だと SWCLK = PB3 が SPI1.MISO と食い合う**が、**1 線なら PB3 が空く**。SPI slave エミュと debug の同時成立は「線の本数を選ぶ」ことで解ける。
2. **X033 / X035 の PC18/PC19 が最悪**。**PIOC.IO0/IO1 = DEBUG.SWDIO/SWCLK = I2C1.SCL/SDA** が同じ 2 本に乗り、PC19 は **9 役**(CMP1.P0 / TIM2.CH1 / TIM3.CH1 / USART3.RX / USART4.RX も)。→ **PIOC を probe の phy に使うと、その chip 自身の debug port と I2C1 が同時に潰れる**。
3. **X315 は SPI1.SCK/MOSI が PA13/PA14 = debug と衝突**。V205 は I2C1.SDA/SCL が同じ場所。**2 線系だから安全、とは言えない**。
4. **harness に最も素直なのは V103 / V203 / V208**(debug pad の衝突 0)。次が V303/V305/V307/V317(USART3 + UART8 の remap 先だけ)。
5. **V003 の PD1(SWIO)には I2C1.SCL / USART1.RX / TIM1.CH3N / ADC1.IETR が乗る**。1 線しか無いので逃げ道が無い — §1 の「1 device 専有」がここで具体的な形になる。

### 8.3 初版(EVT `@Note` grep)の検証 — **4 件のうち 2 件が誤り**

初版は EVT サンプルのコメントから 4 つの衝突を挙げていた。`index/pinout.csv`(`confirmed`)と突き合わせた結果:

| 初版の主張 | 検証結果 |
|---|---|
| **V003**: `SPI1.NSS = I2C1.SDA = PC1` | ✅ **確認**(+ TIM1.BKIN / TIM2.CH1_ETR / TIM2.CH4 / USART1.RX) |
| **V307 / V407**: `DAC1.OUT = SPI1.NSS = PA4` | ✅ **確認**(+ ADC1.IN4 / SPI3.NSS / TIM9.CH3 / USART2.CK) |
| **X035**: `I2C1.SCL(PA10)` が `USART1.RX(PA10)` と衝突 | ⚠ **方向が誤り**。PA10 は **I2C1.SCL(default) + USART1.TX(remap-1)** + SPI1.MOSI(remap-2) + USART4.RX(remap-3)。**USART1.RX の default は PB11**(PA11 が remap-1)。衝突自体は存在する |
| **M030**: `USART1.TX(remap) = I2C1.SDA = PC1` | ❌ **取り下げ**。M030 の PC1 は USART1.TX と TIM のみで、**I2C1 は PC1 に出ない**(default PB2/PB3、remap-1 PB6/PA2、remap-2 PA14/PA15、remap-3 PA3/PA2) |

**誤った 2 件はどちらも「EVT のコメントが他 series からコピペされていた」形**に見える(M030 に書かれていた `I2C1_SCL(PC2)/I2C1_SDA(PC1)` は V003 の配置)。→ **EVT の `@Note` を一次情報にしてはいけない**、が実例で確定した。

## 9. 未決事項(実測してから決める)

1. **PIO I2C slave で複数アドレス + clock stretch が成立するか**。命令数と最悪応答遅延。→ 成立しなければ hw I2C 2 ブロック外部結線 or RP2350。
2. **PL022 slave の実上限**(概算 sysclk/12)。SD の初期化 400 kHz → 通常速度で、target 側 stack が使う周波数に間に合うか。
3. **16ch キャプチャの実効サンプルレートと USB 実効帯域**(RLE 有無で何倍か)。§4.2 の概算を実値に置換。
4. **時間軸の刻み方**(sample index ↔ µs timer の対応、DMA 境界での誤差)。§4.3。
5. **GP14/15 の自己観測で [link-to-target](../protocols/link-to-target.ja.md) の穴が埋まるか**(SWIO パルス幅閾値、RVSWD の STOP 波形・クロック周波数)。
6. **電流測定の分解能**(シャント値・INA gain・ADC の DNL)。何 ch 要るか。→ §7 の ADC/DAC 配分がこれで決まる。
7. **RP2040 で足りるか、RP2350-Zero が要るか**(命令メモリと RAM)。
8. **target 5V 時の扱い**(直列抵抗だけで済む線と、レベル変換が要る線の切り分け)。
9. **エミュが駆動した時刻と firmware の指示時刻のずれ**。大きければ §3.3 の「Pico が駆動する線は窓から落とす」原則を見直す。
10. **名前**。DUT harness / bench probe / DUT scope など仮。[dmi-bridge §8.1](../protocols/dmi-bridge.ja.md) に既に `Bench` プロファイルがあるので、`bench` は避けた方がよいかもしれない。
11. ~~`ch32-device-data` の route データの穴~~ → **穴は無かった**。`index/pinout.csv` に 27 series 全部ある(初版が `evidence/` を読んでいたのが原因。[data/harness-wiring/](data/harness-wiring/README.ja.md) 参照)。
12. **`debug_if = both` の series で 1 線モードに入る手順**(option byte / レジスタ)。`debug_interfaces.csv` は「両対応」までしか言わない。→ [custom-bootloader §2a](../protocols/custom-bootloader.ja.md) の切替レジスタと突き合わせる。
12b. **MCO をキャプチャ窓に入れて「絶対時間」を「比率」に落とすか**。DUT の MCO(`class = clock`、`pinout.csv` にあり)を同じ窓で撮れば、UART の bit 幅などを **DUT の実コア clock 何周期分**として測れる。HSI ±1% と probe の水晶 ±30 ppm を分離できるので、**timing tolerance の決め方が「両者の誤差を分ける」問題から「比率を測る」問題になる**。1ch 消費。
13. **[link-to-target](../protocols/link-to-target.ja.md) §1 の「1/2 線 切替可」の列挙に V205 / V407 / V467 / X305 / X315 / H41x を反映するか**。`debug_wiring.csv` は `confirmed` で 15 series を挙げている(§8.3-1)。→ protocol 側の記述変更なので別判断。
14. **窓に NRST を入れるか**。ArduinoCore-CH32 側の論理信号は 13 本(`MARKER` / `GPIO_OUT` / `INT_IN` / `PWM` / `UART_TX` / `UART_RX` / `I2C_SCL` / `I2C_SDA` / `SPI_SCK` / `SPI_MOSI` / `SPI_MISO` / `SPI_CS` / `MCO`)で、**debug 2 本 + NRST = 16 でちょうど埋まり空きゼロ**。§3.3 の優先順位規則では NRST(Pico 駆動)は窓から落とす候補だが、**測定原点に reset 時刻を使いたい要求**があるので衝突する。→ **Pico(23ch 窓)なら消える**。

## 9b. 仕様をどこに置くか(**まだ決めない**)

実測で形が固まってからでよいが、選択肢だけ挙げておく。

| 案 | 内容 |
|---|---|
| A | **別 protocol doc** を `protocols/` に立て、L1 datagram / L2 ヘッダ / `hello` / `caps` は `dmibridge` をそのまま共有。cmd 範囲と caps flag だけ dmi-bridge 側に予約を書き足す。dumb probe は小さいまま、計測 probe が opt-in で申告する |
| B | **dmi-bridge に節を追加**して 1 仕様に統合。読む場所は 1 つで済むが、「probe は chip を知らない」小ささが薄まる |
| C | **今のまま memo だけ**。byte 仕様は実測後 |

いずれにせよ効いてくる点:

- **キャプチャストリームは control channel と分けたい**(第 2 CDC interface か bulk EP)。`max_inflight = 1` の応答性を保ったまま bulk を流すため。
- **キャプチャは再送できない**([dmi-bridge §2.1](../protocols/dmi-bridge.ja.md) の「化けたものは届かない → 上位が再送で回復」が成り立たない)。落とした量を数えて申告する形(§4.4 の `uart_data` の `dropped` と同じ idiom)+ 連番が要る。
- **既存ツールとの day 1 互換**を [dmi-bridge §7](../protocols/dmi-bridge.ja.md) の ardulink 互換モードと同じ発想で用意できる。キャプチャ側なら SUMP/OLS を喋れば PulseView が初日から使える。

## 10. 参照

- **board 別にどこまで届くか**(RP2040 / RP2350 / ESP32-S3 / CH32X035 / X033 の 8 軸比較): [harness-board-survey.ja.md](harness-board-survey.ja.md)
- **書込のみ / 複数 target / 書込+LA を 1 仕様に載せられるか**: [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md)
- 親の設計メモ(汎用 probe と PC 連携): [generic-probe-design.ja.md](generic-probe-design.ja.md)
- 多 lane 側の protocol 仕様: [../protocols/dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md)
- 自己観測で埋めたい穴(SWIO パルス幅 / RVSWD STOP 波形): [../protocols/link-to-target.ja.md](../protocols/link-to-target.ja.md)
- target 側 print(時間軸に載せるもの (d)): [../protocols/serial-and-print.ja.md](../protocols/serial-and-print.ja.md)
- **§8 の生成データと抽出スクリプト**: [data/harness-wiring/](data/harness-wiring/README.ja.md)(一次データ = `ch32-device-data/evidence/{debug_wiring,remap_routes}.csv`)
- series 差の扱い方の先例: [bootloader-survey.ja.md](bootloader-survey.ja.md)
- 実測の規則(計画 → 実行 → レポート): [../experiments/README.ja.md](../experiments/README.ja.md) / 台帳 [../experiments/LEDGER.ja.md](../experiments/LEDGER.ja.md)
- board 実ピンの出典: [Waveshare RP2040-Zero wiki](https://www.waveshare.com/wiki/RP2040-Zero) / [TinyGo waveshare-rp2040-zero](https://tinygo.org/docs/reference/microcontrollers/machine/waveshare-rp2040-zero/) / [Waveshare RP2350-Zero wiki](https://www.waveshare.com/wiki/RP2350-Zero)
