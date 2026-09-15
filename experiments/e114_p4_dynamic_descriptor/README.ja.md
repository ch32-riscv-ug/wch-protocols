# E114 任意channel descriptorのdevice側実装 — 配線順不同のGPIO、mode・D・phase・polarity、ACCEPT / REJECT

状態: **完了**（2026-09-15。descriptor / ACCEPT-REJECT / generic codec / reference一致は実機で通った。codec費用の仮説（固定profileの1〜2割増）は反証、原因と次の手（E115候補）を記録。途中で追ったUSB停止はhost tool側が原因） — **参考値（独自patch版library）**（EspUsbDevice 2.3.0＋E097/E101/E102/E110の一時patch。正規libraryに取り込まれるまで製品の目安には使わず、修正依頼の根拠にのみ使う）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E105](../e105_p4_spi_mixed_rate_codec/README.ja.md)（reference codec）、[E111](../e111_p4_dual_core_codec/README.ja.md)、[E112](../e112_p4_16ch_allocation_profiles/README.ja.md)、[E113](../e113_p4_2ch_160m_passthrough/README.ja.md)

## 問い

[ロードマップ](../../references/p4-probe-roadmap.ja.md) Phase A: PCから`base_rate / channelごとのGPIO・mode（raw / decimate_hold / any_active / edge_latch）・D・phase・polarity`を渡すdescriptorを決め、deviceが**physical幅・block sample数・payload bit数・padding・raw入力帯域・wire帯域を返し、内部上限を超える設定をREJECTする**。固定profileでなくdescriptorから組んだgeneric codecは、E111の2 worker data pathで通常値（60 M級）をどこまで処理でき、E105 reference codecと同じ意味の出力になるか。

## 仮説

- PARLIOは`data_gpio_nums[lane]`で任意のGPIOを任意のlaneへ置けるので（E086）、deviceがraw channelをlane 0〜F-1、縮約channelをその上へ並べ替えれば、codecは常に「下位F laneはfull、上位は縮約」の形で済む。fast部はF=1〜8のbit gatherを1つの式（mask → fold 2段）で作れる。
- 縮約部はbucketごとのOR / AND（any_active）と立上り / 立下りのOR（edge_latch）を幅全体のwordで一度に作れば、channel数に比例するのはbit取り出しだけで、any_activeの12 channelでもE112のD32 profile程度の費用になる。edge_latchだけは128 sample分のXOR / AND / ORが要る。
- genericの費用はF=3〜5の固定profile（E111 / E112: 72 / 68 / 64 Msps）より1〜2割重い程度で、2 workerなら60 Mspsに収まる。
- REJECTは (a) 形式（N≤16、GPIO重複、Dは2の冪≤128、raw⇒D=1、phase<D、mode≤3）、(b) PARLIO（rate≤160 MHz、幅8/16。幅1/2/4はall rawの素通しのみ）、(c) raw入力≤160 MB/s、(d) codec上限（実測から: 16-bit F=3→72、F=4→68、F=5→64 Msps、8-bit F=3→108を基に線形補間、90%を上限）、(e) wire≤hostが渡す予算、(f) stage整列（`lcm(W,512)≤27,136`。入らなければWを8の倍数へpadding）で決める。

## 反証条件

- genericのcodec率が固定profileの1.5倍を超える → fast部かbucket縮約の式を見直す。
- reference codecとの出力不一致 → 意味（phase、polarity、edge定義）の取り違え。E105 `codec.py`を正とする。
- 配線順不同（GPIO 9,2,7,4,…）で不一致 → lane並べ替えの誤り。

## 方法

- command: 16 byte header（`E`/`I`/`Q` ＋ `D`、rate、blocks、flags、channel数）＋budget_mbps（u16）＋channelごと4 byte（gpio、mode、log2 D、phase|polarity）。`Q`は判定と算出値の返答だけ。
- device: 並べ替え→幅→W→padding→stage fill→上限判定→`E114_DESCRIPTOR accept= reason= width= fast= block= payload_bits= padding_bits= wire_block_bytes= raw_mb_s= wire_mbps= codec_limit_msps= lanes=`を返し、ACCEPTならE111 data pathで走る。wire layoutは「fast F bit/sample sample-major → 縮約channelのplane（E105の意味、bit連結）→ block末尾padding」。
- device側check: GPIO 2〜9がすべて含まれる構成ではlane→Gray bitの表で8-bit Grayを復元しblock先頭の進行を見る。
- host: `host_descriptor.py`。descriptor組立、返答表示、capture受信。検証はloopback源の周期性を使う: Gray counterは4 sampleごとに進み1,024 sampleで一周するので、descriptorから期待wireを8 block（1,024 sample）ぶんreference semanticsで生成し、最初のblockに一致する開始位相（counter値×sub-phase 1,024通り）を探して、以降を**byte一致**で全stream比較する。any_active / edge_latch / phase / polarityの意味がreferenceと同じかがそのまま検査になる。
- 掃引: (1) E106のwide相当（GPIO 2,3,4 raw、5 hold/8、6〜9 hold/64、8〜15はGPIO 2〜9の複製）を40 / 60 Mspsで固定profileと比較。(2) 配線順不同（9,2,7,4,…）。(3) any_active（active-low）とedge_latchを混ぜた構成。(4) REJECT: 17 ch、D=256、raw+D≠1、rate 200 M、予算超え、幅4で縮約あり。(5) F=1〜8の掃引で codec率を記録。

### 追記（2026-09-15、実装中に変えた点。計画本文は残す）

- **codec上限は表ではなくdeviceの実測で決める。** descriptor commandを受けるたびに、その構成のencoderを受けたcoreで8,192 block（1 M sample）走らせて`bench_msps`（単core）を測り、`codec_limit = bench × 1.6（2 worker、1 workerなら0.9）× 0.9`を超えるrateをREJECTする。計画の線形補間表は`model_limit_msps`として返答に残し比較に使う。理由: generic codecの費用はmode構成で5倍以上変わり（下表）、表では追えない。scaleの1.6は本実験のrate掃引で較正する。flag `0x20`（host `--no-codec-limit`）で判定を外し較正に使う。
- **fast部**: F=1〜8のbit gatherは「mask→fold 2段」ではなく、sample 4本ぶんを32-bitに集める素直な4回のmasked shiftにした。64-bit accumulatorの可変shiftはlibgccの呼び出しになり（-Os）、最初の版はcodecが固定profileの約40倍遅かった。F・幅はtemplateで、`#pragma GCC optimize("O2")`の区間に置く。
- **縮約部**: bucketごとのOR/AND/立上り/立下りは「量ごとに1 pass」で、32-bit wordに詰まったsample（8-bit幅は4本、16-bit幅は2本）をshiftで折るword levelから上へ2分木で組む。使わない量は計算しない。立上り/立下りはE105どおりbucket内部の遷移だけを数えるので、2分木の結合時に両半分の境界の遷移（`cross[]`）を足す。extractはchannelごとにbucket値を16 bitずつ集めて一括appendする（bucketごとの1 byte storeはchar aliasingで構造体の再読込を招き、1値25 cycle掛かっていた）。`reduce_model_check.py`がこのword式をE105 `codec.py`とrandom descriptor 1,500本で照合する（全一致）。

## 対象外

D>128（複数block状態）、host gateway（Phase B）、外部GPIO。

## 必要な環境 / ベンチ種別

第三P4、PC直結 usbipd/WSL、`/tmp/EspUsbDevice-e110`。一時。

## 記録する数値 / 完了条件

REJECT判定の一覧、ACCEPT構成のbyte一致、genericと固定profileのcodec率比。Phase A 1〜3が実機で通れば完了。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §2 / §3 Phase A、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)、[E105](../e105_p4_spi_mixed_rate_codec/README.ja.md)の形式。

## 結果（2026-09-15）

firmware `e114_p4_dynamic_descriptor.ino`（E111 data path＋generic codec＋bench）、host `host_descriptor.py`、bench直結usbipd/WSL（対照でWindows native）。runの生ログは`_runs/E114_20260915T115816JST_p4_direct/sweep.log`。

### 1. descriptorとACCEPT / REJECT

16 byte header＋budget＋channel 4 byteのdescriptorで、device側は「raw channelを下位lane、縮約channelを上位laneに並べ替え（GPIOは任意順）→ 幅1/2/4（all rawの素通し）/8/16 → W → padding → stage fill → 上限判定」を返す。返答例（W16、3 raw＋hold/8＋12 hold/64）:

```
E114_DESCRIPTOR accept=1 reason=ok rate_hz=40000000 width=16 passthrough=0 fast=3 channels=16 dec=13 block=128
payload_bits=424 padding_bits=0 wire_block_bytes=53 stage_fill=27136 raw_mb_s=80.000 wire_mbps=132.500 budget_mbps=0
codec_limit_msps=45 model_limit_msps=72 gray_check=1 bench_blocks=8192 bench_us=32994 bench_msps=31.78 lanes=2,3,4,...,17
```

**codec上限は表ではなくdevice上のbenchで決める**ことにした（方法の追記参照）。理由は下表のとおり費用がmode構成で10倍以上ばらつき、線形補間表（`model_limit_msps`）が実測とかけ離れるため。`codec_limit = bench_msps × 1.2（2 worker。1 workerは0.9）× 0.9`（係数は§3で較正）、passthroughは160 Mspsで頭打ち（PARLIOの硬い上限なので余裕率を掛けない）。

### 2. generic codecの費用（device bench、単core、Msps）

`Q`（query）で返る`bench_msps`。列は実装の版: v1＝64-bit accumulatorを32-bit gather 4回に直した版、v2＝縮約部を「bucket値をchannelごとに16 bitずつ集めて一括append」にした版、v3＝縮約を量ごとのword pass＋2分木にした版（最終）。

| layout | v1 | v2 | v3 | 参考 |
|---|---:|---:|---:|---|
| 16-bit: 3 raw＋hold/8＋12 hold/64（E106 wide相当） | 24.1 | 32.2 | **32.1** | 固定wide profileはstreaming 72 Msps（2 worker） |
| 16-bit: 4 raw＋12 hold/32（E112 four相当） | 20.6 | 30.6 | **30.3** | 固定four 68 |
| 16-bit: 5 raw＋11 hold/32（E112 five相当） | 22.1 | 31.7 | **31.6** | 固定five 64 |
| 16-bit: 6 raw＋10 hold/64 | 32.2 | 37.9 | — | |
| 16-bit: 8 raw＋8 hold/64 | 31.8 | 36.6 | — | |
| 16-bit: 3 raw＋hold/8＋12 **any**/32/low | — | 10.0 | **17.1** | |
| 16-bit: 3 raw＋hold/8＋12 **edge**/32/high | — | — | **11.4** | |
| 8-bit: F raw＋(8−F) hold/64、F=1 / 3 / 5 / 7 | 43.8 / 46.3 / 47.7 / 49.9 | 52.5 / 54.4 / 54.2 / 55.5 | — / 54.6 / — / — | 固定eight（F=3）108。F=8はmemcpyで173 |
| 8-bit: 配線順不同（GPIO 9,2,7 raw；4:hold/8/3、6:hold/64、3:hold/64/63、5:hold/32/1、8:hold/16/7） | 29.4 | 43.1 | **42.7** | bucketが多い（16＋2＋2＋4＋8） |
| 8-bit: 2 raw＋any/8 low・high＋edge/16 high・low＋hold/4/3＋hold/128/127 | 7.1 | 8.8 | **9.4** | 4種の縮約passが全部走る |
| 8-bit: 2 raw＋6 any/8/low | — | 11.4 | **21.3** | |
| 8-bit: 2 raw＋6 edge/16/high | — | 9.1 | **17.8** | |
| 8-bit: 2 raw＋6 any/2 ／ 6 edge/2 | — | — | 8.4 ／ 5.2 | bucket 64個×6 channelの取り出しが支配 |
| passthrough 1 / 2 / 4 ch | — | — | 763〜937 / 675 / 534 | memcpy |

読み方。fast部（F bit gather）はFにほとんど依らず約5 cycle/sample。費用を決めるのは縮約channelの**bucket値の取り出し**で、hold値1個あたり約6〜8 cycle（v1は25 cycle。1 byteずつのstoreがchar aliasingで構造体の再読込を招いていた）。any / edgeはv3でword pass化しても取り出しが残るので0.2〜0.4倍に留まる。

### 3. streamingの上限（2 worker、`--no-codec-limit`で判定を外して掃引）

| layout（bench） | 単core internal | 2 worker internal | 2 worker USB（byte一致） | bench比（USB上限/bench） |
|---|---|---|---|---|
| 16-bit 3 raw＋hold/8＋12 hold/64（32.1） | 25 ○ / 30 × | 30・40 ○ / 50 × | **30・40 ○** / 50 × | 1.25〜1.56 |
| 8-bit 3 raw＋5 hold/64（54.5） | 40 ○ / 50 × | 60 ○ / 80 × | **50・60・70 ○** | 1.28〜1.47 |
| 8-bit 配線順不同（42.7） | | | 40 ○ / **60 ×**（codec 100%） | <1.40 |
| 8-bit 2 raw＋6 any/8/low（21.3） | | | 15・20・30 ○ | ≥1.41 |
| 8-bit mix（9.4） | | | 10 ○ | ≥1.07 |
| passthrough 4 ch @80 / 2 ch @160 / 1 ch @160 | | | ○ 331 / 331 / 167 Mbps | PARLIO上限 |

単coreのstreamingはbenchの0.78〜0.93倍（DMA ringからの読み出し、stage書き出し、chunk処理の分）。2 workerは単coreの約1.6倍。ただし通った上限ではcore idleがほぼ0（W16 40: idle 0.2% / 0.6%、F4 35: 1.2% / 4.3%）で余裕がなく、idle約10%が残るのはbenchの約1.1倍（W16 34〜35、8-bit 58、F4 32、any6 28、配線順不同48）だった。**`codec_limit`の係数は1.2×0.9＝1.08倍**にした（W16 34、8-bit 58、F4 32、F5 34、配線順不同45、any6 22、edge12 12、mix8 10 Msps）。W16 36と配線順不同50はREJECTされる。

なお上の表の「×」のうちW16 30/40のUSB、edge12 10/15、8-bit 58は§4のhost tool側の停止で落ちたもので、host修正後は通る。codec律速で落ちたのはW16 45/50、8-bit 80（internal）、配線順不同60、any6 40、edge12 20、F4 40。

**仮説3（固定profileより1〜2割重い）は反証。** 16-bitでは固定wide 72 / four 68に対しgenericは40〜50 Mspsで、費用は約1.5〜1.8倍。8-bit F=3では固定eight 108に対し70〜80で約1.4〜1.5倍。反証条件「1.5倍超」に当たる。原因は§2のとおり縮約channelを1 channelずつ取り出していること。固定profileはD=64の8 channelをwordのbit-spread演算で一度に作る。genericでも同じことはできる: deviceがlaneを（mode, D, phase）で群にまとめて連続laneに置き、群ごとにB bucket×G channelのbit行列を転置すれば、取り出しはchannel数でなく群の数に比例する。**これがE115の候補**（[LEDGER](../LEDGER.ja.md) §2）。

### 4. USB送出が30〜600 ms止まる現象 — 原因はhost toolがURB callbackの中で仕事をしていたこと

W16（GPIO 2〜17）はinternalでは40 Mspsを通るのにUSB送出では30 Mspsでも0.6〜1.0 s後に必ず落ちた。追跡の経過と結論:

1. stage数を4→6、直接arm深さを2に戻し、退避経路の所要時間、USB完了の間隔、arm遅延、host側URB完了間隔をstatusに出した。device側では**TX完了が130〜600 ms止まる**（例 `gaps=1057:127416:1586`＝1.057 sに終わった127 msの間隙、直前の完了から1.6 ms後にはtransferがarm済み）。armしたtransferをhostが取りに来ない。止まっている間もcodecはstageを作り続け、直接arm分を超えて退避が始まり、stageの余裕を使い切ってringが溢れる。退避copy自体は1回200 µs以下で、E111で疑った「退避copyでcore 0 workerが遅れる」は主因ではない。
2. 同じfirmware・同じ分に、**E111 hostで固定profile（W 30/40、eight 60/64）を流すと間隙は最大2.3 ms**で通る。E114 hostだけが止まる。Windows native（WinUSB）でも同じ間隙（313 ms）が出るのでusbip固有ではない。USB-only probe（E110 `host_probe.py`）はその時点で46.6 MB/s（373 Mbps）を出す。
3. 途中、「lane 8〜15をGPIO 10〜17に置くと止まり、GPIO 2〜9の複製なら通る。10/12/14〜17は単独でも止まり、11/13は通る。pull-downを掛けると通る」という相関が14 run連続で出た。これは**見かけの相関**で、実際の経路はfloating入力→lane 8〜15のdataがreferenceと不一致→host検証が1 blockずつ比較する遅い経路に入る→callbackが長く塞がる、だった（後述）。pull-down済みの構成やGPIO 2〜9だけの構成（8-bit 58 Msps、F4 35）でも同じ停止が出て相関が崩れたので、原因から外した。
4. host側で**検証を完全に切る（`--no-validate`）と58 Mspsでも間隙なし**、検証を最初の1 transferだけにしても（`--validate-every 1000000`）止まる。差は、最初のURB完了callbackの中で行う開始位相探索（`find_start`、256×4位相の`Reference`生成＋比較）と検証用threadの仕事。**URB完了callback（libusbのevent loop）がPythonの仕事で数百ms塞がると、その間URBが再投入されず、以後usbipの転送が「約200 msの穴が繰り返す」状態に落ちる**（Linux TCPの最小RTO 200 msと同じ桁で、一度崩れると回復しない）。8 URB×1 MiBの先行投入では足りない。
5. host toolを「callbackは受信bufferを溜めるだけ、開始位相探索と全比較はcapture後」に直すと、8-bit 60 Msps（wire 187 Mbps）、W16 40 Msps、F4 35、any6 30、edge12 15、mix8 10がすべて間隙なし・退避なしで通り、W16 40の25 s soak（434 MB）、8-bit 60の25 s soak（410 MB）も間隙なし。

結論と教訓。
- **測定用host toolは、URB完了callbackの中でPythonの仕事をしない。** 検証・探索・保存はcapture後、またはcaptureと切り離したprocessで行う。E111 hostが通ったのは検証が軽かったからで、設計として安全だったわけではない。
- device側の改良2つは残す。(a) **stage bufferの割り当てをindex固定（`index % N`）からfree listにした。** USBが止まっている間、直接arm済みの2 slotは塞がるが、他のstageはPSRAM退避に回して即座にslotを返すので、codecは退避が満ちる（8 MiB）まで止まらない（F4 35ではhost tool修正前の停止を退避8〜9回で乗り切った）。(b) PARLIO入力に割り当てたGPIOで駆動源のないものは`gpio_set_pull_mode(GPIO_PULLDOWN_ONLY)`にする（flag `0x40`で外せる）。原因ではなかったが、未接続入力をfloatingにしない方針は製品でも正しい。
- **loopback源の限界**: 8-bit幅で58〜60 Mspsではbyte一致検証の開始位相が見つからない（最初のblockに1 bitずれが混じる。同じrunをE111 hostのGray進行checkで見ると通る）。40 / 50 Mspsは一致する。上限付近の8-bit検証はE111 hostのcheckで見る。

### 5. reference semanticsとのbyte一致

hostは`E114_DESCRIPTOR`の`lanes`と`wire_block_bytes`からloopback源（Gray counter、4 sampleごとに進む、周期1,024 sample）の期待wireをE105 `codec.py`の意味で作り、開始位相を探した後は全stream（80 periods＝34.7 MB）をbyte比較する。`bad_blocks=0`で通ったもの:

| 検査対象 | run |
|---|---|
| hold/8とhold/64、lane並べ替え（GPIO 2〜17、10〜17はpull-down） | W16 30 / 40 Msps |
| **配線順不同**（raw GPIO 9,2,7；hold phase 3 / 63 / 1 / 7、D=8 / 64 / 32 / 16） | 8-bit 40 Msps |
| **any_active** active-low（D=8） | 8-bit 15 / 20 / 30 Msps |
| **any_active**両極性＋**edge_latch**両極性（D=16）＋hold/4/3＋hold/128/127 | 8-bit 10 Msps |
| passthrough 1 / 2 / 4 ch | 160 / 160 / 80 Msps |
| **edge_latch** active-high D=32 ×12（16-bit） | 15 Msps |
| 4 raw＋12 hold/32（E112 four相当） | 35 Msps |
| 25 s soak: W16 40 Msps 434 MB／any6 30／mix8 10（host tool修正後、全比較） | `bad_blocks=0` |

device内部の縮約式（word pass＋2分木、`cross[]`）は`reduce_model_check.py`でE105 referenceとrandom descriptor 1,500本を照合済み（mode×log2 D 24通り全部）。

### 6. 判定

- 仮説1（lane並べ替えでgenericのfast部を1つの式に）: **成立**。ただし式は「mask→fold 2段」でなく4回のmasked shift。
- 仮説2（縦約をwordで一度に作れば費用はchannel数に比例しない）: **半分反証**。OR / AND / 立上り / 立下りのbucket値自体はword passで安く作れたが、channelごとのbit取り出しが残り、any / edgeは固定profileの0.2〜0.4倍。
- 仮説3（固定profileの1〜2割重い）: **反証**（1.4〜1.8倍）。§3。
- 仮説4（REJECT判定の項目）: codec上限だけ表からbenchに替えた。他は計画どおり（一覧は追記予定）。
- 反証条件「reference codecとの不一致」「配線順不同で不一致」: **出なかった**。

### 7. REJECT判定の一覧（`Q`で確認）

| 入力 | 返答 |
|---|---|
| 17 channel | `accept=0 reason=channel_count` |
| D=256 | hostが送る前に弾く（`D must be a power of two in 1..128`）。deviceは`decimation_range` |
| raw channelにD=4 | `reason=raw_decimation` |
| rate 200 MHz | `reason=rate_range` |
| W16 40 Msps（wire 132.5 Mbps）にbudget 100 Mbps | `reason=usb_budget` |
| hold/8にphase 8 | `reason=phase_range` |
| W16 60 Msps（bench 31.8 → limit 34） | `reason=codec_limit` |
| mix8 40 Msps（bench 9.4 → limit 10） | `reason=codec_limit` |
| 幅4で縮約あり（3 raw＋hold/8） | **ACCEPT、幅8に昇格**（計画では「REJECT」としていたが、素通しできないだけで符号化はできるので受理） |
| GPIO重複（2:raw, 2:raw） | **ACCEPT**（passthrough 幅2）。計画は「重複はREJECT」だったが、同じGPIOを複数laneへ置くのはE106以来の試験手法なので受理のまま。製品UIで弾く |

### 8. 次

- **E115候補**: 縮約channelを（mode, D, phase）で群にまとめて連続laneに置き、群ごとにbucket×channelのbit行列を転置して取り出す。固定profileの「D=64をbit-spreadで一度に」と同じ費用構造になり、16 ch hold構成で固定profile並み（60〜72 Msps級）に戻る見込み。any / edgeも同じ形で取り出せる。
- host tool共通の規約: URB callbackの中で仕事をしない（§4）。E109〜E113のhostは軽い検証で偶然通っていたので、soak系のhostも同じ形（capture後検証）へ揃える。
- loopback源の上限（8-bit 58〜60 Mspsでbyte一致が取れない）は別件。sampling位相を固定できる源（外部clock同期）に替えるか、検証をGray進行checkに切り替える。

### 追記（2026-09-15、退避16 MiBとE112 soakへの転用）

E112のfour / five 60 Mspsを本firmwareの固定profile経路（`F` / `V` command、free list stage）で約4.7分ずつ測り直した際、hostの転送に209 msの穴が入ったrunで退避が6.9 MiBまで積み、その直後にslot待ちtimeoutで落ちた。`kSpillBytes`を8→16 MiBにし、slot待ちtimeout時に`abort_free_slots / abort_arm_queued / abort_spill_used / abort_ready_waiting / abort_inflight_kind`をstatusへ出すようにした。その後の2本（four 60: 269 Mbps、five 60: 330 Mbps）は穴なし・退避なしで通った（[E112 追記](../e112_p4_16ch_allocation_profiles/README.ja.md)）。33 MB/s級で退避が動くと、in（stage→PSRAM）とout（PSRAM→bounce）のcopyがcore 0のusb taskに載って上限近くになる点は残課題。
