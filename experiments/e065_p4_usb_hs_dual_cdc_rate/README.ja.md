# E065 ESP32-P4 USB HS CDCを2本並列にしたときの合計帯域

状態: **完了 — 反証。2本でも合計は上がらない(比0.943)**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)(1本で約5.6 MB/s飽和)、[E063](../e063_p4_usb_hs_enumerate/README.ja.md)(HSで列挙)

## 問い

**USB 2.0 HS上のCDCを2本同時に流したとき、合計帯域は1本のときの約5.6 MB/sより上がるか。上がるなら何MB/sか。**

## 仮説

**上がる。合計は1本の約2倍(11 MB/s前後)に近づく。**

[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)の事実6から、1本の上限はbus帯域ではなくturnaroundだと読める — 512 Bのtransactionが1 microframeあたり約1.37回しか出ておらず、HSが許す13回には遠い。TinyUSBのCDC TX FIFOは`CONFIG_TINYUSB_CDC_TX_BUFSIZE=512`でbulk 1 packet分しかなく、endpointごとに「1 transferを投げて完了を待つ」形になる。**この待ちがendpointごとに独立なら、endpointを増やした分だけ並ぶ**。

`CFG_TUD_CDC = CONFIG_TINYUSB_CDC_MAX_PORTS = 2`なので、Arduino-ESP32のまま2本までは作れる。

## 反証条件

1. 合計帯域が1本と変わらない。律速はendpointごとのturnaroundではなく、**共通の場所**(usbd task、GDMA、bus scheduling)にある
2. 2本にすると1本あたりが半分以下に落ち、合計が下がる
3. 片方のportだけが流れ、もう片方が止まる
4. data化けまたは欠落がある。その条件の帯域は採らない
5. Windowsが2本目のCDCにCOM portを割り当てない

## 方法

### 構成

- **console** = USB-Serial-JTAG(FS)。設定と結果のみ
- **計測対象** = OTG HS上のCDC ACM **2本**(interface 0–1 と 2–3)。Windowsには2つのCOM portとして生える想定
- identityは`1209:0004`。E063(`0002`)・E064(`0003`)とWindowsのdevice instanceを分ける
- 送出は**portごとに1つのFreeRTOS task**とし、**別coreへpinする**。[E061](../e061_p4_drain_core_split/README.ja.md)で、回収を別coreへ移すとdrainが82.3 → 119.7 MB/sへ上がっている

### 型

1. harnessがconsoleへ`C <bytes/port> <chunk> <ports>`を送る
2. 各portに対してhost側readerを**同時に**起動する。readerは自分のportへ`G`を1 byte書く
3. deviceは**有効な全portで`G`が揃うまで待ち**、揃った時刻を`t0`として両taskを起こす
4. 両taskの完了時刻のうち遅い方を`t1`とし、`span_us = t1 - t0`を報告する。**合計帯域 = 全byte数 ÷ span**
5. reader側も各portの受信byte数と時間を返す

全portが揃うまで待つのは、起動のずれで重なりが減ると「並列にした効果」が薄まって見えるため。

### 掃引

| 段 | ports | port あたり転送 | chunk | 回数 |
|---|---:|---:|---:|---:|
| A(基準) | 1 | 4 MiB | 4,096 B | 3 |
| B | 2 | 4 MiB(合計8 MiB) | 4,096 B | 3 |

chunkはE064で飽和側だった4,096 Bに固定する。**この実験で振るのはport数だけ**。

## 対象外

- vendor bulk / WinUSB経路(`p4-hs-cdc-vs-bulk`)
- 3本以上(`CFG_TUD_CDC=2`のため作れない)
- CDC以外のclassとの混在
- host → device方向
- PARLIO captureとの同時動作(`p4-usb-vs-drain`)
- 1 taskでのround-robin実装との比較。**task分離の方が速いという仮定は[E061](../e061_p4_drain_core_split/README.ja.md)から借りており、この実験では検証しない**

## 必要な環境

[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)と同じ。HS portはWindowsに接続したまま、Windows側はuvで`read_windows.py`を走らせる。

## ベンチ種別

**一時・配線なし**。

## 記録する数値

| 項目 | 列 |
|---|---|
| 条件 | `ports`、`bytes/port`、`chunk` |
| device側 | portごとの`elapsed_us`、`written`、`short`、全体の`span_us` |
| host側 | portごとの受信byte数、経過秒、MB/s、pattern不一致位置 |
| 導出 | 合計MB/s(= 全byte ÷ span)、1本あたりMB/s、基準に対する比 |

各条件3回。min / median / max。

## 完了条件

1. **2本の合計帯域がMB/sで言える**
2. **1本(基準)との比が言える**
3. **pattern検証が通った条件だけを表に入れている**

## 影響

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段のstreaming上限
- [harness-channels](../../references/harness-channels.ja.md) §CDCの上限は endpoint 予算 — 「CDCの本数」を帯域の面から埋める
- 近直の目標「2chで数十Msps」を連続streamingで出せるかの判定。2 channelのPARLIOは1 byteに4 sampleなので、**合計11 MB/sなら約44 Msps相当**になる

---

## 結果

状態: **完了 — 仮説は反証された。2本にしても合計は上がらない(比0.943)。律速はendpointごとではなく共有部分にある**(2026-09-12)

run: `_runs/E065_20260912T025*`。1実行で6条件。

### 合計帯域

| ports | 合計転送 | combined MB/s (min / **median** / max) | span ms (median) | 基準比 |
|---:|---:|---|---:|---:|
| 1 | 4 MiB | 7.54 / **7.94** / 8.11 | 528.3 | 1.000 |
| 2 | 8 MiB | 7.49 / **7.49** / 7.63 | 1,120.2 | **0.943** |

### 2本のときのport別

| port | pin先core | device median | host median |
|---|---:|---:|---:|
| 0 | core 0 | 906.7 ms(4.63 MB/s) | 3.89 MB/s |
| 1 | core 1 | 1,120.1 ms(3.74 MB/s) | 3.74 MB/s |

**2本の和(4.63 + 3.74 = 8.37 MB/s)は1本の値(7.94 MB/s)とほぼ同じで、増分はほぼ無い。** port 0が先に終わり、残りをport 1が単独で流すぶんだけ和が見かけ上わずかに大きい。

### 健全性

pattern検証3回で不一致0、短write 0、stall 0、受信byte数は全12 portで要求と一致。

### 事実

1. **CDCを2本にしても合計帯域は上がらない。** 1本7.94 MB/sに対し2本合計7.49 MB/s(比0.943)で、**わずかに下がる**。**反証条件1が発火した** — 律速はendpointごとのturnaroundではなく、**共有された場所**にある。
2. **2本のとき、帯域は2本でほぼ等分される。** device側で4.63と3.74 MB/s。片方を増やせばもう片方が減る形であり、「endpointを増やせば並ぶ」という[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)からの読みは誤りだった。
3. **port 0(core 0)が常にport 1(core 1)より速い。** 3回とも906〜927 ms対1,100〜1,121 msで、順序は安定している。
4. **共有部分の候補はTinyUSBのdevice taskである。** `esp32-hal-tinyusb.c:886`は `xTaskCreate(usb_device_task, "usbd", 4096, NULL, configMAX_PRIORITIES - 1, NULL)` で、**全endpointを1本の最高優先度taskが捌き、core指定は無い**。単一のservice taskがすべてのtransfer完了を直列に処理する形は、「endpointを増やしても帯域が増えない」ことと整合する。**これは構造の読みであって、この実験が直接測ったものではない。**
5. **同じ1 port・4 MiB・chunk 4,096 Bの条件で、[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)の5.59 MB/sに対しこの実験は7.94 MB/s(+42%)だった。** 違いは**送出の実行context**で、E064はArduinoの`loop()`(= `loopTask`、優先度1、`ARDUINO_RUNNING_CORE`にpin)から、E065は**専用のFreeRTOS task(優先度5、coreにpin)**から書いている。**ただしE065は2本目のCDCもbuildに含んでおり、変数が1つではない。** 統制した比較は別実験に要る。
6. 7.94 MB/s ÷ 512 B = 約15,500 transaction/s。HSのmicroframeは8,000回/秒なので**1 microframeあたり約1.94 transaction**。HSが許す13にはなお遠く、**上限を決めているのは依然としてturnaroundである**。

### 候補

- **送出は`loop()`からではなく専用taskから行う**(事実5。ただし統制前)
- **帯域を上げる手段としてendpointを増やすのは効かない**(事実1。却下)
- 残る手段は(a)共有service taskの負荷を下げる、(b)1 transferあたりのbyte数を増やす(FIFOを深くする = Arduinoのままでは不可)、(c)**PSRAMへbatchしてから出す**

### 未決

- **事実5の統制** `—`。「送出taskのpriority / core / contextだけ」を振って1本の帯域を測る。**これが次の問い**(`p4-hs-tx-context`)
- **共有部分が本当にusbd taskか** `—`。taskのCPU使用率やcore別の実行時間を測れば切り分くが、この実験では測っていない
- **port 0とport 1の非対称の理由** `—`。pin先core、interface番号、endpoint番号のどれが効いているか未分離
- **vendor bulkでも同じ天井か** `—`(`p4-hs-cdc-vs-bulk`)。事実1が正しければclassを変えても共有部分は変わらないので、**vendorに期待する理由は弱まった**
- 3本以上は`CFG_TUD_CDC=2`のため試せない

### 近直の目標への含み

**7.94 MB/sは、2 channelのPARLIO(1 byteに4 sample)なら約31.8 Msps相当の連続streamingに当たる。** [E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)時点の22.4 Mspsから上がり、**「2chで数十Msps」は連続streamingでも射程に入った**。ただしそれは事実5(送出context)が効いている場合であり、**統制した確認が先に要る**。

### 反映

- [LEDGER](../LEDGER.ja.md) §1にE065を採番、§3に記録を追加
- [harness-channels](../../references/harness-channels.ja.md) §CDCの上限は endpoint 予算 — **「本数を増やせば帯域が増える」ではないことが実測で付いた**
- 仕様([../../protocols/](../../protocols/))のstatusは動かさない
