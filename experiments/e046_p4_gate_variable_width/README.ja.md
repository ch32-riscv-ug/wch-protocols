# E046 ESP32-P4 幅が可変なgateでwindowを復元する

状態: **完了 — 可変幅でも境界を完全復元**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E045](../e045_p4_gate_rmt_order/README.ja.md)・[E043](../e043_p4_parlio_gate_window_boundary/README.ja.md)

## 問い

**gate幅が1 loopの中で変わる信号に対して、RMTが各windowの長さを個別に正しく返し、その長さからPARLIO側のsample列の境界位置を予測できるか。**

## 仮説

[E045](../e045_p4_gate_rmt_order/README.ja.md)はgate幅を固定して測ったので、RMTが返した8,176 tickという値は「gate幅から計算した期待値と一致した」ことしか示していない。実際のCSやenable線は幅が一定でない。RMTはsymbolごとにlevelとdurationを独立に記録する構造なので、幅が変わっても各windowの長さをそのまま返すはずである。

成立すれば、RMTのhigh duration列からPARLIO側の境界byte offsetを積算で求められる。1 tick = 1 sampleかつdata_width 4は2 sample/byteなので、`window byte長 = high duration ÷ 2`である。この予測位置とcapture data中のgray stepの飛びの位置が一致すれば、**二つのhardware経路を突き合わせて時間軸を再構成できる**ことの直接の証拠になる。

信号源は1 loop 16,384 wordの中に幅の違う4つのwindowを置く。

| window | 開始index | 幅(word) | 期待high(tick) | 期待window(byte) |
|---:|---:|---:|---:|---:|
| 0 | 1,000 | 300 | 1,200 | 600 |
| 1 | 3,000 | 700 | 2,800 | 1,400 |
| 2 | 6,000 | 1,500 | 6,000 | 3,000 |
| 3 | 10,000 | 2,044 | 8,176 | 4,088 |

gapは1,700 / 2,300 / 2,500 / 5,340 wordで、tickにすると6,800 / 9,200 / 10,000 / 21,360である。いずれもRMT symbolのduration上限32,767 tickに収まり、`signal_range_max_ns` 1.6 ms(32,000 tick)より短いのでidleと誤判定されない。

各gapのindex差(最後に取れたwordから次に取れたwordまで)は1,701 / 2,301 / 2,501 / 5,341で、16で割った余りが5 / 13 / 5 / 13である。どれも1でないので、gray4 rampの飛びとして全境界が現れる。

duty合計は4,544 / 16,384 = 27.7%なので、1 loopあたりのPARLIO保存量は9,088 byteになる。

## 反証条件

- RMTのhigh durationが4つの期待値の循環にならない、またはばらつく
- RMTのlow durationが期待gapと一致しない
- gray stepの飛びの位置が、RMT high durationから積算した予測位置と一致しない
- PARLIOの回収量がduty 27.7%から外れる
- 幅の違うwindowのどれかでwindowがbyte境界で終わらない

## 方法

[E045](../e045_p4_gate_rmt_order/README.ja.md)で成立した構成をそのまま使い、gateのpatternだけを可変幅にする。

- RX: data_width 4、`data_gpio_nums[0..3]` = GPIO 2〜5、`valid_gpio_num` = GPIO 6、`valid_sig_line_id` = 4、20 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`、ringは64 KiB internal RAM
- RMT RX: `gpio_num` = GPIO 6、`resolution_hz` = 20,000,000、`mem_block_symbols` = 48、`signal_range_min_ns` = 500、`signal_range_max_ns` = 1,600,000、`en_partial_rx=true`、buffer 32 symbol
- TX: data_width 8、GPIO 2〜9、5 MHz、16,384 wordを`loop_transmission`。bit 0〜3は`gray4(index & 0xF)`、bit 4は上表の4区間だけhigh
- RMTはPARLIO TXの後に作る(E045で順序は無関係と分かっているので、片方に固定する)
- 回収は150 msの固定window。chunkは256 KiB PSRAMへ連結する

記録するのは次の三つで、突き合わせはhost側で行う。

- 先頭16 symbolのlevelとduration
- capture data中の先頭16個のgray step飛びのbyte offset
- 回収byte数と回収rate

host側では、RMTのhigh duration列から`÷2`でwindow byte長を求め、飛びのoffsetの階差がその循環列と一致するかを判定する。回収開始位置はwindowの途中でありうるので、階差は2個目の飛び以降で比較する。

APIの不成立も結果として最後まで記録する。

## 対象外

32,767 tickを超えるgapの扱い、RMT分解能を落としたときの精度、data線とvalid線とRMTの3者同時共有、gating時の最大sample rate、data_width 8 / 16、window単位でhostへ渡すformatの設計、gate幅がrun中に変わる(loopごとに違う)patternの検証。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

各API結果、RMT callback数、退避symbol数、先頭16 symbolのlevel / duration、high durationの一致数、low durationの一致数、飛びの総数、先頭16個の飛びoffset、階差が期待window長と一致した数、回収byte、回収rate、期待duty、経過us。

## 完了条件

幅の違う4つのwindowについて、RMTが返したdurationと、それから予測した境界位置がcapture dataの飛びと一致するかを確定する。一致しない場合はどのwindowでずれたかを特定して完了とする。

## 影響

一致すれば、[E045](../e045_p4_gate_rmt_order/README.ja.md)の未決の筆頭が解け、capture qualificationは幅が任意のgateに対して「間引いたsample列 + window長の列」で時間軸を保てることが確定する。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)の内部圧縮節がqualificationを実装候補として確定できる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E046_20260909T085028Z_default/test_gate_variable_width/dut.log`

RMTが返したsymbol列(先頭16):

```
0:6800 1:2800 0:9200 1:6000 0:10000 1:8176 0:21360 1:1200  ← 1周
0:6800 1:2800 0:9200 1:6000 0:10000 1:8176 0:21360 1:1200  ← 2周目、同一
```

high durationは1,200 / 2,800 / 6,000 / 8,176の4値だけが現れ、期待値と完全に一致した(min 1,200、max 8,176)。low durationも6,800 / 9,200 / 10,000 / 21,360の4値で、期待gapと一致した。**幅の違う4つのwindowを、RMTはそれぞれ正しい長さで個別に返す。**

capture data中のgray step飛びのbyte offset(先頭16):

```
1400 4400 8488 9088 10488 13488 17576 18176 19576 22576 26664 27264 28664 31664 35752 36352
```

階差を取ると次のようになる。

```
3000 4088 600 1400 | 3000 4088 600 1400 | 3000 4088 600 1400 | 3000 4088 600
```

**RMTのhigh durationから予測したwindow byte長の循環列と、階差が完全に一致した。** data_width 4は2 sample/byteなので`window byte長 = high duration ÷ 2`であり、8,176 → 4,088、6,000 → 3,000、2,800 → 1,400、1,200 → 600である。順序も一致しており、回収がwindow 1の先頭から始まったことまで読み取れる(最初の区間1,400 byteがwindow 1の長さ)。

| 項目 | 期待 | 実測 |
|---|---:|---:|
| gate合計 | 4,544 word | 4,544 |
| 1 loopあたり保存量 | 9,088 byte | 9,088(階差の1周分の和) |
| duty | 27.7% | 27% |
| 飛びの総数 | 262,144 ÷ 9,088 × 4 ≒ 115 | 114 |
| 未説明の階差 | 0 | **0 / 15** |

回収rate 1,726 KB/sは、256 KiBのdestinationが約94 msで埋まった後も回収windowが151.8 ms続いたためである。埋まるまでの実効値は262,144 ÷ 94 ms ≒ 2.79 MB/sで、duty 27.7%に対するraw 10 MB/sの値(2.77 MB/s)と一致する。

## 判定

**幅が可変なgateでも、間引いたsample列とRMTのduration列を突き合わせれば時間軸を完全に再構成できる。**

RMTはhighとlowを交互に返すので、保存された区間の長さと捨てられた区間の長さの両方が分かる。累積すれば各windowの絶対時刻位置まで求まる。予測位置とcapture dataの実際の切れ目が15箇所すべてで一致したので、二つのhardware経路の突き合わせは仮説ではなく実測で成立した。

これでcapture qualificationは実装候補として確定する。構成と得られるものは次のとおりである。

```
gate線(1 GPIO)
   ├─→ PARLIO RX valid : gate区間のsampleだけをDMAでPSRAMへ(duty分に間引き)
   └─→ RMT RX          : 各high / lowの長さをsample単位で記録(境界と時刻)
```

- **保存量と転送量がdutyに比例して減る。** 現ベンチのdownloadはCH343 UART上限に縛られているので、これが深度より効く
- **時間軸は失われない。** window長とgap長の両方がhardwareで記録される
- **CPUは介在しない。** PARLIOもRMTもDMAとISRだけで動く
- **capture channelは払わない。** gate線は[E041](../e041_p4_parlio_shared_valid_line/README.ja.md)の手法でdata線と共有できる見込み(3者同時共有は未実証)
- 払うのはRMT RX channel 1つと、1 levelあたり32,767 tickの上限である

## 事実・候補・未決

**事実**

1. 幅300 / 700 / 1,500 / 2,044 wordの4つのwindowに対し、RMTのhigh durationは1,200 / 2,800 / 6,000 / 8,176 tickの4値だけが循環して現れ、期待値と完全に一致した。
2. low durationも6,800 / 9,200 / 10,000 / 21,360 tickで期待gapと一致した。**保存区間と捨てた区間の両方が分かる。**
3. capture data中のgray step飛びの階差は3,000 / 4,088 / 600 / 1,400の循環で、**RMT high durationを2で割った値の列と完全に一致した。未説明の階差は0件(15 / 15一致)。**
4. 飛びの総数は114で、回収量から計算した期待値115とほぼ一致した。1 loopあたり保存量9,088 byte、duty 27%も期待どおりだった。
5. 回収rateの見かけの低さ(1,726 KB/s)はdestinationが途中で埋まったためで、埋まるまでは2.79 MB/s(duty相当の2.77 MB/sと一致)だった。

**候補**: qualificationをhardware 2段構成で実装する。hostへは「間引いたsample列」と「RMTのlevel / duration列」を組で渡し、host側でwindowへ切って時間軸を復元する。gate線はdata線と共有してcapture channelを払わない。

**未決**: data線・valid線・RMT RXの3者同時共有 / gating時の最大sample rate / 32,767 tickを超えるgapの扱い / RMT symbolとPARLIO sample列の先頭同期をrun中に確立する方法(本実験は階差の循環から事後に読み取った) / RMT分解能を落としたときの精度 / data_width 8 / 16でのqualification。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): 内部圧縮節のqualificationを実装候補として確定し、可変幅でも成立することを書く
- [LEDGER](../LEDGER.ja.md): E046の節
