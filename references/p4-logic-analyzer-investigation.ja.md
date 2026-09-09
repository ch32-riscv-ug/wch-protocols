# ESP32-P4 batch logic analyzer予備調査

状態: **進行中 / 調査地図**

## 目的

ESP32-P4と32 MiB PSRAMで、offline batch取得を中心とするlogic analyzer実装例がどこまで実用になるかを、単一の「最高速度」ではなく次の独立した限界として確定する。

- channel数とsampleのpacking
- channel数ごとのsample rate
- trigger条件ごとのsample rate
- capture深度とpre/post比率
- raw sampleの内部圧縮と、圧縮が有利・不利になる入力
- 外部clock、analog、host downloadはbatch本体の後に分離して評価する

各実験は結果によって次の条件を変えるため、着手する1件だけ採番する。最終的に本書へ実測matrixと実装可能な機能一覧を集約する。

## 現在確定している構造上の境界

Arduino-ESP32 3.3.11が使用するESP32-P4のSoC定義では、PARLIOは1 group、RX unitは1基、RX data widthは最大16 lineである。driver APIが受け付けるwidthは1 / 2 / 4 / 8 / 16である。LCD_CAMのcamera入力も最大16 bitで、24-bit幅はLCD出力側である。

したがってchannel数は最初から二つのtierに分ける。

| tier | channel候補 | capture方式 | 現時点の位置づけ |
|---|---:|---|---|
| 高速parallel | 1 / 2 / 4 / 8 / 16 | PARLIO RX + internal DMA ring + PSRAM | 16 channelがhardware上限。幅ごとのpackingとrateは実測する |
| wide低速 | 24 / 32 / 33〜55 | CPUでGPIO input registerをsnapshotしPSRAMへ保存 | 専用DMA入力は無い。成立rate、jitter、core占有率を別に測る |

二つのPARLIO RXを同期して32 channelにする案は、P4にRX unitが1基しかないため候補から外す。MIPI CSIは任意GPIOを同時sampleする経路ではなく、I2S TDMはserial入力なので汎用parallel probeの代替とは扱わない。

## 限界matrix

空欄は未測定。値は「APIが受け付けた」ではなく、既知patternの全data検証とoverflow確認まで通った条件だけを記入する。

| channel | bytes/sample | raw rate上限 | basic trigger上限 | multi-stage上限 | 最大確認深度 | 方式 |
|---:|---:|---:|---:|---:|---:|---|
| 1 | 1/8 | 160 MHz成立（内部clock源の上限）※ | — | — | 1,048,576 sample | PARLIO |
| 2 | 1/4 | 160 MHz成立（内部clock源の上限）※ | — | — | 1,048,576 sample | PARLIO |
| 4 | 1/2 | 160 MHz成立（内部clock源の上限）※ | — | — | 1,048,576 sample | PARLIO |
| 8 | 1 | 104 MHz（1 Mi burst）／持続98 MB/s | 24 MHz、保守候補20 MHz | 16 MHz（固定4-stage） | 16 MiB / 20 MHz | PARLIO |
| 16 | 2 | 48 MHz（実効47.838 MHz、1 Mi sample）※ | — | — | 1,048,576 sample | PARLIO |
| 24 | 4想定 | — | — | — | — | CPU snapshot候補 |
| 32 | 4 | — | — | — | — | CPU snapshot候補 |
| 33〜55 | 8 | — | — | — | — | CPU snapshot候補 |

channel幅とpackingは[E031](../experiments/e031_p4_parlio_channel_width/README.ja.md)、width別raw rateは[E032](../experiments/e032_p4_parlio_width_rate_coarse/README.ja.md)と[E033](../experiments/e033_p4_parlio_width_rate_fine/README.ja.md)、8 channelのsample単位再検証は[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)、8 channelのtrigger・深度は[E025](../experiments/e025_p4_sump_trigger_rate_boundary/README.ja.md)、[E028](../experiments/e028_p4_sump_four_stage_trigger/README.ja.md)、[E030](../experiments/e030_p4_deep_batch_capture/README.ja.md)による。triggerなしPSRAM spoolは約98 MB/sで飽和するため、80 MB/sを安定tierとする。

※ を付けた行の検証は、100 kHzのLEDC PWMを信号源としたlaneごとのdutyとedge数によるものである。定常・周期的な信号源では、ringがcopy前に上書きされてもdutyとedge数がほぼ変わらないため、この検証はsample単位の欠落を検出できない（[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)で実証）。8 channel行だけがgray code rampによるsample単位検証を通っている。※ の行は再検証待ちとして扱う。

## rate限界は単一の値にならない

[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)で、8 channelのraw rateは独立した三つの量に分かれた。単一の「上限」を申告すると、深度が変わったときに嘘になる。

| 量 | 8 channelでの値 | 決まり方 |
|---|---|---|
| **sampling上限** | 少なくとも120 MHz。構造上の天井は160 MHz | PARLIO RXの内部clock源はPLL_F160Mが最上位。これを超えるにはexternal clock経路しかない |
| **持続spool帯域** | 約98 MB/s | internal ring → PSRAMのtask copyの限界。channel数ではなくpacking後のbyte rateで決まる |
| **burst深度** | ring容量 ÷ (sampling − spool) | sampling超過分をringが吸収できる間だけ成立する。ring 64 KiBなら104 MHzで約1.2 Mi sample、112 MHzで約0.54 Mi sample |

つまりsample rateの成立・不成立は**capture深度と一緒でなければ意味を持たない**。8 channel 100 MHzは1 Mi sampleでは成立するが、超過分1.692 MB/sをringが吸収しきる約3.87 Mi sampleで破綻するので、[E030](../experiments/e030_p4_deep_batch_capture/README.ja.md)の16 MiB deep captureには適用できない。深度を伸ばすほど公称rateは持続spool帯域へ漸近する。

drop判定にも同じ整理が要る。`queue_overflow`はchunk queueが満杯になった時点しか見ておらず、queue 64段はring容量の約3.8倍あるので、ringが上書きされてもAPIは`ESP_OK`を返す。判定に使う量は**ring上の未読byte数がring容量を超えたか**である。

## 調べる機能

### Batch capture本体

- 1 / 2 / 4 / 8 / 16 channelのsample packingとdata完全性
- channel幅ごとのraw capture rate上限
- 16 MiB以上を含む深度と、確保失敗時の安全な縮退
- internal clockとexternal clock
- arm、manual stop、timeout、one-shot、re-arm
- 任意pre/post比率とcircular pre-trigger
- capture metadata: actual rate、sample count、trigger index、overflow、drop

### Trigger

- level / pattern + mask
- rising / falling / either edge
- occurrence count
- 複数stageとstageごとの条件
- trigger delay、pre/post位置
- pulse width / timeout条件
- UART / I2C / SPI等のprotocol-aware triggerは、raw triggerの成立後にCPU負荷とrateを別測定する

trigger能力は対応条件だけでなく、channel幅・条件種・stage数ごとの最大sample rateとして申告する。

### 内部圧縮

最低限、次の方式をraw保存と比較する。

| 方式 | 向く入力 | 悪化条件 | 調べる値 |
|---|---|---|---|
| run-length encoding (RLE) | idleが長いdigital信号 | 毎sample反転、短run | 圧縮率、最大処理rate、最悪時膨張率 |
| transition + delta timestamp | UART/I2C等のedgeが疎な信号 | 高速clockやrandom data | 最大edge/s、timestamp wrap、複数channel同時edge |
| channel bit packing | 1 / 2 / 4 channel | 8 / 16 channelでは効果なし | hardware packing順、host展開cost |
| block raw/RLE選択 | 入力特性が途中で変わる信号 | block判定cost | block size、切替cost、random data時の上限 |

圧縮は常に有効にしない。各blockにencoding、raw sample数、encoded byte数を持たせ、圧縮後がraw以上ならraw blockを保存する方式を基準候補とする。これなら最悪入力でも容量を大きく失わない。

### 後段

batch本体の限界を確定した後に、PSRAM→USB device Bulk IN、IP、file保存を測る。streamingは最後に独立して測り、batch側のsample rateやtrigger結果と混ぜない。PC側applicationでsigrok互換形式、VCD、CSV等へ変換する。

### Analog capture

ESP32-P4のSoC定義とADC continuous driverから、実装前に次が確定する。

| 項目 | P4の値 | 意味 |
|---|---:|---|
| ADC unit | 2 | ADC1とADC2 |
| 外部channel | ADC1 8、ADC2 6 | GPIO16〜23、GPIO49〜54 |
| continuous pattern長 | 最大16 entry | 複数channelをscan可能。最大rate時の出力順は実測が必要 |
| resolution | 12 bit固定 | raw resultは1 conversionあたり4 byte |
| aggregate sample rate | 611〜83,333 conversion/s | 複数channel時はchannelごとのrateが概ね分割される |
| DMA | 対応 | continuous batch取得のdriver経路がある |

この上限から、内蔵ADCはMSa/s級digital captureの代替ではなく、電源、ゆっくりしたsensor、threshold前後の電圧を同時に残す補助analog traceとして扱う。[E034](../experiments/e034_p4_adc1_continuous_batch/README.ja.md)ではADC1の1 / 2 / 4 / 8 channelすべてで最大aggregate 83,333 conversion/s、各1 MiBのPSRAM退避が成立した。8 channel時の実効値は各約10.4 kSa/s、pool overflowは0だった。

E034ではchannel別sample数とIDは正しかった一方、最大rateの複数channelは設定順の単純な循環列にならなかった。したがってraw resultのchannel IDを必ず保持してtraceへ分離し、配列位置だけからchannel間skewを推定しない。

無配線で確認できるのは、continuous driverの初期化、1〜14 channelのpattern順、aggregate rate、DMA pool overflow、PSRAMへのbatch退避、metadata復元までである。入力電圧の正確さ、noise、ENOB、attenuation別範囲、SDM出力をRC filterした波形はanalog pinへの配線が必要なので分離する。P4はSDM出力を持つが、それをADCへ内部routingするanalog経路はない。

digitalとの同期は、まず共通timerで開始時刻と完了時刻を記録する疎結合方式を評価する。sample単位の位相同期が必要ならhardware trigger/ETM経路の有無を別に確認し、software同時startだけで同期済みとは扱わない。

## 実験の依存関係

1. PARLIO widthとpacking
2. width別raw sample rate（sampling / 持続spool / burst深度の三分割。8 channelは[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)済、他widthは再検証待ち）
3. width別basic triggerとmulti-stage trigger
4. wide CPU snapshot（24 / 32 / 33〜55 channel）
5. raw batchの深度・停止・再arm耐久
6. RLE、transition timestamp、block adaptive encoding
7. ADC continuous batchのrate・channel・深度（無配線）
8. analog精度・noise・digital同期（要配線）
9. external clock
10. batch download
11. streaming throughput

失敗した段階で後続条件を組み替える。たとえば16 channelのraw rateがPSRAM byte帯域で制限される場合、trigger実験はその成立rate以下だけを対象にする。
