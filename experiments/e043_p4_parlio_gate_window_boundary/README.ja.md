# E043 ESP32-P4 hardware gate windowの境界は復元できるか

状態: **完了 — window長は決定論的。境界は自己記述されない**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E040](../e040_p4_parlio_level_open_frame/README.ja.md)・[E039](../e039_p4_parlio_level_gate/README.ja.md)

## 問い

**hardware gateで間引かれたstreamから、各gate windowの境界と長さを復元できるか。callback境界はwindowに対応するか。windowは常にbyte境界で終わるか。**

## 仮説

[E040](../e040_p4_parlio_level_open_frame/README.ja.md)で、level delimiterのgateがactiveな区間のsampleだけがDMAへ渡ることが分かった。保存量がgateのdutyに比例して減るので、CPU負荷ゼロのcapture qualificationとして使える。現ベンチのdownload経路はCH343 UART bridgeに限られ良くても約600 KB/sなので、この間引きは深度を伸ばすより効く。

しかし間引かれたdataは連結されて渡るため、**どこからどこまでが1つのgate windowだったのかがそのままでは分からない**。protocol解析では「CSがassertされていた区間」の切れ目が必要になるので、境界が復元できなければqualificationの用途は大きく狭まる。

復元の手がかりは二つ考えられる。

1. **callback境界** — E040ではgate 16回に対しcallbackが20回だった。driverのdescriptor境界(`max_recv_size`を4,032 byte単位で分割)とgate境界が一致しないためと解釈したが、確認していない。もしcallbackがwindowごとに切れるなら`recv_bytes`がそのままwindow長になる
2. **dataの不連続** — 信号源のrampがwindow間で飛ぶので、飛びを検出できれば境界が分かる

2を使えるようにするため、gate幅をrampの周期の倍数から外す。gray4の周期は16 wordなので、gate幅を2,044 word(16の倍数でない)にすると、window末尾から次のwindow先頭へのindex差は`(1 - 2044) mod 16` = 5になり、+1 stepではないstepとして必ず現れる。E040のgate幅2,048 wordは16の倍数だったため、飛びがrampの周期と一致して見えなかった。

もう一つ確かめることがある。gate開放・閉止の位置は1 sample程度ずれる(E037・E039)。data_width 4では1 byteに2 sampleが入るので、**windowのsample数が奇数になると以降のnibble整列がずれる**。windowが常に偶数sampleで終わるのかは、width 8未満でqualificationを使えるかを決める。

## 反証条件

- callbackの`recv_bytes`がwindow長と対応しない、かつdataの飛びも検出できない(= 境界を復元する手段が無い)
- 検出した飛びの位置がwindow長の倍数からずれる
- windowのsample数が奇数になりnibble整列がずれる
- gate幅を変えてもwindow長が変わらない

## 方法

RX rateは20 MHzに固定する。問いはrateではなく境界の復元性だからである。

- RX: data_width 4、`data_gpio_nums[0..3]` = GPIO 2〜5、`valid_gpio_num` = GPIO 6、`valid_sig_line_id` = 4
- delimiter: level delimiter、active high、`eof_data_len` = 0、`timeout_ticks` = 0
- 受信は`partial_rx_en=true`、ringは64 KiB internal RAM
- TX: data_width 8、GPIO 2〜9、5 MHz、16,384 wordを`loop_transmission`
  - bit 0〜3: `gray4(index & 0xF)`
  - bit 4: `kGateIndex`から`kGateWords`だけhigh
- chunkは回収しながら256 KiB PSRAMへ連結する。回収は50 msの固定windowで打ち切る

case:

| # | gate幅 | 期待window長 |
|---:|---:|---:|
| 1 | 2,044 word | 4,088 byte(8,176 sample) |
| 2 | 1,020 word | 2,040 byte(4,080 sample) |

gate幅 × sample分周比4 ÷ 2 sample/byteが期待window長である。どちらも16の倍数でないので、window境界はgray stepの飛びとして現れる。

記録するのは次の三つである。

- 先頭24 callbackの`recv_bytes`の列
- gray stepの飛びの総数と、先頭8個の位置(連結後のbyte offset)
- 飛びの位置が期待window長の倍数と一致するか

APIの不成立も結果として最後まで記録する。

## 対象外

gating時の最大sample rate、gate幅の細かい掃引、gate開放・閉止のedgeに対する絶対的なsample精度(ここで測るのはwindow長の一貫性まで)、data_width 8 / 16でのqualification、PSRAMへの直接受信、windowごとのtimestamp取得、pulse delimiterとの併用。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、gate幅、期待window長、各API結果、callback数、回収byte数、queue overflow、先頭24 callbackの長さ、gray step飛びの総数、先頭8個の位置、期待倍数と一致した飛びの数、runs、run長min/max、回収rate、経過us。

## 完了条件

gate windowの境界を復元する手段があるか否かを確定する。callback境界とdataの飛びのどちらが使えるか、またwindowが常に偶数sampleで終わるかを記録する。

## 影響

境界が復元できれば、capture qualificationは「windowごとに切ったtrace」としてhostへ渡せる。できなければ「gate内のsampleを連結したもの」に留まり、時間軸の再構成ができないので用途がburstの内容確認だけに狭まる。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)の内部圧縮節にあるcapture qualificationの行の「調べる値」がここで埋まる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E043_20260909T082419Z_default/test_parlio_gate_window_boundary/dut.log`

| # | gate幅 | 期待window長 | callback数 | 回収byte | 飛びの数 | 期待倍数と一致した飛び | run長 | 回収rate |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 2,044 word | 4,088 byte | 17 | 65,536 | 16 | **16 / 16** | 4〜4 | 1,241 KB/s |
| 2 | 1,020 word | 2,040 byte | 8 | 32,256 | 15 | **15 / 15** | 4〜4 | 587 KB/s |

**window長は厳密に決定論的だった。** 検出したgray stepの飛びはすべて期待window長の整数倍の位置にあった。

```
case 1 (期待 4,088): 4088 8176 12264 16352 20440 24528 28616 32704   ← 4,088 × 1〜8
case 2 (期待 2,040): 2040 4080  6120  8160 10200 12240 14280 16320   ← 2,040 × 1〜8
```

16回(case 2は15回)連続して1 byteの狂いもない。gate幅 × sample分周比4 ÷ 2 sample/byteという計算値と完全に一致するので、**gate開放から閉止までに渡るsample数はgate幅から一意に決まる。** 長さのばらつきは0である。

**windowは常にbyte境界で終わった。** 期待window長4,088と2,040はどちらも整数byteで、飛びの位置がその整数倍に並んだことから、nibble整列のずれは一度も起きていない。data_width 4でもqualificationは使える。

**callback境界はwindowに対応しない。** 回収したchunk長は両caseとも4,032 byte固定で、gate幅を半分にしても変わらなかった。

```
case 1 chunk長: 4032 ×15, 2496, 2560
case 2 chunk長: 4032 ×8
```

4,032 byteはdriverがDMA descriptorを分割する単位である([E031](../e031_p4_parlio_channel_width/README.ja.md)以降と同じ値)。**callbackはdescriptor境界で起き、gate境界とは無関係である。** E040でgate 16回に対しcallbackが20回だったのは、両者が独立に並んでいたためだと確認できた。

run長は両caseとも4固定で、20 MHzではsample間隔の揺れが無いという[E042](../e042_p4_parlio_16ch_seq_verify/README.ja.md)の観測と一致する。

回収rateはduty比とほぼ一致した。case 1はduty 12.5%に対し1,241 KB/s(raw 10,000 KB/sの12.4%)、case 2はduty 6.2%に対し587 KB/s(5.9%)である。

## 判定

**window長はgate幅から一意に決まり、byte境界で終わる。しかし境界はcapture dataの中で自己記述されない。** 復元できるのは「gate幅が既知で一定のとき」に限られ、その場合は算術で切り分けられる。

これはqualificationの用途を分ける。

| gateの性質 | 境界の復元 | 使える用途 |
|---|---|---|
| 幅が既知で一定 | **算術で可能**(window長 = gate幅 × 分周比 ÷ sample/byte) | 周期的なframeの取得。window単位に切ってhostへ渡せる |
| 幅が可変(実際のCS等) | **不可** | burstの中身の確認だけ。時間軸の再構成はできない |

callback境界が使えないことが確定したので、可変長gateでは次のどちらかが必要になる。

- **gate edgeをsoftwareでtimestampする** — GPIO割り込みでgate線の両edgeを記録する。ただしE023〜E028が示したようにCPUは既に律速側なので、高いrateでは成り立たない
- **1 channelを自由走行する信号に割り当てる** — 本実験でgray rampが境界を見せたのは、rampの周期(16 word)がgap長を割り切らなかったからである。同じ原理で、captureする1 channelに周期の分かった自由走行信号(counterやclock)を入れておけば、**gapの長さをその周期を法として測れる。** gapが周期より短ければ一意に決まる

後者はqualifierとは別に1 channelを払うが、CPU負荷はゼロで、間引きの利得(duty分)を保ったまま時間軸を再構成できる。現ベンチのdownloadがCH343 UART上限に縛られていることを考えると、この組み合わせが実用的な候補になる。

## 事実・候補・未決

**事実**

1. gate幅2,044 / 1,020 wordに対し、検出したgray stepの飛びはすべて期待window長(4,088 / 2,040 byte)の整数倍の位置にあった。16回・15回連続で1 byteの狂いも無い。
2. 期待window長は`gate幅 × 分周比4 ÷ 2 sample/byte`と完全に一致した。**window長のばらつきは0。**
3. windowは常にbyte境界で終わり、nibble整列のずれは起きなかった。
4. **callbackの`recv_bytes`は両caseとも4,032 byte固定で、gate幅に依存しない。** driverのDMA descriptor分割単位であり、gate境界とは無関係である。E040のcallback数とgate数の不一致はこれで説明できる。
5. run長は両caseとも4固定。回収rateはduty比とほぼ一致した(12.4% / 5.9%)。

**候補**: qualificationのcapabilityを二つに分ける。gate幅が既知・一定なら算術でwindowへ切り分けてhostへ渡す。可変幅なら、captureする1 channelに周期既知の自由走行信号を入れてgap長を測る。callback境界は境界情報として使わない。

**未決**: 自由走行信号によるgap測定の実証 / gate edgeのsoftware timestampが成立するrate / gate幅が可変のときのwindow長の決定性(本実験は固定幅のみ) / gating時の最大sample rate / gate開放位置の絶対的なsample精度 / data_width 8 / 16でのqualification / windowごとのtimestampをhardwareで得る経路の有無。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): 内部圧縮節のcapture qualification行の「調べる値」を埋め、境界復元の条件を書く
- [LEDGER](../LEDGER.ja.md): E043の節
