# E078 2 channel を継ぎ目なく連続で降ろせる rate の上限

状態: **計画 — EspUsbDevice の release 待ち**(2026-09-13 更新)

> **止まっている理由**: console は復旧した(挿し直し済み)が、**この実験は [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) の新しい版で回したい**。[CR-4 / CR-7 / CR-9](../../references/espusbdevice-change-requests.ja.md) が入って **降ろす側が 8.80 → 約 21 MB/s** になり、**`waitWritable()` で spin を潰せる**ようになったため、**旧版で測っても釣り合い点がすぐ古くなる**。release が出た版でピンして回す。
>
> なお board 1 の sketch は先方の検証 firmware で上書きされている。**焼き直しが要る。**

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
