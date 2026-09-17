# E120 EspUsbDevice 2.5.0（pin）の公開APIだけで測るUSB IN天井 — 以後の正式値のベースライン

状態: **計画**（2026-09-17）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E110](../e110_p4_usb_in_ceiling/README.ja.md)（独自patch版、参考値）、[E116](../e116_p4_usb_in_ceiling_release/README.ja.md)（2.4.0 pin、ただし同じsketch.yamlに`dir:` profileが同居していて出所を証明できない）

## 問い

**EspUsbDevice 2.5.0（Library Managerからpin、`dir:`を一切持たない）の公開APIだけで組んだprobeで、P4のvendor bulk INの天井はいくつか。そしてその数値は、buildの出所（ライブラリ版）を実行記録から証明できる形で取れるか。**

## なぜやるか

持ち主の指示（2026-09-17）: **`dir:`（working tree）で測った数値は再現性がないので全部使えない。正式な数値は、正規リリース版をpinした公開APIで取り直す。**

この指示は既存の記録に次のように効く。

| 実験 | sketch.yamlの中身 | 扱い |
|---|---|---|
| [E108](../e108_p4_zero_copy_stream/README.ja.md)〜[E115](../e115_p4_grouped_plane_transpose/README.ja.md) | `dir:`のみ | **参考値**（従来どおり） |
| [E116](../e116_p4_usb_in_ceiling_release/README.ja.md) | `EspUsbDevice (2.4.0)` pin（既定）＋`dir:` profile同居 | **pin版として有効**だが、`dir:`を選べる状態にしていたうえ、実行記録（`_runs/E116_*/sweep.log`）にライブラリ版が残っていない |
| [E117](../e117_p4_stream_release_api/README.ja.md) / [E118](../e118_p4_generic_fast_path/README.ja.md) / [E119](../e119_p4_fast_part_words/README.ja.md) | pinのみ（`dir:`なし） | **正式値として有効。** 構造上working treeではbuildできない。ただし実行記録にライブラリ版が残っていないので、証拠の強さでは E120 / E121 に劣る |

**共通の欠陥は「何でbuildしたかを実行記録に残していなかった」ことである。** この実験はそこを直す。

## 仮説

2.5.0の公開APIで組んだprobeは、E116が記録した45.6〜45.8 MB/s（27,136 byte transfer）と同じ水準に出る。2.5.0の変更（UVC class、isochronous FIFOの検査、HID登録順のfix、`deviceVersion`、`deviceInterfaceGuid`、`msOs20VendorRevision`）は**`writeDirect()`とvendor bulkのdata pathに触っていない**ため。

## 反証条件

1. 45 MB/s（27,136）に届かない。2.5.0でdata pathが変わったか、E116の数値が出所不明のまま過大だった
2. `direct_supported=0`、`last_direct_error`がNone以外、`arm_failures>0`のいずれか
3. host側で`short>0`または`pattern_bad>0`
4. compile logにライブラリ版が出ず、出所を記録できない（この場合は記録方法を先に直す）

## 方法

- device: `e120_p4_usb_baseline_250.ino`＝E116のprobeをそのまま（marker名と product string だけ変更）。**公開APIのみ**（`Vendor.writeDirect()` / `Vendor.onTxComplete()` / `Vendor.onRxData()` / `directWriteSupported()` / `lastDirectError()`）。`build_opt.h`は`-DCFG_TUD_VENDOR_TXRX_BUFFERED=0`とEPSIZE 512の3行だけ。
- **sketch.yamlはpin 1本だけにする（`dir:` profileを置かない）。** 出所が選べる状態にしない。
- **buildの出所を記録に残す**: `arduino-cli compile --clean` の出力（`Used library ... EspUsbDevice 2.5.0` の行を含む）を`_runs/E120_*/compile.log`へ保存し、host logと同じディレクトリに置く。
- host: `host_probe.py`（E110 / E116と同形。1 MiB URB×depth 8、256周期patternの全照合、`short`と完了数）。
- 掃引: transfer 27,136 / 65,024 / 8,192 を各3回。27,136でarm depth 1 / 2も。

## 対象外

stream data path（E117 / E118相当の取り直しは別実験）、Windows native経路、P4 host（EspUsbHost側）。

## 必要な環境 / ベンチ種別

第三P4（`esp32-p4-80f1b2d0b261`、`/dev/ttyUSB2`）、HSはPC直結でusbipd/WSL（busid `1-7`）。Library Managerに2.5.0。**一時。** 着手前にEspUsbDevice側sessionへ板の使用を宣言する（共有機材）。

## 記録する数値

transferごとのhost MB/s（3回）、`short`、`pattern_bad`、deviceの`blocks` / `arm_failures` / `zerolen`、`direct_supported`、`last_direct_error`、`gahbcfg`、**compile logのライブラリ版行**。

## 完了条件

27,136で45 MB/s以上を3回、`short=0`・`pattern_bad=0`・`arm_failures=0`、**compile logに`EspUsbDevice 2.5.0`が残っていること**。届かない場合も、内訳と出所を記録して完了とする。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md)のUSB予算（現在は366 Mbps→90%で329）、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)、[README](../../README.md)の製品数値。E116〜E119の扱い（出所を証明できない旨の注記）。

---

## 結果

生ログ: `_runs/E120_20260917T*_p4_baseline_pin250/`（`compile.log` / `used_libraries.txt` / `linked_library.txt` / `upload.log` / `sweep.log`）。

### 0. buildの出所（この実験の主目的）

| 証拠 | 中身 |
|---|---|
| `arduino-cli compile --clean` | 成功。ただし**出力にライブラリ版は出ない**（「automatically added from sketch project」だけ） |
| `--format json` の`used_libraries` | `EspUsbDevice 2.5.0` / `~/.arduino15/internal/EspUsbDevice_2.5.0_55353dc9af97a625/EspUsbDevice` |
| **生成ELFの`strings`** | **`EspUsbDevice_2.5.0_55353dc9af97a625`** |

**ELFから実リンク版が取れる**（EspUsbDevice側sessionの助言）。staging dirのhashまで一致するので、pinされたLibrary Manager版がリンクされたことが成果物自体で示せる。`--clean`の出力だけでは足りない——先方は**`build/<profile>/libraries.cache`がsketch.yamlのpin変更に追従せず`--clean`でも消えない**事例（2.9.0 pinのbuildが2.8.0をリンク）を今日踏んでいる。**以後、正式値を出す実験はELFの`strings`を実行記録に残す。**

### 1. 掃引（各3回、1 MiB URB×depth 8、64 MB）

| transfer | host MB/s（3回） | device MB/s | short | pattern_bad | arm_failures |
|---|---|---|---|---|---|
| 27,136 | 43.43 / 43.38 / 43.68 | **45.58 / 45.67 / 45.66** | 0 | 0 | 0 |
| 65,024 | 45.83 / 45.82 / 45.61 | **47.29 / 47.28 / 47.30** | 0 | 0 | 0 |
| 8,192 | 37.96 / 38.03 / 38.04 | **39.16 / 39.27 / 39.26** | 0 | 0 | 0 |

`direct_supported=1`、`last_direct_error=None`、`gahbcfg=0x00000027`（DMA mode）。

### 2. E116（2.4.0、出所を証明できない版）との比較

**device側は完全に一致する。host側だけ今日は3〜5%低い。**

| transfer | device: E116 → E120 | host: E116 → E120 |
|---|---|---|
| 27,136 | 45.60 / 45.62 / 45.58 → **45.58 / 45.67 / 45.66** | 45.60 / 45.75 / 45.76 → **43.43 / 43.38 / 43.68** |
| 65,024 | 47.32 / 47.30 / 47.31 → **47.29 / 47.28 / 47.30** | 47.30 / 46.82 / 47.22 → **45.83 / 45.82 / 45.61** |

**2.5.0は`writeDirect()`とvendor bulkのdata pathを変えていない**ことが数値で裏付いた（device側が小数点以下まで同じ）。host側の差は**PC側の状態**である。この測定中、同じマシンで別session（EspUsbHost）がUSBのテストを走らせており、リグの物理構成も昨日から変わっている。

**したがって「deviceの天井」と「hostが受け取れる速度」を分けて書く必要がある。** deviceの天井は再現する。hostの速度は環境で動く。製品の規則（**接続ごとに実測してその9割を使う**）はこの差をそのまま吸収する。

### 3. 静かなマシンで取り直した（同日、EspUsbHost側sessionが全テストを停止した直後）

`_runs/E120_20260917T*_p4_baseline_pin250_quiet/`。`ps`で`pytest` / `arduino-cli`が0プロセスであることを確認してから実行。

| transfer | host MB/s（静か） | host MB/s（混雑時） | 差 | device MB/s（静か） |
|---|---|---|---|---|
| 27,136 | 44.18 / 44.15 / 44.26 | 43.43 / 43.38 / 43.68 | **＋1.8%** | 45.65 / 45.62 / 45.69 |
| 65,024 | 45.81 / 45.81 / 45.72 | 45.83 / 45.82 / 45.61 | **±0%** | 47.30 / 47.29 / 47.33 |
| 8,192 | 38.17 / 38.13 / 38.26 | 37.96 / 38.03 / 38.04 | ＋0.4% | 39.28 / 39.15 / 39.29 |

**当方の帰属は行き過ぎだった。** 「host側が3〜5%低いのは別sessionのUSB負荷」と書いたが、**負荷を外して測ると戻ったのは27,136で1.8%だけ**で、65,024では差がない。URB間隙が102.7 msまで伸びていたのは事実だが、**平均のthroughputへの影響はその程度**である。deviceが埋め切っているので、間隙の後に取り返している。

**残る差は説明できていない。** E116（2.4.0、混雑なしと思われる時間帯）の27,136は host 45.60 / 45.75 / 45.76 で、**静かなマシンでの今日の44.2より3.4%高い**。device側は小数点以下まで一致しているので、**差はhost側の経路にある**。心当たりは2026-09-16に持ち主がリグを物理的に触ったこと（CH32板とWCH-Linkが3つ増えた）で、同じhost controllerの下にぶら下がるdeviceが増えている。**未検証の推測である。**

## 事実 / 候補 / 未決

- **事実**: 2.5.0 pin（ELFで確認）の公開APIだけで、device側の天井は27,136で45.6 MB/s、65,024で47.3 MB/s、8,192で39.2 MB/s。すべて`short=0` / `pattern_bad=0` / `arm_failures=0`
- **事実**: 2.4.0と2.5.0でdevice側の数値は一致する。data pathに変更はない
- **事実**: buildの出所はELFの`strings`で証明できる
- **事実（先方が確認、2026-09-17）**: URB間隙が28〜102.7 msに伸びたのは、**同じマシンでEspUsbHost側sessionがUSBのテストを走らせていた**ため。ただし**throughputへの影響は27,136で1.8%、65,024で0%**だった（上の§3）。`/tmp/pytest-embedded/`は全sessionで共有なので実行時刻が見える（**ディレクトリ名はUTC、mtimeはJST**）。当方の測定は11:20〜12:00 JSTで、同じ帯にEspUsbHost側sessionのUVC hostテスト（`test_video_stream_is_discovered`）と`pytest --clean`の全テスト（`test_p4_role_reversal` / `test_hid_keyboard_nkro`）が走っていた。**先方が自分の記録で確定した**: 11:20:49は`pytest --collect-only`（build / flashなし）、11:29:47と11:35:00がUVC hostのペアテスト、**11:38:51から`pytest --clean`の全テスト（20〜30分、arduino-cliのbuildが全コアを使い、serial flashとboard resetによる再列挙が断続する）**。間隙のピークはbuild中に寄っているとみられる。

EspUsbDevice側sessionが当初「自分の82件テスト（01:03〜01:53 JST）が原因」と申告したが、**当方の測定とは10時間離れていて重ならない**ことを指摘し、先方が撤回した。**共有PCでは「同じPCでUSBを使っていた」だけでは原因にならない。時刻を突き合わせる。**
- **未決**: 静かなマシンでもE116よりhost側が3.4%低い（27,136）。**device側は一致しているので原因はhost経路**。リグの物理構成が変わったことが心当たりだが未検証
- **未決**: stream data path（[E117](../e117_p4_stream_release_api/README.ja.md) / [E118](../e118_p4_generic_fast_path/README.ja.md)相当）の2.5.0 pinでの取り直し。別実験にする

## 反映

- 完了条件は満たした（27,136でdevice 45 MB/s以上を3回、`short=0`・`pattern_bad=0`・`arm_failures=0`、ELFに`EspUsbDevice_2.5.0`）。ただし**host側の45 MB/s×3回は今日の環境では出ていない**ので、その1点は未達として残す
- [実測の規則](../README.ja.md)に「正式値を出す実験はELFの`strings`で実リンク版を記録する」を追加する
- [P4ロードマップ](../../references/p4-probe-roadmap.ja.md)のUSB予算の根拠をE116からE120へ差し替える
