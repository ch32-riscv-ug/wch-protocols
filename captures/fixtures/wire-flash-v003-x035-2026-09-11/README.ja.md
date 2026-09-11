# WCH-LinkE ↔ target 線上 capture（V003 SWIO / X035 RVSWD）

状態: **生データ収録済み・予備解析**（2026-09-11）

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

元の `.json` は JSON array ではなく 1 行 1 object の NDJSON なので、内容を変えず拡張子だけ `.ndjson` にした。`.sr` は PulseView で再オープンできる生セッション。

## 予備解析

### V003: SWIO

- CH0 に 936,712 edges。CH1 は全期間 LOW。
- データ部の LOW pulse は、おもに短い群（中央値約 240 ns）と長い群（中央値約 860 ns）に分離する。50 MHz でそれぞれ約 12 sample / 43 sample。
- `start + addr7 + rw + data32` と解釈できる **41 pulse** 単位が反復している。[E008](../../../experiments/e008_wire_swio_frame/README.ja.md) の自作送信器で用いた形と整合する。
- flash 書込みの 1 KiB × 4 block と、連続する 4096 byte の readback 候補を識別できる。既知 pattern は完全一致する。
- USB 側には `DMSTATUS(0x11)` read → `DMCONTROL(0x10) = 1` write → `DMSTATUS` read の 3 DMI 往復があり、read 値は両方 `0x004f0382`。線側 decoder の既知ベクタに使える。

### X035: RVSWD

- CH0 に 558,123 rising clock edges。CH1 に data activity がある。
- 2 us より長い clock gap で分割すると、**53-clock 単位が 8,628 回**、**585-clock 単位が 64 回**観測される。予備デコードでは後者が 15 words、続く 53-clock 単位が最後の 1 word に対応し、64 × 16 words = 4 KiB の高速 read と整合する。
- 4096 byte の write/read 候補は既知 pattern と一致する。
- USB 側の同じ 3 DMI 往復で、`DMSTATUS` read 値は両方 `0x00030382`。
- 現在の `link-to-target` §3 にある「42 bit host + 42 bit target」だけでは、観測した 53/585 clock 単位をそのまま説明できない。start/stop、方向切替、parity/status、高速 burst の境界をさらに分離する必要がある。

## 証拠として言える範囲

この capture で、実 WCH-LinkE ↔ 実 CH32 target の通信であること、SWIO の 2 つの pulse 幅群、RVSWD の clock/data activity、既知 flash payload との対応は確認できる。自作送受信器だけで検算した E007/E008 より強い独立の実測根拠になる。

ただし、USB capture と LA に共通の hardware trigger/marker はない。対応は収録時間帯、操作順、既知 payload から合わせている。また、各 bit の方向、parity、status、STOP/turnaround を未検算のため、現時点で `link-to-target` §3 全体を `verified` には上げない。

## commit 可否

**公開 repository へ commit 可能な内容。** flash payload は `00..ff` の反復であり、ユーザーの firmware、source code、credential、個人データは含まない。

ただし USB capture には **probe serial、target UUID、Windows の USB topology** という機材識別情報が含まれる。秘密情報ではないが、完全に匿名のデータでもない。ここでは実測の provenance として意図的に保存する。既存 fixture も probe serial/topology/target UUID を同様に保存している。
