# E066 ESP32-P4 USB HS CDCの帯域は送出contextで決まるか

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)(`loop()`から5.59 MB/s)、[E065](../e065_p4_usb_hs_dual_cdc_rate/README.ja.md)(専用taskから7.94 MB/s、ただし変数が1つではない)

## 問い

**CDC 1本のdownload帯域は、送出を実行するcontext(`loop()`か専用taskか、優先度、pin先core)で変わるか。変わるなら何が効くか。**

## 仮説

**変わる。効くのは優先度で、`loop()`(優先度1)が最も遅い。coreとpinの有無は小さい。**

[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)と[E065](../e065_p4_usb_hs_dual_cdc_rate/README.ja.md)は同じ1 port / 4 MiB / chunk 4,096 Bの条件で5.59と7.94 MB/s(+42%)だった。**この2実験の違いは送出contextと、E065が2本目のCDCをbuildに含むことの2つ**で、どちらが効いたか分かっていない。

contextが効くと考える理由は、`USBCDC::write()`がFIFOが埋まっている間`tud_cdc_n_write_flush()`を呼び続けるbusy loopで、**送出側がどれだけ早くFIFOを埋め直せるかが1 transactionあたりの間隔を決める**から。Arduinoの`loopTask`は`main.cpp:113`で**優先度1**で作られており、ESP-IDFの多くのsystem taskより低い。一方TinyUSBの`usbd` taskは`esp32-hal-tinyusb.c:886`で**`configMAX_PRIORITIES - 1`、core指定なし**で作られる。

## 反証条件

1. 全contextで帯域が変わらない。E064とE065の差は**2本目のCDCをbuildに含むかどうか**で説明されることになり、[E065](../e065_p4_usb_hs_dual_cdc_rate/README.ja.md)の事実5は取り下げる
2. 優先度を上げると遅くなる(送出taskが`usbd` taskを食う)
3. `loop()`でもE065と同じ7.9 MB/s級が出る。E064の5.59 MB/sの原因はcontext以外にある
4. 同一contextの3回のばらつきが条件間の差より大きい。差を主張できない
5. data化けまたは欠落がある

## 方法

**CDCは1本だけbeginする**。descriptor構成を[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)と同じにして、E065の変数を落とす。identityは`1209:0005`。

転送は4 MiB、chunkは4,096 Bに固定し、**振るのは送出contextだけ**。

| mode | context | 優先度 | core |
|---:|---|---:|---|
| 0 | `loop()` | 1 | `ARDUINO_RUNNING_CORE` |
| 1 | 専用task | 1 | 0 |
| 2 | 専用task | 5 | 0 |
| 3 | 専用task | 5 | 1 |
| 4 | 専用task | 20 | 0 |
| 5 | 専用task | 5 | pinしない |

優先度20は`usbd`の`configMAX_PRIORITIES - 1`(既定25構成では24)より下に置く。**`usbd`を追い越さない範囲で上げる**。

各mode 3回。modeごとにtaskを作り直し、終わったら削除する。

## 対象外

- 2本以上のCDC([E065](../e065_p4_usb_hs_dual_cdc_rate/README.ja.md)で合計は増えないと分かっている)
- vendor bulk(`p4-hs-cdc-vs-bulk`)
- chunk sizeと転送総量([E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)で飽和を確認済み)
- `usbd` task自身の優先度やcoreを変えること。**coreのcodeを書き換えない範囲で測る**
- PARLIOとの同時動作

## 必要な環境

[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)と同じ。

## ベンチ種別

**一時・配線なし**。

## 記録する数値

| 項目 | 列 |
|---|---|
| 条件 | `mode`、`priority`、`core`、`bytes`、`chunk` |
| device側 | `elapsed_us`、`written`、`short`、実行中のcore(`xPortGetCoreID()`) |
| host側 | 受信byte数、経過秒、MB/s、pattern不一致位置 |
| 導出 | modeごとのmin / median / max、mode 0に対する比 |

## 完了条件

1. **modeごとの帯域が表で言える**
2. **「contextで変わるか」にyes/noで答えられ、変わるなら効いている軸(優先度 / core / pin)が言える**
3. **pattern検証が通った条件だけを表に入れている**

## 影響

- [E065](../e065_p4_usb_hs_dual_cdc_rate/README.ja.md)の事実5を確定または取り下げ
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段のstreaming上限。**「2chで数十Msps」が連続streamingで出せるかの判定に直結する**
- [harness-channels](../../references/harness-channels.ja.md) §物理IFの帯域表

---

## 結果

状態: **完了 — contextで変わる。ただし効くのは優先度ではなく**pin先core**。core 0で7.4〜8.1 MB/s、core 1で5.2〜5.7 MB/s**(2026-09-12)

run: `_runs/E066_20260912T030*`。1実行で18条件(6 mode × 3)。

### mode別

host側実効MB/s。`ran_on`は`xPortGetCoreID()`が実際に返したcore。

| mode | 優先度 | 指定core | ran_on | min | **median** | max | device median | mode 0比 |
|---|---:|---|---:|---:|---:|---:|---:|---:|
| `loop` | 1 | `ARDUINO_RUNNING_CORE` | 1 | 5.58 | **5.60** | 5.74 | 5.60 | 1.000 |
| `task_p1_c0` | 1 | 0 | 0 | 7.65 | **7.96** | 7.98 | 7.97 | **1.420** |
| `task_p5_c0` | 5 | 0 | 0 | 7.43 | **7.64** | 7.77 | 7.66 | 1.364 |
| `task_p5_c1` | 5 | 1 | 1 | 5.24 | **5.27** | 5.47 | 5.28 | 0.940 |
| `task_p20_c0` | 20 | 0 | 0 | 8.07 | **8.08** | 8.11 | 8.09 | **1.442** |
| `task_p5_free` | 5 | pinしない | 0 | 7.62 | **7.67** | 7.77 | 7.68 | 1.370 |

### 実際に走ったcoreでまとめ直すと

| ran_on | 条件数 | median MB/s | 範囲 |
|---:|---:|---:|---|
| **core 0** | 12 | **7.77** | 7.43〜8.11 |
| **core 1** | 6 | **5.52** | 5.24〜5.74 |

**core 0の最小(7.43)がcore 1の最大(5.74)を上回り、重なりが無い。** 一方、core 0の中での優先度1 / 5 / 20は7.96 / 7.64 / 8.08で**範囲が重なり、単調な傾向が無い**。

### 健全性

pattern検証6回で不一致0、短write 0、stall 0、受信byte数は全18回で要求と一致。

### 事実

1. **帯域は送出contextで変わる。最大と最小の比は1.53倍(8.08 対 5.27 MB/s)。** 反証条件1は否定された。
2. **効いているのはpin先coreである。** core 0で走った12条件はすべて7.43〜8.11 MB/s、core 1で走った6条件はすべて5.24〜5.74 MB/sで、**2群は重ならない**。
3. **優先度は効かない。** core 0の中で優先度を1 → 5 → 20と20倍にしても7.96 / 7.64 / 8.08 MB/sで、3回ずつの範囲が互いに重なる。**[E065](../e065_p4_usb_hs_dual_cdc_rate/README.ja.md)の仮説「効くのは優先度」は誤りだった。**
4. **`loop()`が遅いのはcore 1で走るからである。** `ARDUINO_RUNNING_CORE`は1で、`loop` modeの5.60 MB/sは**同じcore 1に置いた専用task(`task_p5_c1`、5.27 MB/s)とほぼ同じ**。context の種類(loopかtaskか)ではなくcoreが説明する。
5. **[E064](../e064_p4_usb_hs_cdc_rate/README.ja.md)の5.59 MB/sを`loop` modeが5.60 MB/sで再現した。** E064とE065の差(+42%)は**core placementで説明され、2本目のCDCの有無ではない**。[E065](../e065_p4_usb_hs_dual_cdc_rate/README.ja.md)の事実5は**方向としては正しく、原因の帰属が誤っていた**。
6. **pinしない場合は3回とも core 0で走った。** schedulerは空いている側を選んでおり、結果もcore 0群に入る(7.67 MB/s)。
7. 最速条件でも8.08 MB/s ÷ 512 B = 約15,800 transaction/s = **1 microframeあたり約1.97 transaction**。HSが許す13にはなお遠い。**coreを変えても天井の性質は変わらない。**

### 候補

- **USBへ流すtaskは`loop()`と別のcoreへpinする**(採用)。`ARDUINO_RUNNING_CORE`が1なので**core 0へ置く**。優先度は既定(1でも5でもよい)のままでよい
- [E061](../e061_p4_drain_core_split/README.ja.md)が「回収を別coreへ移すとdrainが上がる」と言ったのと**同じ形の効果**が、USB送出でも出た

### 未決

- **core 1が遅い理由** `—`。`USB.begin()`は`setup()`(= `loopTask`、core 1)から呼ばれるので、`esp_intr_alloc`によりUSBの割り込みがcore 1に登録されているはずである。**送出taskが同じcoreで割り込みと競合している**という読みは構造から自然だが、この実験は測っていない。`USB.begin()`をcore 0のtaskから呼んで逆転するかを見れば切り分く(`p4-usb-isr-core`)
- **天井1.97 transaction/microframeを超える手段** `—`。coreを変えても比率は約2のままで、FIFOの深さ(512 B)が効いている可能性が残る。Arduinoのままでは変えられない
- **PARLIO captureと同時に走らせたときのcore配分** `—`。captureの回収も別coreを欲しがる([E061](../e061_p4_drain_core_split/README.ja.md))ので、**captureとUSB送出でcoreを取り合う**。これは`p4-usb-vs-drain`の中心の問いになる
- vendor bulkでの同条件 `—`

### 近直の目標への含み

**最速条件8.08 MB/sは、2 channelのPARLIO(1 byteに4 sample)なら約32.3 Msps相当の連続streaming。** 「2chで数十Msps」は連続streamingで届く。ただしその条件は「送出taskをcore 0へ置く」であり、**PARLIOの回収も別coreを欲しがる**ので、両立するかは未測定である。

### 反映

- [LEDGER](../LEDGER.ja.md) §1にE066を採番、§3に記録を追加
- [E065](../e065_p4_usb_hs_dual_cdc_rate/README.ja.md)の事実5の帰属を訂正(優先度ではなくcore)
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段のstreaming上限
- 仕様([../../protocols/](../../protocols/))のstatusは動かさない
