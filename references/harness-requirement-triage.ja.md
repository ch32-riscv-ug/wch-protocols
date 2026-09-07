# 各要望の個別判定 — 無理なく対応できるか

状態: **判定**(2026-09-07)。**順番は規定しない。** 「無理なく対応できる」= **基本は対応予定**。「難しい」に落ちたものは**個別に判定する**。
対象: コア `docs/harness-requirements.ja.md` の **`H-001`〜`H-191`(全 143 件)**。ベンチの `B-001`〜`B-007` は **H-185〜H-191 に取り込まれている**ので同時に判定済み。

判定の基準は [harness-existing-standards.ja.md](harness-existing-standards.ja.md) §6(限界コスト)と §1(既存 + ch32rv でどこまで)。

## 0. 判定語彙と結果

| 判定 | 意味 | 件数 |
|---|---|---:|
| **済** | **既存 + ch32rv でカバーできている**。我々は何もしない | **11** |
| **A** | **ほぼゼロで対応**。host 側 / 契約を書くだけ / `caps` の項目 / 既存の枠に入る | **47** |
| **B** | **小さな追加で対応**。firmware 側だが安い(hw 周辺・GPIO・PIO 1 本・部品 1 個) | **23** |
| **C** | **core そのもの**。作るなら当然入る | **14** |
| **D** | **条件付き**。board / 上の段(capture) / 外付け部品 / PID 方針 が前提 | **28** |
| **E** | **無理あり → 個別判定** | **21** |
| **F** | **範囲外**(他プロジェクト / 運用) | **3** |

> **`済` + `A` + `B` + `C` = 91 件(63%)が「無理なく対応」。**
> `D` が 28 件、`E` が 21 件、`F` が 3 件。**複合判定(`A/D` `A/E`)が 4 件**あり、これは「**`caps` の項目として持つのは A、実装は D/E**」という形。

**読み方の注意**: `D` は「できない」ではなく「**前提を先に決めれば無理はない**」。前提は 4 種類しかない(§3)。

## 1. 判定一覧


### A. 識別・排他・運用

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-001 | 一意で安定した USB serial | **A** | RP2040 の flash unique ID を読むだけ |
| H-002 | 種別/版/capability を machine-readable | **C** | `caps` があれば自動的に満たす |
| H-003 | per-device advisory lock | **済** | host 側。ch32rv の `flock`/exit 13 を踏襲 |
| H-004 | 1 台 = USB device 1 個(composite) | **D** | **descriptor が固まる = PID 方針に触る**。技術的には TinyUSB で普通 |
| H-005 | exit code / JSON envelope を揃える | **済** | host 側。`ch32rv-contract` |
| H-006 | 候補複数なら fail closed | **済** | host 側 |
| H-007 | `board-identify` に載る | **A** | descriptor に serial があれば OS 側から引ける |
| H-008 | 常時接続台数の上限を運用に織り込む | **済** | host / 運用側 |
| H-009 | 複数 DUT(lane/mux)の余地 | **B** | `lane` は既にヘッダにある。SM を増やすだけ |


### B. 能力宣言と束縛

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-010 | channel は番号のみ。論理名を probe に入れない | **C** | core の設計方針そのもの |
| H-011 | 機能集合・排他・連番制約を申告 | **C** | `caps` の中身 |
| H-012 | 数値上限はすべて `caps` 側 | **C** | dmi-bridge 設計原則 2 に既にある |
| H-013 | `configure` は原子的 | **C** | 「構成集合を宣言して選ぶ」形なら自明に成立 |
| H-014 | `caps` と実装の食い違いで `configure` が落ちる | **C** | 同上 |
| H-015 | 器と模型を `caps` に並べて申告 | **A**/**E** | **caps 項目 = A、模型の実装 = E(L7)** |
| H-016 | capture unit 数を `caps` が申告 | **A**/**D** | caps 項目 = A、capture 実装 = D |
| H-017 | 時間軸の source / 確度 / 対応誤差 | **A**/**D** | 同上 |
| H-018 | attach で書くものを `caps` に出す | **A** | passive attach なら「空」と申告するだけ |
| H-019 | pull-up の所在・抵抗・電圧をベンチ側から宣言 | **A** | ベンチ側の宣言 = host 側 |
| H-020 | 本物のデバイスも同じ語彙で宣言 | **A** | host 側の語彙 |
| H-021 | 束縛が 2 通り以上なら fail closed | **済** | host(resolver)側 |


### C. 制御と transport

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-030 | テスト本体はセッション | **済** | host 側の設計。`Reader` 抽象に backend を足す |
| H-031 | transport を抽象化 | **C** | dmi-bridge の L1 adapter に既にある |
| H-032 | `socket://` を第一級に | **D** | **IP は board 依存**(Pico W / ESP32)。protocol 側は済 |
| H-033 | CLI とライブラリが同一 wire protocol | **A** | host 側 |
| H-034 | mock / 参照実装を protocol と一緒に出す | **A** | protocol を作るなら mock は小さい |
| H-035 | capture と control を別チャネル | **D** | **capture 前提 + descriptor に触る** |
| H-036 | Arduino 専用にしない | **A** | 方針を守るだけ |
| H-037 | 遠隔時の認証 | **D** | L2 前提。localhost なら不要 |


### D. キャプチャと時間軸

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-040 | sigrok が読める形へ無損失で落とせる | **D** | **capture(L5)前提** |
| H-041 | capture に provenance 同梱 | **D** | 同上 |
| H-042 | 落としたサンプル数を申告 | **A**/**D** | **protocol の idiom = A**(`uart_data` の `dropped` と同型)、実装 = D |
| H-043 | 測定原点の優先順位を決め、記録する | **D** | **U3。統合の中心** |
| H-044 | burst と continuous の 2 モード | **D** | capture 前提 |
| H-045 | pre-trigger バッファ | **D** | 同上 |
| H-046 | cross-domain trigger | **D** | **U2。統合の中心** |
| H-047 | 自己観測 | **D** | capture 前提。窓に debug 線を置くだけなので capture があれば安い |
| H-048 | 2 台分業時の時間軸較正 | **E** | 当初対応外に近い |
| H-049 | 論理信号名 → channel の写像 | **A** | host 側の辞書 |


### E. 相手役(器と模型)

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-050 | UART peer ×2 | **B** | **hw UART 2 本で足りる** |
| H-051 | I2C target ×2 アドレス | **B** | hw I2C 2 ブロックを別ピンに出して外部結線 |
| H-052 | I2C target 多アドレス(3 個以上) | **E** | **PIO I2C slave。最大のリスク項目** |
| H-053 | SPI target ×1 | **B** | **PL022 の slave mode** |
| H-054 | 模型は probe 内で完結。転送ごとに host へ問わない | **A** | 設計方針を守るだけ |
| H-055 | preload / inject / readback を別経路 | **A** | protocol 項目 |
| H-056 | 模型に版を付け、版違いは fail closed | **A** | caps 項目 |
| H-057 | 模型はビルド時プロファイルで選ぶ | **A** | R1「能力は build 時」に既にある |
| H-058 | 汎用の器で firmware 更新なしで足せる | **A** | 設計方針 |
| H-059 | SD カード(SPI mode)の相手 | **D** | block device + host backed。実装量あり |
| H-060 | SPI ディスプレイを演じて framebuffer を host へ | **E** | L7 の先 |
| H-061 | 1-Wire / WS2812 / IR の相手役 | **E** | PIO で書けるが数が多い。個別判定 |
| H-062 | ARGB の受信・デコード | **E** | 同上 |
| H-063 | PIOC の相手役 | **E** | 同上 |


### F. 刺激と注入

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-070 | GPIO 刺激(別ポート・別 bit の EXTI) | **B** | GPIO を振るだけ |
| H-071 | I2C 注入(NACK / stretch / SCL 保持 / SDA グリッチ) | **B** | **I2C slave の実装の中**。slave があれば安い |
| H-072 | SPI 注入(busy / 遅延 / MISO 保持) | **B** | 同上 |
| H-073 | UART 注入(framing error / break / 落とし) | **B** | hw UART で break は出せる。framing error は bit 幅を崩す |
| H-074 | UART フロー制御(RTS/CTS)の相手 | **B** | hw UART |
| H-075 | バス事象に同期した NRST | **D** | cross-domain trigger の一部 |
| H-076 | trigger から T µs 後の VDD 切断 | **D** | 同上 + 電源制御 |
| H-077 | 既知パルス幅の生成 | **B** | **E006 で 12.5 ns 分解能を実測済み** |
| H-078 | 既知周波数の生成 | **B** | PWM |
| H-079 | 長時間の刺激(`millis()` wraparound / RTC) | **E** | 時間がかかるだけ。運用側 |


### G. アナログ・電源・電流

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-080 | 既知電圧の出力 | **B** | PWM + RC |
| H-081 | DAC 出力の測定 | **B** | ADC |
| H-082 | VDD 測定 | **B** | ADC + 分圧 |
| H-083 | 電流測定 | **D** | **シャント + INA(外付け部品)が要る** |
| H-084 | 電源制御(on/off、power cycle) | **B** | GPIO + FET |
| H-085 | 5V target の扱い | **D** | **RP2040 では不可。X03x を probe にする build なら** |
| H-086 | VBUS 電圧の測定と安全インターロック | **B** | ADC + 分圧 + 遮断。**安全側なので優先度は高い** |
| H-087 | アナログ刺激の分解能 | **D** | 実測してから決める |
| H-088 | OPA / コンパレータの相手 | **E** | 個別判定 |
| H-089 | TouchKey の相手(既知容量の切替) | **E** | 追加ハードが要る |
| H-090 | 温度・電圧を変えた再測定 | F | 環境試験。範囲外 |


### H. debug 線と target 非破壊

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-100 | passive attach を持ち `caps` で申告 | **C** | **core の中心** |
| H-101 | target に書くものを全部文書化(GPR/CSR 含む) | **C** | 自作なら自明に分かる |
| H-102 | flash 先頭を書き換えない | **C** | 同上 |
| H-103 | 自己観測で「線に何も出していない」を証拠に | **D** | capture 前提(H-047) |
| H-108 | attach 前後で全 GPR を diff | **A** | **host 側でできる** |
| H-109 | 書込後の read がいつ確定するかを仕様に持つ | **A** | 規則を書く |
| H-104 | 1 線 / 2 線を選べる | **B** | **phy を 2 本書く。PIO なら 1 本追加** |
| H-105 | attach 直後のレースを踏まない | **A** | **自作なら descriptor を自分で決めるので起きない** |
| H-106 | 大きい image で固まらない / USB 挿し直し無しで戻る | **C** | 自作の品質 + watchdog |
| H-107 | 壊れ読み値を持ち続けない | **C** | 同上 |


### I. デバッグ出力経路

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-110 | SDI print を probe 非依存にする | **A** | **dmdata で host 側** |
| H-111 | RTT を native に扱う(ELF なし) | **A** | host 側。control block を memory scan |
| H-112 | DMDATA を native に扱う | **A** | host 側 |
| H-113 | 3 経路を CDC として出す | **D** | **CDC を出すと descriptor が composite = PID に触る** |
| H-114 | Pluggable Monitor 対応 | **済** | host 側 |
| H-115 | agent の制御チャネルに使える(DUT の UART を空ける) | **A** | DMI 経由。dmdata / RTT |


### J. 書込・復旧・ISP

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-120 | flash / verify / reset backend | **C** | **core そのもの**。コアは困っていない |
| H-121 | power-off erase / RST erase(unbrick) | **B** | 電源制御 + 手順 |
| H-122 | BOOT0 ピン制御 | **B** | GPIO 1 本 |
| H-123 | 1200bps touch + StartMode の相手 | **A** | host 側 |
| H-124 | UART ISP / USB ISP の相手 | **済** | ch32rv の `isp` route |
| H-125 | board 内蔵ライタと共存できる | **A** | **`caps` の `attach` で表現。builtin-probe で設計済み** |
| H-126 | firmware 更新が全 OS でできる(UF2) | **A** | **RP2040 内蔵 BOOTSEL。ゼロコスト** |


### K. USB / PD / 広帯域

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-130 | USB device(FS)の相手 | **D** | **RP2040 は device/host 排他。ESP32-S3 なら素直** |
| H-131 | USB PD の相手 | **E** | **PD PHY か 2 枚目の X035 が要る** |
| H-132 | PD の keepalive / requestProfile / setVoltage | **E** | 同上 |
| H-133 | PD 試験時に board を焼かない仕組み | **B** | **インターロックは ADC + 遮断で安い。安全側なので先に入れる価値** |
| H-134 | CAN の相手 | **D** | トランシーバ + PIO CAN / ESP32 TWAI |
| H-135 | I2S の相手 | **D** | PIO / ESP32 の I2S |
| H-136 | Ethernet の相手 | **E** | **ESP32-P4 級。core から遠い** |
| H-137 | USB HS device の相手 | **E** | 同上 |
| H-138 | 並列バス(FSMC / LTDC / DVP) | **E** | **難所は本数ではなく速度** |
| H-139 | SDIO(4bit)の相手 | **E** | SPI モードなら B。4bit は要確認 |
| H-140 | QSPI の相手 | **E** | V205 のみ |
| H-141 | 対象外を明示的に宣言する | **A** | 宣言するだけ |


### L. 成果物・provenance・replay

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-150 | 共通 run ID の下に artifact を集約 | **済** | host 側 |
| H-151 | golden corpus を content-addressed で保存 | **済** | host 側 |
| H-152 | decoder / libsigrok の version 固定に耐える形式 | **D** | capture 前提 |
| H-153 | 保存した capture の replay で実機なし回帰 | **済** | **ch32rv に `--capture`/`--replay` が既にある** |
| H-154 | run が何を検証したかのカバレッジ報告 | **A** | host 側 |
| H-155 | 欠落のある capture を golden にしない | **A** | 運用 + drop 計上 |


### M. ベンチ運用

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-160 | DUT 非依存の self-test | **B** | firmware に self-test |
| H-161 | 人手なし復旧(watchdog / host から reset) | **B** | watchdog + reset コマンド |
| H-162 | 遠隔ベンチ | **D** | L2 依存 |
| H-163 | fixture inventory にロット番号 | **A** | host 側 |
| H-164 | 未信頼 fork PR の境界 | F | CI 運用。範囲外 |
| H-165 | 複数 DUT / mux | **D** | lane(H-009) |
| H-166 | 誤配線から board を守る | **B** | 直列抵抗 + 過電圧検出 |


### N. 他プロジェクトとの共用

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-170 | `ch32fun` 等から同じ CLI/API | **A** | 方針 |
| H-171 | EmbedBench の模型をそのまま相手役に | **E** | **L7 + 統合作業** |
| H-172 | EmbedBench が mock harness として同じ口を喋る | **A** | **protocol を出せば向こうが実装できる** |
| H-173 | I2CDeviceDB の相手役 | **E** | H-052 依存 |
| H-174 | TinyGFX の相手役 | **E** | H-060 依存 |
| H-175 | host-arduino-core と socket idiom を共有 | **A** | 既存 |
| H-176 | TinyUSB 上流貢献の HIL を埋める | **E** | H-130 依存 |
| H-177 | 教育 / ワークショップ用途 | **A** | **L0 が届けば自動的に満たす** |
| H-178 | ベンチ機材そのものの共用 | F | 運用 |


### O. 他 repo 由来

| ID | 要求 | 判定 | 理由 |
|---|---|:--:|---|
| H-180 | lock のキーを DUT にもできる | **A** | host 側。設計の合意が要る |
| H-181 | core の状態(halt/running)の調停 | **C** | **session protocol の設計に入れる** |
| H-182 | `caps` を「可否 + 理由」で持つ | **A** | caps の形 |
| H-183 | 往復の記録を NDJSON 形で揃える | **A** | host 側。**ch32rv の形をそのまま** |
| H-184 | 束縛の data rev を manifest に残す | **A** | host 側 |
| H-185 | 圧縮された時間定数の扱いを決める | **A** | **EmbedBench 側が実装を引き受けられると明言**。我々は caps 項目 |
| H-186 | `caps` に `requestWake` の可否と分解能 | **A** | caps 項目 |
| H-187 | 再入禁止を実装契約として明示 | **A** | 契約を書く |
| H-188 | 診断語彙とイベント行の形を揃える | **A** | **決めるだけ。ただし早く決めないと diff の作り直し** |
| H-189 | `caps` に凍結 IF の版 | **A** | caps 項目 |
| H-190 | `channelWrite` を遅延配送の規律に載せる | **A** | 契約 |
| H-191 | `tests/conformance/` を第 3 の環境の門に | **A** | **既にある試験を使う** |

## 2. `E`(無理あり)— **個別判定に回すもの** 21 件

| 束 | ID | 要求 | 難しさの中身 |
|---|---|---|---|
| **PIO I2C slave**(最大のリスク) | H-052 | I2C target 多アドレス(3 個以上) | **PIO I2C slave。最大のリスク項目** |
| **PIO I2C slave**(最大のリスク) | H-173 | I2CDeviceDB の相手役 | H-052 依存 |
| **USB PD**(PHY か 2 枚目の X035 が要る) | H-131 | USB PD の相手 | **PD PHY か 2 枚目の X035 が要る** |
| **USB PD**(PHY か 2 枚目の X035 が要る) | H-132 | PD の keepalive / requestProfile / setVoltage | 同上 |
| **広帯域・高速周辺**(ESP32-P4 級 / 速度が難所) | H-136 | Ethernet の相手 | **ESP32-P4 級。core から遠い** |
| **広帯域・高速周辺**(ESP32-P4 級 / 速度が難所) | H-137 | USB HS device の相手 | 同上 |
| **広帯域・高速周辺**(ESP32-P4 級 / 速度が難所) | H-138 | 並列バス(FSMC / LTDC / DVP) | **難所は本数ではなく速度** |
| **広帯域・高速周辺**(ESP32-P4 級 / 速度が難所) | H-139 | SDIO(4bit)の相手 | SPI モードなら B。4bit は要確認 |
| **広帯域・高速周辺**(ESP32-P4 級 / 速度が難所) | H-140 | QSPI の相手 | V205 のみ |
| **統合の先(L7 以上)** | H-015 | 器と模型を `caps` に並べて申告 | **caps 項目 = A、模型の実装 = E(L7)** |
| **統合の先(L7 以上)** | H-060 | SPI ディスプレイを演じて framebuffer を host へ | L7 の先 |
| **統合の先(L7 以上)** | H-171 | EmbedBench の模型をそのまま相手役に | **L7 + 統合作業** |
| **統合の先(L7 以上)** | H-174 | TinyGFX の相手役 | H-060 依存 |
| **CH32 固有の追加ハード** | H-062 | ARGB の受信・デコード | 同上 |
| **CH32 固有の追加ハード** | H-063 | PIOC の相手役 | 同上 |
| **CH32 固有の追加ハード** | H-088 | OPA / コンパレータの相手 | 個別判定 |
| **CH32 固有の追加ハード** | H-089 | TouchKey の相手(既知容量の切替) | 追加ハードが要る |
| **CH32 固有の追加ハード** | H-061 | 1-Wire / WS2812 / IR の相手役 | PIO で書けるが数が多い。個別判定 |
| **運用・時間がかかるもの** | H-048 | 2 台分業時の時間軸較正 | 当初対応外に近い |
| **運用・時間がかかるもの** | H-079 | 長時間の刺激(`millis()` wraparound / RTC) | 時間がかかるだけ。運用側 |
| **運用・時間がかかるもの** | H-176 | TinyUSB 上流貢献の HIL を埋める | H-130 依存 |

**束ねると 6 つ。** 個別判定は**この束の単位でやれば足りる**(21 件を 1 つずつ見る必要は無い):

1. **PIO I2C slave** — これが通れば H-052 / H-173 が同時に動く。**通らなければ hw I2C 2 ブロック外部結線(H-051、判定 B)で 2 アドレスまで**
2. **USB PD** — 機材(PD PHY / 2 枚目の X035)の調達判断
3. **広帯域・高速周辺** — **board class の判断**(ESP32-P4 を入れるか)
4. **統合の先(L7 以上)** — 幅をどこまで取るかの判断そのもの
5. **CH32 固有の追加ハード** — 個別に必要性を見る
6. **運用・時間** — 後回しで害が無い


## 3. `D`(条件付き)— **前提は 5 種類しかない** 28 件

| 前提 | 該当 ID | 数 |
|---|---|---:|
| **capture(L5 以上)を作るか** | H-016, H-017, H-040, H-041, H-042, H-043, H-044, H-045, H-046, H-047, H-075, H-076, H-103, H-152 | 14 |
| **board を選ぶ(IP / S3 / X03x)** | H-032, H-037, H-085, H-130, H-134, H-135, H-162 | 7 |
| **外付け部品を載せる** | H-059, H-083, H-087 | 3 |
| **PID / descriptor 方針** | H-004, H-035, H-113 | 3 |
| **lane を実装するか** | H-165 | 1 |

→ **前提を 5 つ決めれば、`D` の 28 件はまとめて `A`〜`C` に落ちる。** 個別に判断する必要は無い。

| 前提 | 決めること | 決まると |
|---|---|---|
| **capture を作るか** | 幅の上端(L5 以上を core に入れるか) | **14 件が動く**(最大の束) |
| **board を選ぶ** | IP は Pico W / ESP32、5V は X03x、広帯域は S3 | 7 件 |
| **外付け部品** | シャント + INA、block device の backing | 3 件 |
| **PID / descriptor** | [harness-choices](harness-choices.ja.md) §2 の軸 A〜D | 3 件 |
| **lane を実装するか** | L3 を core に入れるか | 1 件 |


## 4. `F`(範囲外)3 件

| ID | 要求 | 理由 |
|---|---|---|
| H-090 | 温度・電圧を変えた再測定 | 環境試験。範囲外 |
| H-164 | 未信頼 fork PR の境界 | CI 運用。範囲外 |
| H-178 | ベンチ機材そのものの共用 | 運用 |

## 5. 判定から見えたこと

1. **91 件(63%)が無理なく対応できる。** 内訳は **host 側で済むもの(`済`+`A` = 58 件)が最も多い** — つまり **「firmware を書く」より「host と契約を整える」ほうが件数では支配的**。
2. **`E` は 21 件だが、束ねると 6 つ**(§2)。**個別判定は 6 回で足りる。**
3. **`D` は 28 件だが、前提は 5 つ**(§3)。**うち 14 件が「capture を作るか」1 つに依存**している → **幅の上端を決めるのが、最も多くの要求を一度に解く判断**。
4. **`caps` に項目を持つだけで満たせる要求が異常に多い**(H-002 / 011〜020 / 042 / 055〜057 / 182 / 186 / 189 など)。→ **`caps` の形を早く決めると、判定が `A` に倒れる件数が増える。**
5. **ベンチ由来(H-185〜H-191)は全部 `A`。** EmbedBench 側が実装を引き受けられると明言している項目もあり、**我々は `caps` の項目と契約の文言を用意するだけ**。
6. **`H-188`(診断語彙とイベント行の形を揃える)は `A` だが急ぐ。** 決めるだけだが、**後から変えると diff の作り直しになる**と EmbedBench が警告している。

## 6. 参照

- 限界コストの根拠: [harness-existing-standards.ja.md](harness-existing-standards.ja.md) §6
- 既存 + ch32rv でどこまで: 同 §1 / [harness-tool-definition.ja.md](harness-tool-definition.ja.md) §5
- 幅の議論(`D` の前提「capture を作るか」): [harness-tool-definition.ja.md](harness-tool-definition.ja.md) §4 / §6
- PID の軸(`D` の前提): [harness-choices.ja.md](harness-choices.ja.md) §2
- 要求の正本: コア `docs/harness-requirements.ja.md` / ベンチ `docs/HARNESS_REQUESTS.ja.md` / ライタ `docs/data-requests/0006-harness-integration.ja.md`
- 所在: [harness-index.ja.md](harness-index.ja.md)
