# ESP32-P4 の USB 2.0 HS と 2 channel capture — 到達点まとめ

状態: **まとめ**(2026-09-12。[E063](../experiments/e063_p4_usb_hs_enumerate/README.ja.md)〜[E076](../experiments/e076_p4_capture_hs_download/README.ja.md) の 14 実験の結論を 1 枚にした索引)

各実験の全文は `experiments/e0xx_*/README.ja.md`、番号順の索引は [LEDGER](../experiments/LEDGER.ja.md)。

## ベンチ

ESP32-P4 rev 1.3 が 2 枚(`esp32-p4-30eda0e31478` / `...f5`、flash 16 MiB、**PSRAM 32 MiB**)。両者とも USB-Serial-JTAG を console 兼書込み口として WSL へ usbipd で繋ぎ、OTG HS port は用途に応じて **Windows / usbipd 経由の WSL / 2 枚直結**を切り替える。

**過去の P4 実験(E014〜E061)は別個体 `esp32-p4-e8f60ae0aa24`**(flash 32 MiB)で、値をそのまま引き継がない。

## 1. USB 2.0 HS で何が出るか

| 経路 | **実測** | driver | 出典 |
|---|---:|---|---|
| **vendor bulk**(TX FIFO 8 KiB、host は 1 MiB URB) | **10.74 MB/s** | WinUSB | [E071](../experiments/e071_p4_hs_vendor_fifo_depth/README.ja.md) |
| vendor bulk(TX FIFO 512 B = 既定) | 9.0 MB/s | WinUSB | [E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) / [E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md) |
| **CDC ×1** | **8.08 MB/s** | 不要 | [E066](../experiments/e066_p4_usb_hs_tx_context/README.ja.md) |
| vendor bulk(host = P4、継続 IN 512 B) | 5.6 MB/s | — | [E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) |
| **HID(512 B endpoint)** | **4.14 MB/s** | **不要** | [E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) |
| HID(64 B = ライブラリ既定) | 0.52 MB/s | 不要 | 同上 |
| USB-Serial-JTAG(FS CDC) | 0.72〜0.80 MB/s | 不要 | [E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md) |
| (参考)**P4 が host 役**、async queue depth 2 | **36.4 MB/s** | — | EspUsbHost `docs/usb-host-advanced.md` |
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

**同じ chip が host 役では 36.4 MB/s 出る**のに、device 役は 10.74 MB/s。FIFO の深さで説明できるのは 17% だけで、**残りは「endpoint ごとに転送を 1 つしか投げていない」構造**と見ている(host 側は async queue depth 2 で張り付く)。→ [CR-7](espusbdevice-change-requests.ja.md) / [HR-1](espusbhost-change-requests.ja.md)

## 2. 落ちる経路 — CDC は転送途中で packet を捨てる

**4 MiB の転送 30 回中 4 回(13%)で、転送の途中から 2,048〜2,560 B(512 B の整数倍)が消える**([E068](../experiments/e068_p4_hs_cdc_tail_loss/README.ja.md))。

- 受信 stream は `正しい前半 + gap + 正しい後半`。byte 数だけ数えると「末尾が足りない」に見える
- **待っても突いても戻らない。data は失われている**
- **device は気づかない** — `USBCDC::write()` は全 byte を返し、短 write も報告しない
- **vendor bulk では 2 つの stack 合わせて 33 転送で 0 件**([E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) / [E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md))

**結論: 帯域が要る経路で CDC を使わない。** 使うなら**長さと CRC を付けて host が検証し再送要求できるようにする**。

## 3. Windows で WinUSB が当たらない

**device 側は正しい**(MS OS 2.0 request に 178 byte + `WINUSB` を返す)のに、**Windows が driver を当てない**(Code 28)。詳細と残る仮説は[調査記録](windows-winusb-binding.ja.md)。

**当面は usbip で WSL へ引き込み libusb で叩く**(実測はすべてこの経路)。

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
- **usbipd の `bind` は VID:PID と device instance に紐づく。** PID や serial を変えるたびに管理者権限の bind が要る。**usbip で測る実験は identity を固定する**
- **arduino-cli は symlink した library dir の `.cpp` を拾わない。** Library Manager 経由なら問題ない
- **usbip 経由の CDC console は 20 秒級の遅延が出ることがある。** console 待ちの timeout は 45 秒を見込む

## 7. 次にできること

| やること | 要るもの |
|---|---|
| 連続 streaming(釣り合い点は vendor bulk の実測 8.80 MB/s で約 35 Msps) | — **いま測れる** |
| **帯域のばらつき(1.65 倍)の出どころ**を切り分ける | — core 内蔵 stack で [E076](../experiments/e076_p4_capture_hs_download/README.ja.md) の A/B を回す |
| **device 側の本当の天井**を測る | — **PC 側の async URB で先に切り分ける**([着手順](usb-library-change-plan.ja.md)) |
| **FIFO を深くした状態での再評価** | [CR-4](espusbdevice-change-requests.ja.md) |
| **HID を 1,024 B に上げる**(8.2 MB/s 見込み) | [CR-8](espusbdevice-change-requests.ja.md) + [HR-3](espusbhost-change-requests.ja.md) |
| **Windows で driverless**(WinUSB) | [CR-1](espusbdevice-change-requests.ja.md) / [CR-2](espusbdevice-change-requests.ja.md) |
| PulseView から IP 経由で取る | [連携メモ](pulseview-integration.ja.md) §2(BeagleLogic の TCP を演じる) |
| **RVSWD で CH32 に焼く** | 未着手 |

**library の改修に入る前後の段取りは[着手順の提案](usb-library-change-plan.ja.md)にまとめた**(device 側が先)。
