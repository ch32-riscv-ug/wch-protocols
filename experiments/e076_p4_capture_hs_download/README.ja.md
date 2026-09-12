# E076 capture を OTG HS の vendor bulk で降ろす通し

状態: **計画**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E074](../e074_p4_2ch_capture_to_sr/README.ja.md)(capture と `.sr`)、[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)(vendor bulk 10.74 MB/s)、[E067](../e067_p4_usb_vs_capture_core/README.ja.md)(capture と USB の同居) / まとめ: [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §7

## 問い

**capture した data を OTG HS の vendor bulk で降ろすと、4 MiB の download は何秒になり、sample は落ちずに `.sr` まで通るか。**

## なぜこの問いか

[E074](../e074_p4_2ch_capture_to_sr/README.ja.md)は capture から `.sr` までを通したが、**download は console(FS CDC、0.72 MB/s)だった** — HS port が2枚目の board へ配線されていたため。**4 MiB に5.8秒**かかる。

[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)は同じ board の vendor bulk で **10.74 MB/s** を測っている。**この2つを同じ firmware の中で繋ぐと 0.39 秒**になるはず、というのが[まとめ §7](../../references/p4-usb-hs-summary.ja.md)の筆頭項目である。**配線が戻ったので通す。**

繋ぐこと自体に見るべき点がある。E071 は PSRAM を読まずに固定 pattern を送っていた。**今回の送出元は PSRAM 上の 4 MiB** で、[E067](../e067_p4_usb_vs_capture_core/README.ja.md)が「capture と USB を同居させると USB 側が7〜16%落ちる」と測っている経路を、**送出だけ**が使う形になる。

## 仮説

**通る。4 MiB が 0.4 秒前後(9〜11 MB/s)で降り、sample 精度は E074 と同じ(周期 1600 が min = mean = max)。**

## 反証条件

1. download が 5 MB/s を割る(PSRAM 読み出しが vendor bulk の律速になる)
2. byte が落ちる、または順序が崩れる(`.sr` の周期が一致しない)
3. capture 直後の download で device 側が stall し続ける、あるいは転送が完了しない

## 方法

[E074](../e074_p4_2ch_capture_to_sr/README.ja.md)の firmware の `run_dump()` だけを差し替える。**capture 経路は一切変えない。**

- **制御は console(USB-Serial-JTAG)、data は OTG HS vendor bulk**。2経路に分けるので、command の往復が download 時間に混ざらない
- 送出 task は **core 0 に pin**([E066](../e066_p4_usb_hs_tx_context/README.ja.md): core だけが効き、優先度は効かない。`ARDUINO_RUNNING_CORE`=1 の反対側)
- USB stack は **EspUsbDevice 2.2.0**([E070](../e070_p4_hs_vendor_stack_compare/README.ja.md))、TX FIFO は既定の 512 B
- identity は **`1209:0008` / serial `E069-A`** に固定する。usbipd の bind は VID:PID と device instance に紐づくので、変えると管理者権限の bind をやり直すことになる
- host は WSL 側で libusb、**1 MiB ごとの read**([E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md): URB の大きさが 2.7 倍効く)
- 判定は E074 と同じ**立ち上がり edge の間隔**。`min = mean = max = rate ÷ 100 kHz` なら欠落なし
- 3回以上繰り返す([§7-3](../README.ja.md))

### 記録する数値

- download の所要時間と MB/s(host 実測 / device 実測の両方)
- device 側の `stalls`(write が 0 を返した回数)
- capture の `overflow` / `timeout`
- channel ごとの周期 min / mean / max と duty
- 比較対象として **E074 の console 経路の同条件**(4 MiB)

## 対象外

- 連続 streaming(capture しながら降ろす。釣り合い点の測定は別実験)
- TX FIFO を深くした状態での再測([CR-4](../../references/espusbdevice-change-requests.ja.md))
- Windows 側から直接読む([WinUSB が当たらない](../../references/windows-winusb-binding.ja.md))
- PulseView から IP 経由で取る([連携メモ](../../references/pulseview-integration.ja.md) §2)
- trigger、外部信号、3 channel 以上

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、**OTG HS port を PC 側へ配線**、console と HS の両方を usbipd で WSL へ
- `EspUsbDevice (2.2.0)`(Library Manager)
- host: `uv run` の pyusb / pyserial

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**4 MiB の download を3回以上測り、MB/s と sample 精度を記録する。** console 経路(E074)との比を出す。

## 影響

- [P4 USB HS まとめ](../../references/p4-usb-hs-summary.ja.md) §5「残る律速は download だけ」の表 — **見積もりを実測に置き換える**
- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) — download 経路の実測値
