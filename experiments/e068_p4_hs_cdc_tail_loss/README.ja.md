# E068 ESP32-P4 USB HS CDCの転送末尾欠落 — 消失か滞留か

状態: **完了 — 前提が誤り。欠落は転送の途中で、dataは失われる。30回中4回(13%)**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 発端: [E067](../e067_p4_usb_vs_capture_core/README.ja.md)「経路の異常」

## 問い

**USB HS CDCの転送でhostへ届かなかった末尾は、失われているのか、どこかに滞留しているのか。滞留なら、何をすれば出てくるか。**

## 仮説

**滞留している。deviceが転送の後に何かを1 byteでも送れば、滞っていた末尾も一緒に出てくる。**

[E067](../e067_p4_usb_vs_capture_core/README.ja.md)の観測から、次が言える。

- 欠落量は**常に512 B(bulkの`wMaxPacketSize`)の整数倍で2,048〜5,120 B**
- **2,048 Bは device側のbufferに収まらない。** `CONFIG_TINYUSB_CDC_TX_BUFSIZE`は512 Bで、endpoint bufferを足しても最大1,024 B。**それを超える量が滞るなら、滞っている場所はdeviceのFIFOではない**
- device側は`usb_written`が全byte、`usb_short`が0
- 送出後に10 ms間隔で10回`flush()`しても出てこない

「deviceのFIFOに収まらない量が消えている」なら、**bytes は線に出たがhost側で滞留しているか、線にすら出ていないか**のどちらかである。この実験は**まず「出てくるか」を見る** — 出てくるなら滞留、出てこないなら消失で、次に見るべき場所が変わる。

## 反証条件

1. deviceを突いても末尾が出てこない。**消失**であり、次はUSBPcapで線上を見る話になる
2. 突く前に、時間を置くだけで出てくる。滞留だが原因は時間依存の何かで、突く必要は無い
3. 欠落が再現しない。[E067](../e067_p4_usb_vs_capture_core/README.ja.md)の4回は別の条件によるもので、条件を取り違えている
4. 欠落量が512 Bの整数倍でない回が出る。packet単位という理解が誤り
5. 欠落が転送の末尾ではなく途中で起きている(受信したbyte列のpatternが途中で飛ぶ)

## 方法

### 条件

[E067](../e067_p4_usb_vs_capture_core/README.ja.md)で最も頻繁に欠落した条件をそのまま使う。

- USB送出task = **core 0にpin**、captureは**走らせない**
- 転送 4 MiB、chunk 4,096 B
- identityは`1209:0007`

### 手順(1回ぶん)

1. harnessがconsoleへ`C`を送り、deviceを武装させる
2. host側readerがCOM portを開き、`G`を書く
3. deviceが4 MiBを送り、`written`と所要時間をconsoleへ出す
4. readerは4 MiBを**短いtimeout(2秒)**で読む。読み切れたらそこで終わり
5. **読み切れなかったら**、まず**さらに2秒待つ**(反証条件2の判定)。それでも来なければ**`T`を1 byte書いてdeviceを突く**
6. deviceは`T`を受けたら**16 byteのterminator**(`E0 68` + 連番)を送る
7. readerは以降に届いたものを全部集め、**(a) 滞っていた末尾 (b) terminator** のどちらが、どの順で来たかを記録する

### 受信内容の分類

送出patternは32-bit word index(`word[i] = i`)なので、**届いたbyteがどのoffsetのものか一意に決まる**。したがって次を区別できる。

- 末尾が来た(欠落分のoffsetのwordが揃う)
- terminatorだけ来た
- 途中が抜けている(反証条件5)

### 繰り返し

**30回**。欠落の頻度が[E067](../e067_p4_usb_vs_capture_core/README.ja.md)の観測(5回中4回の実行で、1実行24転送中1〜3転送)どおりなら、30回で数回は出る見込み。**出ない場合も回数を記録する**(反証条件3)。

## 対象外

- 原因の特定そのもの。**この実験は「消失か滞留か」までしか答えない**
- USBPcapでの線上観測。反証条件1が成立したときに別実験で行う
- vendor bulkやFS側での比較
- captureとの同時実行(この実験はcaptureを走らせない)
- 直し方の実装

## 必要な環境

[E067](../e067_p4_usb_vs_capture_core/README.ja.md)と同じ。

## ベンチ種別

**一時・配線なし**。

## 記録する数値

| 項目 | 列 |
|---|---|
| 各回 | 転送番号、device側`written` / `elapsed_us`、host側の初回受信byte数 |
| 欠落時 | 不足byte数、512で割った余り、待機だけで来たか、`T`で来たか、来た順 |
| 集計 | 30回中の欠落回数、不足byte数の分布 |

## 完了条件

1. **30回中の欠落回数が言える**
2. **欠落したとき、末尾が「出てくる」か「出てこない」かがyes/noで言える**
3. 出てくる場合、**待つだけで出るのか突く必要があるのか**が言える

欠落が1回も起きなければ、**その事実(30回で0回)を記録して完了**とし、再現条件を未決に回す。

## 影響

- [E067](../e067_p4_usb_vs_capture_core/README.ja.md)の「経路の異常」を一段進める
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段の「条件付き」という但し書きを外せるかどうか
- **転送完了をhostが確実に知る仕組み**(長さの事前通知・終端marker・CRC)を入れるべきかの根拠

---

## 結果

状態: **完了 — 問いの前提が誤っていた。欠落は末尾ではなく転送の途中で起きており、data は失われている(滞留ではない)。4 MiB転送30回中4回(13%)**(2026-09-12)

run: `_runs/E068_20260912T05*`。30回、条件は固定(4 MiB / chunk 4,096 B / 送出taskはcore 0 / captureなし)。

### 集計

| 項目 | 値 |
|---|---|
| 転送回数 | 30 |
| **byte数が不足した回** | **4(13%)** |
| **待つだけで出てきた回** | **0** |
| **`T`で突いて出てきた回** | **0** |
| terminatorが届いた回 | 4/4(**突いた後の応答は正常**) |
| deviceの申告 | 30回すべて`written=4,194,304`、`short=0` |
| 実効帯域(median) | 7.82 MB/s |

### 欠落の位置と大きさ

| attempt | 最初に食い違ったoffset | packet番号 | 欠落量 | 待機で回収 | 突いて回収 |
|---:|---:|---:|---:|---:|---:|
| 5 | 3,291,648 | 6,429.0 | 2,560 B(**5 packet**) | 0 B | 0 B |
| 7 | 3,748,865 | 7,322.0 | 2,048 B(**4 packet**) | 0 B | 0 B |
| 13 | 2,571,264 | 5,022.0 | 2,560 B(**5 packet**) | 0 B | 0 B |
| 15 | 2,242,049 | 4,379.0 | 2,048 B(**4 packet**) | 0 B | 0 B |

欠落量は**すべて512 B(bulkの`wMaxPacketSize`)の整数倍**。offsetが512の倍数からずれている2件は、**gapの直後の1 byteがたまたま期待値と一致した**ためで、gap自体はpacket境界から始まっている。

### 事実

1. **問いの前提が誤っていた。欠落は転送の「末尾」ではなく「途中」で起きている。** 受信streamは`前半 + (gap) + 後半`の形で、**後半は正しく届いている**。byte数だけ数えていたE067では、これが「末尾が足りない」ように見えていた。
2. **dataは失われている。滞留ではない。** 2秒待っても0 byte、`T`で突いても返ってくるのは**16 byteのterminatorだけ**で、欠けた2,048〜2,560 Bは二度と来ない。**反証条件1が成立した。**
3. **欠落は512 Bの整数倍で、4〜5 packet分。** 4件とも4 packetか5 packetだった。
4. **発生率は30回中4回 = 13%。** 4 MiB(8,192 packet)あたり4〜5 packetなので、**packet単位では約0.05〜0.06%**。
5. **deviceは気づいていない。** 30回すべてで`usb_written`は全byte、`usb_short`は0。**`USBCDC::write()`の戻り値は「線に出た」ことを意味しない。**
6. **突いた後の経路は正常。** terminatorは4件とも届いており、**endpointが壊れて止まるのではなく、途中のpacketだけが消える**。
7. 欠落したofffsetは2.2〜3.7 MiBの範囲にばらけており、**転送の特定の位置ではない**。

### 候補(原因の当たり。**この実験は確かめていない**)

- **TinyUSBのCDC TX FIFOに対する、application taskとusbd taskの跨core競合。** `esp32-hal-tinyusb.c:886`の`usbd` taskは**core指定なし**で作られ、`USBCDC::write()`は自前の`tx_lock`でapplication側だけを直列化する。**FIFOのもう一方の読み手(完了callbackからの`tud_cdc_n_write_flush()`)は別coreで同時に走りうる。**
- [E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)は**8転送をpattern照合して不一致0**、[E066](../e066_p4_usb_hs_tx_context/README.ja.md)は6転送で不一致0だった。13%なら14転送で約2件出る計算なので、**発生率は条件(特に送出taskのcore)に依存する可能性がある**。E064の送出は`loop()`(core 1)だった

### 未決

- **原因の特定** `—`。**送出taskのcore別に発生率を測る**のが次(`p4-hs-cdc-drop-rate-by-core`)。跨core競合なら、送出とusbdが同じcoreに寄る条件で下がるはず
- **USBPcapで線上を見る** `—`。packetが線に出ていないのか、出たがhostが捨てているのかは未判定。**この実験はhost applicationから見た結果しか見ていない**
- vendor bulkやUSB-Serial-JTAG(FS)でも起きるか `—`
- ESP-IDF直(Arduino外)で`CFG_TUD_CDC_TX_BUFSIZE`を深くしたら変わるか `—`
- **転送量との関係** `—`。4 MiB固定でしか測っていない。packet単位の率が一定なら、短い転送ほど無事な確率が高い

### 影響 — この経路は現状そのままでは使えない

**logic analyzerのdownloadとしては失格である。** 13%の転送で数KiBが黙って消え、**device側もhost側も気づかない**。[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)〜[E067](../e067_p4_usb_vs_capture_core/README.ja.md)で測った帯域の数値そのものは device側時計なので有効だが、**「その帯域でdataが正しく渡る」とは言えない**。

当面の運用として必要なもの。

- **転送に長さとCRCを付け、hostが検証して再送を要求できるようにする**(protocol側の手当て)
- 原因が分かるまで、**pattern照合なしの転送を信用しない**

### 反映

- [LEDGER](../LEDGER.ja.md) §1にE068を採番、§3に記録を追加
- [E067](../e067_p4_usb_vs_capture_core/README.ja.md)「経路の異常」の記述を訂正(末尾ではなく途中)
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段の但し書きを強める
- 仕様([../../protocols/](../../protocols/))のstatusは動かさない
