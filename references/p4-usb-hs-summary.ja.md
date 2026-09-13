# ESP32-P4 の USB 2.0 HS と 2 channel capture — 到達点まとめ

状態: **まとめ**(2026-09-13。[E063](../experiments/e063_p4_usb_hs_enumerate/README.ja.md)〜[E085](../experiments/e085_p4_transfer_size_model/README.ja.md) の結論を 1 枚にした索引)

**§0 が現在の値。§1 以降は 2.2.0 時点の記録**で、数字はそのまま残してある(どこから何が変わったかが追えるように)。

各実験の全文は `experiments/e0xx_*/README.ja.md`、番号順の索引は [LEDGER](../experiments/LEDGER.ja.md)。

## ベンチ

ESP32-P4 rev 1.3 が 2 枚(`esp32-p4-30eda0e31478` / `...f5`、flash 16 MiB、**PSRAM 32 MiB**)。両者とも USB-Serial-JTAG を console 兼書込み口として WSL へ usbipd で繋ぎ、OTG HS port は用途に応じて **Windows / usbipd 経由の WSL / 2 枚直結**を切り替える。

**過去の P4 実験(E014〜E061)は別個体 `esp32-p4-e8f60ae0aa24`**(flash 32 MiB)で、値をそのまま引き継がない。

**ライブラリは [EspUsbDevice 2.3.0](https://github.com/tanakamasayuki/EspUsbDevice)**([CR-1〜CR-9](espusbdevice-change-requests.ja.md) を含む)。**公開版の `src` は測定に使っていた working tree と byte 単位で同一**(`diff -rq` で差分なし)なので、**ここの数値はすべて 2.3.0 の値として読んでよい**。

## 0. いま出る値(2026-09-13)

| 経路 | **実測** | driver | 出典 |
|---|---:|---|---|
| **vendor bulk**(TX FIFO 8 KiB / 1 転送 8 KiB) | **約 24 MB/s** | **WinUSB(当たる)** | [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) |
| vendor bulk(FIFO 4 KiB / 転送 4 KiB = P4 の既定) | 21.99 MB/s | 同上 | 同上 |
| vendor bulk(**usbip なし、Windows 直**) | **21.2 MB/s**(旧既定での測定) | WinUSB | [E081](../experiments/e081_p4_winusb_bind/README.ja.md) |
| **HID(512 B endpoint)** | **4.03 MB/s** | **不要** | [E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) / CR-8 |
| USB-Serial-JTAG(FS CDC) | 0.72〜0.80 MB/s | 不要 | [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) |
| (参考)**P4 が host 役で送信**、async queue depth 2 | **38.2 MB/s** | — | EspUsbHost `docs/usb-host-advanced.md` |
| (参考)HS bulk の理論上限 | 53.2 MB/s | — | 13 transaction × 512 B × 8,000/s |

**連続 streaming の上限は channel 数ではなく byte rate(23〜24 MB/s)で決まる** — **8ch 23 / 4ch 46 / 2ch 96 Msps**([E086](../experiments/e086_p4_8ch_stream/README.ja.md))。**2 channel は 96 Msps**([E084](../experiments/e084_p4_transfer_tuning/README.ja.md) 追測、64 MiB × 4 回 clean)。既定(4 KiB)で 86、32 KiB で 90 なので、**FIFO は大きいほど良いわけではない**。**batch なら 160 Msps**([E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md))。

> **97 / 99 MHz だけ突発的に滞る**という未特定の観測がある([E084](../experiments/e084_p4_transfer_tuning/README.ja.md))。**96 Msps 以下を使えば避けられる。**

### 何が変わったか(2.2.0 の既定 → 現在)

| | 2.2.0 の既定 | **現在** | 出典 |
|---|---:|---:|---|
| vendor bulk | 8.80〜10.74 MB/s | **23.97 MB/s** | [CR-4 / CR-7](espusbdevice-change-requests.ja.md) |
| **Windows で WinUSB** | **当たらない**(Code 28) | **当たる** | [CR-1](espusbdevice-change-requests.ja.md) / [E081](../experiments/e081_p4_winusb_bind/README.ja.md) |
| 送出の CPU | 4 MiB あたり 2.5〜7 万回の spin | **`stalls` 0、1 転送 1 block** | [CR-9](espusbdevice-change-requests.ja.md) / [E078](../experiments/e078_p4_continuous_stream/README.ja.md) |
| 4 MiB の download | 5.8 秒(console)→ 0.48 秒 | **約 0.18 秒** | [E076](../experiments/e076_p4_capture_hs_download/README.ja.md) → [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) |

**効いたのは in-flight 数ではなく 1 転送あたりの packet 数**(`CFG_TUD_VENDOR_TX_EPSIZE`、旧既定は bulk 1 packet)。512 byte ごとに「完了割り込み → event queue → usbd task → 再 arm」の往復が入っていた。

### 天井の内訳 — host 役の 38.2 MB/s との差

転送長 2 点から `period(S) = S/R + T` を解くと、**こちらの測定とライブラリ側の独立した測定が同じ答え**を出す。

| | **R**(線上の漸近 rate) | **T**(1 転送の死に時間) | 出典 |
|---|---:|---:|---|
| **4 点回帰(残差 1.0%)** | **24.64 MB/s** | **21.67 us** | **[E085](../experiments/e085_p4_transfer_size_model/README.ja.md)** |
| (旧)2 点外挿 | 26.34 MB/s | 30.8 us | [E084](../experiments/e084_p4_transfer_tuning/README.ja.md)。`R` を 7%、`T` を 42% 過大に見ていた |

- **死に時間を完全に消しても 24.6 MB/s** で、**38.2 には届かない**([CR-7](espusbdevice-change-requests.ja.md) を入れても説明できない)。転送長を倍にするたび利得は半減し、16384 → ∞ で +3.5%
- **`R` は microframe あたり 6.02 transaction**(HS は 13、host 役は 9.33)。**device 役はバスの半分以下しか使えていない**
- **capture を止めても天井は同じ**([E088](../experiments/e088_p4_usb_ceiling_idle/README.ja.md): idle 23.88 対 capture 同時 23.36 MB/s、差はばらつきの内側)。**capture 負荷では 38.2 との差を説明できない**
- **`S/R + T` は 8 KiB までの近似。** 16 KiB は予測より 1.2〜1.6 MB/s 遅く、**idle では 8 KiB より遅い**。**`R` を漸近線として引用しない**
- capture 負荷で排出が 3% ほど動く(96 Msps で 23.9、110 Msps で 23.1)ので、**釣り合い点は「届いた値が生成値に追いつく最大 rate」で挟む**
- **比較が対称ではない** — 38.2 は **P4 が host として *送信* した値**で、**host は自分でバスを組めるが device は IN token を待つ**
- **2026-09-13、[E089](../experiments/e089_p4_host_in_queue/README.ja.md) で決着した** — [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) に転送長(HR-2)と queue depth(HR-1)が入り、**P4 を host にすると 24.45 MB/s**。**PC の 23.88 MB/s と同水準**で、**別々の host controller 2 つが同じ天井で止まる**。→ **約 24 MB/s は device 側の限界。PC の controller 説は否定された**
- **残るのは「なぜ microframe あたり 6 transaction で止まるのか」**だけで、**それは device 側(DWC2 / TinyUSB)の構造**である

## 1. USB 2.0 HS で何が出るか(**2.2.0 時点の記録**。現在の値は §0)

| 経路 | **実測** | driver | 出典 |
|---|---:|---|---|
| **vendor bulk**(TX FIFO 8 KiB、host は 1 MiB URB) | **10.74 MB/s** | WinUSB | [E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) |
| vendor bulk(TX FIFO 512 B = 既定) | 9.0 MB/s | WinUSB | [E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) / [E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md) |
| **CDC ×1** | **8.08 MB/s** | 不要 | [E066](../experiments/e066_p4_usb_hs_tx_context/README.ja.md) |
| vendor bulk(host = P4、継続 IN 512 B) | 5.6 MB/s | — | [E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) |
| **HID(512 B endpoint)** | **4.14 MB/s** | **不要** | [E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) |
| HID(64 B = ライブラリ既定) | 0.52 MB/s | 不要 | 同上 |
| USB-Serial-JTAG(FS CDC) | 0.72〜0.80 MB/s | 不要 | [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) |
| (参考)**P4 が host 役**、async queue depth 2 | **38.2 MB/s** | — | EspUsbHost `docs/usb-host-advanced.md` |
| (参考)HS bulk の理論上限 | 53 MB/s | — | 13 transaction × 512 B × 8,000/s |

### 効くもの / 効かないもの

| 効く | どれだけ | 出典 |
|---|---|---|
| **送出 task をどの core に置くか** | **1.53 倍**(core 0 で 7.4〜8.1、core 1 で 5.2〜5.7 MB/s)。`ARDUINO_RUNNING_CORE`=1 なので `loop()` の反対側へ | [E066](../experiments/e066_p4_usb_hs_tx_context/README.ja.md) |
| **host の 1 URB の大きさ** | **2.7 倍**(4 KiB で 3.56、1 MiB で 9.73 MB/s) | [E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) |
| **送信 FIFO の深さ** | **+17%**(512 B → 8 KiB)。**8 KiB で飽和、64 KiB は mount しない** | [E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) |
| **HID の packet size** | **比例**(packet × 8,000/s)。64 → 512 B で 8 倍 | [E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) |

| 効かない | 出典 |
|---|---|
| **送出 task の優先度**(1 / 5 / 20 で差なし) | [E066](../experiments/e066_p4_usb_hs_tx_context/README.ja.md) |
| **CDC を 2 本にする**(合計は比 0.943 で**むしろ下がる**) | [E065](../experiments/e065_p4_usb_hs_dual_cdc_rate/README.ja.md) |
| **PC を経路から外す**(直結の方が遅い。host の読み単位が効く) | [E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) |

### まだ天井に届いていない

**同じ chip が host 役では 38.2 MB/s 出る**のに、device 役は 10.74 MB/s。FIFO の深さで説明できるのは 17% だけで、**残りは「endpoint ごとに転送を 1 つしか投げていない」構造**と見ている(host 側は async queue depth 2 で張り付く)。→ [CR-7](espusbdevice-change-requests.ja.md) / [HR-1](espusbhost-change-requests.ja.md)

> **2026-09-13: この見立ては半分外れた。** 転送長を変えると device 役は **23.97 MB/s** まで伸びたが([E084](../experiments/e084_p4_transfer_tuning/README.ja.md))、内訳を取ると **線上の漸近 rate が 26.3 MB/s**(= microframe あたり 6.4 transaction、HS は 13)で、**1 転送あたりの死に時間 30.8 us を完全に消しても 38.2 には届かない**。しかも **38.2 は P4 が *host として送信* した値**で、**host は自分でバスを組めるが device は IN token を待つ**という非対称がある。**device 役の天井が device 側にあるのか PC の host controller 側なのかは、[HR-1](espusbhost-change-requests.ja.md) が入るまで言えない。**

## 2. 落ちる経路 — CDC は転送途中で packet を捨てる

**4 MiB の転送 30 回中 4 回(13%)で、転送の途中から 2,048〜2,560 B(512 B の整数倍)が消える**([E068](../experiments/e068_p4_hs_cdc_tail_loss/README.ja.md))。

- 受信 stream は `正しい前半 + gap + 正しい後半`。byte 数だけ数えると「末尾が足りない」に見える
- **待っても突いても戻らない。data は失われている**
- **device は気づかない** — `USBCDC::write()` は全 byte を返し、短 write も報告しない
- **vendor bulk では 2 つの stack 合わせて 33 転送で 0 件**([E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) / [E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md))

**結論: 帯域が要る経路で CDC を使わない。** 使うなら**長さと CRC を付けて host が検証し再送要求できるようにする**。

## 3. Windows で WinUSB が当たらない → **解決した**

> **原因は MS OS 2.0 descriptor set の subset 構造**だった。**単一 interface の device では compatible ID を set header の直下(flat)に置く。** [E081](../experiments/e081_p4_winusb_bind/README.ja.md) が同じ board・同じ firmware で layout flag だけ変えて対照実験し、**flat = `Status OK` / `Service WinUSB`、subsets = `CM_PROB_FAILED_INSTALL`**。**この台に残っていた失敗判定は無関係**で、**新しい serial を使えば当たる**。以下は解決前の記録。

**device 側は正しい**(MS OS 2.0 request に 178 byte + `WINUSB` を返す)のに、**Windows が driver を当てない**(Code 28)。詳細と残る仮説は[調査記録](windows-winusb-binding.ja.md)。

**当面は usbip で WSL へ引き込み libusb で叩く**(実測はすべてこの経路)。**なお usbip の取り分は測れる大きさではなかった**([E081](../experiments/e081_p4_winusb_bind/README.ja.md): native 21.2 対 usbip 21.97)。

副産物として **[E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md) に効く観測**が取れた — **`bcdDevice` を変えても Windows の device instance は変わらず、serial を変えると変わる**。しかも**失敗した driver 判定は instance に貼り付いて再判定されない**(`ConfigFlags=0x40`)。

## 4. USB stack はどちらを使うか

**[EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) を使う**([E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md))。

| | core 内蔵 | **EspUsbDevice 2.2.0** |
|---|---:|---:|
| 帯域(既定同士、clean build) | 8.96 MB/s | **9.03 MB/s**(互角) |
| ばらつき | **8.68–9.11** | 6.79–10.02 |
| **DEVICE_QUALIFIER / OTHER_SPEED_CONFIG** | **どちらも STALL** | **答える** |
| flash | 392 KB | **374 KB** |
| **FIFO を深くできるか** | **不可**(precompiled libs に焼かれている) | **可**(10.59 MB/s まで) |

## 5. 2 channel capture は目標を超えた

| 項目 | 結果 | 出典 |
|---|---|---|
| **2ch の sample rate** | **160 Msps(内部 clock 源の上限)でsample精度** | [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) |
| **深さ** | **16,777,216 sample**(104.9 ms @ 160 Msps)、欠落 0 | 同上 |
| **`.sr` 出力** | **sigrok が読み戻す**(channel 数・rate・sample 数・波形一致) | 同上 |
| 1 / 4 channel | **160 Msps で sample 精度** | [E075](../experiments/e075_p4_width_sample_accuracy/README.ja.md) |
| 8 channel | **96 MHz(96 MB/s)まで 1 MiB で sample 精度**。160 MHz は burst 窓のぶんだけ | 同上 |
| capture と USB の同時実行 | **capture は一切落ちない**。落ちるのは USB 側で 7〜16% | [E067](../experiments/e067_p4_usb_vs_capture_core/README.ja.md) |

**判定は duty ではなく立ち上がり edge の間隔**で行う — 周期的な信号源では sample が落ちても duty は変わらない([E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md))。

**`.sr` は 1 sample = 1 byte** なので 2 channel では 4 倍に膨らむ。**線の上は packed のまま運び、host で展開する**。

### download は OTG HS で 12 倍になる — 実測済み

| download 経路 | 4 MiB の所要 | 出典 |
|---|---:|---|
| console(FS CDC、0.72 MB/s) | **5.8 秒** | [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) |
| **OTG HS vendor bulk**(既定 FIFO 512 B) | **0.48 秒**(0.42〜0.58、7回) | **[E076](../experiments/e076_p4_capture_hs_download/README.ja.md)** |
| (見込み)TX FIFO 8 KiB なら | 0.39 秒 | [E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) の 10.74 MB/s から |

**capture → download → `.sr` が通しで成立している**(7回すべて sample 精度、`sigrok-cli` が読み戻す)。**送出元が PSRAM であることの不利はない** — internal RAM を送出元にした対照は 8.38 対 9.04 MB/s で、**PSRAM の方がわずかに速い**([E076](../experiments/e076_p4_capture_hs_download/README.ja.md))。

**ただし同一条件で 1.65 倍ばらつく**(6.6〜10.9 MB/s)。**1回の測定で帯域を語らない。**

## 6. ベンチ運用の注意(実測で刺さったもの)

- **sketch 固有のビルドオプションは `build_opt.h` に置く。** `--build-property 'build.extra_flags=...'` は platform が組み立てる変数を潰し、**`-DBOARD_HAS_PSRAM` と `-DARDUINO_USB_*` が消える**。しかも arduino-cli が `core.a` を cache するので**後から直しても症状が残る**。変更時は `--clean`
- **書き込みの前に usbipd で detach する。** attach したまま書き込む(= チップ reset)と**死んだ vhci entry が残り、serial 側まで巻き込んで落ちる**。`esptool` が `_update_rts_state` で `TimeoutError: [Errno 110]`、`dmesg` に `vhci_hcd: urb->status -104`、やがて read が返らなくなり、Windows 側では `デバイス記述子要求の失敗` として列挙される。**復旧は物理的な挿し直しだけ**
- **usbip は interrupt OUT の URB を配送しない。** control の SET_REPORT は通る(hidraw の `HIDIOCSFEATURE` で確認)。**HID の OUT 方向を usbip 越しに測ろうとすると無反応になる**
- **usbipd の `bind` は VID:PID と device instance に紐づく。** PID や serial を変えるたびに管理者権限の bind が要る。**usbip で測る実験は identity を固定する**
- **arduino-cli は symlink した library dir の `.cpp` を拾わない。** Library Manager 経由なら問題ない
- **usbip 経由の CDC console は 20 秒級の遅延が出ることがある。** console 待ちの timeout は 45 秒を見込む

## 7. 次にできること

**機材待ち**(両 board の console を挿し直せば動くもの):

| やること | 中身 |
|---|---|
| **97 / 99 MHz だけ滞る現象**の特定 | [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) の観測。96 以下を使えば実用上は避けられる |
| **capture を止めた状態での `R`** | [E085](../experiments/e085_p4_transfer_size_model/README.ja.md) は capture 同時のみ。**素の USB 上限がまだ分かっていない** |
| 8 MiB の弾性 FIFO を実際に使い切るまで回す | [E078](../experiments/e078_p4_continuous_stream/README.ja.md) の外挿(88 MHz で約 11 秒)の確認 |
| 4 / 8 channel での連続 streaming | [E078](../experiments/e078_p4_continuous_stream/README.ja.md) / [E083](../experiments/e083_p4_attach_order/README.ja.md) の未決 |

**ライブラリ待ち**:

| やること | 要るもの |
|---|---|
| **device 役の天井が device 側かを確定する** | [HR-1](espusbhost-change-requests.ja.md)(host の IN async queue)。**§0 の内訳で、これにしか答えられない問いになった** |
| **HID を 1,024 B に上げる**(8.2 MB/s 見込み) | [CR-8](espusbdevice-change-requests.ja.md)(済)+ [HR-3](espusbhost-change-requests.ja.md)(未依頼) |

**配線待ち**:

| やること | 要るもの |
|---|---|
| **RVSWD で CH32 に焼く** | **CH32 を P4 へ配線する**(未着手の最後の目標) |
| packet capture | 何を指すか(USB 解析 / CH32 のフレーム)の決め |

**改修の着手順**は[別紙](usb-library-change-plan.ja.md)。**device 側は全件対応済み**で、残るのは host 側([HR-1](espusbhost-change-requests.ja.md) / [HR-3](espusbhost-change-requests.ja.md))。
