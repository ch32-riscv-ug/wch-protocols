# ESP32-P4 probe / ロジアナ — 現在地と次にやること

状態: **計画**(2026-09-13。[E063](../experiments/e063_p4_usb_hs_enumerate/README.ja.md)〜[E090](../experiments/e090_p4_dwc2_double_buffer/README.ja.md) を踏まえた棚卸し)

**何が終わっていて、何が何待ちなのか**を 1 枚にする。個々の結論は各実験と[まとめ](p4-usb-hs-summary.ja.md)にある。

## 1. 近い目標 4 つの現在地

| | 目標 | 状態 |
|---|---|---|
| 1 | **RVSWD で CH32 に焼く** | **未着手。** 4 つで唯一まったく進んでいない。**CH32 を P4 に配線するところから** |
| 2 | **ロジアナ(2ch 数十 Msps、`.sr` 保存)** | **達成、かつ超過。** batch 160 Msps / 継ぎ目なし 96 Msps、8ch でも 20〜23 Msps、`.sr` を sigrok が読み戻す |
| 3 | **packet capture** | **未定義。** 何を指すか(USB 解析か、CH32 のプロトコルフレームか)が決まっていない |
| 4 | **PulseView へ IP 経由** | **達成。** stock の sigrok / PulseView が driver 追加なしで、継ぎ目なく取れる |

**目標 2 の副産物として FX2 ロジアナの置き換えが射程に入った** — 8ch 20 Msps 常用・23 Msps 上限で、FX2 の実用域(16 Msps 程度)を覆う([E086](../experiments/e086_p4_8ch_stream/README.ja.md))。

## 2. いま止まっているもの — 何待ちか

| やること | 待っているもの | 備考 |
|---|---|---|
| **[E090](../experiments/e090_p4_dwc2_double_buffer/README.ja.md)** DWC2 の TX FIFO を 2 packet に | **リグの空き** | [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) 側が host 役を使用中(board 消失で挿し直し待ち)。**ビルド済み、焼いて回すだけ** |
| **RVSWD** | **CH32 の配線** | 挿す先は確定済み([ピンの当たりを付ける](pin-discovery.ja.md))。**電源と GND だけ人が合わせれば、あとは探索で当てられる**設計まで書いてある |
| **packet capture** | **定義** | 下記 §5 |
| **[HR-3](espusbhost-change-requests.ja.md)**(HID 1,024 B) | **持ち主の判断** | keyboard / mouse / CCID と共有の経路なので、帯域のためだけに触る話ではない。**先方から提示済み** |
| EspUsbHost の release | 持ち主の判断 | HR-2 / HR-1 は working tree |
| **未 push の 61 commit** | 持ち主の判断 | 指示どおり push していない |

## 3. リグが空いたらすぐ回せるもの(配線変更なし)

**順序は「安くて決定的」な順**。

| | 実験 | 何が分かるか | 所要 |
|---|---|---|---|
| 1 | **[E090](../experiments/e090_p4_dwc2_double_buffer/README.ja.md)** | **24 MB/s の天井が TinyUSB の FIFO 割り当て既定か、DMA の詰め直しか。** 動けば [CR-10](espusbdevice-change-requests.ja.md) を実測付きで出せる | 10 分 |
| 2 | **depth 8 以上**([E089](../experiments/e089_p4_host_in_queue/README.ja.md) の未決) | depth 4 で頭打ちに見えるのが本当か | 5 分 |
| 3 | **16 KiB で遅くなる理由**([E088](../experiments/e088_p4_usb_ceiling_idle/README.ja.md)) | FIFO 32 KiB 側の問題か転送長そのものか。**FIFO と転送長を独立に振る** | 20 分 |
| 4 | **97 / 99 MHz だけ滞る現象**([E084](../experiments/e084_p4_transfer_tuning/README.ja.md)) | 未特定の観測。実用上は 96 以下で避けられる | 30 分 |
| 5 | **CDC / HID でも同じ天井か**([E088](../experiments/e088_p4_usb_ceiling_idle/README.ja.md)) | 24 MB/s が bulk 固有か、device 側全体の性質か | 30 分 |

**1 と 2 は E090 の firmware でそのまま回せる。** 3 以降は build_opt.h を振るだけ。

## 4. 配線が要るもの

| | やること | 要る配線 |
|---|---|---|
| **本命** | **RVSWD で CH32 に焼く** | **CH32 を P4 のヘッダへ。** 電源 / GND 以外は適当でよい |
| | 外部信号での capture 検証 | 信号源を外から。いまは内部 LEDC のみ |
| | 16 channel の連続 streaming | 16 本を信号源へ。**LEDC は 8 channel しかない**ので duty は重複する |
| | HID 1,024 B([HR-3](espusbhost-change-requests.ja.md) 待ち) | HS 直結のまま |

### RVSWD の段取り(設計済み、未実装)

[ピンの当たりを付ける](pin-discovery.ja.md) にある。

1. **電源と GND だけ人が合わせる**
2. **RVSWD の 2 本を総当たりで探す** — 33 本から順序付き 2 本で 1,056 通り。**成功すると chip ID が返る**ので、当たれば 2 本と向きが同時に確定
3. CH32 に探索 firmware を焼く
4. **残りを符号で一括** — 33 本でも 6 slot
5. **中間電圧(1.47 V)で裏取り** — 「P4 の、駆動していないピンに繋がっている」ことを確定させ、短絡も検出

**2 が未実装。** RVSWD の波形自体は [E007](../experiments/e007_wire_rvswd_frame/README.ja.md) と [link-to-target](../protocols/link-to-target.ja.md) にある。

## 5. 決めてほしいこと

| | 内容 |
|---|---|
| **packet capture の定義** | **USB バスの解析**(USBPcap 相当を P4 で)なのか、**CH32 のプロトコルフレーム**(RVSWD / SWIO の復号)なのか。**作るものが全く違う** |
| [HR-3](espusbhost-change-requests.ja.md) をやるか | HID 1,024 B で 8.2 MB/s 見込み。**ただし WinUSB が当たるようになった**ので、「driver レス」という HID の利点は以前より薄い |
| push するか | main に 61 commit |

## 6. 記録として残す方針

- **実験レポートは追記のみ。** 訂正は本文を書き換えず、訂正節を足す([§3.3](../experiments/README.ja.md))
- **人の測定と自分の測定を混ぜない。** 引用には出典と条件を付ける([E089](../experiments/e089_p4_host_in_queue/README.ja.md) の単位と device 条件がその例)
- **n=3 で分散を語らない**([E084](../experiments/e084_p4_transfer_tuning/README.ja.md) で踏んだ)
- **データシートを読む前に全ピンを舐めない**([E087](../experiments/e087_p4_pin_survey/README.ja.md) で踏んだ)
- **書き込み(chip reset)の前に HS device を detach する。** 失敗した reset を繰り返し叩かない

## 参照

- [P4 USB HS まとめ](p4-usb-hs-summary.ja.md) — 帯域の全体像
- [sample rate の選び方](p4-sample-rate-selection.ja.md) — ロジアナとして何を出せるか
- [ピンの当たりを付ける](pin-discovery.ja.md) — 配線と探索
- [改修の着手順](usb-library-change-plan.ja.md) — ライブラリ 2 つとの関係
- [LEDGER](../experiments/LEDGER.ja.md) — 実験の番号順索引
