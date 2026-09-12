# E078 2 channel を継ぎ目なく連続で降ろせる rate の上限

状態: **完了 — 2 channel は 86 Msps まで継ぎ目なく降ろせる。88 Msps から backlog が時間に比例して積む**(2026-09-13)

## 前提の更新(2026-09-13、計画は書き換えず追記)

**降ろす側が 2.4 倍速くなったので、釣り合い点の見積りが動く。**

| | 計画時(2026-09-12) | **更新後** |
|---|---:|---:|
| vendor bulk の実測 | 8.80 MB/s(既定 FIFO 512 B) | **約 21 MB/s**(FIFO 4096 / 1 転送 4096、新既定) |
| 2 channel の釣り合い点(= MB/s ÷ 0.25) | 約 35 Msps | **約 84 Msps** |
| [E067](../e067_p4_usb_vs_capture_core/README.ja.md) の同居損(84〜93%)を当てると | 29〜33 Msps | **70〜78 Msps** |

**持続 spool 帯域(約 98 MB/s)に近づくので、律速が USB から capture 側へ移る可能性がある。** 掃引する rate を **16 / 32 / 48 / 64 / 80 / 96 MHz** へ広げる。

**送出ループも変える。** [CR-9](../../references/espusbdevice-change-requests.ja.md) の `waitWritable()` で spin を置き換える(旧: 4 MiB あたり 2.5〜7 万回の `taskYIELD()`)。**harvest task と CPU を取り合わなくなるはずで、それ自体が [CR-9](../../references/espusbdevice-change-requests.ja.md) の合格条件**でもある。

**host 側も同期 API から [E079](../e079_p4_host_urb_depth/README.ja.md) の async reader(depth 2 以上)へ変える** — depth 1 では 18.64、depth 2 で 22.68 MB/s と **+22%** 違うため、**host 側が律速だったという読み違いを避ける**。

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E076](../e076_p4_capture_hs_download/README.ja.md)(download 8.80 MB/s)、[E067](../e067_p4_usb_vs_capture_core/README.ja.md)(capture と USB の同居)、[E077](../e077_p4_pulseview_over_ip/README.ja.md)(batch を繋ぐと継ぎ目が出る)

## 問い

**PARLIO の capture を PSRAM に貯めずにそのまま OTG HS へ流したとき、欠落なく continuous に保てる sample rate の上限は何 Msps か。**

## なぜこの問いか

[E077](../e077_p4_pulseview_over_ip/README.ja.md)で PulseView から取れるようになったが、**1回の capture(4 M sample)を超える要求は別々の capture を繋ぐので継ぎ目に空白が入る**。継ぎ目を無くすには **capture しながら降ろす**しかない。

[E067](../e067_p4_usb_vs_capture_core/README.ja.md)は「capture と USB を同時に走らせても capture は落ちず、落ちるのは USB 側」まで測ったが、**その2つを繋いでいない** — capture した data をそのまま USB へ流してはいない。[まとめ §7](../../references/p4-usb-hs-summary.ja.md)の釣り合い点(約35 Msps)は**排出 8.80 MB/s ÷ 0.25 byte/sample という割り算で、実測ではない**。

## 仮説

**30〜35 Msps あたりで折れる。** 2 channel は 1 byte に 4 sample なので、rate × 0.25 が必要な排出 byte rate になる。[E076](../e076_p4_capture_hs_download/README.ja.md)の実測 8.80 MB/s(6.6〜10.9)をそのまま当てると **26〜43 Msps**、中央で 35 Msps。

**ただし下振れする**と見ている。[E067](../e067_p4_usb_vs_capture_core/README.ja.md)で capture と同居した USB は単独比 **84〜93%** に落ちるので、**7.4〜8.2 MB/s = 29〜33 Msps** が実際の線になるはず。

## 反証条件

1. 釣り合い点が 26 Msps を下回る(同居の損失が予想より大きい)
2. 釣り合い点が 43 Msps を上回る(降ろす側が予想より速い)
3. **rate を下げても継ぎ目なく保てない**(elastic buffer の設計が悪く、瞬間的な詰まりを吸収できない)
4. 落ちるときに `overflow` が 0 のまま(計数が捉えられない)

## 方法

E076 の firmware を**貯めてから送る**から**流しながら送る**へ変える。

```
PARLIO RX --(partial receive)--> 内部 ring 64 KiB --ISR--> queue
   --harvest task (core 1)--> PSRAM の弾性 FIFO 8 MiB
   --usb task (core 0)--> vendor bulk
```

- **弾性 FIFO** を PSRAM に 8 MiB 置く。単一 producer / 単一 consumer の ring。**瞬間的な USB の詰まりを吸収するのが役目**で、これが満杯になったら「追いつかなかった」と判定する
- **core の割り当ては[E067](../e067_p4_usb_vs_capture_core/README.ja.md)の最良配置**(harvest = core 1、USB = core 0)
- rate を振る: **16 / 24 / 32 / 36 / 40 / 48 MHz**(= 4 / 6 / 8 / 9 / 10 / 12 MB/s)
- 各 rate で **32 MiB 送り切る**(8 MB/s なら約4秒)。**弾性 FIFO の最大占有**を記録する
- host は bulk IN を読み続け、**受け取った byte 列をそのまま周期判定にかける**([E074](../e074_p4_2ch_capture_to_sr/README.ja.md)の方法)

### 判定

| 見るもの | 落ちていない条件 |
|---|---|
| ring `overflow` | 0 |
| 弾性 FIFO の `overflow` | 0 |
| 弾性 FIFO の最大占有 | **増え続けない**(定常なら一定の範囲に収まる) |
| 立ち上がり edge の間隔 | `min = mean = max = rate ÷ 100 kHz` |

**周期判定を通ることが本質**である。byte 数だけ合っていても、途中で捨てていれば周期が乱れる([E068](../e068_p4_hs_cdc_tail_loss/README.ja.md))。

### 記録する数値

- rate ごとに: 実測の排出 MB/s、ring overflow、FIFO overflow、FIFO 最大占有、周期 min / mean / max
- **落ちる rate と落ちない rate の境界**

## 対象外

- 3 channel 以上(2 channel が目標。幅は[E075](../e075_p4_width_sample_accuracy/README.ja.md))
- trigger、pre/post
- PulseView から continuous で引く(**本実験は device 側の上限を決めるだけ**)
- TX FIFO を深くした状態([CR-4](../../references/espusbdevice-change-requests.ja.md))
- 外部信号

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、HS port を usbipd で WSL へ
- `EspUsbDevice (2.2.0)`、host は `uv run` の pyusb / pyserial

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**落ちない最大 rate と落ちる最小 rate を、周期判定つきで挟む。** 各 rate 3回以上。

## 影響

- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) — 「連続 streaming の釣り合い点は約29.7 Msps」を**実測に置き換える**
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §7
- [E077](../e077_p4_pulseview_over_ip/README.ja.md) — 継ぎ目を無くせる rate が決まる

## 結果

ライブラリは [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) の working tree、**commit `7a6d9dc`**(`library.properties` は 2.2.0 のまま。release 前)。`build_opt.h` で **TX FIFO 4096 / 1 転送 4096**、送出は `waitWritable()`、host は URB を 4 本 in-flight。

### 16 MiB(約 0.78 秒)

| rate | MB/s | ring | fifo | 弾性 FIFO 最大占有 | stalls | 周期 |
|---:|---:|---:|---:|---:|---:|---|
| 16 MHz | 4.04 / 3.92 | 0 | 0 | 4 KB / 16 KB | 0 | 160/160 |
| 32 MHz | 7.99 / 8.04 | 0 | 0 | 16 KB | 0 | 320/320 |
| 48 MHz | 12.16 / 11.74 | 0 | 0 | 57 KB / 27 KB | 0 | 480/480 |
| 64 MHz | 15.88 / 16.00 | 0 | 0 | 33 KB / 45 KB | 0 | 640/640 |
| **80 MHz** | 20.12 / 20.13 / 20.13 | 0 | 0 | **61〜73 KB** | 0 | 800/800 |
| **84 MHz** | 21.21 / 20.40 / 20.77 | 0 | 0 | **61〜71 KB** | 0 | 840/840 |
| 88 MHz | 21.27 / 21.13 / 21.43 | 0 | 0 | **512〜698 KB** | 0 | 880/880 |
| 92 MHz | 21.66 / 21.58 / 21.64 | 0 | 0 | **1.14〜1.26 MB** | 0 | 920/920 |
| 96 MHz | 21.71 / 21.89 / 22.42 | 0 | 0 | **1.03〜1.25 MB** | 0 | 960/960 |

**64 MHz までは MB/s が rate ÷ 4 にぴったり一致する** — つまり律速は capture 側であり、USB は遊んでいる。

### 64 MiB(約 3.1 秒)— backlog が伸びるかどうか

**「追いつけているか」は帯域では分からない。** 88 MHz 以上でも 16 MiB は全部落とさずに届く(弾性 FIFO が吸っているだけ)。**分かれ目は「占有が時間に比例して伸びるか」**である。

| rate | 16 MiB での最大占有 | **64 MiB での最大占有** | 伸び | 判定 |
|---:|---:|---:|---:|---|
| **84 MHz** | 61〜71 KB | **110〜152 KB** | 約 2 倍(時間比 4 倍より小さい) | **保つ** |
| **86 MHz** | — | **108〜153 KB**(3 回) | — | **保つ** |
| 88 MHz | 512〜698 KB | **2.02〜2.20 MB** | **約 3.6 倍 ≒ 時間比** | **積む** |
| 92 MHz | 1.14〜1.26 MB | **4.47〜4.65 MB** | **約 3.8 倍 ≒ 時間比** | **積む** |

**88 MHz 以上は占有が duration に比例する。** 積む速さから外挿すると、8 MiB の弾性 FIFO を使い切るのは **88 MHz で約 11 秒、92 MHz で約 5 秒**である。

### sample は落ちていない

**全条件で `ring_overflow` = 0、`fifo_overflow` = 0、周期は head / tail とも完全一致。** さらに 92 MHz・16 MiB の 1 本を**全長 scan**(67,108,864 sample、72,944 周期)したところ、**期待値 920 と違う周期は 1 つだけ**で、それは **sample 0**(capture 開始時の PWM 位相)だった。

## 事実

1. **2 channel を継ぎ目なく連続で降ろせるのは 86 Msps まで**(= 線の上で 21.5 MB/s)。**88 Msps から backlog が時間に比例して積む。**
2. **釣り合い点の見積り(約 84 Msps)はほぼ当たった。** 降ろす側 21.5 MB/s ÷ 0.25 byte/sample から出した値で、実測は 86 Msps。
3. **capture と同居しても USB は落ちない。** 飽和条件での実測は **21.4〜22.4 MB/s** で、ライブラリ側が単体で測った新既定の **21.1 MB/s と同等かわずかに上**。[E067](../e067_p4_usb_vs_capture_core/README.ja.md) の「同居すると USB が 7〜16% 落ちる」は**再現しなかった**。
4. **落ちなくなった理由は [CR-9](../../references/espusbdevice-change-requests.ja.md) である。** `stalls` は**全条件で 0**、`waits` は転送数とほぼ一致する(16 MiB で 4,095、64 MiB で 16,383 = byte ÷ 4096)。**1 転送につき 1 回だけ block する**形になり、[E067](../e067_p4_usb_vs_capture_core/README.ja.md) で見えていた損は**競合ではなく spin だった**と考えてよい。
5. **64 Msps までは USB が遊んでいる。** MB/s が rate ÷ 4 に一致する。**この範囲なら capture 側に余力を回せる。**
6. **「帯域が出ている」ことは「追いつけている」ことを意味しない。** 96 MHz でも 16 MiB は完走し、周期も一致する。**弾性 FIFO の占有が duration に比例して伸びるかどうかだけが判定になる。**

### 方法の誤り(こちらの firmware 側。§7-6 に従い残す)

**最初の掃引で 88 MHz 以上に出ていた `ring_overflow`(8〜271)は、data の欠落ではなく teardown の数え間違いだった。**

`run_stream()` が **usb task の完了を先に待ち、その間 PARLIO を止めていなかった**ため、harvest が抜けたあとも ISR が**誰も読まない queue へ積み続け**、その失敗が全部 `ring_overflow` に入っていた。**backlog が大きい高 rate ほど drain に時間がかかるので、見かけ上「高 rate でだけ overflow する」**という、いかにも本物らしい形になっていた。

**harvest の完了を先に待って PARLIO を止め、それから USB を drain させる**順序に直したところ、**全条件で 0** になった。全長 scan で欠落が無いことも確認した(上記)。**`overflow` は信用してよい([E075](../e075_p4_width_sample_accuracy/README.ja.md))が、それは数え方が正しいときに限る。**

## 候補

- **2 channel の連続 streaming は 86 Msps を上限に置く。** 余裕を見るなら 84 Msps
- **batch なら 160 Msps(内部 clock 源の上限)まで**([E074](../e074_p4_2ch_capture_to_sr/README.ja.md))。**継ぎ目が許されるかどうかで上限が 2 倍違う**
- **送出は必ず `waitWritable()` で。** spin だと capture と同居したときに損をする
- **host 側は URB を 2 本以上 in-flight にする。** depth 1 では 18.64 対 22.68 MB/s([E079](../e079_p4_host_urb_depth/README.ja.md))

## 未決

- **8 MiB の弾性 FIFO を実際に使い切るまで回していない** `—`。88 MHz で約 11 秒、92 MHz で約 5 秒の外挿。firmware の `kStreamBytesMax` が 64 MiB(約 3 秒)で頭打ち
- **86 と 88 の間** `—`。1 MHz 刻みでは詰めていない
- **PulseView から continuous で引く** `—`。[E077](../e077_p4_pulseview_over_ip/README.ja.md) の batch 継ぎ目を、この経路で消せるはず
- **4 / 8 channel での連続 streaming** `—`。8 channel は 86 Msps 相当なら 86 MB/s で、持続 spool 帯域(約 98 MB/s)の内側だが USB が持たない

## 影響

- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) — 「連続 streaming の釣り合い点は約 29.7 Msps」を **86 Msps** に置き換える
- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) — capture 同居時の実測が入る
- [E067](../e067_p4_usb_vs_capture_core/README.ja.md) — **同居の損(7〜16%)は spin が原因だった**
- [EspUsbDevice CR-9](../../references/espusbdevice-change-requests.ja.md) — 合格条件を満たした(`stalls` 0、1 転送 1 block)

## 追記 — 送出を FIFO 容量ちょうどに揃えた場合(2026-09-13)

ライブラリ側の指摘で、**`write()` が「空いているぶん」しか受け取らないため半端な長さの転送が出る**ことが分かった。**packet size の倍数でない転送は短い packet で終わり、host の URB をそこで完了させる**。

送出ループを **`waitWritable(4096)` → `write(4096 ちょうど)`** に変えて測り直した(弾性 FIFO の大きさは 4096 の倍数なので、tail が常に整列し、巻き戻り位置でも半端が出ない)。

### 短い URB は狙いどおり消えた(16 MiB)

| rate | 短く返った URB(前: 1 KiB スライス) | **後: 容量ちょうど** |
|---:|---:|---:|
| 84 MHz | — | 183 / 296 |
| 88 MHz | — | **0** |
| 92 MHz | — | **0** |

**釣り合い点より上では 0 になる。** 下では残る — が、これは半端な転送ではなく **ZLP** である。**生産が追いついていない rate では FIFO が頻繁に空になり**、TinyUSB は「FIFO が空 かつ 直前の転送長が packet size の倍数」で ZLP を送る([CR-5](../../references/espusbdevice-change-requests.ja.md) の機序)ので、**4096 ちょうどを書くほど条件に当たる**。**host 側から見ると、これは律速ではない側の徴候である。**

### 釣り合い点は動かなかった(64 MiB)

| rate | 最大占有(前) | **最大占有(後)** | 判定 |
|---:|---:|---:|---|
| **86 MHz** | 108〜153 KB | **50〜75 KB** | **保つ** |
| 88 MHz | 2.02〜2.20 MB | **1.41〜1.61 MB** | 積む |
| 90 MHz | — | 3.07〜3.22 MB | 積む |

**88 MHz の積み方は約 3 割ゆるくなったが、依然として duration に比例する。** 超過分は 0.68 → 0.48 MB/s 相当。**上限は 86 Msps のままである。**

**二通りの送出実装で同じ境界が出た**ので、[事実 1](#事実) の 86 Msps は書き方に依存しない値と見てよい。

### この追記で分かったこと

7. **半端な長さの転送は送出側の書き方で消せる。** `waitWritable(容量)` → `write(容量)` にすれば、釣り合い点より上では短い URB が 0 になる。**帯域への効きは 1% 程度**(超過分 0.68 → 0.48 MB/s)で、**host 側の URB 再投入が減るぶんが主な利得**である
8. **短い URB の数は「追いついているか」の副次的な指標になる。** 釣り合い点より下では ZLP のぶん出続け、上では 0 になる。**占有の伸びと逆向きに効く**ので、2 つ揃えて見ると判定が早い
