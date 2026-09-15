# EspUsbDevice / EspUsbHost への還元 — 既定値・機能・運用ノウハウ（E106〜E114から）

状態: **送付済み**（2026-09-15にEspUsbDevice側sessionへ要点を送付。P4 mixed-rate stream最適化の過程で分かったことを、library側へ渡す形に整理。個々の依頼の正式化は[EspUsbDevice宛](espusbdevice-change-requests.ja.md) / [EspUsbHost宛](espusbhost-change-requests.ja.md)で持ち主が判断する）

数値の証拠は各実験にある。特記なければESP32-P4 rev v1.3、EspUsbDevice 2.3.0、TinyUSB 0.21.0、PC直結（Windows 11、usbipd-win→WSL2、またはWindows native WinUSB）。

## 1. EspUsbDevice — 既定値を変えた方がよい項目

| # | 項目 | 現状 | 提案 | 根拠 |
|---|---|---|---|---|
| D1 | **bulk IN endpointのDWC2 TX FIFO** | TinyUSB `dfifo_alloc()`既定の1 packet（`bm_double_buffered`未使用） | bulk INは2 packetを既定にする（`tud_configure()`の`bm_double_buffered`）。P4 HSのDFIFO 1,024 wordsはRX 304＋EP0 16＋EPInfo 32を除いて672 words空き、bulk IN 2本を2 packetにしても収まる | [E110](../experiments/e110_p4_usb_in_ceiling/README.ja.md): 同一host・firmwareで29.7→**49.3 MB/s**（理論53.2の93%）、Windows nativeで27.7→47.1 MB/s。4 packetは2と同値。1 packetではendpointが99.9% armedでもpacket間隔が理論の約2倍 |
| D2 | **usbd taskとDWC2割り込みのcore** | `Device.begin()`を呼んだcoreに`esp_intr_alloc`、usbd taskは非固定でその割り込みcoreで起床。Arduinoでは`setup()`のcore 1 | configで割り込み・usbd taskのcoreを指定できるようにし、dual coreの既定はArduino loopと反対のcore 0にする | [E107](../experiments/e107_p4_stream_core_placement/README.ja.md): codecと同居していたcore 1が飽和し結合上限44 Msps。core 0で初期化すると同一firmwareで52、以後の改善の前提になった |
| D3 | **buffered vendor writeの短いdata** | `write()`が`CFG_TUD_VENDOR_TX_EPSIZE`未満（実際はmps=512未満）を残すとflushまで送られない。`waitWritable(容量いっぱい)`は端数が残る限り成立しない | `write()`の後に自動flush（1 ms tick）、または`waitWritable()`の中でflushする。少なくともREADMEに「短いmessageは`flush()`必須」「`waitWritable(writeCapacity())`は端数が残ると永久待ち」を明記 | [E107](../experiments/e107_p4_stream_core_placement/README.ja.md) §6: status行132 byteが残り、次runの`waitWritable(8192)`が10 s待って諦める連鎖 |
| D4 | READMEの記述 | `CFG_TUD_DWC2_DMA_ENABLE`は`tusb_option.h`既定0に見えるが、libraryの`EspUsbTinyUsbConfig.h`がP4/S2/S3で1にしている | 「DWC2は既定でDMA mode。slave modeとの切替は非対応（併用不可）」をREADMEに書く | [E109](../experiments/e109_p4_stream_soak/README.ja.md) §4: `GAHBCFG.DMAEn=1`、`GINTMSK.RXFLVL=0`。当方はslave前提で2実験ぶん誤解した |

## 2. EspUsbDevice — 機能追加の提案

| # | 機能 | 内容 | 根拠 |
|---|---|---|---|
| F1 | **zero-copy TX（non-buffered）** | 呼び出し側bufferをそのまま`usbd_edpt_xfer()`へ渡すwrite。完了までbuffer所有権は呼び出し側。長さはepbufに縛らない（TinyUSBの16-bit上限65,535まで） | [E108](../experiments/e108_p4_zero_copy_stream/README.ja.md): 209→247 Mbps、送出側coreのtask負荷57〜66%→7%。D1と合わせて389 Mbps。実験用patch: `experiments/e108_p4_zero_copy_stream/espusbdevice-e108.patch` |
| F2 | **TX完了callback** | 完了byte数を渡すhook（usbd task context）。callback内から次のbufferを投入できること（chain） | E108のarm ring。task再arm（隙間18 µs）でも27 KiB以上のtransferなら差は出ない（[E110](../experiments/e110_p4_usb_in_ceiling/README.ja.md) §3）ので、chainは「小さいtransferでも落ちない」保険 |
| F3 | **direct RX callback（non-buffered）** | 受信bufferと長さを渡すhook。現在の`onRx(size)`はbuffered専用 | E108〜E114のcommand受信（16〜82 byte）。E097 patch |
| F4 | `tud_configure()`の露出 | `bm_double_buffered`と`vbus_sensing`をconfigから触れるように | D1の実装手段 |
| F5 | 転送長の32-bit化（余地） | TinyUSB `usbd_edpt_xfer`は`uint16_t`。DWC2のxfer sizeは19 bit | E110の残り7〜12%はtransfer境界（65 KiBで1 packet分）とhost側。upstream変更が要るので優先度は低い |

## 3. EspUsbDevice / 利用側 — 運用ノウハウ

- **transfer長は27 KiB以上、arm深さ（queue先行）は2以上**。8 KiBは2 packet FIFOでも78%に落ちる（E110 §4）。
- **host URBはdepth 2以上で飽和、URB長は64 KiB〜1 MiBで差なし**。WSL（usbip vhci）では4 MiB URBが`LIBUSB_ERROR_NO_MEM`（E110 §2）。
- **経路差**: 同じdevice firmwareでusbipd/WSL 247〜395 Mbps、Windows native WinUSB 221〜377 Mbps。nativeが遅い。予算はprobe→90%で経路ごとに取る（E109 / E110）。
- **同一identity（VID:PID:serial）でinterface構成を入れ替えてもusbipdのbindは維持**され、cacheの問題は出なかった（vendor⇄DFU 2往復、WSL観測に限る。[E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md)参考観測）。Windowsネイティブのdevnode挙動は未確認。
- **FreeRTOS run-time statsはISR時間を中断されたtaskに計上する**。idle中の割り込みはidleに見えるので、USB負荷はpriority 1のspin taskで測る（E108 §4）。
- 再列挙後はusbipdが「Shared」に戻るので`usbipd.exe attach --wsl --busid N`（管理者不要）。WSLの`/dev/bus/usb`はudev ruleでplugdevに。
- DWC2 DMA modeではDMA元bufferをcache line（64 byte）整列・整列長にし、書いたcore側で`esp_cache_msync(C2M)`してから渡す（TinyUSBも`dcd_dcache_clean`するが、別coreで書いた行は自分で落とす）。
- **host側の測定toolは、URB完了callbackの中で仕事をしない。** callback（libusbのevent loop）がPythonの処理で数百ms塞がるとURBが再投入されず、usbipd/WSL経路は以後「約200 msの穴が繰り返す」状態に落ちて回復しない（Windows nativeでも同種の穴が出る）。device側から見るとarm済みのbulk IN transferの完了が130〜600 ms止まる。8 URB×1 MiBの先行投入では足りない。検証・探索・保存はcapture後に行う（[E114](../experiments/e114_p4_dynamic_descriptor/README.ja.md) §4。同じ経路のprobe自体は46.6 MB/s出ている）。両libraryのhost example / probeにもこの規約を書く価値がある。（当初「floating GPIO入力でUSBが止まる」と報告したのは見かけの相関で、実際は上記。未接続入力にpullを掛ける方針自体は妥当）

## 4. EspUsbHost へ

- [E089](../experiments/e089_p4_host_in_queue/README.ja.md)の「約24 MB/sはdevice側の限界」は、**device側TX FIFOが1 packetだったこと**で説明がつく（E110）。P4 host自身のIN上限はまだ見えていない。D1適用後のdeviceを相手にE089 / [E102](../experiments/e102_p4_vendor_in_zero_copy_precomputed/README.ja.md)を再測すると、P4 hostが36.2 MB/sを超えるか、host側で止まるかが分かる。止まるならhost側のRX FIFO割当・NAK再試行の間隔が次の候補。
- HR-1（in-flight数）/ HR-2（転送長）はそのまま有効。深さ2以上、転送長は27 KiB以上が目安。
- USB-only probe（`EP` command、既知patternの最大速送信＋90%）は両libraryのexampleとして持たせる価値がある。経路差が大きく、固定値の仕様説明ができない。

## 5. 送付記録

| 日付 | 宛先 | 内容 |
|---|---|---|
| 2026-09-15 | EspUsbDevice側session | CR-10〜13の要約と根拠の所在（FYI） |
| 2026-09-15 | EspUsbDevice側session（`espusbdevice-2f`） | 本文書の要点全部（D1〜D4、F1〜F5、運用ノウハウ、EspUsbHost向け項目）。EspUsbHost側sessionへの転送を依頼 |
| 2026-09-15 | EspUsbDevice側session → 当方 | **D1（CR-13）を先方が自前で実測して採用**: buffered経路（`waitWritable`＋`write`、FIFO 4096）、P4 HS、usbipd/WSL、32 MB×3で **22.98→28.93 MB/s（+26%）**、分散±0.7→±0.1 MB/s。DFIFO収支の自動計算で収まるときだけ有効化（P4 HSはbulk IN 2本まで、S2/S3は4本でも収まる）。当方の29.7→49.3はzero-copy経路の値で、差はcopy律速ぶん。D3 / D4は先に着手、D2は測る、F1〜F3は実測後に設計との回答 |
| 2026-09-15 | EspUsbDevice側session → 当方 | **D3を修正**（`tu_edpt_stream_write()`は1 packet分たまるまでarmしないので、`waitWritable()`の前にflushする形。`write()`側の自動flushは短packetでhost URBが早期完了して速度が落ちるため入れず、「短いmessageは`flush()`必須」は応用ガイドに明記）、**D4を明記**（応用ガイド2.3冒頭に「全targetでDMA mode、slaveは選択肢でない」）。**D2は実測へ**（`config.taskCoreId`を追加、pinなし対core 0固定をbulk IN harnessで比較） |
| 2026-09-15 | EspUsbDevice側session → 当方 | **D2を実測、既定は変えず`config.taskCoreId`（既定-1＝pinしない）として公開**。usbip経由bulk IN 32 MiB×3（double buffering有効、producerは`loop()`からのmemcpy）でpinなし28.61、core 0固定28.02 / 28.90 MB/s、差なし。解釈は当方と同じで「producerが重い（PARLIO capture＋codecが反対coreにいる）ときに効く条件付きの話」。E107の44→52 Mspsを「効く条件の実例」として先方docから参照する。1回だけ出た16.38 MB/sは同期read（callbackなし）でも出るusbip経路の穴として記録しない。D1〜D4はこれで完了、F1〜F3は設計から |
| 2026-09-15 | EspUsbHost側session（`espusbhost-c7`） | §4の4点（E089「約24 MB/sはdevice天井」の正体＝TX FIFO 1 packetとCR-13採用済みの案内、2 packet化deviceでのE089 / E102再測依頼、HR-1 / HR-2の目安、USB-only probe example、host tool側の「完了callback内で処理しない」規約）。P4同士の再測はそちらのrig（ttyACM10/11）で、device側firmwareはEspUsbDevice側のCR-13入り版で、と併記 |
