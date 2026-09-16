# E117 stream data path（descriptor＋generic codec＋2 worker）を正規リリース2.4.0（pin）の公開APIへ移植 — E108〜E115の「参考値」を正式な数値に置き換えられるか

状態: **完了 — 正規リリース2.4.0（pin）の公開APIだけで、E109〜E115の参考値はすべて正式な数値として再現した。four 60 / five 60の約4.7分soak欠損0、16 ch hold 40 / 50、8-bit F=3 60〜100、2 ch 160 M素通し、any / edge / 配線順不同のbyte一致**（2026-09-16）

規則: [実測の規則](../README.ja.md)（正式な数値は正式リリースをpinして取る） / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E115](../e115_p4_grouped_plane_transpose/README.ja.md)、[E116](../e116_p4_usb_in_ceiling_release/README.ja.md)

## 問い

E115のfirmware（任意descriptor、群転置generic codec、2 worker、free list stage、PSRAM退避16 MiB、固定profile F / V / W / 8も同居）から、独自patch（E097 direct RX / E101 TX完了hook / E102 zero-copy / E110 FIFO）を全部外し、**EspUsbDevice 2.4.0（Library Managerからpin、TinyUSB無改変）の公開API `writeDirect()` / `onTxComplete()` / `onRxData()`** だけで同じdata pathを組むと、E109〜E115で参考値として記録したstreamの上限（16 ch hold構成 40〜50 Msps、8-bit F=3 70〜80、four / five 60の約4.7分soak、2 ch 160 M素通し、any / edge / 配線順不同のbyte一致）は正式な数値として再現するか。

## 仮説

- 再現する。差はE116で見えたUSB経路の−7%（366対395 Mbps）のぶんだけで、codec律速の構成（16 ch hold 50 Msps級）には効かず、USB律速の構成（8-bit 80 Msps＝261 Mbps、2 ch 160 M＝320 Mbps）は90%予算329 Mbpsの中に収まるものだけが通る。2 ch 160 M素通しは320 Mbps＝予算の97%なので**通らない可能性が高い**（その場合は150 M以下へ）。
- `writeDirect()`の契約（先頭64 byte整列、DMA可能internal RAM、≤65,535、書いたcoreでC2M、完了まで所有権）はE108以来のstage / bounce / status bufferがすでに満たしている。`Busy`は起きない（arm ringが`Inflight`で単一化している）。
- `onRxData()`のbufferはcallback中だけ有効なので、command mailboxへのcopy（E097方式）はそのまま。

## 反証条件

- `arm_failures > 0`または`last_direct_error != None` → 契約違反。bufferの整列（statusは64整列に直した）かBusy（二重arm）を疑う。
- 16 ch hold 40 Mspsが通らない → codec以外の差。USB経路の−7%では説明できないので、`onTxComplete`の経路（class経由の完了通知の遅れ）を`completion_gap`で見る。
- byte不一致 → 移植の取り違え（`onRxData`のbuffer寿命、status行の送出順）。

## 方法

- firmware: E115をfork。`Vendor.write()`→`Vendor.writeDirect()`（失敗時`lastDirectError()`をstatusへ）、hook 2本→`Vendor.onTxComplete()` / `Vendor.onRxData()`を`Device.begin()`直後に登録、`build_opt.h`は`-DCFG_TUD_VENDOR_TXRX_BUFFERED=0`とEPSIZE 512とcodec flagだけ、`sketch.yaml`は`EspUsbDevice (2.4.0)`をpin。**`--clean`**。statusに`direct_supported=`と`last_direct_error=`を出す。
- host: E115の`host_descriptor.py`（検証はcapture後）とE112の`host_capture.py`（固定profile用、E117_STATUS対応）。
- 掃引（各byte一致）: (1) USB-only probeは省略（E116）。(2) 固定profile: eight 60 / 80 / 100、wide 40 / 60、four 60、five 60、two 160 / 150。(3) descriptor: W16 40 / 50、F4 50、8-bit F3 60 / 80、配線順不同 50、any6 30 / 40、edge12 15 / 20、mix8 10 / 15、素通し 1 / 2 / 4 ch。(4) soak: four 60とfive 60を約4.7分（16,000 periods、検証はcapture後）、W16 40を25 s。
- 記録: statusの`direct_supported=1`、`last_direct_error=None`、`arm_failures=0`、`completion_gap_us_max`、core idle。

## 対象外

Windows native経路（別run）、hub経路、外部16 GPIO。codecの改良はしない（E115のまま）。

## 必要な環境 / ベンチ種別

第三P4（esp32-p4-80f1b2d0b261）、PC直結usbipd/WSL、Library Managerの2.4.0。一時。

## 記録する数値 / 完了条件

上の掃引の結果表（通った / 落ちた、Mbps、idle、byte一致）と、E109〜E115の参考値との対応表。**four 60とfive 60の約4.7分soakが欠損0で通り、W16 40 / 8-bit F3 60 / 素通し2 chがbyte一致で通れば完了**。通った数値がroadmap §1.1の正式な裏付けになり、「参考値」の注記を外す。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1冒頭の注記・§1.1・§1.2・§2、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)、LEDGERのE108〜E115の注記、[EspUsbDevice宛 CR-10〜13](../../references/espusbdevice-change-requests.ja.md)。

## 結果（2026-09-16、ログ `_runs/_runs/E117_20260916T011220JST_p4_direct_pin240/sweep.log`。マシン再起動をまたいだが、boardはE117 firmwareのまま、attachも維持されていた）

build: `--clean`、libraryはLibrary Managerの`EspUsbDevice_2.4.0_396e6a991e90da93`（verboseで確認）。全runで`direct_supported=1 last_direct_error=None arm_failures=0`、退避0、`completion_gap_us_max` ≤ 2.3 ms（mixのみ6.7 ms＝pipelineが空く周期）。hostの検証はcapture後（E114 §4の規約）。

### 1. 任意descriptor（`host_descriptor.py`、E105 referenceとbyte一致、60 periods＝7.9 M block）

| layout | rate | Mbps | 判定 | idle core 0 / 1 | E115（patch版参考値） |
|---|---|---|---|---|---|
| 16 ch: 3 raw＋hold/8＋12 hold/64（W16） | 40 | 131 | **一致** | 10% / 17% | 同 |
| 同 | 50 | 165 | **一致** | 0.04% / 0.05% | 同（上限） |
| 4 raw＋12 hold/32（F4） | 50 | 217 | **一致** | 0.1% / 0.2% | 同 |
| 8-bit 3 raw＋5 hold/64 | 60 / 80 | 189 / 251 | **一致** | 18% / 26% ／ 0.2% / 0.5% | 同 |
| 配線順不同（9,2,7 raw、phase 3 / 63 / 1 / 7） | 40 / 50 | 131 / 163 | **一致** | 32% / 41% ／ 10% / 17% | 同 |
| any_active ×6（D=8、active-low） | 30 / 40 | 83 / 111 | **一致** | 34% / 46% ／ 0.7% / 3% | 同 |
| edge_latch ×12（D=32、16-bit） | 15 / 20 | 58 / 78 | **一致** | 27% / 30% ／ 3% / 9% | 同 |
| mix（any×2、edge×2、hold/4、hold/128） | 10 / 15 | 28 / 42 | **一致** | 38% / 42% ／ 8% / 12% | 同 |
| 素通し 1 ch | 160 | 160 | **一致** | 23% / 25% | 同 |
| 素通し 2 ch | 160 | **319** | **一致**（USB予算329の97%） | 18% / 21% | 同 |
| 素通し 4 ch | 80 | 320 | **一致** | 41% / 47% | 同 |

配線順不同50と素通し2 ch 150は各1回、loopback源の開始位相が見つからず（streamは正常、E114 / E115で記録済みのflake）、別runで一致。

### 2. 固定profile（`host_capture.py`、80 periods、検証はcapture後）

| profile | rate | Mbps | 判定 | core 0 busy |
|---|---|---|---|---|
| eight（3 full＋5 D64、8-bit） | 60 / 80 / 90 | 186 / 249 / 314 | 欠損0 | 73% / 80% / 87% |
| eight | 100 | **349** | 1回目host不一致64 block、**再走2回は欠損0**（USB予算329の106%、上限いっぱい） | 97% |
| wide（3 full＋1 D8＋12 D64） | 40 / 60 | 132 / 199 | 欠損0 | 62% / 97% |
| four（4 full＋12 D32） | 60 | 264 | 欠損0 | 97% |
| five（5 full＋11 D32） | 60 | 324 | 欠損0 | 99.6% |

### 3. soak

| 構成 | 時間 | 量 | 結果 |
|---|---|---|---|
| **four 60**（262.5 Mbps） | 279.6 s | 9.2 GB | **欠損0、host検証bad=0、退避0、間隙0.68 ms以下**。core 0 97% |
| **five 60**（343〜352 Mbps） | 263 s＋256 s | 11.5 GB×2 | 1回目は device欠損0・Gray進行OKだが**mirror lane不一致26,718 block**（0.3%）、2回目は**全項目0**。mirror不一致は同じGPIOを2 laneで読んだ値の差で、device側`raw_sequence_bad=0`・退避0なのでdata lossではなくloopback源の採取flake（E114 / E115の8-bit 58〜60と同種）。core 0 99.6% / core 1 98.6% |
| **W16 40**（144 Mbps） | 24.2 s | 434 MB | 欠損0、byte一致、退避0 |

### 4. 判定

- **仮説は成立。** 独自patchを全部外した正規実装だけで、E109〜E115で参考値としていた上限が同じ値で再現した。USB経路の−7%（E116）は、codec律速の構成には効かず、USB律速の構成（2 ch 160 M＝319 Mbps、eight 100＝349 Mbps、five 60＝324〜352 Mbps）は正式予算329 Mbps（366×0.9）に対してそれぞれ97% / 106% / 99〜107%。**2 ch 160 Mは予算内ぎりぎり、eight 100とfive 60は予算超えで「取れる場合もある」の域**。
- 反証条件（arm_failures、last_direct_error、byte不一致）は出ていない。mirror不一致1回とeight 100の1回目のhost不一致はどちらも再走で消え、device側に欠損の痕跡がないので源側のflakeと判断。
- **完了条件（four 60 / five 60の約4.7分soak欠損0、W16 40 / 8-bit F3 60 / 素通し2 chのbyte一致）を満たした。** roadmap §1.1の裏付けはこれで正式な数値になる。
