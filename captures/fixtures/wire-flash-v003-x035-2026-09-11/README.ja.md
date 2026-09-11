# WCH-LinkE ↔ target 線上 capture（V003 SWIO / X035 RVSWD）

状態: **生データ収録済み・構造解析済み・一部フィールドの意味は未確定**（2026-09-11）

目的: [WCH-Link ↔ target](../../../protocols/link-to-target.ja.md) §3/§5 に残っている、実 target に対する SWIO/RVSWD の線上エンコードを検証する。

## 収録条件

Windows 版 `ch32rv 0.7.0` で、内容が既知の 4 KiB pattern を flash/verify した。同じ操作の USB 往復は `--capture` で記録し、WCH-LinkE と target の線は LA2016 + PulseView で同時間帯に収録した。

```console
ch32rv flash fixtures/pattern-4k.bin --probe serial:FC928F068181 --capture X035.json
ch32rv flash fixtures/pattern-4k.bin --probe serial:F90E8F067DFD --capture V003.json
```

| 項目 | V003 | X035 |
|---|---|---|
| target | CH32V003 系 | CH32X035 系 |
| probe | WCH-LinkE `F90E8F067DFD` | WCH-LinkE `FC928F068181` |
| probe firmware | USB `GetProbeInfo` 応答上 2.22 | USB `GetProbeInfo` 応答上 2.22 |
| debug 線 | 1 線 SWIO | 2 線 RVSWD |
| LA | LA2016, 50 MHz, digital | LA2016, 50 MHz, digital |
| channel | CH0 = SWIO, CH1 = 未使用（LOW） | CH0 = SWCLK, CH1 = SWDIO |
| 収録長 | 1.00000008 s | 0.99999084 s |
| 線上 activity | 0.09998968–0.96663024 s | 0.09998480–0.60703210 s |

target の厳密な package、電源電圧、pull-up 値、LA probe の電気条件は収録時に記録していない。`.sr` 内の sigrok メタデータは `sigrok 0.6.0-git-f06f788` を示す。

## ファイル

| ファイル | 内容 |
|---|---|
| [`wire-v003.sr`](wire-v003.sr) | V003 SWIO の PulseView session |
| [`usb-v003.ndjson`](usb-v003.ndjson) | V003 flash の USB 往復（121 transfers） |
| [`wire-x035.sr`](wire-x035.sr) | X035 RVSWD の PulseView session |
| [`usb-x035.ndjson`](usb-x035.ndjson) | X035 flash の USB 往復（64 transfers） |
| [`pattern-4k.bin`](pattern-4k.bin) | `00 01 … ff` を 16 回繰り返す既知の 4096 byte |
| [`SHA256SUMS`](SHA256SUMS) | 上記生データの SHA-256 |
| [`analyze.py`](analyze.py) | 以下の構造・timing・既知 pattern 一致を再計算する標準 library のみの script |

元の `.json` は JSON array ではなく 1 行 1 object の NDJSON なので、内容を変えず拡張子だけ `.ndjson` にした。`.sr` は PulseView で再オープンできる生セッション。

解析は外部 package 不要で再現できる。既知 pattern の 4096 byte 一致を `assert` し、一致しなければ静かに結果を出さず失敗する。

```console
python3 analyze.py
```

## 解析方法

- `.sr` の `logic-1-*` を session metadata の順で連結し、little-endian 16-bit sample として読む。計測分解能は 20 ns/sample。
- V003 は CH0 の LOW pulse 幅を 500 ns で 2 値化する。短 pulse = `1`、長 pulse = `0`。pulse 開始間隔が 4 µs を超えたところをフレーム候補の切れ目にする。
- X035 は CH0 の rising edge で CH1 を sample する。既知 pattern が 1024 words すべて一致する位置からフィールド境界を決めた。
- `00..ff` の反復は bit の 0/1、byte 順、word 境界を同時に含むため、単純な全 0/全 1 より強い検算ベクタになる。

## V003: SWIO 詳細

### 通常フレーム

10,288 個の 41-pulse フレームが次の形で復号できた。数値はすべて **MSB first**。

| pulse | bit 数 | 解釈 |
|---|---:|---|
| 0 | 1 | start = `1` |
| 1–7 | 7 | DMI address |
| 8 | 1 | R/W（read = `0`, write = `1`） |
| 9–40 | 32 | write data または target からの read data |

例として線上から `DMSTATUS(0x11)` read = `0x004f0382`、`DMCONTROL(0x10)` write = `0x80000001`、`DMDATA0(0x04)` の既知 pattern word を復号できる。これは E008 で仮定した `start + addr7 + R/W + data32` が実 WCH-LinkE ↔ V003 でも使われている実測根拠になる。

### pulse timing

41-pulse と確定したフレーム内の 411,520 pulse を対象にした。

| bit | n | LOW min | median | p99 | max | mean |
|---|---:|---:|---:|---:|---:|---:|
| `1` | 131,541 | 200 ns | **240 ns** | 340 ns | 380 ns | 248 ns |
| `0` | 279,979 | 780 ns | **860 ns** | 880 ns | 880 ns | 849 ns |

2 群の間に 400 ns の空きがあり、500 ns 閾値で全対象 pulse を重なり無く分離できる。先行実装から転記した 290/890 ns より、この LinkE の `1` は約 40–50 ns 短い。

R/W bit 終了から data 先頭の LOW 開始までは、write で 360–380 ns（中央値 380 ns）、read で 740–780 ns（中央値 760 ns）。**read のみ約 380 ns 長い**ため、pulse 8 と 9 の間が LinkE から target への方向切替/turnaround であることを timing からも支持する。

### flash write / readback

| phase | 線上時刻 | 構造 | 結果 | 線上実効値 |
|---|---|---|---|---:|
| write 1 | 0.44312942–0.48323460 s | 256 × `DMDATA0` write | 1 KiB 一致 | 24.93 KiB/s |
| write 2 | 0.53155652–0.57132188 s | 同上 | 1 KiB 一致 | 25.15 KiB/s |
| write 3 | 0.61957722–0.65922576 s | 同上 | 1 KiB 一致 | 25.22 KiB/s |
| write 4 | 0.70750554–0.74731802 s | 同上 | 1 KiB 一致 | 25.12 KiB/s |
| readback | 0.88819538–0.94986538 s | 1024 words | **4 KiB 完全一致** | 64.86 KiB/s |

readback は 64-byte（16 words）ごとに、毎回正確に次の配列になる。

```text
F s s s s s s s s s s s s s s F
```

- `F` = 41 pulse の full frame。`start1 + addr7(0x04) + read0 + data32`
- `s` = 33 pulse の short response。先頭 `0` + data32
- 64 block の合計は full 128 + short 896 = 1024 words

したがって LinkE の V003 fast-read は、最初と最後の word だけ address/RW 付きの full frame にし、中間 14 words は 33 pulse に短縮している。short response 先頭の `0` の意味は未確定。

## X035: RVSWD 詳細

### 52-bit short packet + termination clock

CH0 rising edge で CH1 を読むと、既知 pattern の 1024 write words は次の境界ですべて一致する。address/data は **MSB first**。

START（DIO falling while CLK high）からSTOP（DIO rising while CLK high）までには物理的に53 rising edgesある。最初の52 bitは[`sigrok-rvswd`](https://github.com/perigoso/sigrok-rvswd/blob/5d2e1d5ba1e10e70fdef293bdcf3b7d6c976f8af/pd.py#L242-L275)と[`SaleaeRVSWDAnalyzer`](https://github.com/bmx/SaleaeRVSWDAnalyzer/blob/8e1041b39e79e48113cb12ed86dabbaf29cf46ce/source/RVSWDAnalyzer.cpp#L184-L237)のshort packet境界に一致し、53番目は常に`0`で直後にSTOPとなる。ここでは**52-bit packet + 1 termination clock**として区別する。

| rising edge index | bit 数 | 実測と既存decoderからの解釈 |
|---|---:|---|
| 0–6 | 7 | DMI address |
| 7 | 1 | R/W（read = `0`, write = `1`） |
| 8 | 1 | address + R/W のeven parity。8,628 packetすべて一致 |
| 9 | 1 | park/don't-care。実測ではbit 8と同値（8,628/8,628） |
| 10–13 | 4 | host padding/don't-care。`0000` × 8,358、`0100` × 270 |
| 14–45 | 32 | write/read data |
| 46 | 1 | data32 のeven parity。8,628 packetすべて一致 |
| 47 | 1 | park/don't-care。実測ではbit 46と8,613/8,628一致 |
| 48–49 | 2 | target status。`00` × 8,560、`01` × 68 |
| 50–51 | 2 | target padding/don't-care = `11`。8,628 packetすべて一致 |
| 52 | — | termination clock。sample値は常に`0`、直後にSTOP |

4 KiB pattern write の 1024 packets では、`addr=0x04, R/W=1`、header後半6 bit=`000000`、data後半6 bit=`000011`、STOP sample=`0`がすべて一致した。data32 は `0x03020100, 0x07060504, …` と1024 words全体が一致する。この境界は偶然に合うのではなく、実装に使える強さで確定できる。

書込み frame 内の clock period は中央値 400 ns（約 2.5 MHz）。payload 部はおもに 380–400 ns 周期だが、aux/trailer 部に 0.7–1.3 µs の長い周期が混ざる。フレーム全体を単一の「クロック周波数」で表すのは不正確である。

### 585-clock bulk-read burst

4 KiB readback には 64 個の 585-clock burst がある。1 burst の境界は次で確定できた。

```text
[addr7 + R/W + aux6]
  + data32[0]
  + 14 × [aux6 + data32[n]]
  + trailer7
= 14 + 32 + 14 × 38 + 7
= 585 clocks / 15 words
```

その後に `addr=0x04, R/W=0` の52-bit packet + termination clockで16 word目が返る。これが64回繰り返され、`64 × (15 + 1) = 1024 words = 4096 bytes` が既知 pattern と完全一致した。

| 領域 | 観測値 |
|---|---|
| burst header 14 bit | `00001000110100` × 46 / `00001000110000` × 18（先頭 8 bit は両方 `addr=0x04, read=0`） |
| word 間 aux6 | `000000` × 853 / `000100` × 42 / `000001` × 1 |
| burst trailer7 | `0000110` × 61 / `0001110` × 3 |
| burst 内 clock period | median 1.10 µs（約 0.91 MHz）, p99 1.22 µs |
| 最後の 1 word frame | median 2.08 µs（約 0.48 MHz） |

585-edge burstも、`14-bit header + 15 × data32 + 14 × 6-bit feedback + 6-bit final feedback + termination clock` とすると過不足なく説明できる。6-bit feedbackを`data parity1 + park1 + status2 + padding2`と読む配置は既存decoder/実装と一致する。

statusは通常packetとburstの両方で`00`または`01`だった。[`ch32-tapioca-probe`](https://github.com/pierrejay/ch32-tapioca-probe/blob/084110c4c02b099ac4e0181ea0c56585789d0d82/src/wchlink/rvswd_frame.hpp#L116-L147)はWCH-LinkE→V307 captureとX035/V203/V307実機試験を根拠に、`0/1=OK, 2=fail, 3=busy`としている。今回のflash成功とも矛盾しないが、`2/3`は今回のcaptureには現れていない。

### 既存実装との照合

| 実装 | 確認したrevision | 一致する点 | 相違・注意点 |
|---|---|---|---|
| [`sigrok-rvswd`](https://github.com/perigoso/sigrok-rvswd) | [`5d2e1d5`](https://github.com/perigoso/sigrok-rvswd/commit/5d2e1d5ba1e10e70fdef293bdcf3b7d6c976f8af) | 52-bit short / 84-bit long、START/STOPを実装。shortのdata境界14–45とtail境界が実測に一致 | 585-edge bulk burstには非対応。parity/status値の検証はしない |
| [`SaleaeRVSWDAnalyzer`](https://github.com/bmx/SaleaeRVSWDAnalyzer) | [`8e1041b`](https://github.com/bmx/SaleaeRVSWDAnalyzer/commit/8e1041b39e79e48113cb12ed86dabbaf29cf46ce) | 独立したSaleae decoderも52/84 bitを認識し、shortのaddr/op/parity/data位置が一致 | short bit 47以降を`extra`として扱い、bulk burst非対応 |
| [`ch32-tapioca-probe`](https://github.com/pierrejay/ch32-tapioca-probe) | [`084110c`](https://github.com/pierrejay/ch32-tapioca-probe/commit/084110c4c02b099ac4e0181ea0c56585789d0d82) | WCH-LinkE→V307 captureから52-bit layout、read turnaround @14、write turnaround @48、statusを導出。X035/V203/V307で実機試験済み | 84-bit longも観測するがdirect-DMIにはshortを使用。bulk burstは実装しない |
| [`pico-rvswd`](https://github.com/i-infra/pico-rvswd) | [`4cd07ff`](https://github.com/i-infra/pico-rvswd/commit/4cd07ffbc35e4750a60a9402b16af929dee3efef) | X035で52-bit frameを実機試験。400 kHz–4 MHz、5,000–20,000 readsでparity failure 0 | park/paddingを`10101`/`10111`として駆動。LinkE実測値とは違うがX035が受理するためdon’t-careの根拠になる |
| [`esp32-component-rvswd`](https://github.com/Nicolai-Electronics/esp32-component-rvswd) | [`889892e`](https://github.com/Nicolai-Electronics/esp32-component-rvswd/commit/889892e94885652077c50698c2222a4dd57582a1) | 52 clock、MSB first、rising-edge sample、data/parity位置が一致 | `10101`/`10111`を使用。README上の検証対象はCH32V203 |
| [`RVSWD_pico`](https://github.com/ImproperCatGirl/RVSWD_pico) | [`2d9fa7f`](https://github.com/ImproperCatGirl/RVSWD_pico/commit/2d9fa7f3ae1f9939e5a3e566d3365cc17910b260) | RP2040 PIOでも同じ52-clock構造 | ESP32実装由来の`10101`/`10111`を継承 |
| [`rvswdog`](https://github.com/coocoscoocos/rvswdog) | [`3e816ee`](https://github.com/coocoscoocos/rvswdog/commit/3e816eeca870352e491023d5d0c20a289cea9eed) | STM8 bit-bangでも同じ52-clock/固定marker実装 | target機種・end-to-end検証範囲がREADMEに明記されていない |

したがって、52-bit short packetの**長さ・data位置・turnaround・parity・status位置**は複数decoder、複数probe実装、V307/X035実測で強く支持される。`10101`/`10111`とLinkE実測値の差は必須variantではなく、park/paddingというdon’t-care値の選択差と考えるのが最も整合的である。実装では既に複数targetで動作実績のある固定値を送れるが、decoderはそれらを厳密な同期語として検証すべきではない。

### flash write / readback

| phase | 線上時刻 | 構造 | 結果 | 線上実効値 |
|---|---|---|---|---:|
| write | 0.34054218–0.44258282 s | 1024 ×（52-bit packet + termination clock） | **4 KiB 完全一致** | 39.20 KiB/s |
| readback | 0.52493602–0.59536728 s | 64 ×（15-word burst + 1-word packet） | **4 KiB 完全一致** | 56.79 KiB/s |

## USB DmiOp と線上 DMI の対応

USB capture の末尾には両 target とも次の 3 操作がある。

| # | USB request | USB response（data） |
|---|---|---|
| 1 | `DMSTATUS(0x11)` read | V003 `0x004f0382` / X035 `0x00030382` |
| 2 | `DMCONTROL(0x10) = 0x00000001` write | `0x00000001` |
| 3 | `DMSTATUS(0x11)` read | #1 と同じ |

同時間帯の線上では、両 target とも `DMCONTROL` に **`0x80000001` を 2 回**書いている。V003 ではさらに `ABSTRACTCS(0x16)` read が間に入る。少なくともこの操作について、USB `DmiOp` と線上 DMI は「同じ `(addr, data, op)` が必ず 1:1 で出る」とは言えない。LinkE firmware が `haltreq` bit を足し、再試行/内部 polling している候補が強い。

USB と LA に共通 marker が無いため、この対応は同時間帯の操作順・address・data 一致による。正確な 1:1 時刻対応と、上記変換がどの USB request の実装に属するかは、marker 付きの単独 `DmiOp` capture で追試が要る。

## 現行文書との差

現行 [link-to-target §3](../../../protocols/link-to-target.ja.md) の `42 bit host + 42 bit target` 説は、実 LinkE で観測した次の構造を説明できない。

- V003: 41-pulse full frame と 33-pulse short response
- X035: 52-bit short packet + termination clock と585-clock/15-word burst
- USB `DmiOp` が線上で値の変換・複数トランザクション化される例

84-bit long形式と52-bit short形式、さらにWCH-LinkEのbulk burstは分けて記述する必要がある。short packetのparity/status/turnaround位置は既存実装との照合で解決したが、585-edge burstの正式なcommand条件とtermination clockの役割は未決とする。

## 証拠として言える範囲

この capture で、実 WCH-LinkE ↔ 実 CH32 target の通信であることに加え、次を 4096 byte の既知 payload 全体で確認できる。

- SWIO の 0/1 pulse 幅、41-pulse full frame、read turnaround、33-pulse fast-read response
- RVSWD の rising-edge sample、52-bit short packet、parity/status/turnaround、585-clock/15-word burst
- 両方の write/read が既知 pattern と完全一致すること

自作送受信器だけで検算した E007/E008 より強い独立の実測根拠になる。

ただし、USB capture と LA に共通の hardware trigger/marker はない。USB ↔ 線上操作の対応は収録時間帯、操作順、address/data から合わせている。また、X035 bulk burstのcommand条件、termination clock、V003 short response先頭bitの意味は未確定なので、RVSWD/SWIO全体を一括して`verified`にはしない。

## commit 可否

**公開 repository へ commit 可能な内容。** flash payload は `00..ff` の反復であり、ユーザーの firmware、source code、credential、個人データは含まない。

ただし USB capture には **probe serial、target UUID、Windows の USB topology** という機材識別情報が含まれる。秘密情報ではないが、完全に匿名のデータでもない。ここでは実測の provenance として意図的に保存する。既存 fixture も probe serial/topology/target UUID を同様に保存している。
