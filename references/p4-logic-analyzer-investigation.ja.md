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
| 1 | 1/8 | **160 MHz（sample精度確認）** | — | — | 1,048,576 sample | PARLIO |
| 2 | 1/4 | **160 MHz（sample精度確認）** | — | — | **16,777,216 sample** | PARLIO |
| 4 | 1/2 | **160 MHz（sample精度確認）** | — | — | 1,048,576 sample | PARLIO |
| 8 | 1 | **96 MHz（1 MiBでsample精度）**／104 MHz（1 Mi burst）／持続98 MB/s | 24 MHz、保守候補20 MHz | 16 MHz（固定4-stage） | 16 MiB / 20 MHz | PARLIO |
| 16 | 2 | 48 MHz（実効95.884 MB/s、1 Mi burst）／持続98 MB/s | — | — | 1,048,576 sample | PARLIO |
| 8 + gate | 1 | **160 MHz**（内部clock源の上限。gate前提） | — | — | 1 MiB検証済 | PARLIO + qualification |
| 24 | 4想定 | — | — | — | — | CPU snapshot候補 |
| 32 | 4 | — | — | — | — | CPU snapshot候補 |
| 33〜55 | 8 | — | — | — | — | CPU snapshot候補 |

channel幅とpackingは[E031](../experiments/e031_p4_parlio_channel_width/README.ja.md)、width別raw rateは[E032](../experiments/e032_p4_parlio_width_rate_coarse/README.ja.md)と[E033](../experiments/e033_p4_parlio_width_rate_fine/README.ja.md)、sample単位再検証は8 channelが[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)、16 channelが[E042](../experiments/e042_p4_parlio_16ch_seq_verify/README.ja.md)、8 channelのtrigger・深度は[E025](../experiments/e025_p4_sump_trigger_rate_boundary/README.ja.md)、[E028](../experiments/e028_p4_sump_four_stage_trigger/README.ja.md)、[E030](../experiments/e030_p4_deep_batch_capture/README.ja.md)による。triggerなしPSRAM spoolは約98 MB/sで飽和するため、80 MB/sを安定tierとする。

**1 / 2 / 4 channel行はsample単位の裏付けが付いた**([E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) が2 channel、[E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) が1 / 4 / 8 channel)。判定は**立ち上がりedge間隔のmin = mean = max が `rate ÷ 信号源周波数` と一致するか**で、dutyでは欠落を検出できないため使っていない。2 channelは16 Mi sampleの深さでも欠落0、8 channelは**96 MHz（96 MB/s）までなら1 MiBでsample精度**で、160 MHz（160 MB/s）では1 MiBで`overflow=131`が出た — **持続spool帯域を超えた分はburst窓の内側しか保たない**というモデルどおりである。

(以下は当初の注記。16 channel行に当たる)

※ を付けた行の検証は、100 kHzのLEDC PWMを信号源としたlaneごとのdutyとedge数によるものである。定常・周期的な信号源では、ringがcopy前に上書きされてもdutyとedge数がほぼ変わらないため、この検証はsample単位の欠落を検出できない（[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)で実証）。8 / 16 channel行はgray code rampによるsample単位検証を通っている。1 / 2 / 4 channelは160 MHz設定でもpacking後20 / 40 / 80 MB/sで持続spool帯域に余裕があり、E036とE042でbyte rate modelがwidthをまたいで成立したので、再検証の優先度は低い。

## rate限界は単一の値にならない

[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)で、8 channelのraw rateは独立した三つの量に分かれた。単一の「上限」を申告すると、深度が変わったときに嘘になる。

| 量 | 8 channelでの値 | 決まり方 |
|---|---|---|
| **sampling上限** | **160 MHz**(構造上の天井そのもの) | PARLIO RXの内部clock源はPLL_F160Mが最上位。E036で120 MHzまでcallback数から推定し、[E038](../experiments/e038_p4_parlio_pulse_trigger_rate/README.ja.md)がframe間隔による絶対測定で160 MHzまで設定の99.9〜100.1%を確認した。これを超えるにはexternal clock経路しかない |
| **持続spool帯域** | 約98 MB/s | internal ring → PSRAMのtask copyの限界。channel数ではなくpacking後のbyte rateで決まる |
| **burst深度** | ring容量 ÷ (sampling − spool) | sampling超過分をringが吸収できる間だけ成立する。ring 64 KiBなら104 MHzで約1.2 Mi sample、112 MHzで約0.54 Mi sample。これは[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)のtriggerなし経路での粗い形で、床とchunk単位まで精密化した形は下の**条件2**にある(gate前提で測ったもので、triggerなし経路に同じ床とdrain曲線が当てはまるかは未測定) |

**gateがあるとsample rateの上限が上がる。** [E048](../experiments/e048_p4_gated_rate_ceiling/README.ja.md)で、hardware qualificationを併用したgated captureは内部clock源の上限160 MHzまで成立した。ただし持続spool帯域そのものは変わっていない。[E049](../experiments/e049_p4_gated_window_absorption/README.ja.md)がwindow長を伸ばして境界を探し、成立条件は次だと確定した。

```
sample rate ≤ 160 MHz（内部clock源）
かつ duty × sample rate × bytes/sample < 持続spool帯域（約98 MB/s）
```

つまり**gateは平均byte rateを持続限界の下へ下げている**。持続spool帯域そのものは上がっていない。

ただしこれは条件の一本目にすぎない。**window長にも上限がある。** [E050](../experiments/e050_p4_gated_window_at_fixed_duty/README.ja.md)はduty 50%固定でwindow byte長を224,000まで振って「全条件正常、window長は無関係」と結論したが、[E056](../experiments/e056_p4_ring_period_alias/README.ja.md)がその判定を無効にした(検証patternの周期がring容量を割り切っていたため、上書きが見えていなかった)。alias から外して測り直した[E057](../experiments/e057_p4_gated_ring_boundary/README.ja.md)で、window長は確かに効くことが分かっている。正確な形は下の**条件2**である。

8 channel(1 byte/sample)なら実用上の境界は次のようになる。

| duty | 許容sample rate | 実際に取れるrate |
|---:|---:|---:|
| 25% | 392 MHz相当 | 160 MHz(clock源) |
| 50% | 196 MHz相当 | 160 MHz |
| 60% | 163 MHz相当 | 160 MHz |
| 61%以上 | 160 MHz未満 | dutyから逆算 |

**duty 61%までは常に160 MHzが取れる。**

ただし条件はもう1本ある。

```
条件1（平均）duty × sample rate × bytes/sample < 持続spool帯域（約98 MB/s）
条件2（尖頭）ceil((8,064 + window byte長 × (1 − drain(rate) ÷ sample rate)) ÷ chunk size)
             ≤ min(floor(ring容量 ÷ chunk size), queue深さ)
```

chunk sizeは4,032 byteで、これは実測値ではなくSoC定義から決まる。`hal/dma_types.h`の`DMA_DESCRIPTOR_BUFFER_MAX_SIZE`は4,095(descriptorのsize fieldが12 bit)で、64 byte整列版の`DMA_DESCRIPTOR_BUFFER_MAX_SIZE_64B_ALIGNED`が`4095 − 63` = **4,032**である。P4のinternal RAMのcache line整列要件は64 byteなので([E016](../experiments/e016_p4_parlio_psram_direct/README.ja.md))、driverはこの値でtransactionを刻む。

**容量は「完全なchunkがいくつ入るか」で数える** — ringの端数にはdescriptorが載らないので使えない([E057](../experiments/e057_p4_gated_ring_boundary/README.ja.md))。したがって**ring容量は4,032の整数倍で取るのが無駄がない**。

尖頭未読の式は[E058](../experiments/e058_p4_window_drain_vs_rate/README.ja.md)が直接測って確定した。**8,064 byte(2 chunk)の床**はchunk通知とqueue投入のpipeline分で、過負荷が無くても常に乗る。**window中のdrain帯域はrate依存で、sample rateが上がるほど下がる。**

| sample rate | window中のdrain（ISRと同一core） |
|---:|---:|
| 100 MHz以下 | 100 MB/s以上（過負荷が生じないため上限は未測定） |
| 120 MHz | 95.9 MB/s |
| 160 MHz | 86.1 MB/s |

**回収をISRと別coreへ置くとdrainは大きく上がる。** [E061](../experiments/e061_p4_drain_core_split/README.ja.md)が160 MHzで測ったところ、同一coreの82.3 MB/sに対し分離で**119.7 MB/s(+45%)**になった。memcpy帯域自体も107.6から131.0 MB/sへ上がるので、ISRがmemcpyを内側で中断していた分が消えたことになる。

| 回収core | 160 MHzでのdrain | ring 15 chunkで許容されるwindow byte長 |
|---|---:|---:|
| ISRと同一 | 82.3 MB/s | 約107,900 |
| **分離** | **119.7 MB/s** | **約208,000** |

**同じringで約1.9倍長いwindowが通る。** window長を固定するならring容量が半分で済む。実装ではdriverの生成・enableをloop task上で行い(ISRはそのcoreに載る)、回収loopだけを`xTaskCreatePinnedToCore`で別coreへ出す。分離時のrate依存は160 MHzでしか測っていない。

[E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)の持続spool帯域98 MB/sはこの曲線上の一点にあたる。予測の当てはまりはwindow 96,000 byteで**48 byte以内**である。

**低下の理由は[E059](../experiments/e059_p4_drain_breakdown/README.ja.md)で分解できた。** memcpyを直接計時すると、gated capture中のmemcpy帯域は**100〜160 MHzで107 MB/s一定**である(この107はISRがmemcpyを内側で中断した分を含む値で、別coreへ分ければ131.0 MB/sになる。[E020](../experiments/e020_p4_psram_copy_bandwidth/README.ja.md)のDMA無し138.6〜182.7 MB/sとの残差5〜25%が本当のmemory競合分)。drainが下がるのは**1 chunkあたり3.6〜5.2 usの固定cost**(ISR、`xQueueReceive`、loop本体)が原因で、window byte長が同じならchunk数も同じなので、windowが短くなるほどこのcostの占める割合が増える。

```
drain(rate) = 107 MB/s × memcpy占有率(rate)
memcpy占有率 = 100 MHzで89%、120 MHzで86%、160 MHzで77%
```

改善の方向として1 chunkあたりのcostを削る手を[E060](../experiments/e060_p4_drain_batch_coalesce/README.ja.md)で試したが、**task側の書き方では下がらなかった**。

- `xQueueReceive`のまとめ取り(1 batch最大12 chunk)は**効果ゼロ** — ISR側の未読最大は54,656で完全に同一
- 連続chunkを1回のmemcpyへまとめる(呼び出し1,070→277、平均copy size 3,920→15,142 byte)も**memcpy時間は1%減**だけ

memcpyの累積時間は3条件でほぼ一定なので、**固定costはdriver側のISRが支配している**。同一core上ではmemcpy帯域は107〜109 MB/sでcopy sizeにも依存しない。**残った手のcore分離は[E061](../experiments/e061_p4_drain_core_split/README.ja.md)で実際に効いた**(上の表)。

**per-chunk copyを維持する。** memcpyをまとめると`consumed_bytes`がrun単位でしか進まず、未読の実測値が最大1 run分過大に出る(E060では54,656が70,784になったがdataは正常)。**未読の実測を条件2の判定に使えるのはper-chunk copyのときだけ**で、それ以外は計算で判定する。

**未読の測定はISR内で行う。** taskがdequeueごとに標本化する方法はちょうど1 chunk分だけ尖頭を見落とす(E058が4条件すべてで正確に4,032 byteの差を確認)。未読が増えるのはISRがchunkを通知する瞬間だけである。

容量が`min()`の二つである根拠は二段ある。[E055](../experiments/e055_p4_gated_buffer_source/README.ja.md)はqueueを64から8 entryへ浅くするとring容量に関係なく破綻することを示し、[E056](../experiments/e056_p4_ring_period_alias/README.ja.md)はringも独立に効くことを示した。**ringとqueueは両方が制約である。**

E057がalias から外したring 63,488(完全chunk 15個)で境界を実測し、この形で先行実験の8条件すべてが説明できることを確認した。

| 条件 | window byte | 必要chunk | ring完全chunk | queue深さ | 予測 | 実測 |
|---|---:|---:|---:|---:|---|---|
| E057 gate 3,000 | 96,000 | 12 | 15 | 64 | 正常 | 正常 |
| E057 gate 4,000 | 128,000 | 16 | 15 | 64 | 破綻 | 破綻 |
| E057 gate 5,000 | 160,000 | 20 | 15 | 64 | 破綻 | 破綻 |
| [E050](../experiments/e050_p4_gated_window_at_fixed_duty/README.ja.md) gate 4,000 | 128,000 | 16 | 16 | 64 | 正常(境界上) | 正常 |
| [E055](../experiments/e055_p4_gated_buffer_source/README.ja.md) ring 64 KiB / queue 64 | 192,000 | 24 | 16 | 64 | 破綻 | 破綻 |
| E055 ring 128 KiB / queue 64 | 192,000 | 24 | 32 | 64 | 正常 | 正常 |
| E055 queue 8の2条件 | 192,000 | 24 | 16 / 32 | 8 | 破綻 | 破綻 |
| [E048](../experiments/e048_p4_gated_rate_ceiling/README.ja.md) 160 MHz | 65,408 | 8 | 16 | 64 | 正常 | 正常 |

**drop判定はwindow長からの計算で行う。** task側で標本化した未読は1 chunk分だけ尖頭を過小に見る — E057のgate 4,000は破綻しているのに未読最大59,456で完全chunk容量60,480すら下回った(ISR側なら63,488相当)。ringは周回bufferではなく`max_recv_size`まで線形に歩くが(chunk offsetは4,032 byte刻みで単調増加し、ring 128 KiBでは最大offsetも128,000まで伸びる)、transactionが端まで行くと先頭から書き直すので未読は上書きされる。**ring容量はchunk sizeの整数倍で取る**(端数は使えない)。

> **検証器の落とし穴。** E048からE055までの間、未読がring容量を超えてもdataが正常に見える条件が続いた。原因は検証用gray code rampの周期(4,096 byte)がring容量65,536と131,072を割り切っていたことで、位置Xを`X + ring容量`のdataで上書きしても同じ値が書かれ痕跡が残らなかった。[E056](../experiments/e056_p4_ring_period_alias/README.ja.md)でring容量を倍数から外すと、同じ条件で飛びが22から172へ跳ねた。**検証用patternの周期は、経路上のどのbuffer size(ring容量・chunk size・queue容量・destination容量)も割り切ってはならない。** [E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)が定常性の盲点を直したのに対し、これは周期性の別の盲点である。この影響でE049・E050・E055の一部条件のdata検証は無効になっており、各レポートに追記した。

triggerなしの場合に戻ると、sample rateの成立・不成立は**capture深度と一緒でなければ意味を持たない**。8 channel 100 MHzは1 Mi sampleでは成立するが、超過分1.692 MB/sをringが吸収しきる約3.87 Mi sampleで破綻するので、[E030](../experiments/e030_p4_deep_batch_capture/README.ja.md)の16 MiB deep captureには適用できない。深度を伸ばすほど公称rateは持続spool帯域へ漸近する。

triggerなし経路のdrop判定にも同じ整理が要る。`queue_overflow`はchunk queueが満杯になった時点しか見ておらず、queue 64段はring容量の約3.8倍あるので、ringが上書きされてもAPIは`ESP_OK`を返す。判定に使う量は**ring上の未読byte数がring容量を超えたか**である。gate前提の経路では容量が`min(ring容量, queue深さ × chunk size)`になり、床とchunk単位まで含めた形が条件2である。

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
- 32,767 tickを超えるgapの扱いと、RMT分解能を落としたときの精度
- destinationを大きくしたgated captureの長時間持続(現在はdata検証が1 MiB分)
- `en_partial_rx=false`のときの発火条件と、48 symbol溜まる前に`rmt_disable`して取れる分だけ回収できるか(応答性が要る用途の逃げ道)
- core分離時のdrainのrate依存(160 MHzでしか測っていない)と、分離後に残る9%の非memcpy時間の内訳
- triggerなしspool経路がpattern周期のalias で盲にならなかった理由
- duty 61%付近で160 MHzが取れなくなる点の実測
- data_width 16での3者共有(`valid_sig_line_id`に空きslotが無い可能性)
- RMT symbolとPARLIO sample列の先頭同期
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
| **hardware capture qualification** | CS / enable線でburstが区切られる信号(SPI、I2C等) | qualifierが常にactiveな信号。gate外の情報は完全に失われる | 削減率はdutyに一致([E040](../experiments/e040_p4_parlio_level_open_frame/README.ja.md))。window境界はRMT RXがsample単位で記録([E045](../experiments/e045_p4_gate_rmt_order/README.ja.md)) |

圧縮は常に有効にしない。各blockにencoding、raw sample数、encoded byte数を持たせ、圧縮後がraw以上ならraw blockを保存する方式を基準候補とする。これなら最悪入力でも容量を大きく失わない。

**PARLIO RXのsample clockは160 MHz(`PLL_F160M`)の分数分周**である — integer 1〜256 に numerator / denominator が付くので、**160 ÷ 整数に限られない**(86 / 90 / 94 MHzが周期860 / 900 / 940でちょうど取れることで実証済み。[E084](../experiments/e084_p4_transfer_tuning/README.ja.md))。source は他に XTAL 40 MHz / RC_FAST / 外部clock入力も選べる。**上限は源の160 MHz。**

**内部信号源を使うときは、信号源を先にattachしてからPARLIO receiverを作ること**([E083](../experiments/e083_p4_attach_order/README.ja.md))。逆順にすると**cold bootの29%で1 channelが無音になる**(`overflow`は0のまま)。**同一boot内では再現しない**ので、「再実行したら直った」は直っていない。

**周期的な信号をどこまで圧縮できるかの見積りは[capture の圧縮](capture-compression.ja.md)にまとめた**(要点: 効くかどうかは信号周波数ではなく **oversampling比 K** で決まり、**RLEの圧縮率 ≈ K/8**。K<8では逆に膨らむ。SPIならRLEより**CSでgateするほうが強く、CPUも使わない**)。

hardware capture qualificationはこの表の中で唯一**CPUを使わない**手段である。[E040](../experiments/e040_p4_parlio_level_open_frame/README.ja.md)で、level delimiterのgateがactiveな区間のsampleだけがDMAへ渡ることを実測した(gate duty 12.5%に対して回収byte rateはraw byte rateの12%)。入力の性質に依存せず最悪時膨張も無い代わりに、qualifier線を1本消費し、gate外の情報は残らない。同じPSRAM容量でduty分だけ長い時間を覆え、spool帯域([E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)の約98 MB/s)も同じ比率で緩む。現ベンチのdownloadがUART上限に縛られている(下の「後段」)ことを考えると、深度を伸ばすより効く手段である。

[E043](../experiments/e043_p4_parlio_gate_window_boundary/README.ja.md)で、**間引かれたdataの中にwindow境界は残らない**ことが分かった。window長自体はgate幅から一意に決まりばらつき0だが、callbackはDMA descriptorの4,032 byte単位で切れるだけでgate境界とは無関係である。

これは**gate線をRMT RXへも分岐させることで解決した**([E045](../experiments/e045_p4_gate_rmt_order/README.ja.md))。gate線はGPIOなので、GPIO matrixのfan-outで同じpinをPARLIOのvalid入力とRMT RXの入力へ同時に渡せる。RMT分解能をsample rateに合わせれば1 tick = 1 sampleとなり、各high / low区間の長さがsample単位で読める。実測ではhigh 16個すべてが8,176 tick、low 16個すべてが24,592 tickで期待値と完全一致した。生成順の制約は無く、PARLIO側のcaptureも壊れない。

つまりqualificationはhardware 3役の1本に集約される。[E047](../experiments/e047_p4_gate_three_way_share/README.ja.md)で、同じGPIOをPARLIOのdata線・PARLIOのvalid線・RMT RXの入力へ**同時に**割り当てられることを確認した。

```
GPIO 9(1本 / data_width 8、GPIO 2〜9で8 channel)
   ├─→ PARLIO RX data line 7 : channel 7としてsampleに残る
   ├─→ PARLIO RX valid       : gate区間のsampleだけをDMAでPSRAMへ
   └─→ RMT RX                : 各high / lowの長さをsample単位で記録
```

**8 channel + qualification + window timestampが8 pin・追加channel 0・CPU負荷0で成立する。** data_width 8を選べば1 sample = 1 byte、RMT分解能20 MHzで1 tick = 1 sampleとなり、`window byte長 = RMT high duration`が割り算なしで成り立つ。実測ではbit 7が0のsampleは262,144中0件、飛びの階差はRMT durationと15箇所すべてで一致した。

qualifierに選ぶchannelには制約がある。gate区間内では常にactiveなので、その線の波形情報は「activeだった」以外に残らない。CS / enable / frame同期のように**値の変化に意味のない線**へ割り当てる。

**RMT側のtimestampが届くtimingは計算できる**([E052](../experiments/e052_p4_rmt_callback_timing/README.ja.md)・[E053](../experiments/e053_p4_rmt_dma_block/README.ja.md))。

```
group = mem_block_symbols ÷ 2
n = floor(user_buffer ÷ group)          ← n = 0 なら永久に発火しない
1 callbackあたりのsymbol数 = n × group
初回遅延 = (mem_block_symbols + (n − 1) × group) × gate周期
以降の更新間隔 = n × group × gate周期
```

`mem_block_symbols`はP4のnon-DMA modeでは48が最小である。**user bufferを`group`(= 24)ちょうどにすると遅延が最小化され**、gate周期2.4 msで初回115 ms・以降57.6 msごと、gate周期0.4 msで初回19.2 ms・以降9.6 msごとになる。user bufferを広げると1回あたりのsymbol数は増えるが初回遅延と間隔も同じ比率で伸びる(buffer 128では初回346 ms・1回120 symbol)。

**`user_buffer < group`だとcallbackは一度も来ない。** これを踏み外すと無音になるので、設定時に必ず確認する。

**DMA mode(`flags.with_dma`)は使わない。** [E054](../experiments/e054_p4_rmt_dma_block_min/README.ja.md)で48未満(8 / 16 / 24 / 32 / 40)はすべて`ESP_ERR_INVALID_ARG`で拒否され、受理された48も**64として振る舞う**ことが分かった。初回遅延は常に`64 × gate周期`でnon-DMAの`48 × gate周期`より遅い。

したがって**初回遅延の下限は`48 × gate周期`**で、構成はnon-DMA・`mem_block_symbols` 48・user buffer 24である。

| gate周期 | 初回遅延 | 更新間隔 |
|---:|---:|---:|
| 0.4 ms | 19.2 ms | 9.6 ms |
| 0.8 ms | 38.4 ms | 19.2 ms |
| 1.6 ms | 76.8 ms | 38.4 ms |
| 2.4 ms | 115.2 ms | 57.6 ms |

**CPU負荷では変わらない** — 160 MHz・duty 50%の入力をPSRAMへcopyし続けてCPUを飽和させても発火時刻の差は3 usだった。gate周期の遅い信号では初回のtimestampが数百ms遅れて届くが、durationの正確さは損なわれない。この規則でE045からE053までの16条件すべての実測callback回数が説明できる。

hostへはsample列とwindow長の列を組で渡せば、間引いたまま時間軸を再構成できる。**幅が可変なgateでも成立する**([E046](../experiments/e046_p4_gate_variable_width/README.ja.md))— 幅の違う4 windowに対しRMTは各長さを個別に正しく返し、capture data中の切れ目の階差がRMT high durationの列と15箇所すべてで一致した。lowのdurationも記録されるので、捨てた区間の長さと各windowの絶対時刻位置まで求まる。 払うものはRMT RX channel 1つ(P4は4 channel)、1 levelあたり32,767 tickの上限(20 MHz分解能で1.638 ms。超えるなら分解能を落とす)、そしてcallback遅延(初回`mem_block_symbols × gate周期`、以降その半分。詳細は下の「Trigger」節)である。captureするchannel数は払わない。

### 後段

batch本体の限界を確定した後に、PSRAM→USB device Bulk IN、IP、file保存を測る。streamingは最後に独立して測り、batch側のsample rateやtrigger結果と混ぜない。PC側applicationでsigrok互換形式、VCD、CSV等へ変換する。

**USB 2.0 HSのdevice経路は[E063](../experiments/e063_p4_usb_hs_enumerate/README.ja.md)で開いた。** 別個体のESP32-P4(`esp32-p4-30eda0e31478`)をHS portとUSB-Serial-JTAGの2本でWindows 11へ繋いだベンチで、Arduino-ESP32 3.3.11が**High-Speedで列挙する**ことを確認した(device側`tud_speed_get()=2`、host側`Device Bus Speed 0x02`、bulk endpoint 512 B)。**USB-Serial-JTAGのconsoleと書込みは同時に生きる**ので、download経路を試しながら焼き直せる。

**CDC経路のthroughputは[E064](../experiments/e064_p4_usb_hs_cdc_rate/README.ja.md)で測った。約5.6〜5.7 MB/sで飽和する。**

| 取得量 | HS CDC実測(5.65 MB/s) | 旧ベンチ CH343 6 Mbaud想定 |
|---:|---:|---:|
| 64 KiB(hardware trigger frameの上限) | 約0.012秒 | 約0.11秒 |
| 1 MiB | 約0.19秒(実測) | 約1.7秒 |
| 16 MiB([E030](../experiments/e030_p4_deep_batch_capture/README.ja.md)のdeep batch) | **2.968秒(実測)** | 約28秒 |

**9.4倍速くなった結果、「深度を伸ばすことの価値はdownload時間に食われる」という判断は緩む。** 16 MiBが3秒で出るなら、深度はもう律速ではない。ただし**連続streamingの上限としては5.6 MB/s**であり、2 channel(1 byteに4 sample)なら約22.4 Msps相当にとどまる。

**送出taskをどのcoreに置くかで1.53倍変わる**([E066](../experiments/e066_p4_usb_hs_tx_context/README.ja.md))。`ARDUINO_RUNNING_CORE`は1で、**core 1(= `loop()`と同じ側)では5.2〜5.7 MB/s、core 0では7.4〜8.1 MB/s**。優先度は効かない。上の5.65 MB/sは`loop()`から流した値なので、**core 0へ置けば16 MiBは約2.08秒**になる。`USB.begin()`が`setup()`(core 1)から呼ばれるためUSB割り込みがcore 1にあり、送出がそれと競合している、というのが構造からの読み(未測定)。

飽和の理由はturnaroundと読める。最速の8.08 MB/s ÷ 512 B = 約15,800 transaction/s、HSのmicroframeは8,000回/秒なので**1 microframeあたり約1.97 transaction**しか出ていない(HSは13まで許す)。TinyUSBのCDC TX FIFOがbulk 1 packet分(`CONFIG_TINYUSB_CDC_TX_BUFSIZE=512`)しかないことと整合する。**endpointを増やしても合計は増えない**ことは[E065](../experiments/e065_p4_usb_hs_dual_cdc_rate/README.ja.md)で確認済み(2本で比0.943)なので、**vendor bulkへ逃がしても同じ天井に当たる可能性が高い**(未測定)。

**captureとUSB送出を同時に走らせても、折れるのはUSB側だけである**([E067](../experiments/e067_p4_usb_vs_capture_core/README.ja.md))。2 channel / 32 MHz(8.0 MB/s)のcaptureは18回すべてで**8.00 MB/s、overflow 0、取りこぼし0**で、USBが何をしていても揺れなかった。USB側は単独比**93%(分離)〜84%(同居)**へ落ちる。最良の配置は**harvest = core 1、USB = core 0で7.42 MB/s**。順位を決めているのは主に**USB taskのcore**で、2つを分けることの上積みは約9 pointにとどまる。

したがって**連続streamingの釣り合い点は約29.7 Msps(2 channel)**である — 生成8.00 MB/sに対し排出7.42 MB/s。それを超える rate は**PSRAMへbatchしてから出す**。

> **2026-09-13 実測([E078](../experiments/e078_p4_continuous_stream/README.ja.md))**: [EspUsbDevice の改修](espusbdevice-change-requests.ja.md)後、**連続 streaming の上限は 96 Msps(線上 24.0 MB/s)**([E084](../experiments/e084_p4_transfer_tuning/README.ja.md) で TX FIFO / 1 転送を 8 KiB にした後。既定の形では 86 Msps)。88 Msps から弾性 FIFO の占有が duration に比例して積む。**同居による USB の損は消えた**(飽和時 21.4〜22.4 MB/s で単体と同等以上)。上の 7〜16% の損は**競合ではなく送出 task の spin**が原因で、`waitWritable()` で block するようにしたら `stalls` は全条件 0 になった。**batch なら 160 Msps まで取れる**([E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md))ので、**継ぎ目を許すかどうかで上限が約 2 倍違う**。

⚠ **この経路は現状そのままでは使えない。** [E068](../experiments/e068_p4_hs_cdc_tail_loss/README.ja.md)で、**4 MiBの転送30回中4回(13%)、転送の途中で512 B packet単位(4〜5 packet、2,048〜2,560 B)のdataが黙って消える**ことが分かった。**待っても突いても戻らず、dataは失われている。** deviceは`written`も`short`も正常と申告し、**device側もhost側も気づかない**。

したがって**この節の帯域の数値(device側時計で測ったもの)は有効だが、「その帯域でdataが正しく渡る」とは言えない**。原因は未特定で、候補はTinyUSBのCDC TX FIFOに対するapplication taskとusbd taskの跨core競合。**当面は転送に長さとCRCを付けてhostが検証・再送要求できるようにし、pattern照合なしの転送を信用しないこと。**

**vendor bulkへ逃がすと天井は上がり、欠落もなくなる。** [E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md)がCDCの8.08 MB/sを超える9.73 MB/sを出し(**上の「同じ天井に当たる可能性が高い」は否定された** — 天井はdeviceではなくWindowsの`usbser`側だった)、[E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md)がTX FIFOを8 KiBにして10.74 MB/sへ伸ばした。**data欠落は2つのstack合わせて33転送で0件。**

**capture → download → `.sr`の通しは[E076](../experiments/e076_p4_capture_hs_download/README.ja.md)で成立した。**

| 取得量 | **OTG HS vendor bulk 実測(8.80 MB/s)** | HS CDC(5.65 MB/s) | 旧ベンチ CH343 6 Mbaud |
|---:|---:|---:|---:|
| 64 KiB | 約0.007秒 | 約0.012秒 | 約0.11秒 |
| 1 MiB | 約0.12秒 | 約0.19秒 | 約1.7秒 |
| **4 MiB** | **0.48秒(実測、7回)** | 約0.74秒 | 約7秒 |
| 16 MiB | 約1.9秒 | 2.968秒(実測) | 約28秒 |

送出元がPSRAMであることの不利はない — 同じloopでinternal RAMから送った対照は8.38、PSRAMは9.04 MB/sで**PSRAMの方がわずかに速い**。**ただし同一条件で1.65倍ばらつく**(6.6〜10.9 MB/s)ので、**連続streamingの釣り合い点は最良値ではなく実測mean 8.80 MB/s = 約35 Msps(2 channel)で見る**。

> **2026-09-13 追記**: [EspUsbDevice の改修](../references/espusbdevice-change-requests.ja.md)で **vendor bulk は約 21 MB/s(飽和 23)** になった。効いたのは 1 転送あたりの packet 数で、**ばらつきの機序(ZLP と short packet による URB 再投入)も特定された**。**4 MiB は約 0.20 秒、2 channel の釣り合い点は約 84 Msps** になる見込みで、**持続 spool 帯域(約 98 MB/s)に近づくため律速が USB から capture 側へ移る可能性がある**。実測は [E078](../experiments/e078_p4_continuous_stream/README.ja.md)。

### 旧ベンチ(UART download)の見積り

以下は`esp32-p4-e8f60ae0aa24`(host側portが`1a86:55d3` = WCH CH343のUSB-UART bridge)での見積りで、**上のHS実測に置き換わった**。

| 取得量 | 6 Mbaud想定のdownload時間 |
|---:|---:|
| 64 KiB(hardware trigger frameの上限) | 約0.11秒 |
| 1 MiB | 約1.7秒 |
| 16 MiB([E030](../experiments/e030_p4_deep_batch_capture/README.ja.md)のdeep batch) | 約28秒 |

この前提のもとでは**深度を伸ばすことの価値はdownload時間に食われる**。16 MiBを取れてもhostへ出すのに30秒かかるなら、深度より次の二つが効く。**HS経路(E064)ではこの前提が崩れており、以下はUART経路に限った話である。**

- **capture qualification**([E040](../experiments/e040_p4_parlio_level_open_frame/README.ja.md))— gateのdutyだけ保存量が減り、CPU負荷はゼロ
- **内部圧縮** — 上の表の方式

「速いが浅い」hardware trigger frame(64 KiB)は0.11秒で出せるので、downloadの制約を受けない唯一のmodeである。

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
