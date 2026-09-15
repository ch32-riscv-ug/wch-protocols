# E113 2 channelを160 Mspsで — PARLIO 2-bit幅の素通し

状態: **完了 — PARLIO 2-bit幅×160 Mspsの素通し（320 Mbps）は10回＋30 s soak（1.2 GB）すべて欠損0。USB予算の87%、core 0 30% / core 1 20%で余裕あり。最初の1回のhost不一致はhost decoderの初期anchorのbugで、修正後は全phaseで自己試験PASS**（2026-09-15）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E112](../e112_p4_16ch_allocation_profiles/README.ja.md)、[E111](../e111_p4_dual_core_codec/README.ja.md)、[E061](../e061_p4_drain_core_split/README.ja.md)

## 問い

[ロードマップ §1.1](../../references/p4-probe-roadmap.ja.md)の「2 channel以下なら160 Mspsで取得できる場合もある」は、PARLIOを2-bit幅・160 MHzで回し、DMA ringの内容をcodecなしでそのまま送る（2 bit/sample＝320 Mbps）と、E111のdata pathで欠損0の連続転送になるか。成立するなら律速はUSB予算か、PARLIO 2-bit RXの160 MHzか。

## 仮説

- PARLIO RXは`PLL_F160M`を分周1で160 MHzに設定でき、E061では8-bit幅で160 MHzを使った。2-bit幅ではraw 40 MB/sで、DMA・ISR（約10 k/s）・memcpy（40 MB/s）はどれも軽い。
- 320 MbpsはUSB-only probe 378〜389 Mbpsの82〜85%で予算内。退避は動かない。
- 内部loopbackのTX（8 lane、rate/4 = 40 MHz）をlane 0〜1だけ受けるので、hostはGray bit 0〜1の進行（4 sampleごとに1 step、256 sample blockで64 step）で検査できる。

## 反証条件

- 160 MHzでqueue overflowや進行不一致が出る → 2-bit RXの160 MHz（clock、pin）が境界。120 / 140 Mspsで再測して上限を出す。
- 320 Mbpsで退避が伸びる → USB予算超え。

## 方法

E112 firmwareにprofile `2`（2-bit幅、256 sample→64 byte、memcpyのみ、stage 27,136 byte＝424 block）を足す。hostは2 bit/sampleのdecoderでGray bit 0〜1の進行を全block検査。掃引: 120 / 140 / 160 Msps、各1,310,720 block（83.9 MB）、160は3回と30 s soak。probeも1回。

## 対象外

外部信号源、1-bit / 4-bit幅、他のchannel数。

## 必要な環境 / ベンチ種別

第三P4、PC直結 usbipd/WSL、`/tmp/EspUsbDevice-e110`。一時。

## 記録する数値 / 完了条件

160 MspsのPASS可否、USB予算比、ring未読最大、上限。§1.1の「2 ch以下で160 Msps」に実測の状態を書ければ完了。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.1の対応表、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)。

## 結果

生ログ: `_runs/E113_20260915T112046JST_p4_direct/sweep.log`。usbipd/WSL直結、host URB 1 MiB×8。この日のUSB-only probeは380.6 Mbps（90%予算342.5 Mbps）。firmwareはE112にprofile `2`（2-bit幅、256 sample→64 byte、memcpyのみ、stage 27,136 byte）を足したもの。device側はchunk内でblock先頭のGray bit 0〜1がblockごと（64 step）に一致することを見る弱い検査、hostは全blockで2 bit/sampleのGray進行（4 sampleごとに1 step、blockで62〜66 step）を検査する。

| rate | wire | host実測 | 判定 | core 0 / core 1 busy | codec（memcpy）core 0 / 1 | ring未読最大 |
|---:|---:|---:|---|---:|---:|---:|
| 120 | 240 Mbps | 248〜250 Mbps | PASS 2回 | 22.5% / 15.2% | 15.1% / 14.4% | 4,032 |
| 140 | 280 | 289 | PASS | 25.6 / 17.6 | 17.6 / 16.7 | 4,032 |
| **160** | **320** | 329〜336 | **PASS 10回**（初回1回はhost側のanchor bug、後述）＋**30 s soak 1.2 GB欠損0**（標本37 MiB検査、標本落ち107） | 30.0 / 20.0 | 20.9 / 19.0 | 4,032（soak 4,800） |

PARLIO RXの160 MHz（`PLL_F160M`÷1）・2-bit幅は成立し、raw 40 MB/sのDMAとISR（約10 k/s）、64 byte blockのmemcpyはcore 0を30%しか使わない。USBは320 Mbpsで予算342 Mbpsの93%、実測380 Mbpsの84%。退避は一度も動いていない。

### hostのbug

初回の160 Msps runは`sequence_bad`が全blockで立った（1,310,720 block×2）。例を見ると`fast=1100000000111133332222222233331111…`で、これはGray bit 0〜1の正しい列（0×8、1×4、3×4、2×8、3×4、1×4、0×8…の繰り返し）である。decoderが最初のblockのanchor（binary値）を`fast[0]`そのものにしていたため、可視2 bitが同じでも別のbinary値から始まる位相では最初の遷移で外れ、失敗後のre-anchorも同じ規則だったので全blockが外れた。Gray下位2 bitは周期8なので、anchorを`fast[0]`に一致する8候補から「blockを最後まで歩ける値」で選ぶよう直し、合成data（開始値16通り×位相4通り）で不一致0、1 byte化けは検出、を確認した。以後の4回（r9〜r11、120）も欠損0。初回runのdevice側counterは全て0で、data自体は正しかった。

### 検査の限界

可視bitが2本なので、「4 stepの倍数ぶんのblock欠落・重複」は進行検査では見えない（自己試験で64 byte block 1個を落としても不一致0）。欠落はhostの受信byte総数（`bytes=`と`checked_blocks=`が計画値と一致）とdevice側counter（queue overflow、退避overflow、arm失敗）で判定している。

## 事実 / 候補 / 未決

**事実**

1. **2 ch以下で160 Mspsは成立**（PARLIO 2-bit幅×160 MHz、素通し320 Mbps）。10回＋30 s soakで欠損0、USB予算の93%、CPUはcore 0 30% / core 1 20%。
2. 律速はUSB予算。線上320 Mbpsは実測380 Mbpsの84%で、300 Mbpsの環境では9割規則に入らない（そこでは2 ch 120〜140 Msps）。
3. 2 bit可視の進行検査はblock単位の欠落を見られない。

**候補**

- §1.1の「2 channel以下なら160 Mspsで取得できる場合もある」は「USBが350 Mbps級の環境なら」の条件付きで実測済みにできる。
- 2 ch素通しにもcodec段を残すなら、sequence番号か時刻印をstage先頭に付けてblock欠落を可視化する（Phase Bの正しさ項目と同じ）。

**未決**

- 外部信号源での160 MHz入力（pin skew、閾値）。内部loopbackはTX 40 MHz出力を同じchipで受けている。
- 1-bit幅×160 Msps（1 ch）、4-bit幅（4 ch 80 Msps＝320 Mbps）は未測だが同じ経路で測れる。
