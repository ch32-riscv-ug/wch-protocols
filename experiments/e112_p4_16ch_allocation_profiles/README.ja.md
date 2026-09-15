# E112 16 channel配分例の実測 — 60 M×4＋1/32×12 と 60 M×5＋1/32×11

状態: **完了 — 60 M×4＋1/32×12（272 Mbps実測）は3回＋30 s soak 2回すべて欠損0、上限68 Msps。60 M×5＋1/32×11（334 Mbps）は3回PASS・上限64 Mspsだが、30 s soakは2回中1回に一過性の不一致があり、USB予算92%・core 0 99%の縁の構成**（2026-09-15）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E111](../e111_p4_dual_core_codec/README.ja.md)、[E110](../e110_p4_usb_in_ceiling/README.ja.md)、[E106](../e106_p4_mixed_rate_capture_stream/README.ja.md)

## 問い

[ロードマップ §1.1](../../references/p4-probe-roadmap.ja.md)の16 channel配分例、**60 Msps×4 ch＋1/32（1.875 Msps）×12 ch＝262.5 Mbps** と **60 Msps×5 ch＋1/32×11 ch（≒322.5 Mbps、線上）** は、E111のdata path（2 worker codec、zero-copy、TX FIFO 2 packet）で欠損0の連続転送として成立するか。成立するなら上限rateはどこで、律速はcodecかUSB予算か。

## 仮説

- 4 full＋12 D32は128 sample→70 byte（4×128 bit＋12 lane×4 snapshot×1 bit）。wide profile（53 byte）よりbit gatherが1 lane多いだけで、codecは2 workerで60 Mspsを80%台で通す。USBは262.5 Mbpsで予算350 Mbpsの75%。
- 5 full＋11 D32は128 sample→86 byte（5×128 bit＝80 byte＋11×4 bit＝44 bit→6 byte）。60 Mspsで322.5 Mbps、予算の92%。実測389 Mbpsの環境では通るが、300 Mbpsの環境では9割規則の外になる（§1.1の「5 chまで」は上限いっぱいの意味）。
- どちらもstage fillは512の倍数に取れる（70×256＝17,920、86×256＝22,016）ので、USB transferの形はE108〜E111と同じ。

## 反証条件

- 60 Mspsでqueue overflow / 退避overflow / host不一致が出る → 配分例の見直し（4 fullを3 fullへ、または1/32を1/64へ）。
- 5 full＋11 D32で退避が伸び続ける → USB予算超え。§1.1の「5 chまで」に「9割規則では4 ch」を残す。

## 方法

- E111 firmwareにprofile `F`（4 full＋12 D32）と`V`（5 full＋11 D32）を足す。どちらも16-bit幅、128 sample block。D32のsnapshotはsample 0 / 32 / 64 / 96。
  - `F`: 8 sampleの下位4 bitを2 word（16-bit×2）から`(w0 & 0x000f000f) | ((w1 & 0x000f000f) << 8)`→`(x | x >> 12) & 0xffff`で集める。lane 4〜15の12 bit×4 snapshot＝48 bitをbyte 64〜69へ。
  - `V`: 8 sampleの下位5 bitを`(w0 & 0x1f) | ((w0 >> 11) & 0x3e0) | ((w1 & 0x1f) << 10) | ((w1 >> 1) & 0xf8000)`で集め、5 byte/8 sample。lane 5〜15の11 bit×4 snapshot＝44 bitをbyte 80〜85へ。
- hostは新profileのdecoderを持ち、fast laneのGray進行（±2許容）、snapshotの複製lane一致（lane 8〜15はlane 0〜7の複製）、snapshot間の進行（32 sample＝8 step）、block間の進行（128 sample＝32 step）を検査する。device側はE111と同じchunk内Gray check。
- 掃引: 両profileで60 / 64 / 68 / 72 Msps、各69,468,160 byte相当（1,310,720 block）。60 Mspsは3回と30 s soak（`--validate-every 8`）。USB-only probeも1回。

## 対象外

任意descriptor（Phase A）、1/32以外の刻み、2 ch 160 M素通し（別候補）。

## 必要な環境 / ベンチ種別

第三P4、PC直結 usbipd/WSL、`/tmp/EspUsbDevice-e110`。一時。

## 記録する数値 / 完了条件

profile別のPASS境界、60 Mspsでのcodec率・USB予算比・退避、soakの欠損。§1.1の2例が「実測済み」に変われば完了。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.1の対応表、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md) §0。

## 結果

生ログ: `_runs/E112_20260915T110249JST_p4_direct/sweep.log`。usbipd/WSL直結、host URB 1 MiB×8。この日のUSB-only probeは378 Mbps（90%予算340 Mbps）。判定はE106〜E111と同じで、host側は新profileのdecoderでfast laneのGray進行、snapshotの複製lane一致、snapshot間・block間の進行を全block検査した（合成dataでの自己試験: 正常200 blockで不一致0、1 byte化けで2件検出）。

### 4 full＋12 D32（`F`、128 sample→70 byte、60 Mspsで262.5 Mbps）

| rate | wire | 判定 | codec core 0 / core 1 | core 0 / core 1 busy | ring未読最大 | 退避 |
|---:|---:|---|---:|---:|---:|---:|
| **60** | 262.5 Mbps | **PASS 3回＋30 s soak 2回（各984 MB）欠損0** | 81.5% / 85.7% | 95.5% / 86.9% | 16,896 | 0 |
| 64 | 280 | PASS | 83.3 / 91.2 | 98.3 / 92.5 | 20,928 | 0 |
| 68 | 297.5 | PASS | 83.2 / 96.1 | 99.4 / 97.4 | 24,960 | 0 |
| 72 | 315 | 1回目: host mirror不一致11,765＝device `duplicate_bad` 11,765（raw dataでlane 8〜15がlane 0〜7と不一致）、2回目: PASS | 81.9 / 99.8 | 100 / 100 | 28,992 | 0 |

72の不一致はcodec出力ではなくraw captureのlane間不一致で、内部loopback配線（TXの8 GPIOをRX lane 0〜7と8〜15へ複製）が72 MHzで境界にある兆候と見る。E111のwide 72では0だったので、run間で揺れる。

### 5 full＋11 D32（`V`、128 sample→86 byte、60 Mspsで322.5 Mbps）

| rate | wire | 判定 | codec core 0 / core 1 | core 0 / core 1 busy | ring未読最大 | 退避 |
|---:|---:|---|---:|---:|---:|---:|
| **60** | 322.5 Mbps（host実測334） | **PASS 3回**。30 s soak（1.21 GB）は**2回中1回**にhost不一致2・device `raw_sequence_bad` 1、退避3 stage、ring未読最大45,120。再走は欠損0 | 85.1% / 94.6% | 99.2% / 95.8% | 20,928 | 0（soak 1回目のみ3 stage） |
| 64 | 344 | PASS（予算340の101%） | 83.9 / 99.3 | 99.9 / 99.9 | 20,928 | 0 |
| 68 | 365.5 | FAIL（queue overflow 17,714、中止） | 95.8 / 99.2 | 100 / 100 | 522,240 | |
| 72 | 387 | FAIL | | | | |

soak 1回目の不一致は、USBが一時的に遅れて退避経路（core 0のusb taskによるPSRAM copy）が動き、99%まで使っていたcore 0のworkerが遅れてringが45 KBまで積まれた連鎖と読める。同時刻に別sessionがこのdeviceのdescriptorを1回読んでいたが、因果は決められない。

## 事実 / 候補 / 未決

**事実**

1. **60 M×4＋1/32×12（262.5 Mbps）は成立**。3回と30 s soak 2回で欠損0、codec 81 / 86%、USB予算の77%。上限は68 Msps（297.5 Mbps）。
2. **60 M×5＋1/32×11（322.5 Mbps）は「上限いっぱい」**。3回PASSするがcore 0は99%、USBは予算の92%で、30 s soakの1回に一過性の不一致が出た。64 Mspsは予算超え（101%）でも通ったが、68で溢れる。
3. hostの新profile decoderは合成dataで検出能力を確認済み（1 byte化けを検出）。
4. 16-bit loopbackは72 MHzでlane 8〜15の複製が揺れることがあり、内部試験の上限は72付近。

**候補**

- §1.1の例は「300 Mbpsなら60 M×5まで、9割で使うなら60 M×4＋1/32×12」の書き方で正しい。5 fullは「入る」が「余裕なし」。
- 5 full構成を余裕ありにするなら、縮約laneを1/64にする（5×60＋11×0.94 = 310 Mbps）か、base 56 Mspsにする。

**未決**

- 5 full soakの一過性不一致の再現条件（host側の遅れの発生源、descriptor読み出しの影響）。分単位のsoakで頻度を測る。
- 外部16 GPIOでの72 MHz以上のlane skew。
