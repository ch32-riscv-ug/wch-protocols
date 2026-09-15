# ESP32-P4 ロジアナの sample rate — 何を出せて、何を選ばせるか

状態: **reference**(2026-09-15。[E108](../experiments/e108_p4_zero_copy_stream/README.ja.md) / [E107](../experiments/e107_p4_stream_core_placement/README.ja.md) / [E106](../experiments/e106_p4_mixed_rate_capture_stream/README.ja.md) / [E086](../experiments/e086_p4_8ch_stream/README.ja.md) / [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) / [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) の実測から)

PulseView などへ「選べる sample rate」を出すとき、**値は 3 つの事情で決まる** — clock で作れるか、線に載るか、client の driver が受け付けるか。

## 0. 製品としての要約

方針（持ち主、2026-09-15）: **16 channel時の仕様を前面に出す。** 最大60 Mspsの高速取得を複数channelで行い、時間解像度を落としたchannelを多数足して合計16 channelまで取る。USBの通信速度は環境で変わるので、高速取得できるchannel数は環境で異なる。速度は実測してその約9割で使うことを推奨する。8 channel以下なら100 Msps、2 channel以下なら160 Mspsで取得できる場合もある。数値の目安は最終的に持ち主が書き換える。

配分の式は`Σ(channel iのrate) ≤ 0.9 × probe実測`である。高速channelは1 bit/sampleなので60 Mspsは60 Mbps、1/32へ縮約したchannelは1.875 Mbpsになる。

base 60 Mspsのとき、縮約したchannelの時間刻みは 1/2 = 30 M（33 ns）、1/4 = 15 M（67 ns）、1/8 = 7.5 M（133 ns）、1/16 = 3.75 M（267 ns）、1/32 = 1.875 M（533 ns）、1/64 = 0.94 M（1.07 µs）。高速channelを何本残すか、残りをどの刻みで取るかの組み合わせで、同じ環境でも配分は何通りも作れる。いずれも合計16 channel。

| 環境（probe実測） | 構成（channel数 × rate） | 合計 | 実測比 |
|---|---|---:|---:|
| 389 Mbps（E110、PC直結 usbipd/WSL） | 60 M × 5 ＋ 1/64（0.94 M）× 11 | 310.3 Mbps | 80% |
| 389 Mbps | 60 M × 4 ＋ 1/2（30 M）× 2 ＋ 1/8（7.5 M）× 4 ＋ 1/32（1.875 M）× 6 | 341.25 Mbps | 88% |
| 389 Mbps | 全16 chを同率20 M | 320 Mbps | 82% |
| 300 Mbps | 60 M × 5 | 300 Mbps | 100%（上限いっぱい。9割規則では入らない） |
| 300 Mbps | 60 M × 4 ＋ 1/32（1.875 M）× 12 | 262.5 Mbps | 87.5% |
| 300 Mbps | 60 M × 3 ＋ 1/2（30 M）× 2 ＋ 1/8（7.5 M）× 2 ＋ 1/64（0.94 M）× 9 | 263.4 Mbps | 88% |
| 300 Mbps | 60 M × 2 ＋ 1/2（30 M）× 2 ＋ 1/4（15 M）× 4 ＋ 1/32（1.875 M）× 8 | 255 Mbps | 85% |
| 300 Mbps | 全16 chを同率15 M | 240 Mbps | 80% |
| 200 Mbps | 60 M × 2 ＋ 1/4（15 M）× 2 ＋ 1/16（3.75 M）× 4 ＋ 1/64（0.94 M）× 8 | 172.5 Mbps | 86% |
| 200 Mbps | 60 M × 3（SPI CLK / MISO / MOSI）＋ 1/8（7.5 M）× 1（CS）＋ 1/64 × 12（GPIO） | 198.75 Mbps | 99%（入らない。base 50 Mにすると165.6 Mbps＝83%） |
| 150 Mbps（hub 2段の実測120〜150） | 40 M × 3 ＋ 1/8（5 M）× 1 ＋ 1/64（0.625 M）× 12（E106のwide profile） | 132.5 Mbps | 88% |
| 150 Mbps | 60 M × 1 ＋ 1/2（30 M）× 1 ＋ 1/4（15 M）× 2 ＋ 1/64（0.94 M）× 12 | 131.25 Mbps | 87.5% |

以下は従来の説明で、数値の裏付けは各実験にある。全channelを高速rateでPCへ送れるという意味ではない。内部raw帯域、channel別縮約codec、USB帯域のすべてに収まる構成だけをacceptする。少数channelにはさらに高速な技術的余地があるが、連続転送まで成立する通常仕様としては前面に出さず、検証後に必要なら高速モードとして分離する。

USB帯域はcapture方向(P4→PC)をcapture開始前に短時間probeする。たとえばprobe実測が**200 Mbpsなら、その90%の180 Mbpsを推奨予算**とする。150〜300 MbpsはPCのUSB controller、hub、cable、OS経路による参考範囲であり、固定保証値にはしない。これまでの実測で約300 Mbpsが出たのは主にPC→P4方向で、LAに必要なP4→PCは約120〜200 Mbpsだった。

channel別rateは別々のsampling clockではない。全pinを共通base clockでcaptureした後、高速channelは全sampleを残し、CS / INT / button等だけ1/2、1/4、1/8…へ縮約してtransport/storage量を減らす。
通常UIでは1/2、1/4、1/8、1/16、1/32、1/64を選択肢とする。形式上は1/128以下も可能だが、3本の高速channelが残る構成では追加の帯域削減が小さく、blockをまたぐcodec状態と待ち時間が増えるため初期仕様には含めない。

例としてprobe 200 Mbps、推奨予算180 Mbpsなら、SPIのCLK/MISO/MOSIを各50 Msps、CSを1/8の6.25 Mspsとして、合計は**156.25 Mbps**になる。4本すべてを同率にすると180 Mbpsでは45 Mspsが上限なので、高速3本の時間解像度を上げながら23.75 Mbpsの余裕も残せる。60 Msps×3＋CS 7.5 Msps = **187.5 Mbps**は理論200 Mbpsには入るが、90%予算180 Mbpsには入らない。

16 channel / 40 Mspsでは、3本をfull rate、1本を1/8、残る12本を1/64にすると合計**132.5 Mbps**となる。128-sample codec blockならpaddingなしの53 byteになり、150 Mbps実測の90%予算135 Mbpsにも収まる。P4内部sinkで40 Mspsを3回連続PASSし、現在のUSB経路では32 Msps / 106 Mbpsを69.5 MB完全検査PASSした。1/64 channelの時間刻みは1.6 us（625 ksps）なのでbuttonには十分で、CS / INTには用途に応じて`any_active`を使い短pulseの存在を残す。

`decimate_hold`はbucket中の短いpulseを見落とす。active-low CS/INTには、bucket内で一度でもactiveなら残す`any_active`を選べるようにする。この場合pulseの存在は残せるが、edge位置は最大D-1 base sampleぶん量子化・拡幅される。表示時はbase sample gridへhold展開するため、低rate channelの見た目の時間精度が上がるわけではない。

8-bitの3 raw＋5 slow D=64は内部持続試験で61 Mspsまで成立し、62 Mspsで破綻したため内部安全値を60 Mspsとする。16-bit wideの3 raw＋1 D8＋12 D64は内部40 Mspsを3回PASSした。hub 2段ではUSB probe 120.86 Mbps、90%予算108.77 Mbpsだったが、PC直結では212.67 Mbps、90%予算191.40 Mbpsへ改善した。E106の結合で8-bit 44 / 16-bit wide 32 Mspsに留まった原因は、[E107](../experiments/e107_p4_stream_core_placement/README.ja.md)でUSB割り込みとusbd taskがcodecと同じcore 1に乗っていたことと分かった。USBをcore 0で初期化しcodec loopをprofile別に直すと、PC直結（Windows native）で8-bit 60 Msps 5回、wide 40 Msps 3回PASSした。同経路のUSB probeは193 Mbps、90%予算173 Mbpsなので8-bit 60 Msps（187.5 Mbps）は予算超えであり、accept判定はUSB予算、内部sink上限、結合上限の最小を見る。この経路での8-bitの通常値は52〜55 Msps、wideは40 Mspsになる。さらに[E108](../experiments/e108_p4_zero_copy_stream/README.ja.md)でUSB帰路をzero-copyにするとUSB-onlyはusbipd/WSL直結で247 Mbps（90%予算222 Mbps）、結合上限はcodecだけで8-bit 72 / wide 52 Mspsになり、8-bit 60（予算の84%）とwide 40（60%）は余裕を持つ通常値になる。accept判定の式は変わらず、probe値と内部上限・結合上限の最小を取る。[E110](../experiments/e110_p4_usb_in_ceiling/README.ja.md)でDWC2のbulk IN TX FIFOを2 packetにするとprobeは389 Mbps（usbipd/WSL、90%予算350 Mbps）／377 Mbps（native、339 Mbps）になり、8-bit 3.125 bit/sampleなら約110 Msps、wide 3.3125 bit/sampleなら約105 MspsまでUSB予算に収まる。上限を決めるのはcodecである。[E111](../experiments/e111_p4_dual_core_codec/README.ja.md)でcodecを2 workerにすると結合上限は8-bit 108 / wide 72 Mspsになり、8-bitはUSB予算（350 Mbps≒112 Msps）、wideはcore 1のcodecが次の律速になる。[E109](../experiments/e109_p4_stream_soak/README.ja.md)でこの通常値は60 s soak・交互20回・Windows native（probe 221 Mbps、90%予算199 Mbps）でも欠損0で、直結については確定した。

## 1. clock で作れる rate — 1 MHz 刻みでも 10 MHz 刻みでも全部出る

**source は `PLL_F160M` = 160 MHz、分周器は integer(1〜256)+ numerator / denominator の分数分周**(`parlio_ll_rx_set_clock_div()`)。**160 ÷ 整数に限られない。**

実測(周期 = rate ÷ 100 kHz がちょうど出るかで判定):

| 掃引した rate | 結果 |
|---|---|
| **1 / 2 / 3 / 4 / 5 / 6 / 7 / 8 / 9 MHz** | **全部ちょうど**(周期 10 / 20 / … / 90) |
| **10 / 20 / 30 / 40 / 50 / 60 / 70 / 80 / 90 / 100 MHz** | **全部ちょうど**(周期 100 / 200 / … / 1000) |
| 86 / 88 / 90 / 92 / 94 / 96 / 98 MHz | ちょうど([E084](../experiments/e084_p4_transfer_tuning/README.ja.md)) |
| 160 MHz | ちょうど([E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) / [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md)) |

**上限は源の 160 MHz、下限は integer 256 分周で約 625 kHz**(分数分周でさらに細かく)。

> **希望どおりの刻みが作れる** — 一桁は 1 MHz 刻み、それ以上は 10 MHz 刻み。**clock 側の制約は無い。**

## 2. 線に載る rate — byte rate 23 MB/s が天井

**packing は `1 byte = 8 ÷ channel 数 sample`** なので、**線上の byte rate = `rate × channel 数 ÷ 8`**。

| channel | **継ぎ目なく流せる最大 rate** | = byte rate | 出典 |
|---:|---:|---:|---|
| **8ch** | **23 Msps**(余裕を見て 20) | 23.0 MB/s | [E086](../experiments/e086_p4_8ch_stream/README.ja.md) |
| **4ch** | **46 Msps**(48 は際どい) | 23.0 MB/s | 同上 |
| **2ch** | **96 Msps** | 24.0 MB/s | [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) |
| 1ch | (byte rate では 184 Msps 相当だが)**160 Msps で clock 頭打ち** | 20.0 MB/s | 未測定 |

**上限は sample rate ではなく byte rate で決まる**(8 / 4 / 2ch が同じ byte rate で頭打ちになる)。**見積もりは `rate × channel ÷ 8 ≤ 23 MB/s`。**

### batch なら別の天井

**PSRAM へ貯めてから降ろす**なら USB は関係ない。律速は **PARLIO の持続 spool 帯域(約 98 MB/s)**と **PSRAM の容量**。

| channel | batch の上限 | 出典 |
|---:|---|---|
| 1 / 2 / 4ch | **160 Msps**(clock 源の上限) | [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) / [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) |
| 8ch | **96 Msps**(1 MiB で sample 精度)。160 Msps は burst 窓のぶんだけ | [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) |

**継ぎ目を許すかどうかで上限が 2〜4 倍違う。**

## 3. client が受け付ける rate — driver ごとに違う

**「選べる rate」は演じる device の driver が決める。** ここが target によって変わる部分である。

| 経路 | client 側の list | 効く制約 |
|---|---|---|
| **BeagleLogic の TCP を演じる**([E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md) / [E080](../experiments/e080_p4_pulseview_gapless/README.ja.md)) | **10 Hz〜100 MHz**(libsigrok の `beaglelogic` driver) | **100 MHz を超える rate は要求できない。** 160 Msps は出せない |
| `.sr` を書く([E082](../experiments/e082_p4_spool_then_convert/README.ja.md)) | **制約なし**(metadata に書くだけ) | 160 Msps も書ける |
| (参考)fx2lafw | 20 kHz〜24 MHz の離散 list | 置き換える相手の値 |

**BeagleLogic の sample unit は 1 byte(8 channel)か 2 byte(最大 14 channel)** なので、**8 channel はそのまま 1 byte/sample に載る**。[E086](../experiments/e086_p4_8ch_stream/README.ja.md) のとおり **PARLIO の packed がそのまま sigrok の形式**になり、host 側の展開が要らない。

## 4. したがって、何を出させるか

**channel 数が決まると上限が決まる**ので、**server は channel 数に応じて list を変える**のがよい。

| channel | 出してよい list(streaming) | 上限の根拠 |
|---:|---|---|
| **8ch** | 1 / 2 / 3 / 4 / 5 / 6 / 7 / 8 / 9 / 10 / **20** Msps | 23 Msps([E086](../experiments/e086_p4_8ch_stream/README.ja.md)) |
| **4ch** | … / 10 / 20 / 30 / 40 Msps | 46 Msps |
| **2ch** | … / 10 / 20 / … / 80 / 90 Msps | 96 Msps |

**余裕を 1 割見て切りのよい値で止める**(8ch なら 23 ではなく 20)。理由は 2 つ。

- **排出は一定ではない** — capture 負荷が上がると下がる([E085](../experiments/e085_p4_transfer_size_model/README.ja.md))
- **97 / 99 MHz のように突発的に滞る rate がある**([E084](../experiments/e084_p4_transfer_tuning/README.ja.md)、未特定)

**batch なら別 list**(8ch は 96 まで、それ以外は 100 まで = driver の上限)。

## 5. pin mappingは自由、同時幅は最大16

**`data_gpio_nums[lane]` にlaneごとのGPIOを渡せ、連番や昇順である必要はない。** `9,2,7,4,12,6,20,8`で8 laneすべて正しく取れることを[E086](../experiments/e086_p4_8ch_stream/README.ja.md)で確認した。PARLIO RX APIとpackingの同時幅は最大16 lane。ただし「任意GPIO」はflash / PSRAM / USB / board配線に占有されていないGPIO matrix経由可能pinの範囲である。16本の独立した外部padによる電気試験は未完了で、E042/E106の16-bit試験は8 GPIOを上位laneへ複製している。

## FX2(fx2lafw)との比較

| | fx2lafw | **ESP32-P4** |
|---|---|---|
| channel | 8 | **最大16**(利用可能なboard pin数には依存) |
| 公称 | 24 Msps | **23 Msps**(継ぎ目なし streaming) |
| 実用 | 16 Msps 程度 | **20 Msps を常用**([E086](../experiments/e086_p4_8ch_stream/README.ja.md)) |
| 少ない channel で | 変わらず | **2ch なら 96 Msps** |
| batch | 無し | **160 Msps / 16 Mi sample**([E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md)) |
| host 側 | 専用 driver(sigrok 同梱) | **stock の PulseView が driver 追加なし**([E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md)) |

**公称 24 Msps には 1 Msps 届かないが、実用域(16〜20 Msps)では置き換えになる。**

## 参照

- [E086](../experiments/e086_p4_8ch_stream/README.ja.md) — 8 / 4 channel の連続 streaming 上限、pin の自由度
- [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) — 2 channel の上限と転送の形
- [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) / [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) — batch の上限と幅ごとの sample 精度
- [PulseView / sigrok 連携](pulseview-integration.ja.md) — 経路と protocol
- [capture の圧縮](capture-compression.ja.md) — 帯域を減らす方向の選択肢
