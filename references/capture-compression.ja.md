# capture を転送前に圧縮できるか — 周期的な信号の場合

状態: **設計メモ**(2026-09-13。実測と机上計算を分けて書く。**on-device 実装はまだ無い**)

「SPI clock のような周期性のあるものは転送で圧縮できるか」への回答。**結論から言うと、効くかどうかは信号の周波数ではなく oversampling 比で決まる**。

## 前提 — いま何が律速か

| | 実測 |
|---|---|
| capture(PARLIO → PSRAM) | 持続 約 98 MB/s([E036](../experiments/e036_p4_parlio_rate_seq_verify/README.ja.md)) |
| **download(OTG HS vendor bulk)** | **23.97 MB/s**([E084](../experiments/e084_p4_transfer_tuning/README.ja.md)) |
| 2 channel の連続 streaming 上限 | **90 Msps**(= 22.5 MB/s) |

**律速は USB 側**なので、**線に載せる前に減らせれば sample rate の上限がそのぶん上がる**。capture 側は 4 倍の余裕がある。

## 効き方は oversampling 比で決まる

packing は **2 bit/sample、4 sample で 1 byte**。RLE を「2 bit の値 + 6 bit の run 長 = 1 byte」(64 sample を超える run は byte を足す)という**いちばん安い形**で見積もると、**1 run = 1 byte 対 raw の (run 長 / 4) byte** なので、

```
RLE の圧縮率 ≈ K / 8        K = sample rate ÷ 信号周波数(oversampling 比)
```

**K = 8 が損益分岐点**である。合成波形(CLK + MOSI、MOSI は立ち下がりで変化)で確かめた。

| 信号 | **K** | RLE | deflate(参考) | |
|---|---:|---:|---:|---|
| SPI 20 MHz @ 80 Msps | 4 | **0.5x** | 6.3x | **RLE は 2 倍に膨らむ** |
| SPI 10 MHz @ 80 Msps | 8 | 1.0x | 10.6x | 損益分岐 |
| SPI 5 MHz @ 80 Msps | 16 | **2.0x** | 17.8x | |
| SPI 2 MHz @ 80 Msps | 40 | **5.0x** | 34.0x | |
| SPI 1 MHz @ 80 Msps | 80 | **10.0x** | 52.8x | |
| SPI 100 kHz @ 80 Msps | 800 | 14.3x(**頭打ち**) | 128.6x | 6 bit の run 長が上限 |

**実データでも同じ**。手元の 2 channel PWM 100 kHz @ 90 Msps(K = 900)の capture 8.4 MB で:

| | 圧縮率 |
|---|---:|
| RLE(6 bit 長) | **14.1x** |
| deflate | **257.6x** |

**14.3x で頭打ちなのは run 長を 6 bit にしたから**で、450 sample の run が 8 byte かかる。**長さの field を広げれば上がる**(その代わり短い run が高くつく)。

## だから SPI では

- **oversampling を 8 倍より浅くするなら、RLE は損**。edge の分解能のために 4 倍程度で回すことは普通にあるので、**「SPI だから圧縮できる」とは言えない**
- **16 倍以上なら素直に効く**。20 MHz の SPI を 320 Msps で見る、といった使い方はできない(源が 160 MHz)ので、**実際には「遅い SPI を高い rate で見る」ときに効く**
- **deflate は桁違いに強いが、on-device では現実的でない**。この PC で 138 MB/s、P4 でその数分の一しか出ないうえ、**CPU は harvest と送出で既に使っている**([E078](../experiments/e078_p4_continuous_stream/README.ja.md))

## 圧縮より先に、CS で gate するほうがよい

**PARLIO は level delimiter で「qualifier が active な区間だけ DMA へ渡す」ことができる**([E040](../experiments/e040_p4_parlio_level_open_frame/README.ja.md) で実測、**gate duty 12.5% に対し回収 byte rate は raw の 12%**)。

**SPI の CS はまさにその qualifier である。** 転送は burst で、間は idle なので、

| CS duty | RLE | **gate(実測ベース)** |
|---:|---:|---:|
| 50% | 1.9x | **2x** |
| 20% | 4.0x | **5x** |
| 5% | 9.1x | **20x** |

**gate のほうが強く、しかも CPU を 1 cycle も使わず、最悪時膨張が無い。** 代償は **qualifier 線を 1 本消費すること**と、**gate 外の情報が完全に失われること**。

## on-device 圧縮を入れるなら

**最悪時に膨らませないこと**が絶対条件である。logic analyzer は「落とさない」ことが仕事で、**K が小さい区間で RLE が 2 倍に膨らむと弾性 FIFO が溢れる**([E078](../experiments/e078_p4_continuous_stream/README.ja.md) の判定がそのまま効く)。

- **block 単位で「生」と「RLE」を選ぶ**(deflate の stored block と同じ考え)。これなら **raw + header 以上には絶対にならない**
- **圧縮器が line rate(22.5 MB/s 以上)で回るかは未測定。** table 駆動の RLE なら 1 byte あたり十数 cycle の予算があるので**可能性はある**が、**測るまでは分からない**
- **batch では話が別**。深さを稼ぐ用途なら、capture 後に PSRAM 上で圧縮すればよく、line rate の制約が無い

## まだ測っていないこと

- **on-device RLE の実 throughput**(P4 上で何 MB/s 回るか)
- **block 単位 store / RLE 切替の実装と、最悪時の挙動**
- **gate と RLE の併用**(CS で削ってから RLE をかける)
- **外部信号での K の実態**。ここは合成波形と内部 PWM だけで、**本物の SPI を見ていない**

## 参照

- [P4 logic analyzer 予備調査](p4-logic-analyzer-investigation.ja.md) — 内部圧縮の選択肢一覧と、hardware capture qualification の位置づけ
- [E040](../experiments/e040_p4_parlio_level_open_frame/README.ja.md) — level delimiter の gate が duty ぶんだけ削ることの実測
- [E084](../experiments/e084_p4_transfer_tuning/README.ja.md) — download 側の上限 23.97 MB/s
- [E078](../experiments/e078_p4_continuous_stream/README.ja.md) — 連続 streaming の釣り合い点と、溢れの判定
