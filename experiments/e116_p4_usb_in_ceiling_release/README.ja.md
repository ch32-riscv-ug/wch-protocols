# E116 正規リリースEspUsbDevice 2.4.0（pin）でのUSB HS bulk INの上限 — 独自patch版E110の値に届くか

状態: **完了 — 正式リリース2.4.0（pin）で27,136 byte transfer 45.6〜45.8 MB/s（365〜366 Mbps）×3回、欠損0。以後の製品目安に使える最初の正式な数値**（2026-09-16）

規則: [実測の規則](../README.ja.md)（正式な数値は正式リリースをpinして取る） / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E110](../e110_p4_usb_in_ceiling/README.ja.md)（独自patch版、参考値）

## 問い

EspUsbDevice **2.4.0（正式リリース、`sketch.yaml`で`EspUsbDevice (2.4.0)`をpin、同梱TinyUSB無改変）**の公開API `writeDirect()` / `onTxComplete()` / `onRxData()`（`-DCFG_TUD_VENDOR_TXRX_BUFFERED=0`、TX FIFO 2 packetは既定）で、E110と同じUSB-only probe（既知pattern、zero-copy、arm ring、PC直結usbipd/WSL、1 MiB URB×depth 8）を流すと、独自patch版E110の**49.3 MB/s（27,136 byte transfer）、49.0〜49.2（65,024）、41.2（8,192）**に届くか。これが以後の製品の目安に使える**最初の正式な数値**になる。

## 仮説

- 届く。正規実装はpatch版と同じ経路（呼び出し側bufferを`usbd_edpt_xfer()`へ、完了callback内で次をarm、DFIFO 2 packet以上）で、working tree版の予備測定（precomputed build、同じhost）が46.6 / 48.3 / 41.4〜42.9 MB/sだった（[CR-10](../../references/espusbdevice-change-requests.ja.md)）。差3〜6%はrun間のばらつき程度。
- arm ringの深さ（1 / 2 / 4）とtask再arm対callback chainの差はE110と同じ傾向（深さ2以上で飽和、27 KiB以上ならchainと再armに差なし）。

## 反証条件

- 27,136 byteで45 MB/s未満が続く → 正規実装とpatch版の経路差（`writeDirect()`のチェック・msync、classの完了経路）を疑い、E110 §3の方法（arm間隔`gap_*`、transfer所要`dur_*`）で分ける。
- `arm_failures > 0`や`last_direct_error != None` → 契約違反（整列、DMA可否）かBusy（二重arm）。arm ringの実装側。
- `pattern_bad > 0`や`short > 0` → data path欠陥。即中止。

## 方法

- device: `e116_p4_usb_in_ceiling_release.ino`＝E110のprobe deviceを2.4.0の公開APIに移植（`Vendor.write()`→`Vendor.writeDirect()`、E097/E101のexperimental hook→`Vendor.onTxComplete()` / `Vendor.onRxData()`、`E110_IN_FIFO_PACKETS`と`ESP_USB_DEVICE_EXPERIMENTAL_*`は撤去）。`build_opt.h`は`-DCFG_TUD_VENDOR_TXRX_BUFFERED=0`とEPSIZE 512だけ。**`arduino-cli compile --clean`**。sketch.yamlの既定profileは`EspUsbDevice (2.4.0)`のpin。indexが遅れている間の**compile確認だけ**`esp32p4_device_tree`（release treeへの`dir:`）で行い、数値には使わない。
- host: `host_probe.py`（E110と同じ。1 MiB URB×depth 8、256周期pattern全照合、`short`と完了数）。
- 掃引: transfer 27,136 / 65,024 / 8,192 × arm depth 4（chain）を各3回、加えて27,136でarm depth 1 / 2、`--task-rearm`。64 MB×1回のprobeも（E109のprobeと同形）。
- 記録: statusの`direct_supported=1`、`last_direct_error=None`、`gahbcfg`。identity（303a:4021 / e104-p4-windows-v1）はそのまま。

## 対象外

Windows native経路（別run）、P4 host（EspUsbHost側）、stream data path（E108/E111の移植は次の実験）。

## 必要な環境 / ベンチ種別

第三P4（esp32-p4-80f1b2d0b261）、PC直結usbipd/WSL、Library Managerに2.4.0。一時。

## 記録する数値 / 完了条件

transfer長×arm深さのhost MB/s（3回）、`short` / `pattern_bad` / `arm_failures`、device側`dur_*` / `gap_*`。**27,136 byteで45 MB/s以上を3回、欠損0**で完了。この値がroadmap §1のUSB予算の正式値になる（`× 0.9`）。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1冒頭の注記と§2「USB経路の予算測定」、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md) §0、[EspUsbDevice宛 CR-10〜13](../../references/espusbdevice-change-requests.ja.md)の「直ったことの確認」。

## 結果（2026-09-16 01:05〜、ログ `_runs/_runs/E116_20260916T010458JST_p4_direct_pin240/sweep.log`）

build: `arduino-cli compile --clean --profile esp32p4_device`、libraryは**Library Managerの`EspUsbDevice_2.4.0_396e6a991e90da93`**（`--verbose`のinclude pathで確認。working treeへの`dir:`ではない）。device status: `direct_supported=1 last_direct_error=None arm_failures=0 gahbcfg=0x00000027`（DMA mode）。host: `host_probe.py`、1 MiB URB×depth 8、usbipd/WSL、256 MiB/run、pattern全照合。

| transfer | arm | run 1 / 2 / 3（MB/s） | Mbps | short | pattern_bad | device `dur_avg_us` / `gap_avg_us` | 参考: E110 独自patch版 |
|---|---|---|---|---|---|---|---|
| **27,136** | 4 chain | **45.60 / 45.75 / 45.76** | **365〜366** | 0 | 0 | 593 / 1 | 49.3 |
| 65,024 | 4 chain | 47.30 / 46.82 / 47.22 | 375〜378 | 0 | 0 | 1,372 / 1 | 49.0〜49.2 |
| 8,192 | 4 chain | 39.28 / 39.38 / 39.24 | 314〜315 | 0 | 0 | 206 / 1 | 41.2〜41.3 |
| 27,136 | 1 | 43.97 | 352 | 0 | 0 | 591 / 19（gap max 40） | — |
| 27,136 | 2 chain | 45.53 | 364 | 0 | 0 | 592 / 1 | — |
| 27,136 | 4 task再arm | 44.99 | 360 | 0 | 0 | 585 / 18（gap max 39） | — |
| 27,136 | 4 chain、64 MB | 45.69 / 45.56 | 365 | 0 | 0 | 592 / 1 | — |

- **完了条件（27,136で45 MB/s以上×3、欠損0）を満たした。** run間のばらつきは0.3%。
- 独自patch版E110との差は27,136で**−7.4%**（45.7対49.3）、65,024で−4%、8,192で−4.7%。run間ばらつき（0.3%）より大きいので実差。device側の`dur_avg`が27,136で593 µs（E110は約550 µs）と、transferそのものが約8%長い。候補は`writeDirect()`の事前チェックとclaim、classの完了経路、DFIFO割り当て（E110 patchは`fifo_size *= 2`、2.4.0は自動計算で先方の実測では4 packet分）。原因の切り分けは次の実験へ（数値としては十分で、製品目安には45.7 MB/sを使う）。
- arm深さ・再arm位置の傾向はE110と同じ: 深さ1で−4%（gap 19 µs）、深さ2以上で飽和、task再armは−1.5%。
- **製品目安に使うUSB予算（PC直結usbipd/WSL）**: probe **366 Mbps → 90%で329 Mbps（41 MB/s）**。Windows native経路は別途測る。

## 判定

仮説「届く」は**ほぼ成立**（届いたのは45.7で、49.3には7%足りない）。反証条件（45未満、arm_failures、pattern_bad）はいずれも出ていない。正規実装の公開API（`writeDirect()`＋`onTxComplete()`内arm、TX FIFO自動）は独自patch版と同じ経路で動き、差は7%以内。**E108〜E115の「参考値」を正規版で取り直す作業は、このUSB予算を前提に進める**（stream data pathの2.4.0 API移植は次の実験）。

## 追記（2026-09-17）: buildの出所を実行記録から証明できない

持ち主の方針（2026-09-17）: **`dir:`（working tree）で取った数値は再現性がないので使えない。正式値は、リリース版をpinし、その版が実際にリンクされたことを証明できるものだけ。**

この実験は`sketch.yaml`で`EspUsbDevice (2.4.0)`をpinしているが、**実行記録（`_runs/`）にライブラリ版が残っていない**。当時はhost側のログしか保存していなかった。加えて、EspUsbDevice側sessionが2026-09-17に**`build/<profile>/libraries.cache`が`sketch.yaml`のpin変更に追従せず`--clean`でも消えない**事例を実測している（別版をpinしたbuildが前の版をリンクした）。**pinを書いただけでは、意図した版がリンクされたとは限らない。**

したがって**この実験の数値は、出所を証明できない値として扱う。** [E120](../e120_p4_usb_baseline_250/README.ja.md)以降は、生成ELFの`strings`で実リンク版を実行記録に残す（[実測の規則](../README.ja.md)）。

**USB天井については[E120](../e120_p4_usb_baseline_250/README.ja.md)（2.5.0 pin、ELF証明）で取り直し済みで、device側の数値はE116と小数点以下まで一致した。** stream側（E117 / E118）の取り直しは未了である。
**訂正（2026-09-17）**: 上の注記は行き過ぎだった。**却下の対象は、当方のpatchを当てて測った数値（[E108](../e108_p4_zero_copy_stream/README.ja.md)〜[E115](../e115_p4_grouped_plane_transpose/README.ja.md)）である。素のリリース版へのpinは問題ない。** この実験はpin 1本で組んでおり、**正式値として有効**である。

残る弱点は証拠の強さだけで、実行記録にライブラリ版を残していないので成果物からリンク版を示せない。**製品の目安は現行リリースで取り直した[E120](../e120_p4_usb_baseline_250/README.ja.md) / [E121](../e121_p4_stream_baseline_250/README.ja.md)（2.5.0 pin＋ELF証明）を一次の裏付けにする。**

**この実験固有の注意**: `sketch.yaml`にpin（既定）と`dir:` profileを同居させていた。既定はpinなので数値はpin版のはずだが、選べる状態にしていたこと自体が良くない。以後は同居させない。
