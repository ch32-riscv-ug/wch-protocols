# E109 E108 data pathの長時間・繰り返し・経路差

状態: **完了 — 8-bit 60 / wide 40 Mspsは60 s連続・20回の交互繰り返し・Windows native経路のすべてで欠損0。native probeは221 Mbps。DWC2はEspUsbDeviceの既定でDMA modeだった**（2026-09-15） — **参考値（独自patch版library）**（EspUsbDevice 2.3.0＋E097/E101/E102/E110の一時patch。正規libraryに取り込まれるまで製品の目安には使わず、修正依頼の根拠にのみ使う）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E108](../e108_p4_zero_copy_stream/README.ja.md)、[E107](../e107_p4_stream_core_placement/README.ja.md)、[E104](../e104_p4_windows_continuous_bulk/README.ja.md)

## 問い

E108のzero-copy data pathで、通常値の8-bit 60 Msps（3 full＋5 D64）と16-bit wide 40 Msps（3 full＋1 D8＋12 D64）は、**分単位の連続capture**と**繰り返しの開始・停止・profile切替**で欠損0を保てるか。USB-only probeと結合PASSはusbipd/WSLとWindows nativeの2経路でどう違うか。E108で`GAHBCFG.DMAEn`がslave buildでも1と読めた点は、slave初期化経路が走った証拠（`GINTMSK.RXFLVL`）と矛盾しないか。

## 仮説

- 8-bit 60はcodec 82.6%・USB予算の84%、wide 40はcodec約86%・予算60%なので、60 s連続でもring追い越し・退避overflow・USB欠損は出ない。
- 繰り返し開始停止は、arm ringとstage / bounceの所有権がrunごとに`resetRunState()`で初期化されるので、profileを交互に変えても状態が残らない。
- Windows nativeのUSB-onlyはE107の193 Mbpsからusbipd/WSLと同じ比率（＋18%）で約230 Mbpsになる。
- `GINTMSK.RXFLVL`はslave buildで1、DMA buildで0になり、`GAHBCFG.DMAEn`の読み値とは独立にTinyUSB側の初期化経路が分かる。

## 反証条件

- 60 s連続で`raw_sequence_bad`、`queue_overflow`、`spill_overflow`、host側sequence不一致のどれかが0でない → 短時間の3回PASSは持続を保証しない。
- 繰り返しで途中のrunがhang・timeout・stale byteを起こす → run間の状態初期化に漏れがある。
- nativeのprobeが200 Mbps前後のまま → nativeの天井はhost側にある。

## 方法

- firmwareはE108のvariant D（slave、`-O2`、先読み）に`GAHBCFG`生値と`GINTMSK.RXFLVL`の読み出しを足しただけ。data pathは同一。
- hostに`--validate-every N`を足す。N=1（既定）は全byteを受信後に全検査。N>1は1 MiB transferをN個おきにworker threadへ渡し、transferの先頭位相からblock境界を合わせて全検査する。検査が追い付かない時は標本を捨てて数える。device側は全blockでGray連番、queue overflow、退避overflowを数える。
- 繰り返し: 8-bit 60とwide 40を交互に各10回（65.536 / 69.468 MB、全検査）。
- soak: 8-bit 60を60 s（8-bit 3.125 bit/sampleで約1.4 GB）、wide 40を60 s（約1.0 GB）、`--validate-every 8`。
- 経路: 上記をusbipd/WSLで行い、Windows nativeではprobe 3回、8-bit 60、wide 40、8-bit 60の30 s soakを行う。
- 判定はE106〜E108と同じ。soakは標本transferのsequence不一致0、device側3 counter 0、全byte受信、`arm_failures=0`。

## 対象外

hub経路（配線変更が要る）、codecのさらなる高速化、任意descriptor、host gateway。

## 必要な環境 / ベンチ種別

E108と同じ第三P4、PC直結。WSL経路はudev rule、native経路は`usbipd.exe detach`後にWindows側`uv.exe`。一時。

## 記録する数値 / 完了条件

各runのbyte数、sequence不一致、device counter、標本数。soak 60 s×2 profileが欠損0、繰り返し20 runが全PASSなら、ロードマップ§7の「長時間・複数回、欠損0」と「start / stop / restart」を通常値について満たしたとする。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.2 / §7、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)。

## 再現

```sh
arduino-cli compile --profile esp32p4_device && arduino-cli upload --profile esp32p4_device --port /run/board-identify/by-id/esp32-p4-80f1b2d0b261
usbipd.exe attach --wsl --busid 1-7
for i in $(seq 10); do uv run --with libusb1 python host_capture.py --width 8 --rate-mhz 60 --periods 320 --depth 8; uv run --with libusb1 python host_capture.py --width 16 --wide-profile --rate-mhz 40 --periods 160 --depth 8; done
uv run --with libusb1 python host_capture.py --width 8 --rate-mhz 60 --periods 6866 --depth 8 --validate-every 8      # 60 s
uv run --with libusb1 python host_capture.py --width 16 --wide-profile --rate-mhz 40 --periods 2289 --depth 8 --validate-every 8   # 60 s
```

## 結果

生ログ: `_runs/E109_20260915T083445JST_p4_direct/sweep.log`。firmwareはE108 variant D＋register読み出し、hostは`--validate-every`を足したもの。

### 1. 繰り返し（usbipd/WSL、全byte検査）

8-bit 60 Msps（65,536,000 byte）とwide 40 Msps（69,468,160 byte）を交互に10回ずつ、計20 run。**全runがPASS**で、host sequence不一致0、複製lane不一致0、device側`raw_sequence_bad` / `queue_overflow` / `spill_overflow` / `arm_failures`すべて0、全segment直接送信（2,560 / run）、退避0。host実測は8-bitが193.7〜195.5 Mbps、wideが135.3〜138.0 Mbps。ring未読最大は4,032〜4,928 byte（64 KiBの8%）。codec task率は8-bit 81.1〜81.2%、wide 80.6%、core 0は7.4% / 6.4%で、runごとのばらつきは0.1 point以内。profile切替をまたいでもstageやarm ringの状態は残らなかった。

### 2. soak（usbipd/WSL、60 s、標本検査）

| profile | 受信byte | host経過 | 実測 | 直接segment | 標本transfer（1 MiB） | 標本内block | 標本落ち | device counter |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| 8-bit 60 Msps | 1,406,156,800 | 57.54 s | 195.5 Mbps | 54,928 | 164 | 6,878,502 | 4 | すべて0 |
| wide 40 Msps | 993,828,864 | 57.82 s | 137.5 Mbps | 36,624 | 119 | 2,354,234 | 0 | すべて0 |

device側のcapture時間は8-bitが59.996 s、wideが60.005 sで、hostの経過はtransfer開始からの計時なので短く見える。標本はhostのPython検査が追い付く範囲で8 transferごとに1 MiBを全検査した（8-bitでは4個だけ検査待ちが4個を超えて落とした）。device側は全blockでGray連番を検査しており、退避FIFOは一度も使われていない（`spill_high_water=0`）。ring未読最大は両方4,032 byte。

### 3. Windows native経路（WinUSB、`uv.exe run --with libusb1`）

| 項目 | E107 firmware（E107で測定） | E109 firmware |
|---|---:|---:|
| USB-only probe 64 MB×3 | 192.7 / 192.8 / 193.9 Mbps | **220.5 / 221.5 / 221.2 Mbps**（90%予算 198〜199 Mbps） |
| 8-bit 60 Msps | PASS（USB予算超え、FIFO増加） | PASS、187.4 Mbps、退避0 |
| 8-bit 68 Msps | 未測 | PASS、212.5 Mbps、退避0（probeの96%） |
| wide 40 Msps | PASS | PASS、132.5 Mbps |
| 8-bit 60 Msps 30 s soak | 未測 | PASS、703,078,400 byte（device capture 29.998 s）、標本57、標本落ち27、device counter 0 |

nativeもzero-copyで**＋14%**（193→221 Mbps）。usbipd/WSL（247 Mbps）との差は残るが、8-bit 60の187.5 Mbpsは両経路の90%予算に収まる。標本落ちがWSLより多いのはWindows側Pythonの検査が遅いためで、device側検査には影響しない。

### 4. DWC2の動作modeの決着

`GAHBCFG`の生値は`0x00000027`（GINT、HBstLen、**DMAEn=1**）、`GINTMSK.RXFLVL`は**0**だった。TinyUSBの`dwc2_core_init()`はslave経路でだけRXFLVLを立てるので、走ったのはDMA経路である。理由はEspUsbDevice 2.3.0の`src/internal/EspUsbTinyUsbConfig.h`が**P4で`CFG_TUD_DWC2_DMA_ENABLE 1` / `CFG_TUD_DWC2_SLAVE_ENABLE 0`を定義している**ことで、コメントにはdcache保守が`tusb_mcu.h`の既定で有効になること、slave modeとDMA modeを同時に有効にすると`tu_edpt_stream`系classがNULL bufferをDMAすることが書かれている。したがって

- E107の仮説「slave modeのISRがTX FIFOへCPU storeで押し込む」は前提が誤り。core 1の競合はDWC2完了割り込みとusbd taskの処理（8 KiBごとのring→endpoint buffer memcpyと再arm）で起きていた。
- E108の「wire 1 byteがcore 0で4回運ばれる」は3回（Stage→PSRAM、PSRAM→ring、ring→endpoint buffer）が正しく、4回目はDMAだった。
- E108 variant B（`CFG_TUD_DWC2_DMA_ENABLE=1`を明示）がAと同値だったのは、両方DMAだったから。Bで足した`CFG_TUD_DWC2_SLAVE_ENABLE=1`はnon-buffered vendorだけを使うE108では害が出なかったが、buffered classと併用すると上記コメントの通り壊れる。

## 事実 / 候補 / 未決

**事実**

1. E108 data pathで8-bit 60 / wide 40 Mspsは、60 s連続（1.41 GB / 0.99 GB）、20回の交互繰り返し、Windows native経路のすべてで欠損0だった。退避FIFOは一度も使われていない。
2. Windows nativeのUSB-only probeは193→221 Mbps。8-bit 68 Msps（212.5 Mbps）も退避なしで通った。
3. P4のDWC2はEspUsbDeviceの既定でDMA modeで動いている。E107 / E108のslave mode前提は誤りで、結論（core配置、zero-copy）は変わらないが機序の説明を訂正する。

**候補**

- 8-bit 60 / wide 40 Mspsを直結の通常値として確定する。probe→90%規則ではusbipd/WSL 222 Mbps、native 199 Mbpsのどちらでも収まる。
- soak検証の恒久手順: device側全block検査＋host標本検査（`--validate-every 8`）。全byte検査はhostのPythonが律速で、65 MB級の短runに限る。

**未決**

- hub経路（配線変更）。分単位を超える（10分〜）soak、途中切断からの復帰、host timeout時の再同期はPhase Bの正しさ固めで扱う。
- codecのさらなる高速化（PIE SIMD）は必要になった時に別実験で。

### 追記（2026-09-15、host toolの検証位置）

`host_capture.py`（E109 / E111 / E112 / E113共通の構造）は、sampled transferの検証をcapture中のthreadで行っていた。E114で「URB完了callback（libusbのevent loop）がPythonの仕事で塞がるとusbip経路が約200 msの穴を繰り返す状態に落ちる」ことが分かったので、4本とも検証をcapture後へ移した（`--keep-mib`、既定768 MiB）。本実験の60 s / 5分soakは検証が軽く偶然通っていた。以後のsoakはこの版で取る。
