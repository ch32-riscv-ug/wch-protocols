# ESP32-P4 probe / ロジアナ — 現在地と今後の計画

状態: **計画**（2026-09-15。[E063](../experiments/e063_p4_usb_hs_enumerate/README.ja.md)〜[E111](../experiments/e111_p4_dual_core_codec/README.ja.md)を踏まえた棚卸し）

この文書は、実験ごとの細部ではなく、**いま何を通常仕様として説明でき、何が検証済みで、次に何をするか**を管理する。数値の証拠は各実験、USB単体の経緯は[P4 USB HSまとめ](p4-usb-hs-summary.ja.md)、rate選択規則は[sample rateの選び方](p4-sample-rate-selection.ja.md)を正本とする。

## 1. 現時点の結論

> **数値の扱い（2026-09-15、持ち主の方針）**: E108以降（E108〜E115）の数値は、出荷版EspUsbDevice 2.3.0に一時patch（E097 direct RX / E101 TX完了callback / E102 zero-copy / E110 TX FIFO 2 packet）を当てた**独自patch版libraryでの参考値**であり、製品の目安・仕様には使えない。使えるのは**正規libraryに取り込まれた機能で取った数値**だけである。patch版の数値はlibraryへの修正依頼（[EspUsbDevice宛](espusbdevice-change-requests.ja.md) CR-10〜13、[EspUsbHost宛](espusbhost-change-requests.ja.md)）の根拠にのみ使う。正規版（2.3.0、buffered経路、TX FIFO 1 packet）で成立している数値は[E106](../experiments/e106_p4_mixed_rate_capture_stream/README.ja.md) / [E107](../experiments/e107_p4_stream_core_placement/README.ja.md): **8-bit 60 Msps×5回、16-bit wide 40 Msps×3回PASS（Windows native）、USB-only probe 193 Mbps（native）/ 213 Mbps（usbipd/WSL）**。CR-13（TX FIFO 2 packet）とCR-10〜12（zero-copy TX / TX完了callback / direct RX）は先方のworking treeに入っているが未release。working treeで取る数値は**予備測定**で、正式な数値は**正式リリース後の版をsketch.yamlでpinして**取り直したものだけを使う。以下の表と文中の「実測済み」のうちE108以降を根拠とするものは、その時点で置き換える。

### 1.1 製品向けの説明

方針（2026-09-15、持ち主）: **16 channel時の仕様を前面に出す。** 数値の目安は最終的に持ち主が書き換える。以下は方針と、現時点の実測に基づく例である。

- **最大60 Mspsの高速取得を複数channelで行い、時間解像度を落としたchannelを多数足して、合計16 channelまで取得できる。**
- USBの通信速度は環境（PC、cable、hub、OS経路）で変わるため、**高速取得できるchannel数は環境で異なる。** 例: 300 Mbpsの環境では60 Mspsを5 channelまで。60 Mspsを4 channelにして、解像度1/32の1.875 Mspsを12 channelにすると合計262.5 Mbpsとなり、転送速度にも余裕が生まれる。
- 遅いchannelの刻みは組み合わせられる（base 60 Mで1/2 = 30 M、1/4 = 15 M、1/8 = 7.5 M、1/16 = 3.75 M、1/32 = 1.875 M、1/64 = 0.94 M）。同じ300 Mbps環境でも、60 M×3＋30 M×2＋7.5 M×2＋0.94 M×9 = 263 Mbps、60 M×2＋30 M×2＋15 M×4＋1.875 M×8 = 255 Mbps、全16 chを同率15 Mで240 Mbps、のように配分を変えられる。389 Mbpsの直結なら60 M×5＋0.94 M×11 = 310 Mbps、全16 ch 20 Mで320 Mbps。一覧は[sample rateの選び方](p4-sample-rate-selection.ja.md) §0。
- 速度は接続ごとに実測し、**その約9割で使う**ことを推奨する。
- 8 channel以下なら100 Msps、2 channel以下なら160 Mspsで取得できる場合もある（環境と構成次第）。
- CS / INT / buttonなどはchannel単位で`1/2、1/4、1/8、1/16、1/32、1/64…`へ時間解像度を下げられる。中心となる特徴は最高rateではなく、**高速信号の分解能を残しながら、低速信号のrateをchannelごとに下げて転送予算を配分できること**である。

説明の各項目と実測の対応は次のとおり。§1.1の例はすべて実測済みになった（E109〜E113）。

| 説明 | 裏付け | 状態 |
|---|---|---|
| 60 Msps×複数＋縮約channelで合計16 ch | 16-bit wide profile（3 full＋1 D8＋12 D64）を72 MspsまでPASS（E111）、40 Mspsは60 s soak欠損0（E109） | 3 fullは実測済み。4〜5 fullや1/32はcodec未実装（Phase Aの任意descriptorで） **参考値（独自patch版library）** |
| 300 Mbps環境で60 Msps×5 ch | 5 full＋1/32×11（322.5 Mbps）を60 Mspsで3回PASS、64まで通る（[E112](../experiments/e112_p4_16ch_allocation_profiles/README.ja.md)）。**約4.7分soak欠損0（330 Mbps、E112追記）**。ただしcore 0 99.6% / core 1 98%で余裕なし。以前の「30 s soakで一過性の不一致」はhost tool側の原因 | **実測済み（上限いっぱいの構成）**。9割規則では4 ch＋縮約 **参考値（独自patch版library）** |
| 60 Msps×4＋1.875 Msps×12＝262.5 Mbps | 4 full＋12 D32 profileを60 Mspsで3回＋30 s soak 2回欠損0、**約4.7分soak欠損0（269 Mbps、E112追記。E114のfree list stage＋退避16 MiB、検証をcapture後に回したhost）**、上限68 Msps（[E112](../experiments/e112_p4_16ch_allocation_profiles/README.ja.md)）。USB予算の77%、core 0 97% / core 1 91% | **実測済み** **参考値（独自patch版library）** |
| 8 ch以下で100 Msps | 8-bit 3 full＋5 D64を100 Mspsで30 s soak欠損0、108 Mspsまで3回PASS（E111） | 「3 full＋5縮約」として実測済み。8本すべて100 Mspsは800 Mbpsで線に載らない **参考値（独自patch版library）** |
| 2 ch以下で160 Msps | PARLIO 2-bit幅×160 MHzの素通し（320 Mbps）を10回＋30 s soak欠損0（[E113](../experiments/e113_p4_2ch_160m_passthrough/README.ja.md)）。USB予算の93%、core 0 30% | **実測済み（USBが350 Mbps級の環境で）**。300 Mbpsの環境では2 ch 120〜140 Msps **参考値（独自patch版library）** |
| 実測の9割で使う | probe→90%規則。E106〜E111の全経路で適用 | 測定は実装済み。自動ACCEPT / fallbackはPhase B **参考値（独自patch版library）** |

### 1.2 実証済みの代表profile

E108以降の列（zero-copy、TX FIFO 2 packet、2 worker）は独自patch版libraryでの参考値。正規版の列はE106 / E107。

| physical幅 | channel構成 | 内部capture→codec（E108、`-O2`） | USB結合（E106: buffered、USBをcore 1で初期化） | USB結合（E107: USBをcore 0＋codec改修、Windows native） | USB結合（E108: zero-copy、usbipd/WSL直結） | USB結合（E110/E111: TX FIFO 2 packet＋codec 2 core） |
|---:|---|---|---|---|---|---|
| 8 bit | 3 full＋5 D=64 | 76 Mspsまで成立（codec 96.8%）。**内部安全値60 Msps**はcodec約82% | **44 Msps / 137.5 Mbpsを3回PASS**、45で破綻 | **60 Msps / 187.5 Mbpsを5回PASS**。64も通るがUSB帰路（probe約193 Mbps）が生成に追い付かない | **72 Msps / 225 MbpsまでPASS**、76で破綻（codec 100%）。60はcodec 82.6%、core 0 7%。**E109: 60 Mspsは60 s soak 1.41 GB・交互10回・native 30 s soakすべて欠損0** | 単coreでも76（E110）、**2 workerで108 Msps 3回PASS、112も1回**。100 Mspsは30 s soak欠損0。次はUSB予算350 Mbps（≒112 Msps） |
| 16 bit | 3 full＋1 D=8＋12 D=64 | 52 Mspsまで成立（codec 95.8%）。**内部安全値40 Msps**はcodec約86% | **32 Msps / 106 Mbpsを3回PASS**、33で破綻 | **40 Msps / 132.5 Mbpsを3回PASS**、44は1回PASS、48で破綻 | **52 Msps / 172 MbpsまでPASS**、`-O2`で56もPASS（codec 99.4%）。**E109: 40 Mspsは60 s soak 0.99 GB・交互10回・native 1回すべて欠損0** | 単coreで56（E110）、**2 workerで72 Msps PASS**、64は30 s soak欠損0。次はcore 1のcodec |
| 16 bit（旧profile） | 3 full＋8 D=64 | E106: 42 Msps成立、43 Msps不安定。**安全値40 Msps** | 32 Msps / 100 Mbps、65.536 MB完全検査PASS | 未測 | 未測 | 未測 |

16 channel / 40 Mspsの代表例は、`3×40 + 40/8 + 12×40/64 = 132.5 Mbps`である。128 sampleを53 byteにまとめることでpaddingをなくした。1/64は625 ksps、時間刻み1.6 usなのでbuttonには十分であり、短いCS / INTは`any_active`でbucket内のactiveを残せる。

HS hub 2段＋usbipd/WSLでのUSB-only probeは120.860 Mbps、90%予算108.774 Mbpsだった。PC直結へ変更後は256 MBで212.666 Mbps、90%予算191.400 Mbpsまで改善した。E106の結合試験ではUSB予算内の8-bit 60 Mspsと16-bit 40 Mspsでもringを追い越し、当初は「速くなったUSB taskがcore 0上のRX callback / spoolと競合した」と推定した。[E107](../experiments/e107_p4_stream_core_placement/README.ja.md)でFreeRTOS run-time statsを取ると、競合はcore 0ではなく**codecと同じcore 1**にあった。TinyUSBのDWC2割り込みとusbd taskは`Device.begin()`を呼んだcore（Arduinoの`setup()`＝core 1）に乗る。USBをcore 0で初期化し、codec loopをprofile別に直すと、PC直結（Windows native）で8-bit 60 Msps 5回、16-bit wide 40 Msps 3回PASSした。同経路のUSB-only probeは193 Mbps、90%予算173 Mbpsで、8-bit 60 Msps（187.5 Mbps）は予算超えである。**USB予算と内部sink上限に加えて結合上限も持つが、現在の結合上限はcodec速度とUSB帰路そのもので決まる。** さらに[E108](../experiments/e108_p4_zero_copy_stream/README.ja.md)でcodec stage（27,136 byte）をDWC2へ直接渡すzero-copy送信にすると、USB-onlyは同じusbipd/WSL直結で209→**247 Mbps**（90%予算222 Mbps）、core 0のtask負荷は8-bit 60 Mspsで57〜66%→**7%**になり、結合上限はcodecだけで決まる（8-bit 72 / wide 52〜56 MspsまでPASS）。8-bit 60 Mspsは予算の84%、wide 40は60%で、どちらも通常値として余裕がある。

### 1.3 実装上分かったこと

USB帰路に関する項目（zero-copy、TX FIFO、完了callback）はpatch版での知見で、正規libraryへの修正依頼の根拠として使う。codecやPARLIO側の知見はlibraryに依らない。

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
| USB経路の予算測定 | **測定コマンド成立**。TX FIFO 2 packetでprobeは389 Mbps（usbipd/WSL）／377 Mbps（native）、90%予算350 / 339 Mbps。90%予算による自動ACCEPT / fallbackは未実装 |
| 任意descriptorとACCEPT / REJECT | **成立**（E114）。16 byte header＋channelごと4 byteのdescriptorをdeviceが受け、幅・block・payload・padding・raw / wire帯域・bench由来のcodec上限を返し、形式・PARLIO・raw帯域・codec上限・USB予算・stage整列でREJECTする。generic codecの出力はE105 referenceとbyte一致（配線順不同、any_active、edge_latch、phase、polarity）。通常値はgenericで16 ch hold 42 Msps（E115後。50までbyte一致）、8-bit 67（80まで通る）。固定profile並み（60〜72）にはfast部の見直しが残る（E115 §4） **参考値（独自patch版library）** |
| task配置と結合上限 | **E107〜E111で確定**。USBはcore 0で初期化、stageをzero-copyでDWC2へ（TX FIFO 2 packet）、codecは2 worker。8-bit 108 / wide 72 MspsまでPASS、8-bit 100 / wide 64は30 s soak欠損0。60 / 40は大きな余裕を持つ通常値。残りはhub経路と分単位超のsoak **参考値（独自patch版library）** |
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

1. descriptorをprofile別の固定高速codecへdispatchするか、generic codecを最適化するか比較する。**E114で測った: genericは固定の1.4〜1.8倍の費用。E115で縮約取り出しを群ごとのbit行列転置にして16 ch holdは32→48 Msps/core、streaming 50まで（固定wide 72の0.7倍）。残りはfast部の費用（E115 §4）**。E107ではprofile別templateが必要だった。genericにする場合も`-Os`でのlambda / 間接呼び出しを避ける。E111の2 worker構成ならgeneric codecの重さを吸収する余地がある（8-bit 60 Mspsでcodec合計約90%相当の予算）。
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
