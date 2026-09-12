# E064 ESP32-P4 USB HS CDCのdownload帯域

状態: **完了 — 約5.6〜5.7 MB/sで飽和、16 MiBが2.968秒**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E063](../e063_p4_usb_hs_enumerate/README.ja.md)(HSで列挙することを確認) / 影響先: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段

## 問い

**PSRAM上のdataをUSB 2.0 HSのCDC bulk INでWindowsへ連続送出したとき、実効帯域は何MB/sか。device側の1回の`write()`量(chunk size)と転送総量でどう変わるか。**

## 仮説

**FSの約1 MB/sとCH343 6 Mbaudの約600 KB/sを大きく超えるが、HS bulkの理論上限53.2 MB/s(512 B × 13 transaction × 8,000 microframe/s)には届かない。** 律速はArduino-ESP32のCDC送信経路にあると予想する。

根拠:

1. chip側のmemoryは律速しない。[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)のPSRAM copy帯域は最低138.590 MB/s、[E021](../e021_p4_parlio_psram_spool/README.ja.md)の持続spoolは約98 MB/sで、どちらもHSの理論上限より速い。
2. TinyUSBのCDC TX FIFOはP4用precompiled libsの`sdkconfig`で`CONFIG_TINYUSB_CDC_TX_BUFSIZE=512`、つまり**bulk 1 packet分しかない**。`USBCDC::write()`は書き込むたびに`tud_cdc_n_write_flush()`を呼ぶので、1 packetごとにturnaroundが入る形になりうる。
3. [E063](../e063_p4_usb_hs_enumerate/README.ja.md)でbulk endpointは`wMaxPacketSize=512`で開いている。

したがって**chunk sizeを大きくしても、512 B FIFOの上で頭打ちになる**というのが仮説の中心である。

## 反証条件

1. 帯域がFSの1 MB/s級と変わらない。HSで列挙してもCDC経路では速度が出ないことになり、vendor bulkへ分岐する
2. chunk sizeを64 Bから64 KiBまで振っても帯域が変わらない。律速がdevice側のchunk化とは別の場所にある
3. data化けまたは欠落がある。その条件の帯域の数値は採らない
4. 転送総量を増やすと帯域が落ちる、または止まる
5. **host側reader(Windows Python + pyserial)が律速している疑いが晴れない。** device側の所要時間とhost側の所要時間が大きく食い違う場合がこれに当たる

## 方法

### 経路

- **console** = USB-Serial-JTAG(FS)。設定と結果の受け渡しだけに使い、計測対象のdataは流さない
- **計測対象** = OTG HS上のCDC bulk IN。Windowsに`usbser`のCOM portとして生える([E063](../e063_p4_usb_hs_enumerate/README.ja.md))
- **host側reader** = Windows側のPython 3.13 + pyserial 3.5。WSLからではなく**Windows上で直接COM portを開く**。usbipd経由にすると測っているものがusbipになる

### 型

開始時刻は握手で合わせる([README.ja.md §7-10](../README.ja.md) (A) PING-PONG)。

1. harnessがconsoleへ`C <bytes> <chunk>`を送り、deviceを武装させる
2. Windows側readerがCOM portを開き、**HS CDCへ`G`を1 byte書く**
3. deviceは`G`を受けた瞬間に`micros()`を取り、PSRAMからchunk単位で送出し、終了時刻を取る
4. readerは指定byte数を読み切るまでの時間を測る
5. deviceはconsoleへ`SEND ...`を出す。harnessが両者を突き合わせる

readerがportを開く前にdeviceが流し始めると先頭を落とすので、**開始の合図はHS CDC側から出す**。

### 掃引

| 段 | 固定 | 振るもの | 回数 |
|---|---|---|---|
| A | 転送 4 MiB | chunk size = 64 / 512 / 4,096 / 16,384 / 65,536 B | 各3 |
| B | Aで最速だったchunk | 転送総量 = 1 / 4 / 16 MiB | 各3 |

PSRAMには16 MiBのbufferを確保し、32-bit word index(`word[i] = i`、little-endian)で埋める。転送はその先頭から線形に読む。**patternはhost側で全word検証する**(反証条件 3)。

### 律速の切り分け

device側の`elapsed_us`とhost側の経過時間を両方記録する。**device側が速くhost側が遅ければreaderが律速**、**両者が一致すればUSB経路が律速**である(反証条件 5)。

## 対象外

- **vendor bulk / WinUSB経路**。CDCとの比較は別実験(`p4-hs-cdc-vs-bulk`)
- host → device方向(OUT)の帯域
- 双方向同時
- PARLIO captureとの同時動作(`p4-usb-vs-drain`)
- usbip経由(WSL)との比較(`usbipd-overhead`)
- CDCを複数本にしたときの合計帯域(`p4-cdc-budget-hs`)
- IPやfile保存の経路

## 必要な環境

[E063](../e063_p4_usb_hs_enumerate/README.ja.md)と同じ。加えて:

- Windows側にPython 3.13と`pyserial` 3.5(確認済み)
- HS portはWindowsに接続したまま。**usbipdでWSLへ引き込まない**
- PSRAM 32 MiBのうち16 MiBをbufferに使う

## ベンチ種別

**一時・配線なし**。E063と同じ2 port構成を維持する。

## 記録する数値

| 項目 | 列 |
|---|---|
| 条件 | `bytes`、`chunk` |
| device側 | `elapsed_us`、`written`(実際に`write()`が返したbyte数の合計)、`short`(短く返った回数) |
| host側 | 受信byte数、経過秒、実効MB/s、pattern不一致の最初の位置 |
| 比較 | device側MB/s、host側MB/s、その比 |

各条件3回。min / median / maxを出す([README.ja.md §7-3](../README.ja.md))。

## 完了条件

1. **実効帯域がMB/sで言える**(chunk別・転送総量別の表が埋まる)
2. **律速がdevice側かhost側かUSB経路かを、device/host双方の時間から言える**
3. **pattern検証が通った条件だけを帯域の表に入れている**

いずれかの条件が機材の都合で測れなくても、測れた範囲で表が埋まれば完了とし、残りは未決に書く。

## 影響

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段の**6 Mbaud前提のdownload時間の表**を実測で置き換える。「深度を伸ばすことの価値はdownload時間に食われる」という優先順位の判断もここで決まる
- [harness-channels](../../references/harness-channels.ja.md) §物理IFの帯域表(`native USB CDC ×1 = ~1 MB/s`、生サンプル`16chで0.5〜1 MB/s`)— すべてFS前提で「実測は未」と断ってある行
- streaming tier(依存関係 #11)が成立しうるかの入口

**仕様のstatusは動かさない。**

---

## 結果

状態: **完了 — 約5.6〜5.7 MB/sで飽和。chunk 512 Bが膝。転送量を16 MiBまで伸ばしても落ちない**(2026-09-12)

run: `_runs/E064_20260912T024*`。1回の実行で24条件すべてを取得した。

### 掃引A — chunk size(転送4 MiB、各3回)

host側実効MB/s。`比`はdevice側MB/s ÷ host側MB/s。

| chunk (B) | min | **median** | max | device median | 比 |
|---:|---:|---:|---:|---:|---:|
| 64 | 1.47 | **1.51** | 1.52 | 1.51 | 1.000 |
| 512 | 5.52 | **5.60** | 5.64 | 5.60 | 1.000 |
| 4,096 | 5.46 | **5.74** | 5.80 | 5.75 | 1.001 |
| 16,384 | 5.66 | **5.70** | 5.81 | 5.69 | 1.000 |
| 65,536 | 5.59 | **5.73** | 5.85 | 5.73 | 1.000 |

### 掃引B — 転送総量(chunk 4,096 B、各3回)

| 転送 | host min/median/max (MB/s) | device median (MB/s) | 所要 median |
|---:|---|---:|---:|
| 1 MiB | 5.48 / **5.51** / 5.80 | 5.53 | 0.190 s |
| 4 MiB | 5.58 / **5.59** / 5.65 | 5.59 | 0.750 s |
| 16 MiB | 5.62 / **5.65** / 5.68 | 5.66 | **2.968 s** |

### 健全性

| 項目 | 値 |
|---|---|
| pattern検証 | 8条件(各条件の1回目)で全word検証、**不一致0** |
| 短く返った`write()` | **0** |
| host側のstall | **0** |
| 受信byte数 | 全24回で要求と一致 |

### 事実

1. **実効帯域は約5.6〜5.7 MB/sで飽和する。** chunk 512 B以上ではどの値でも同じで、chunkを128倍(512 B → 64 KiB)にしても差は測定のばらつきの中に収まる。
2. **chunk 512 Bが膝である。** 64 Bでは1.51 MB/sまで落ちる(飽和値の約1/3.8)。512 BはHSのbulk `wMaxPacketSize`であり、`CONFIG_TINYUSB_CDC_TX_BUFSIZE`の値でもある。
3. **転送量を1 MiBから16 MiBへ16倍にしても帯域は落ちない。** むしろ1 MiBの方がわずかに遅く(5.51 vs 5.65 MB/s)、立ち上がりのぶんだけ短い転送が損をしている。**16 MiBは2.968秒で出る。**
4. **律速はhost側readerではない。** device側の`esp_timer_get_time()`とhost側の`perf_counter()`の比が全24条件で1.000〜1.001。**両者は同じものを測っており、USB経路そのものが上限を決めている**(反証条件 5を否定)。
5. **data化けと欠落は無い。** 検証した8条件で全word一致、短writeもstallも0。
6. **HS bulkの理論上限53.2 MB/sに対して約10.6%にとどまる。** 飽和値5.6 MB/s ÷ 512 B = 約10,940 transaction/s。HSのmicroframeは8,000回/秒なので、**1 microframeあたり約1.37 transaction**しか出ていない。HSは1 microframeに最大13 transactionを許すので、**帯域ではなくturnaroundが上限を決めている**と読める。TX FIFOがbulk 1 packet分しかないという仮説と整合する。
7. **旧ベンチ(CH343 6 Mbaud、約600 KB/s)の9.4倍である。** [p4-logic-analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段の表では16 MiBのdownloadに約28秒かかる見積りだったが、**実測は2.968秒**になった。
8. PIDを`1209:0003`に変えたら、Windowsは**COM8ではなくCOM9**を割り当てた。PIDごとに別のdevice instanceになることの傍証である([E062](../e062_usb_same_identity_layout_change/README.ja.md)の前提)。

### 候補

- **chunkは4 KiB以上を既定にする**(採用)。512 Bでも飽和するが、`write()`の呼び出し回数が1/8になるのでCPUが空く
- **帯域が要る経路はCDCではなくvendor bulkへ逃がす**(未検証)。事実6の読みが正しければ、送信FIFOを深くできる経路では大きく伸びる余地がある。[harness-channels](../../references/harness-channels.ja.md) §6cの「capture の帯域が要るときは Vendor 側へ逃がす」と同じ結論に、実測から到達したことになる
- **device側とhost側の時間を両方記録する型**(採用)。今回は比が1.000だったので律速の所在を1行で言えた

### 未決

- **vendor bulk(WinUSB)ならいくつ出るか** `—`。事実6が正しければCDCの上限はturnaroundなので、FIFOを深くすれば伸びるはず。これが次の問い(`p4-hs-cdc-vs-bulk`)
- **CDCのTX FIFOを512 Bより深くできるか。** `CONFIG_TINYUSB_CDC_TX_BUFSIZE`はprecompiled libsの`sdkconfig`で固定されており、Arduino-ESP32のまま変えられるかは未確認
- **1 microframeあたり1.37 transactionという読みの直接確認** `—`。USBPcapでtransactionを数えれば裏が取れるが、フルHSレートでは取りこぼす前提
- **2本目以降のCDCを足したとき、合計帯域は増えるか**(`p4-cdc-budget-hs`)
- **host→device方向(OUT)の帯域** `—`。未測定
- **PARLIO captureと同時に流したときの帯域とdrainへの影響**(`p4-usb-vs-drain`)
- **usbip経由(WSL)との差** `—`(`usbipd-overhead`)

### 近直の目標への含み

2 channelのPARLIO captureは1 byteに4 sampleを詰めるので、**5.6 MB/sは約22.4 Msps相当の連続streamingに当たる**。「2chで数十Msps」を連続で出すにはこの経路では足りず、次のどちらかが要る。

- **vendor bulkで帯域を上げる**(未測定。上の未決の1つ目)
- **PSRAMへbatchしてから流す**。16 MiBのbufferは2 channel / 50 Mspsなら約1.34秒ぶんで、download は2.968秒。**「撮ってから出す」なら現状の帯域でも成立する**

### 反映

- [LEDGER](../LEDGER.ja.md) §1にE064を採番、§3に記録を追加
- [p4-logic-analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段のdownload時間の表を実測値で置き換え
- 仕様([../../protocols/](../../protocols/))のstatusは動かさない
