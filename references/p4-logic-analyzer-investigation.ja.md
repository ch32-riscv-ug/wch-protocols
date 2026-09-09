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
| 16 | 2 | 48 MHz（実効95.884 MB/s、1 Mi burst）／持続98 MB/s | — | — | 1,048,576 sample | PARLIO |
| 24 | 4想定 | — | — | — | — | CPU snapshot候補 |
| 32 | 4 | — | — | — | — | CPU snapshot候補 |
| 33〜55 | 8 | — | — | — | — | CPU snapshot候補 |

channel幅とpackingは[E031](../experiments/e031_p4_parlio_channel_width/README.ja.md)、width別raw rateは[E032](../experiments/e032_p4_parlio_width_rate_coarse/README.ja.md)と[E033](../experiments/e033_p4_parlio_width_rate_fine/README.ja.md)、sample単位再検証は8 channelが[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)、16 channelが[E042](../experiments/e042_p4_parlio_16ch_seq_verify/README.ja.md)、8 channelのtrigger・深度は[E025](../experiments/e025_p4_sump_trigger_rate_boundary/README.ja.md)、[E028](../experiments/e028_p4_sump_four_stage_trigger/README.ja.md)、[E030](../experiments/e030_p4_deep_batch_capture/README.ja.md)による。triggerなしPSRAM spoolは約98 MB/sで飽和するため、80 MB/sを安定tierとする。

※ を付けた行の検証は、100 kHzのLEDC PWMを信号源としたlaneごとのdutyとedge数によるものである。定常・周期的な信号源では、ringがcopy前に上書きされてもdutyとedge数がほぼ変わらないため、この検証はsample単位の欠落を検出できない（[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)で実証）。8 / 16 channel行はgray code rampによるsample単位検証を通っている。1 / 2 / 4 channelは160 MHz設定でもpacking後20 / 40 / 80 MB/sで持続spool帯域に余裕があり、E036とE042でbyte rate modelがwidthをまたいで成立したので、再検証の優先度は低い。

## rate限界は単一の値にならない

[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)で、8 channelのraw rateは独立した三つの量に分かれた。単一の「上限」を申告すると、深度が変わったときに嘘になる。

| 量 | 8 channelでの値 | 決まり方 |
|---|---|---|
| **sampling上限** | **160 MHz**(構造上の天井そのもの) | PARLIO RXの内部clock源はPLL_F160Mが最上位。E036で120 MHzまでcallback数から推定し、[E038](../experiments/e038_p4_parlio_pulse_trigger_rate/README.ja.md)がframe間隔による絶対測定で160 MHzまで設定の99.9〜100.1%を確認した。これを超えるにはexternal clock経路しかない |
| **持続spool帯域** | 約98 MB/s | internal ring → PSRAMのtask copyの限界。channel数ではなくpacking後のbyte rateで決まる |
| **burst深度** | ring容量 ÷ (sampling − spool) | sampling超過分をringが吸収できる間だけ成立する。ring 64 KiBなら104 MHzで約1.2 Mi sample、112 MHzで約0.54 Mi sample |

つまりsample rateの成立・不成立は**capture深度と一緒でなければ意味を持たない**。8 channel 100 MHzは1 Mi sampleでは成立するが、超過分1.692 MB/sをringが吸収しきる約3.87 Mi sampleで破綻するので、[E030](../experiments/e030_p4_deep_batch_capture/README.ja.md)の16 MiB deep captureには適用できない。深度を伸ばすほど公称rateは持続spool帯域へ漸近する。

drop判定にも同じ整理が要る。`queue_overflow`はchunk queueが満杯になった時点しか見ておらず、queue 64段はring容量の約3.8倍あるので、ringが上書きされてもAPIは`ESP_OK`を返す。判定に使う量は**ring上の未読byte数がring容量を超えたか**である。

## channel間のedge一致精度は±1 sample

[E042](../experiments/e042_p4_parlio_16ch_seq_verify/README.ja.md)で、同一のGPIOをPARLIO RXの二つのlane slotへ入力し、両者が常に同じ値を読むかを測った。

| sample rate | run長の散らばり | 複製lane不一致(1 Mi sample中) |
|---:|---:|---:|
| 20 MHz | 4固定 | 0 |
| 40 MHz | 3〜5 | 1,066 |
| 48 MHz | 3〜5 | 373 |
| 52 MHz | 3〜5 | 345 |
| 56 MHz | 3〜5 | 480 |

同じ物理信号でも、遷移がsampling edge付近に来ると二つのlaneが別の値を読む。遷移1回あたり0.13〜0.41%である。原因はlane slot間の到達時間差か、二つの経路が遷移中の信号を独立に確定させるためかを切り分けていない。

実装上の帰結は三つある。

- **channel間のedge位置は±1 sampleの精度で申告する。** それより細かいtiming差をsample列から読み取ってはならない
- **sample rateを上げるほど食い違いは出やすい。** 20 MHzでは0件、40 MHz以上では常に出た。遷移がsampling周期に対して速くなるためである
- **protocol decodeでは、同時に変わるべき複数channelが1 sampleずれて見える前提を置く。** clockとdataが同時に変わるbusで、setup/hold判定をsample列から行うのは危険である

この現象はgray code検証には現れない。gray codeでは遷移中のsampleが必ず前後どちらかの値になるためで、欠落検証とlane一致検証は別に持つ必要がある。

## 調べる機能

### Batch capture本体

- 1 / 2 / 4 / 8 / 16 channelのsample packingとdata完全性
- channel幅ごとのraw capture rate上限(sampling / 持続spool / burst深度の三分割)
- 1 / 2 / 4 channelでのrun揺れとchannel間edge一致精度
- 16 MiB以上を含む深度と、確保失敗時の安全な縮退
- internal clockとexternal clock
- arm、manual stop、timeout、one-shot、re-arm
- 任意pre/post比率とcircular pre-trigger
- capture metadata: actual rate、sample count、trigger index、overflow、drop

### Trigger

triggerは性質の違う二段に分かれる。[E037](../experiments/e037_p4_parlio_pulse_trigger/README.ja.md)でhardware側の経路が成立した。

| tier | 条件にできるもの | channelの代償 | pre-trigger | rateの決まり方 |
|---|---|---|---|---|
| software走査 | pattern + mask、rising / falling / either edge、occurrence count、複数stage | なし | 可(circular ring) | CPUの走査能力。8 channelで24 MHz、固定4-stageで16 MHz |
| hardware pulse delimiter | trigger源にした1 channelのedge。極性は`pulse_invert` | **なし**(源のchannelを選ぶだけ) | **不可** | CPU負荷ゼロ。**160 MHzまで実測成立**([E038](../experiments/e038_p4_parlio_pulse_trigger_rate/README.ja.md)・[E041](../experiments/e041_p4_parlio_shared_valid_line/README.ja.md)) |
| hardware level delimiter | trigger源にした1 channelがactiveな区間。極性は`active_low_en` | **なし**(同上) | **不可** | CPU負荷ゼロ。20 MHzで成立([E039](../experiments/e039_p4_parlio_level_gate/README.ja.md))。rate上限は未測定 |

**hardware triggerはchannelを消費しない。** `valid_gpio_num`はGPIO matrix経由なのでdata線のいずれかと同一GPIOに設定でき、その線はdataとして記録されつつtriggerにもなる([E041](../experiments/e041_p4_parlio_shared_valid_line/README.ja.md))。`valid_sig_line_id`はGPIOではなく内部line slotの番号なので、data widthより大きい値を選べばdata lineと衝突しない。したがって8 channel + hardware triggerは8 pinで成立する。

hardware tierはvalid線1本を払う代わりに、frame開始と`eof_data_len`停止をCPU走査なしで行う。[E037](../experiments/e037_p4_parlio_pulse_trigger/README.ja.md)と[E038](../experiments/e038_p4_parlio_pulse_trigger_rate/README.ja.md)で、data_width 4の20〜160 MHzすべてで受信byteが`eof_data_len`と完全一致し、gray rampのstep違反0、誤trigger0だった。pulseからの取得開始のずれは1 sample以内。

上限は**内部clock源(PLL_F160M)の160 MHz**である。E036の約98 MB/sはinternal ring → PSRAM copy段の限界であり、copy段の無い有限frameには効かない。[E041](../experiments/e041_p4_parlio_shared_valid_line/README.ja.md)はdata_width 8の160 MHz、つまり160 MB/sで4 KiB frameを違反0で取得したので、**internal RAMへのDMA write自体は少なくとも4 KiB burstで160 MB/sを通す**。持続帯域は未測定である。

この経路には引き換えがある。

- **pre-trigger dataは取れない。** pulseでframeを開始する方式なので、pre/post windowが必要ならsoftware走査 + circular ring([E027](../experiments/e027_p4_sump_circular_pretrigger/README.ja.md))に戻る
- **深度が浅い。** `eof_data_len`は16 bitで最大65,535 byte。16 KiB frameは160 MHzで205 us分にすぎない。深い取得はspool経路へ戻る。つまり「速いが浅い」と「遅いが深い」の二つのmodeになる
- **arm待ちのtimeoutは`timeout_ticks`では取れない。** frame開始前は`on_timeout`が発火しないので、softwareで持つ
- **完了eventが出る終了条件は`eof_data_len`だけである。** `eof_data_len` = 0にすると完了eventは来ないが、[E040](../experiments/e040_p4_parlio_level_open_frame/README.ja.md)でDMA自体は正しく走っていることが分かった。したがって窓のmodeは三つある

| mode | 長さ | 完了event | 深度の上限 |
|---|---|---|---|
| pulse または level + 有限transaction | 固定(`eof_data_len`) | 有り | 65,535 byte |
| level + `eof_data_len` = 0 + `partial_rx_en` | 可変(softwareが止める) | **無し** | software次第 |
- **level active lowは開始位置が再現しない。** armした時点で既にactiveならその瞬間から始まるため、開始位置が信号の位相ではなくarmのタイミングで決まる
- **sample間隔の均一性は分周比だけでは決まらない。** E038ではdata_width 4で160 MHzの整数分周(160 / 80 / 40 / 20 MHz)だけが均一だったが、E041ではdata_width 8の80 / 160 MHzが±1 sample揺れた。整数分周は十分条件ではなく、独立した二つの分周器の位相関係が実際の変数と考えられる。**時間精度を分周比から申告してはならない**(未解明)

まだ測っていないもの:

- data_width 16でのvalid線共有(`valid_sig_line_id`に空きslotが無い可能性)
- 有限frameの持続byte rate(4 KiB burstより長い取得で98 MB/sを超えられるか)
- `eof_data_len` 65,535超のpost長
- level delimiterでのgating時の最大sample rateとgate境界のsample精度
- gate境界のsample精度(gate開放・閉止のedgeに対して何sampleずれるか)とgate window境界のmetadata復元
- `has_end_pulse`によるhardware停止と`pulse_invert`の極性
- trigger delay
- UART / I2C / SPI等のprotocol-aware triggerは、raw triggerの成立後にCPU負荷とrateを別測定する

trigger能力は対応条件だけでなく、tier・消費channel・channel幅・条件種・stage数ごとの最大sample rateとして申告する。

### 内部圧縮

最低限、次の方式をraw保存と比較する。

| 方式 | 向く入力 | 悪化条件 | 調べる値 |
|---|---|---|---|
| run-length encoding (RLE) | idleが長いdigital信号 | 毎sample反転、短run | 圧縮率、最大処理rate、最悪時膨張率 |
| transition + delta timestamp | UART/I2C等のedgeが疎な信号 | 高速clockやrandom data | 最大edge/s、timestamp wrap、複数channel同時edge |
| channel bit packing | 1 / 2 / 4 channel | 8 / 16 channelでは効果なし | hardware packing順、host展開cost |
| block raw/RLE選択 | 入力特性が途中で変わる信号 | block判定cost | block size、切替cost、random data時の上限 |
| **hardware capture qualification** | CS / enable線でburstが区切られる信号(SPI、I2C等) | qualifierが常にactiveな信号。gate外の情報は完全に失われる | duty別の保存量削減率、gate境界のsample精度、境界metadataの持ち方 |

圧縮は常に有効にしない。各blockにencoding、raw sample数、encoded byte数を持たせ、圧縮後がraw以上ならraw blockを保存する方式を基準候補とする。これなら最悪入力でも容量を大きく失わない。

hardware capture qualificationはこの表の中で唯一**CPUを使わない**手段である。[E040](../experiments/e040_p4_parlio_level_open_frame/README.ja.md)で、level delimiterのgateがactiveな区間のsampleだけがDMAへ渡ることを実測した(gate duty 12.5%に対して回収byte rateはraw byte rateの12%)。入力の性質に依存せず最悪時膨張も無い代わりに、qualifier線を1本消費し、gate外の情報は残らない。同じPSRAM容量でduty分だけ長い時間を覆え、spool帯域([E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)の約98 MB/s)も同じ比率で緩む。

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
3. width別basic triggerとmulti-stage trigger（software走査tier。hardware pulse tierは[E037](../experiments/e037_p4_parlio_pulse_trigger/README.ja.md)・[E038](../experiments/e038_p4_parlio_pulse_trigger_rate/README.ja.md)でdata_width 4の160 MHzまで成立）
4. wide CPU snapshot（24 / 32 / 33〜55 channel）
5. raw batchの深度・停止・再arm耐久
6. RLE、transition timestamp、block adaptive encoding
7. ADC continuous batchのrate・channel・深度（無配線）
8. analog精度・noise・digital同期（要配線）
9. external clock
10. batch download
11. streaming throughput

失敗した段階で後続条件を組み替える。たとえば16 channelのraw rateがPSRAM byte帯域で制限される場合、trigger実験はその成立rate以下だけを対象にする。
