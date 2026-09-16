# ESP32-P4 probe / ロジアナ — 現在地と今後の計画

状態: **計画**（2026-09-15。[E063](../experiments/e063_p4_usb_hs_enumerate/README.ja.md)〜[E111](../experiments/e111_p4_dual_core_codec/README.ja.md)を踏まえた棚卸し）

この文書は、実験ごとの細部ではなく、**いま何を通常仕様として説明でき、何が検証済みで、次に何をするか**を管理する。数値の証拠は各実験、USB単体の経緯は[P4 USB HSまとめ](p4-usb-hs-summary.ja.md)、rate選択規則は[sample rateの選び方](p4-sample-rate-selection.ja.md)を正本とする。

## 1. 現時点の結論

> **数値の扱い（2026-09-15〜16、持ち主の方針）**: 製品の目安に使えるのは**正規libraryに取り込まれた機能で、リリース版をsketch.yamlでpinして取った数値**だけ。E108〜E115はEspUsbDevice 2.3.0への一時patch（E097 / E101 / E102 / E110）で取った参考値で、修正依頼（CR-10〜13）の根拠に使った。**2026-09-15にEspUsbDevice 2.4.0がリリースされ（CR-10〜13を収録、同梱TinyUSB無改変）、[E116](../experiments/e116_p4_usb_in_ceiling_release/README.ja.md)（USB-only、366 Mbps）と[E117](../experiments/e117_p4_stream_release_api/README.ja.md)（stream data path一式）で2.4.0をpinして取り直し、E109〜E115の参考値はすべて同じ値で再現した。** 以下の表の値は、E117の結果で正式な数値として読める。正式USB予算は366 Mbps × 0.9 ＝ 329 Mbps（独自patch版の350より約7%低い）。

### 1.1 製品向けの説明

方針（2026-09-15、持ち主）: **16 channel時の仕様を前面に出す。** 数値は正式値（EspUsbDevice 2.4.0をpin、[E116](../experiments/e116_p4_usb_in_ceiling_release/README.ja.md) / [E117](../experiments/e117_p4_stream_release_api/README.ja.md) / [E118](../experiments/e118_p4_generic_fast_path/README.ja.md)）から、**推奨値＝USBは実測の90%、codecは`codec_limit`（core idle約10%）**、**上限＝通ったが余裕のない値（「取れる場合もある」）**の2段で書く。**保守的に書く（持ち主、2026-09-16）: 推奨値は測定済みかつ予算の90%以内のものだけ、「取れる場合もある」は予算100%以内で通ったものだけ、予算超え（five 60、eight 100）は製品説明に載せない。計算だけで未測の構成は推奨値にしない。**

- **最大60 Mspsの高速取得を複数channelで行い、時間解像度を落としたchannelを多数足して、合計16 channelまで取得できる。** 推奨構成は**60 Msps×4本＋1/32（1.875 Msps）×12本＝262.5 Mbps**（当方環境の予算330 Mbpsの80%。約4.7分の連続取得を欠損0で確認）。
- USBの通信速度は環境（PC、cable、hub、OS経路）で変わるため、**高速取得できるchannel数は環境で異なる。** 当方のPC直結（usbipd/WSL）は実測366 Mbps、推奨予算は9割の330 Mbps。60 Msps×5本＋1/32×11本は実測324〜352 Mbpsで予算超え（99〜107%）なので**製品説明には載せない**。300 Mbpsの環境でも60 Msps×4本＋1/32×12本（87.5%）が目安。
- 遅いchannelの刻みは組み合わせられる（base 60 Mで1/2 = 30 M、1/4 = 15 M、1/8 = 7.5 M、1/16 = 3.75 M、1/32 = 1.875 M、1/64 = 0.94 M）。330 Mbps予算では60 M×4＋30 M×1＋7.5 M×1＋1.875 M×10 = 296 Mbps（90%）、60 M×3＋30 M×2＋7.5 M×2＋0.94 M×9 = 263 Mbps（80%）、全16 chを同率18 Mで288 Mbps（87%）のように配分できる（60 M×4＋1/32×12以外は計算値。deviceのbenchがcodec上限を超える構成をREJECTする）。一覧は[sample rateの選び方](p4-sample-rate-selection.ja.md) §0。
- 速度は接続ごとに実測し、**その約9割で使う**ことを推奨する。deviceは構成ごとにcodecの上限も自分で測って（bench）、超える構成をREJECTする。
- **8 channel以下なら80 Msps（3本を高速、5本を1/64にして実測249 Mbps、予算の76%）が推奨、90 Msps（314 Mbps、95%）は取れる場合もある。2 channel以下なら150 Msps（300 Mbps、91%）が推奨、160 Msps（319 Mbps、97%）は取れる場合もある。** 1 channelなら160 Msps（160 Mbps）。100 Msps×8 chは実測349 Mbpsで予算超え（106%）なので載せない。
- CS / INT / buttonなどはchannel単位で`1/2、1/4、1/8、1/16、1/32、1/64…`へ時間解像度を下げられる。中心となる特徴は最高rateではなく、**高速信号の分解能を残しながら、低速信号のrateをchannelごとに下げて転送予算を配分できること**である。

説明の各項目と実測の対応（すべて2.4.0 pinの正式値）。

| 説明 | 推奨値（書く数字） | 上限（取れる場合もある） | 裏付け |
|---|---|---|---|
| 16 ch: 60 Msps×4＋1/32×12（262.5 Mbps） | **60 Msps×4本** | — | E117: 約4.7分soak欠損0（fixed four）、E118: generic F4 60 byte一致・`codec_limit` 60。USB予算の80% |
| 16 ch: 60 Msps×5＋1/32×11（実測324〜352 Mbps） | —（載せない） | —（予算の99〜107%で予算超え。fixed five 約4.7分soak欠損0、generic `codec_limit` 57は事実として残す） | E117 / E118 |
| 16 ch: 60 M×3＋7.5 M×1＋0.94 M×12（実測214 Mbps） | 57 Msps（`codec_limit`） | **60 Msps**（byte一致＋25 s soak）、65で溢れる | E118 |
| 8 ch: 3本高速＋5本1/64 | **80 Msps**（実測249 Mbps、76%） | **90 Msps**（314 Mbps、95%）。100 Msps（349 Mbps、106%。1回目host不一致、再走2回は欠損0）は予算超えで載せない。generic 8-bit F3の`codec_limit`は90 | E117 / E118 |
| 2 ch素通し | **150 Msps**（300 Mbps、91%） | **160 Msps**（319 Mbps、97%、byte一致） | E117 |
| 1 ch素通し | 160 Msps（160 Mbps） | — | E117 |
| 4 ch素通し | —（製品説明に載せない。70 Mspsは計算値280 Mbpsで未測） | 80 Msps（320 Mbps、97%、byte一致） | E117 |
| USB予算 | probe実測×0.9（当方366→**330 Mbps**） | — | E116（27,136 byte transfer 45.7 MB/s） |
| any_active / edge_latch を含む構成 | deviceの`codec_limit`に従う（例: any×6は33、edge×12は17、混在8 chは11 Msps） | — | E118 |

### 1.2 実証済みの代表profile

E108以降の列（zero-copy、TX FIFO 2 packet、2 worker）は独自patch版で取った値だが、E117（2.4.0 pin）で同じ値が再現したので正式な数値として読める。

| physical幅 | channel構成 | 内部capture→codec（E108、`-O2`） | USB結合（E106: buffered、USBをcore 1で初期化） | USB結合（E107: USBをcore 0＋codec改修、Windows native） | USB結合（E108: zero-copy、usbipd/WSL直結） | USB結合（E110/E111: TX FIFO 2 packet＋codec 2 core） |
|---:|---|---|---|---|---|---|
| 8 bit | 3 full＋5 D=64 | 76 Mspsまで成立（codec 96.8%）。**内部安全値60 Msps**はcodec約82% | **44 Msps / 137.5 Mbpsを3回PASS**、45で破綻 | **60 Msps / 187.5 Mbpsを5回PASS**。64も通るがUSB帰路（probe約193 Mbps）が生成に追い付かない | **72 Msps / 225 MbpsまでPASS**、76で破綻（codec 100%）。60はcodec 82.6%、core 0 7%。**E109: 60 Mspsは60 s soak 1.41 GB・交互10回・native 30 s soakすべて欠損0** | 単coreでも76（E110）、**2 workerで108 Msps 3回PASS、112も1回**。100 Mspsは30 s soak欠損0。次はUSB予算350 Mbps（≒112 Msps） |
| 16 bit | 3 full＋1 D=8＋12 D=64 | 52 Mspsまで成立（codec 95.8%）。**内部安全値40 Msps**はcodec約86% | **32 Msps / 106 Mbpsを3回PASS**、33で破綻 | **40 Msps / 132.5 Mbpsを3回PASS**、44は1回PASS、48で破綻 | **52 Msps / 172 MbpsまでPASS**、`-O2`で56もPASS（codec 99.4%）。**E109: 40 Mspsは60 s soak 0.99 GB・交互10回・native 1回すべて欠損0** | 単coreで56（E110）、**2 workerで72 Msps PASS**、64は30 s soak欠損0。次はcore 1のcodec |
| 16 bit（旧profile） | 3 full＋8 D=64 | E106: 42 Msps成立、43 Msps不安定。**安全値40 Msps** | 32 Msps / 100 Mbps、65.536 MB完全検査PASS | 未測 | 未測 | 未測 |

16 channel / 40 Mspsの代表例は、`3×40 + 40/8 + 12×40/64 = 132.5 Mbps`である。128 sampleを53 byteにまとめることでpaddingをなくした。1/64は625 ksps、時間刻み1.6 usなのでbuttonには十分であり、短いCS / INTは`any_active`でbucket内のactiveを残せる。

HS hub 2段＋usbipd/WSLでのUSB-only probeは120.860 Mbps、90%予算108.774 Mbpsだった。PC直結へ変更後は256 MBで212.666 Mbps、90%予算191.400 Mbpsまで改善した。E106の結合試験ではUSB予算内の8-bit 60 Mspsと16-bit 40 Mspsでもringを追い越し、当初は「速くなったUSB taskがcore 0上のRX callback / spoolと競合した」と推定した。[E107](../experiments/e107_p4_stream_core_placement/README.ja.md)でFreeRTOS run-time statsを取ると、競合はcore 0ではなく**codecと同じcore 1**にあった。TinyUSBのDWC2割り込みとusbd taskは`Device.begin()`を呼んだcore（Arduinoの`setup()`＝core 1）に乗る。USBをcore 0で初期化し、codec loopをprofile別に直すと、PC直結（Windows native）で8-bit 60 Msps 5回、16-bit wide 40 Msps 3回PASSした。同経路のUSB-only probeは193 Mbps、90%予算173 Mbpsで、8-bit 60 Msps（187.5 Mbps）は予算超えである。**USB予算と内部sink上限に加えて結合上限も持つが、現在の結合上限はcodec速度とUSB帰路そのもので決まる。** さらに[E108](../experiments/e108_p4_zero_copy_stream/README.ja.md)でcodec stage（27,136 byte）をDWC2へ直接渡すzero-copy送信にすると、USB-onlyは同じusbipd/WSL直結で209→**247 Mbps**（90%予算222 Mbps）、core 0のtask負荷は8-bit 60 Mspsで57〜66%→**7%**になり、結合上限はcodecだけで決まる（8-bit 72 / wide 52〜56 MspsまでPASS）。8-bit 60 Mspsは予算の84%、wide 40は60%で、どちらも通常値として余裕がある。

### 1.3 実装上分かったこと

USB帰路に関する項目（zero-copy、TX FIFO、完了callback）はpatch版で得た知見で、2.4.0の公開API（`writeDirect()` / `onTxComplete()` / `onRxData()`）に取り込まれ、E116 / E117で正規版として再現した。

- TinyUSBのsoftware ringとDWC2 hardware TX FIFOは別物。hardware FIFOを1 packetから2 packetへ増やしても25.575 MB/sの天井は変わらず、E090の仮説は反証された。
- P4→PCは、事前生成zero-copyなら36.159 MB/sまで出る。実captureではPARLIO callback、codec、stage queue、PSRAM copy、USBが合成された上限を見る必要がある。
- 8→16-bitでraw入力が1 sampleあたり1→2 byteになり、詰め替え込みの安全値は60→40 Mspsになる。
- codec block境界とUSB packet境界は一致させない。53-byte blockをそのままtransfer終端にするとshort packetが連発してusbipdがerrorになった。codecは128 sample単位で作り、PSRAM FIFOから最終回以外512 byte単位で送ると成立した。
- genericな1-bitずつのpackingでは40 Mspsに足りない。代表profileのD=64部分を固定bit-spread演算にすると40 Mspsを回復できた。
- 現在の16-bit試験は内部TXの8 GPIOを上位laneへ複製している。16本の独立した外部padの電気試験ではない。
- 直結はUSB-onlyを約121→213 Mbpsへ改善したが、E106のtask配置では結合上限が8-bit 44 / 16-bit 32 Mspsだった。原因はUSB割り込みとusbd taskがcodecと同じcore 1にあったこと（E107）。`Device.begin()`を**core 0固定のtaskから呼ぶ**と解消する。
- FreeRTOS run-time stats（`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y`）でcapture窓のcore別idleを取ると、律速coreを推定でなく数値で決められる。E107では8-bit 60 Mspsでcore 1（codec）約90%、core 0（PARLIO ISR＋spool＋usbTask＋usbd＋DWC2 ISR）60〜70%。
- `-Os`ではlambda化したcodec loopがE106の直書きloopより遅く、内部sinkで60 Mspsを落とした。profile別templateと短いbit gather（8-bit: `w&0x07070707`→`(w|w>>5)&0x003f003f`→`(w|w>>10)&0xfff`）で8-bit 60 Mspsのcodec率は100%→85.9%になった。
- spool / usbd taskの優先度、PARLIO割り込みのcore、内部RAM FIFO（128 KiB）は結合上限を上げなかった。PSRAM FIFOは必要。
- E106由来の潜在不具合2件をE107で修正した。FIFO overflowでspoolが抜けるとcodecが永久待ちになる点と、未flushの短いstatus行が残るとusbTaskがFIFO満容量待ちで10 s後に諦める点。
- USB帰路のcopyは消せる（E108）。TinyUSB vendor classをnon-bufferedにし、`vendord_ep_write`で呼び出し側のbufferをそのまま`usbd_edpt_xfer()`へ渡すと、Stage→PSRAM→ring→endpoint bufferの3回のmemcpyがなくなる。EspUsbDevice 2.3.0にはこのAPIがなく、E097 / E101 / E102の一時patchを合わせた[patch](../experiments/e108_p4_zero_copy_stream/espusbdevice-e108.patch)で実験した。製品化にはbuffer ownershipを明示したzero-copy TX APIの改修依頼が要る。
- PSRAM FIFOは常時経由ではなく退避だけにする。stage（512の倍数、lcm(53,512)=27,136 byte）をそのまま1 transferにすると、codec block境界とUSB packet境界の分離はtransfer長の性質だけで保てる。全stageをPSRAM経由にした対照はcore 0が約29 point重い。
- **USB帰路の真の天井はDWC2のbulk IN TX FIFOが1 packet分だったこと**（[E110](../experiments/e110_p4_usb_in_ceiling/README.ja.md)）。TinyUSBの`dfifo_alloc()`既定で、2 packetにすると同じhostで29.7→**49.3 MB/s（395 Mbps、理論53.2 MB/sの93%）**、Windows nativeで27.7→47.1 MB/s。transfer長（≥27 KiB）・arm深さ（≥2）・URB depth（≥2）・再arm経路・device CPU（空き98%）は主因ではなかった。E090が反証したのはbuffered送信時代の話で、zero-copy後はFIFOだけが残る。streaming firmwareに当てるとUSB-only 389 Mbps（90%予算350 Mbps）で、8-bit 76 / wide 56 Mspsまで退避なしに通る。EspUsbDeviceへは`tud_configure()`の`bm_double_buffered`で正規に指定できる改修依頼にする。
- **codecは2 coreで回せる**（[E111](../experiments/e111_p4_dual_core_codec/README.ja.md)）。PARLIO chunkにraw stream offsetを持たせ、blockの出力位置をoffsetから決めれば、2 workerが同時に別stageへ書いても順序は崩れない。chunk長は2,368〜4,032 byteで可変なので、chunk先頭の跨ぎblockはringに残る直前chunkの末尾と組み合わせて符号化する。結合上限は8-bit 108 / wide 72 Msps、両coreの合計codec率は約180〜190%。core 0側のworkerはISR・USBと同居するので取り分は約41%。
- P4のDWC2はEspUsbDeviceの既定（`src/internal/EspUsbTinyUsbConfig.h`）で**DMA mode**で動いている（E109で`GAHBCFG.DMAEn=1`・`GINTMSK.RXFLVL=0`を確認）。E107 / E108で書いた「slave modeのISRがFIFOへ押す」は誤りで、buffered経路のcopyは3回、`CFG_TUD_DWC2_DMA_ENABLE`の明示は何も変えない。run-time statsはISR時間を中断されたtaskに計上するので、core 0の真の負荷はpriority 1のspin taskで測る。8-bit 60 Mspsで約23%（task計上は7%）。
- codecは`-O2`で8-bit約1 point、wideで約4 point軽くなる。cache line先読みは約1 point。device側Gray checkは7〜8 pointで、製品firmwareには載らない。
- **任意descriptorは成立、generic codecの費用は固定profileの1.4〜1.8倍**（[E114](../experiments/e114_p4_dynamic_descriptor/README.ja.md)）。deviceがraw channelを下位lane・縮約channelを上位laneに並べ替えるので、fast部はF=1〜8共通の4回のmasked shiftで済み、縮約のOR / AND / 立上り / 立下りはwordに詰まったsample（8-bit幅は4本、16-bit幅は2本）を折って2分木で作れる。費用を決めるのは縮約channelごとのbucket値の取り出し（hold 1値あたり6〜8 cycle）で、16 ch hold構成は34〜40 Msps、8-bit F=3は58〜70 Msps、any / edge構成は固定の0.2〜0.4倍。（mode, D, phase）の群ごとにbit行列を転置すれば固定profileと同じ費用構造になる（E115候補）。
- **縮約channelの取り出しは群ごとのbit行列転置で費用がほぼ消える**（[E115](../experiments/e115_p4_grouped_plane_transpose/README.ja.md)）。deviceが（mode, D, phase, polarity）の同じchannelを連続laneに置き、bucket kのG bitを`spread`で広げてORすればplaneが直接出る（1群5本で費用≈0）。generic codecは16 ch hold構成で47.9 Msps/core（E114の1.5倍）、8-bit F=3で75、streamingは16 chで50 Msps byte一致（55で落ちる）、8-bitで80。固定wide 72に対して0.7倍で、残りはfast部（F bit gather、約5 cycle/sample）とper-block overhead。1 channelだけの群はE114経路のまま（B×1の転置は損）。群関数はalways_inlineでないと呼び出し1回約100 cycleを失う。
- **streamingがbenchの0.58倍しか出なかった正体はworker周辺の費用**（[E118](../experiments/e118_p4_generic_fast_path/README.ja.md)）: 単coreでencodeは980 cycle/block（benchどおり）なのに、chunkごとの`xQueueReceive`（約6 µs＝158 cycle/block）とrunごとの`esp_cache_msync`（155）で計520 cycle/blockを失っていた。RX callbackで隣接DMA nodeを≤16 KiBに結合（chunk数3.4分の1）し、runごとの書き戻しを撤去（P4のL1データキャッシュは2 coreで共有（`hal/esp32p4/include/hal/cache_ll.h`、Dキャッシュのregisterは1つでcore選択なし）なので、TinyUSBがarm時に行う`dcd_dcache_clean`で別coreの書き込みも書き戻される。host全照合bad 0）すると、**16 ch hold構成は製品モードで60 Msps byte一致＋25 s soak、上限65**、固定five 60のcore 0は99.6→83.5%。codec_limitの係数はbench×1.215（W16 57、F4 60、8-bit F3 90）。fast部（690 cycle/block）とhold/8単独channel（200）について[E119](../experiments/e119_p4_fast_part_words/README.ja.md)でスカラーの4手（非整列word store: 17 cycle/回で悪化、整列padding付きword store: word詰めが高く悪化、1 channel群の転置展開: 悪化、割り込みのcore移動: 偏りが反転するだけ）を試して全部反証。**E118がスカラーコードの最適点、16 ch hold構成の上限は65 Msps。** これ以上はP4のSIMD拡張（PIE）でbit gatherを書く（候補`p4-pie-bit-gather`）。
- **codec上限は表でなくdevice上のbenchで決める。** descriptorを受けるたびに同じencoderを8,192 block走らせ`bench_msps`を返し、`bench × 1.2 × 0.9`を超えるrateをREJECTする。2 workerのstreamingは単core benchの1.25〜1.56倍まで通るが、core idle約10%が残るのは約1.1倍まで。線形補間表はmode構成で10倍ずれるので使わない。
- **host toolはURB完了callbackの中で仕事をしない。** libusbのevent loopがPythonの処理（開始位相探索など）で数百ms塞がるとURBが再投入されず、usbipd/WSL経路は「約200 msの穴が繰り返す」状態に落ちて回復しない（Windows nativeでも出る）。device側ではarm済みtransferの完了が130〜600 ms止まって見える。E109〜E113のhostは検証が軽くて偶然通っていた。検証・探索はcapture後に行う。
- stage bufferは`index % N`固定でなくfree listから割り当てる。USBが止まっても直接arm済みの2 slot以外は退避に回して即返せるので、codecは退避（8 MiB）が満ちるまで止まらない（E114）。
- **16 chの60 M×4＋1/32×12（269 Mbps）と60 M×5＋1/32×11（330 Mbps）は約4.7分のsoakを欠損0で通った**（E112追記、E114 firmware＋検証をcapture後に回したhost）。以前の「four 60が2.8分で落ちた」はhost toolの検証がUSBを止めていたのと、hostの転送に時々入る100〜200 msの穴に対してE112 firmwareのstage割り当てが2 stage分しか耐えなかったため。穴に耐えるにはfree list stage＋PSRAM退避16 MiB（33 MB/sで約480 ms）が要る。退避のin/out copyがcore 0のusb taskに載る点（33 MB/s級で上限近い）が次の壁。
- loopback源（PARLIO TXのGray counter）は8-bit幅58〜60 Mspsでbyte一致検証の開始位相が取れない（1 bitずれ）。Gray進行checkなら通る。上限付近のbyte一致には別の源が要る。

## 2. 近い目標の現在地

| 目標 | 状態 |
|---|---|
| mixed-rateロジアナのdata path | **代表profileは成立**。固定profileでcapture、codec、PSRAM、USB、PC復元まで通った |
| `.sr`保存とstock decoder | **達成**。P4でcaptureした`.sr`をsigrok decoderが読める |
| PulseViewへIP経由 | **raw streamでは達成**。mixed-rate descriptorからbase gridへ復元するgatewayは未実装 |
| USB経路の予算測定 | **測定コマンド成立。正式値はE116（2.4.0 pin）: probe 366 Mbps（usbipd/WSL）、90%予算329 Mbps。** 独自patch版の389〜395 / 377 Mbpsは参考値。90%予算による自動ACCEPT / fallbackは未実装 |
| 任意descriptorとACCEPT / REJECT | **成立**（E114）。16 byte header＋channelごと4 byteのdescriptorをdeviceが受け、幅・block・payload・padding・raw / wire帯域・bench由来のcodec上限を返し、形式・PARLIO・raw帯域・codec上限・USB予算・stage整列でREJECTする。generic codecの出力はE105 referenceとbyte一致（配線順不同、any_active、edge_latch、phase、polarity）。通常値はgenericで16 ch hold 42 Msps（E115後。50までbyte一致）、8-bit 67（80まで通る）。固定profile並み（60〜72）にはfast部の見直しが残る（E115 §4） **正式値はE117（2.4.0 pin）で再現済み** |
| task配置と結合上限 | **E107〜E111で確定**。USBはcore 0で初期化、stageをzero-copyでDWC2へ（TX FIFO 2 packet）、codecは2 worker。8-bit 108 / wide 72 MspsまでPASS、8-bit 100 / wide 64は30 s soak欠損0。60 / 40は大きな余裕を持つ通常値。残りはhub経路と分単位超のsoak **正式値はE117（2.4.0 pin）で再現済み** |
| RVSWDでCH32へ書込 | **未着手**。CH32とP4の配線待ち |
| RVSWD / SWIO decoder | **未着手**。実信号取得は上記配線待ち |

「packet capture」はロジアナで捕ってdecoderで読むことを指す。SPI / UART / JTAGなどstock decoderがあるprotocolは既に処理できる。RVSWD / SWIOだけはWCH固有decoderが必要である。

## 3. 次に再開するときの順序

### Phase A — 設定と正しさを固める

1. ~~PCから`base_rate_hz / sample_count / GPIO mapping / channelごとのmode・D・phase・polarity`を渡すdescriptorを決める。~~ **済（E114）**。
2. ~~deviceが`physical幅 / block sample数 / payload bit数 / padding / raw入力帯域 / wire帯域`を返し、内部上限を超える設定をREJECTする。~~ **済（E114）**。codec上限は表でなくdevice上のbench。
3. `D=2 / 4 / 8 / 16 / 32 / 64`の実機codecを確認する。reference codecはround-trip PASS済み。**E114でhold D=4/8/16/32/64/128、any D=8/32、edge D=16/32がloopback源とのbyte一致で通った**。D=2はbenchのみ。
4. `D=128 / 256 / 512 / 1024`を通常UIへ出すか決める。形式上は可能だが、複数blockをまたぐ状態、待ち時間、追加の帯域削減量を測ってから決める。
5. `decimate_hold / any_active / edge_latch`について、短pulse、bucket境界、active polarity、端数captureを固定fixtureで検査する。

### Phase B — host統合を固める

1. USB probe結果の90%を予算にし、要求profileを`ACCEPT / REJECT`する。
2. 超過時はbase rate候補を下げ、必要なら32 / 30 Mspsなどを提示または自動選択する。
3. descriptorに従ってmixed-rate streamをbase sample gridへ復元し、PulseView gatewayへ接続する。
4. PulseView要求量よりcodec block単位で多めに受信し、出力時に分割する。`close`時は先読み分を捨ててcaptureを停止する。
5. start / stop / restart、設定変更、端数sample、host切断、timeoutを繰り返す。Monitorや別DOS窓の入力待ちに依存しないCLIにする。

### Phase C — 実機条件を広げる

1. 16本の独立GPIOを外部pattern源へ配線し、lane順、任意GPIO mapping、同時変化を検査する。
2. SPI相当のCLK / MISO / MOSI / CSと、短INT、button相当を同時生成し、`/8`と`/64`の見え方をPulseViewで確認する。
3. 現在のhub 2段、PC直結、Windows native、WSL usbipdで同じprobeと長時間captureを行う。
4. 長時間soak、繰り返し列挙、途中切断後の復帰を確認する。

### Phase D — 最後にチューニングする

1. descriptorをprofile別の固定高速codecへdispatchするか、generic codecを最適化するか比較する。**E114→E115→E118で解決: 縮約取り出しは群転置（E115）、streamingの残差はworker周辺（chunk結合と書き戻し撤去、E118）で、genericの16 ch holdは60 Msps byte一致・上限65。fast部（690 cycle/block）は次の余地**。E107ではprofile別templateが必要だった。genericにする場合も`-Os`でのlambda / 間接呼び出しを避ける。E111の2 worker構成ならgeneric codecの重さを吸収する余地がある（8-bit 60 Mspsでcodec合計約90%相当の予算）。
2. PARLIO callback量、ring / queue / stageサイズ、PSRAM copyの配置を掃引する。
3. **E107 / E108で実施。** 競合はcore 0ではなくcore 1側で、USBをcore 0で初期化して解消した（E107）。zero-copy送信でcore 0のUSB負荷はほぼ消え、USB-onlyも247 Mbpsへ上がった（E108）。DWC2 DMA flagは効果なし。task優先度、PARLIO割り込みのcore、内部RAM FIFOは効かない。E090で反証済みのhardware TX FIFO増量は繰り返さない。
4. 安全marginを再測定し、表向きの60 / 40 Mspsを最終確定する。**E109で直結については確定**。E110 / E111後は8-bit 100（codec 84 / 87%、USB予算の89%）、wide 64（codec 89 / 90%、USB 61%）が「余裕を持つ点」で、通常値を引き上げるかは持ち主判断。残りはhub経路、分単位超のsoak。
5. 少数channel高速モードが実用上必要な場合だけ、通常仕様と分離して着手する。

## 4. いったん保留するもの

- 160 Mspsを主要な製品値として掲げること。誤解を招き、複数channelの連続USB転送とは両立しない。
- RLE / deflateを通常streamへ入れること。最悪入力で膨張しない仕組みが先に必要で、現段階ではchannel別縮約を優先する。
- 1/1024より下の単純間引き。button等はedge/event表現の方が適する可能性が高い。
- USBの最高値だけを追うこと。経路依存性はprobe＋90%予算で吸収し、まず設定・復元・停止の正しさを固める。

## 5. 配線または環境変更が必要な項目

| 項目 | 必要なもの |
|---|---|
| 16 channel独立入力 | 16本を外部pattern源へ接続。現在の内部loopbackは8本複製 |
| 実SPI / INT波形 | P4または別deviceの信号源とGPIO配線 |
| USB直結比較 | **実施済み**。USB-only約213 Mbps（E106、usbipd/WSL）／193 Mbps（E107、Windows native）／247 Mbps（E108 zero-copy、usbipd/WSL）／221 Mbps（E109 zero-copy、Windows native）／**395 Mbps（E110 zero-copy＋TX FIFO 2 packet、usbipd/WSL）／377 Mbps（同、native）**。結合はE106配置で8-bit 44 / 16-bit 32、E107で8-bit 60 / wide 40、E108で8-bit 72 / wide 52 Msps |
| RVSWD / SWIO | CH32とP4の電源、GND、信号線 |

task配置変更後の再測定はE107（Windows native）、E108（usbipd/WSL、udev ruleで`/dev/bus/usb`をplugdevに開放）、E109（両経路のsoakとnative probe）で実施した。hub経路での同じ掃引が残る。動的descriptorやhost復元は現在の配線のまま進められる。

## 6. 作業環境と他のprobe課題

- E104〜E107の対象は第三P4（MAC `80:f1:b2:d0:b2:61`）。UARTは`/run/board-identify/by-id/esp32-p4-80f1b2d0b261`で指定する（`/dev/ttyUSBn`の番号は再列挙で変わる。2026-09-15は`ttyUSB2`で、`ttyUSB0`は別のESP32だった）。HSは直結でWindows bus `1-7`。usbipdでWSLへattachすると`/dev/bus/usb`ノードがroot専用になることがあり、`sudo`が使えない環境ではWindows nativeのWinUSB（`uv.exe run --with libusb1`）からhostを動かす。
- `/home/mt/dev/EspUsbHost/tests/.env`は別のfull testが使用中であり、この検証から変更・流用しない。
- MACの近い2台はHS同士で結線された別リグであり、今回のmixed-rate検証では触れていない。
- firmware uploadでHS deviceは再列挙される。古いusbipd attachやendpoint待ちを残さず、再attachしてdevice nodeを取り直す。
- `n=3`は再現確認であり、分散や保証値を決める統計ではない。最終値は長時間soakと環境差を含めて決める。

ロジアナ以外ではRVSWDでCH32へ書き込む作業が未着手である。電源とGNDを合わせた後、順序付き2本の探索でchip IDが返る組を見つけ、残りの配線を符号化patternで同定する段取りは[pin discovery](pin-discovery.ja.md)にある。RVSWD / SWIO decoderは、この実配線から得た波形と同時に進める。

EspUsbHostのHR-3（HID 1,024 byte）とrelease判断は、ロジアナのmixed-rate data pathとは分けて持ち主判断のまま残す。

## 7. 再開時の完了条件

通常仕様を確定する前に、少なくとも次を満たす。

- 任意のchannel descriptorをdeviceとhostが同じbudgetとして解釈する。
- budget超過設定をcapture開始前にrejectできる。
- 8-bit / 16-bit代表profileを長時間・複数回、欠損0で再現できる。**E109で60 s soakと交互20回まで達成（直結）。**
- PulseViewで高速channelと展開後の低速channelを同時表示できる。
- start / stop / restartとhost切断から回復できる。**E109で連続20 runのstart / stopは達成。host切断・timeoutからの再同期は未。**
- 16本独立GPIOでmappingと値を確認できる。
- hub / 直結差はprobe結果へ反映され、固定のUSB速度を仮定しない。**直結2経路（247 / 221 Mbps）の差はprobeで見えている。hubは未。**

## 8. 参照

- [E105 mixed-rate形式](../experiments/e105_p4_spi_mixed_rate_codec/README.ja.md)
- [E106 実capture→codec→USB](../experiments/e106_p4_mixed_rate_capture_stream/README.ja.md)
- [E107 結合上限のcore配置・codec改修](../experiments/e107_p4_stream_core_placement/README.ja.md)
- [E108 zero-copy USB帰路](../experiments/e108_p4_zero_copy_stream/README.ja.md)
- [E109 soak・繰り返し・経路差](../experiments/e109_p4_stream_soak/README.ja.md)
- [E110 USB bulk IN天井の再測定（TX FIFO 2 packet）](../experiments/e110_p4_usb_in_ceiling/README.ja.md)
- [E111 codecの2 core化](../experiments/e111_p4_dual_core_codec/README.ja.md)
- [E112 16 ch配分例の実測](../experiments/e112_p4_16ch_allocation_profiles/README.ja.md)
- [E113 2 ch 160 Msps素通し](../experiments/e113_p4_2ch_160m_passthrough/README.ja.md)
- [sample rateの選び方](p4-sample-rate-selection.ja.md)
- [PulseView / sigrok連携](pulseview-integration.ja.md)
- [capture圧縮](capture-compression.ja.md)
- [P4 USB HSまとめ](p4-usb-hs-summary.ja.md)
- [ピンの当たりを付ける](pin-discovery.ja.md)
- [実験台帳](../experiments/LEDGER.ja.md)
