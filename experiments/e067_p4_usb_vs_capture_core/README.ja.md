# E067 ESP32-P4 PARLIO captureとUSB HS送出のcore競合

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E066](../e066_p4_usb_hs_tx_context/README.ja.md)(USB送出はcore 0で速い)、[E061](../e061_p4_drain_core_split/README.ja.md)(captureの回収は別coreでdrainが上がる)、[E021](../e021_p4_parlio_psram_spool/README.ja.md)(spool経路)

## 問い

**PARLIO captureとUSB HS送出を同時に走らせると、互いの性能をどれだけ食うか。2つをどのcoreへ置くかで結果は変わるか。**

## 仮説

**食う。そして`ARDUINO_RUNNING_CORE`(= core 1)を避けて両者を別々のcoreへ分けた配置(harvest = core 1、USB = core 0)が最良になる。**

2つの先行結果が**同じcoreを要求している**。

- [E066](../e066_p4_usb_hs_tx_context/README.ja.md): USB送出は**core 0で7.4〜8.1 MB/s、core 1で5.2〜5.7 MB/s**。`USB.begin()`が`setup()`(core 1)から呼ばれるためUSBの割り込みがcore 1にあり、送出がそれと競合している、という読み
- [E061](../e061_p4_drain_core_split/README.ja.md): PARLIOの回収を別coreへ移すとdrainが82.3 → 119.7 MB/sへ上がる

**両方がcore 0を欲しがるなら、同時に最良にはならない。** どちらを譲るのが安いかを測る。

## 反証条件

1. 同時に走らせても両者とも単独時と変わらない。競合は無く、core配分を考える必要が無い
2. core配分を変えても結果が変わらない。[E066](../e066_p4_usb_hs_tx_context/README.ja.md)のcore依存はUSB単独のときだけの性質だったことになる
3. **同じcoreに両方を置いた配置が、分けた配置より速い**。「分ける」という前提が誤り
4. captureがどの配置でもdropする。この rate では同時実行が成立しない
5. USB送出がどの配置でも単独時と同じ。USB側は競合の影響を受けない

## 方法

### 構成

- **capture** = PARLIO RX、**data_width 2**、sample rate 32 MHz。信号源はLEDC PWM 100 kHzを`PARLIO_PINS`の先頭2本へ出し、GPIO matrix経由でPARLIO RXが読み戻す(**配線なし**、[E015](../e015_p4_parlio_routing_order/README.ja.md)で確認済みの経路)
- 経路は[E021](../e021_p4_parlio_psram_spool/README.ja.md)と同じ形 — internal DMA ring 64 KiB → `on_partial_receive`がchunkをqueueへ → harvest taskがPSRAMのsinkへ`memcpy`。**sinkは1 MiBで周回させる**ので、copyの費用は実物のまま容量だけ抑えられる
- **USB送出** = OTG HS上のCDCへPSRAMから4 MiB。chunk 4,096 B、[E066](../e066_p4_usb_hs_tx_context/README.ja.md)の形
- identityは`1209:0006`。E063〜E066(`0002`〜`0005`)とWindowsのdevice instanceを分ける

**sample rate 32 MHzを選ぶ理由**: 2 channelは1 byteに4 sampleなので**byte rateは8 MB/s**になり、[E066](../e066_p4_usb_hs_tx_context/README.ja.md)の最速USB帯域8.08 MB/sとほぼ同じである。**「2chで数十Mspsを流し続ける」ときに実際に起きる負荷**をそのまま再現する。

### mode

| mode | capture | usb | harvest core | usb core | 役割 |
|---|---|---|---:|---:|---|
| `usb_c0` | — | ○ | — | 0 | USB単独の基準 |
| `usb_c1` | — | ○ | — | 1 | 同上 |
| `cap_c0` | ○ | — | 0 | — | capture単独の基準 |
| `cap_c1` | ○ | — | 1 | — | 同上 |
| `cap1_usb0` | ○ | ○ | 1 | 0 | **分ける(仮説の本命)** |
| `cap0_usb1` | ○ | ○ | 0 | 1 | 分ける(逆) |
| `cap0_usb0` | ○ | ○ | 0 | 0 | 同じcoreへ寄せる |
| `cap1_usb1` | ○ | ○ | 1 | 1 | 同じcoreへ寄せる(逆) |

各mode 3回。**振るのはcore配分だけ**で、rateもUSBの転送量も固定する。

USBを含むmodeはhost側readerの`G`で始まり、USB送出が終わったらcaptureを止める。capture単独のmodeはreaderが居ないのでconsoleから直接始め、600 ms走らせる。

### dropの判定

pattern照合ではなく**計数**で見る。

- `cap_overflow` = ISRがqueueへ積めなかった回数(> 0ならdrop)
- `cap_callback_bytes`(DMAが作った量)と`cap_copied`(harvestが運んだ量)の差
- `cap_timeout` = harvestが200 msの間chunkを受け取れなかったか

**周期的なPWMを信号源にした照合では sample単位の欠落を検出できない**([E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)で実証済み)ので、この実験は照合を使わず計数だけで判定する。**したがって「dropなし」は計数の範囲での主張にとどまる。**

## 対象外

- sample rateの掃引。**この実験で振るのはcore配分だけ**。rate境界は別の問い
- captureしたdataをそのままUSBへ流す真のstreaming pipeline。ここでは**負荷として同時に走らせるだけ**で、2つは別のbufferを使う
- sample単位の欠落検証(上記の理由)
- 2 channel以外のwidth
- vendor bulk

## 必要な環境

[E066](../e066_p4_usb_hs_tx_context/README.ja.md)と同じ。加えて`.env`の`TEST_PARLIO_PINS`(先頭2本を使う)。**外部配線は不要**。

## ベンチ種別

**一時・配線なし**。

## 記録する数値

| 項目 | 列 |
|---|---|
| 条件 | `mode`、`name`、`harvest_core`、`usb_core`、`rate_hz` |
| USB | `usb_elapsed_us`、`usb_written`、`usb_short`、host側MB/s |
| capture | `cap_elapsed_us`、`cap_callback_bytes`、`cap_copied`、`cap_overflow`、`cap_timeout`、実効MB/s |
| 導出 | 単独基準に対する比(USB / capture それぞれ) |

各mode 3回。min / median / max。

## 完了条件

1. **同時実行で互いがどれだけ落ちるかが比で言える**
2. **core配分の4通りの順位が言える**
3. **どの配置でdropが出るか出ないかが言える**

## 影響

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段 — 「captureとUSB送出はcoreを取り合う」を実測で埋める
- 近直の目標「2chで数十Mspsを連続streaming」が**現実の負荷で成立するか**の最初の実測

---

## 結果

状態: **完了 — captureは一切落ちない。落ちるのはUSB側だけで、7〜16%。最良は仮説どおり`cap1_usb0`(harvest = core 1、USB = core 0)。ただし効きの大きさは「分離」より「USBをcore 0へ置くこと」**(2026-09-12)

採用run: `_runs/E067_20260912T04*`(最終の1実行、24条件)。**計測器の不具合と経路の異常で4回作り直しているので、下の「経路の異常」節を必ず併せて読む。**

### mode別

`usb`はdevice側の`esp_timer_get_time()`による値。host側との差は最終runでは0.1%以内だった(`host med`列)。

| mode | harvest core | usb core | usb min | **usb median** | usb max | host median | capture median | overflow |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `usb_c0` | — | 0 | 7.80 | **7.97** | 8.19 | 7.96 | — | 0 |
| `usb_c1` | — | 1 | 5.90 | **5.96** | 6.01 | 5.96 | — | 0 |
| `cap_c0` | 0 | — | — | — | — | — | **8.00** | 0 |
| `cap_c1` | 1 | — | — | — | — | — | **8.00** | 0 |
| **`cap1_usb0`** | 1 | 0 | 7.36 | **7.42** | 7.47 | 7.40 | **8.00** | 0 |
| `cap0_usb0` | 0 | 0 | 6.62 | **6.69** | 6.81 | 6.68 | **8.00** | 0 |
| `cap0_usb1` | 0 | 1 | 5.52 | **5.52** | 5.57 | 5.51 | **8.00** | 0 |
| `cap1_usb1` | 1 | 1 | 5.07 | **5.11** | 5.12 | 5.11 | **8.00** | 0 |

### 単独時に対する残り

| 配置 | 同じcoreの単独基準 | 同時実行時 | 残り |
|---|---:|---:|---:|
| `cap1_usb0`(**分離**) | 7.97 | 7.42 | **93.1%** |
| `cap0_usb1`(**分離**) | 5.96 | 5.52 | **92.6%** |
| `cap0_usb0`(同居) | 7.97 | 6.69 | 84.0% |
| `cap1_usb1`(同居) | 5.96 | 5.11 | 85.7% |

**分離すると約93%、同居だと約84〜86%が残る。** 一方、core 0とcore 1の差は単独時点で7.97対5.96(**+34%**)ある。**つまりcoreの選択の方が、分離するかどうかより効きが大きい。**

### capture側

**18回のcapture全部で8.00 MB/s(min 7.999 / max 8.000)、queue overflow 0、`callback_bytes`と`copied`の差0。** 設定の32 MHz × 2 bit ÷ 8 = 8.0 MB/sにそのまま一致する。**USBが何をしていてもcaptureは影響を受けなかった。**

### 事実

1. **captureは同時実行の影響を受けない。** 2 channel / 32 MHz(8.00 MB/s)では、USB送出をどのcoreで走らせてもcaptureは8.00 MB/sを維持し、overflowも取りこぼしも0だった。
2. **落ちるのはUSB側だけで、7〜16%。** 最良の`cap1_usb0`で単独比93.1%、最悪の`cap0_usb0`で84.0%。
3. **最良の配置は`cap1_usb0`(harvest = core 1、USB = core 0)で7.42 MB/s。** 仮説の予測どおりだった。
4. **ただし理由は仮説と違う。** 「分離が効く」のは93%対84%の**約9 point**だが、「USBをcore 0へ置く」効果は単独時点で7.97対5.96の**+34%**ある。**順位を決めているのは主にUSB taskのcoreで、分離はその上の小さな上積みである。**
5. **同居でも配置次第で分離を上回る。** `cap0_usb0`(同じcore 0に両方、6.69 MB/s)は`cap0_usb1`(分離だがUSBがcore 1、5.52 MB/s)より速い。**反証条件3は部分的に発火した** — 「分ければ速い」は成り立たない。
6. [E066](../e066_p4_usb_hs_tx_context/README.ja.md)のcore依存は**capture負荷の下でもそのまま残る**(単独7.97/5.96、同時7.42/5.52で比はほぼ同じ)。

### 経路の異常 — 転送末尾の欠落

**規則 [§7-6](../README.ja.md)に従い、failureではなく観測として残す。**

掃引を完走させるまでに5回実行し、**そのうち4回で「device側は全byte書き終えているのに、host側に末尾が届かない」**という現象が出た。

| 実行 | 欠落したmode | 欠落量 | 備考 |
|---|---|---:|---|
| 1回目 | `usb_c0` ×3 | 5,120 B | mode 0が最初 |
| 2回目 | `usb_c0` | 5,120 B | 計測器修正後 |
| 3回目 | `usb_c0` | 2,560 B | **`usb_c1`を先に走らせても`usb_c0`で発生**(「最初の転送だから」ではない) |
| 4回目 | `cap1_usb0` | 2,048 B | **転送長を512 Bの整数倍から外した**(4 MiB − 100 B)ら別modeへ移った |
| 5回目(採用) | 無し | — | 同じ4 MiB − 100 B、reader timeoutを30 → 5秒 |

観測できること。

- 欠落量は**常に512 B(bulkの`wMaxPacketSize`)の整数倍**で、2,048〜5,120 B
- device側は`usb_written`が全byte、`usb_short`が0。**deviceは「書き終えた」と思っている**
- 送出後に`HsCdc.flush()`を10 ms間隔で10回繰り返しても届かない。**deviceのFIFO(512 B)に溜まっているのではない** — 2,048 Bは512 Bに収まらない
- **転送長を512 Bの整数倍から外しても消えない。** short packetでの終端が無いことが唯一の原因ではない
- 発生するmodeは実行ごとに変わる。**間欠的である**

**原因は未特定。** ただし**logic analyzerのdownloadが末尾を静かに失う経路は使えない**ので、**これは次に潰すべき最優先の問題**である(`p4-hs-cdc-tail-loss`)。この実験の帯域の数値はdevice側時計で取っており、欠落の有無に影響されない。

### 候補

- **USB送出はcore 0、captureのharvestはcore 1**(採用)。`ARDUINO_RUNNING_CORE`が1なので、USBを`loop()`の反対側へ置く
- **captureの方が丈夫なので、譲るならUSB側**(採用の方向)。8 MB/s級のcaptureはUSBの有無で揺れない
- 転送の完了をhostが確実に知る仕組み(長さの事前通知、終端marker、CRC)が要る。**現状はhostが「来るはずの数」を待つだけで、来なければ黙って止まる**

### 未決

- **転送末尾の欠落の原因** `—`(`p4-hs-cdc-tail-loss`)。**最優先**。USBPcapでの観測、TinyUSBのZLP送出の確認、`usbser`側のbuffer挙動の切り分けが要る
- **sample rateを上げたときの境界** `—`。この実験は32 MHz固定。captureが8.00 MB/sで揺れなかったので、**captureが折れるrateはもっと上にある**
- **真のstreaming(captureしたdataをそのままUSBへ)** `—`。ここでは別々のbufferを使った同時実行であり、**ring → USBの受け渡しを挟んだときの費用は測っていない**
- vendor bulkでの同じ表 `—`
- core 1が遅い理由([E066](../e066_p4_usb_hs_tx_context/README.ja.md)から継続) `—`

### 近直の目標への含み

**2 channel / 32 MHzのcaptureは8.00 MB/sを出し、同時にUSBは最良で7.42 MB/s出る。** つまり**生成8.00に対し排出7.42で、連続streamingは釣り合わない**。釣り合う点は7.42 MB/s = **約29.7 Msps**。

- **「2chで約30 Mspsまでなら連続streamingが成立する」**見込み(ただし上の末尾欠落を潰してから)
- それ以上は**PSRAMへbatchしてから出す**。16 MiBは2ch/50 Mspsで約1.34秒ぶん、downloadは[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)の実測で約2〜3秒

### 反映

- [LEDGER](../LEDGER.ja.md) §1にE067を採番、§3に記録を追加
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段の「captureとUSB送出はcoreを取り合う」を実測で置き換え
- 仕様([../../protocols/](../../protocols/))のstatusは動かさない
