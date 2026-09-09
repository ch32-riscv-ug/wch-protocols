# E036 ESP32-P4 PARLIO rate境界のsample単位再検証

状態: **完了 — 律速はspool側。1 Mi burstは104 MHzまで成立**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E031](../e031_p4_parlio_channel_width/README.ja.md)・[E032](../e032_p4_parlio_width_rate_coarse/README.ja.md)・[E033](../e033_p4_parlio_width_rate_fine/README.ja.md)

## 問い

**PARLIO TXの連番rampを信号源にし、ring上の未読byte量でdropを直接検出してsample単位の連番検証を行うと、8 channel triggerなしbatchがdropなしで成立するRX rate境界はどこか。**

## 仮説

E031〜E033の信号源は100 kHzのLEDC PWM 8本で、firmware内の検証はlaneごとのdutyとedge数だけである。100 MHz samplingに対して1000倍のoversamplingなので、ring上のdataがcopy前に上書きされても、上書き後のdataは同じ定常波形であり、dutyとedge数はほぼ変わらない。つまりこの検証器はsample単位の欠落を検出できない。

firmware側の`overflows`は64段のFreeRTOS queueが満杯になったときだけ増える。ringは65,536 byte、1 transactionは`eof_data_len` 65,408 byte = 4,032 × 16 + 928の17 descriptorなので、未読byteが65,536を超えた時点でcopy前の上書きが起きる。queue 64段はring容量の約3.8倍あり、`overflows == 0`かつ`result == ESP_OK`でもsampleが失われている区間があるはずである。E033のhost側判定は`max_queue < 16`を条件に含めており、これはring容量のproxyとして機能していたが、16という値の根拠は記録されていない。

またE033採用runでは、8 channelの104〜120 MHzでcapture_usが10,670〜10,788 usとほぼ一定なのに、callback数は288 → 299 → 310 → 321 → 335と設定rateに比例して増えた。callback byteをcapture_usで割ると設定rateにほぼ一致する。したがってPARLIOのsampling clockは設定へ追従しており、約98 MB/sの飽和はring→PSRAM copy側の限界である。E032・E033のレポートは「設定rateへ追従できなかった」と書いており、律速の所在が入力側に読める。

連番rampをRX rateの1/4で送出すれば、各counter値は約4 sample連続で現れる。run崩し後のstepが+1 mod 256でない箇所を数えれば、欠落・重複・順序異常をsample単位で検出できる。chunk 1個(4,032 sample = 1,008 counter step)の欠落は256の倍数ではないので必ず違反として現れる。

## 反証条件

- 未読byteがring容量を超える条件でも連番違反が0件
- 健全なrate(20 / 80 MHz)で連番違反が出る
- PARLIO TXとRXを同一GPIO・同一groupで共存させられない
- sampling rate(callback byte由来)が設定rateへ追従しない
- TXのloop transmissionが連続rampにならない(run長が想定と大きく外れる)

## 方法

信号源をPARLIO TXへ置き換え、それ以外のcapture経路はE031と完全に同じにしてchunk分割を保ち、E032・E033と比較できるようにする。

- TX: `parlio_new_tx_unit`、data_width 8、GPIO 2〜9、256 byteの0..255 rampを`loop_transmission`で連続送出。`output_clk_freq_hz` = RX設定rate ÷ 4
- RX: data_width 8、同一GPIO、`io_loop_back=false`、64 KiB internal ring、`eof_data_len` 65,408、queue 64段、1,048,576 sampleを1 MiB PSRAMへ退避
- 生成順はE031と同じくRX unitを作ってから信号源を構成する
- RX rate掃引: 20 / 80 / 96 / 100 / 104 / 112 / 120 MHz
- drop検出: `callback_bytes - consumed_bytes`の最大値をring容量65,536 byteと比較する。ISRは単調増加のcounterだけを書き、taskは読むだけにして競合を避ける
- data検証: 1 MiB全域をrun崩しし、run間のstepが+1 mod 256でない箇所を数える

APIの不成立も結果として最後まで記録する。

### 計画の改訂(実装前 / 2026-09-09)

pattern を単純な binary ramp から **8-bit Gray code ramp** へ変える。TXとRXのclockは独立した分周器から作られ位相関係がないため、binary rampでは値の遷移時に複数bitが同時に変わり、遷移中のsampleがprevでもnextでもない第三の値になりうる。それをdropと区別できない。Gray codeなら隣接値は1 bitしか違わないので、遷移中のsampleは必ずprevかnextのどちらかになる。検証はrun崩し後の値をbinaryへ戻し、indexが+1 mod 256で進むことを条件にする。問いと反証条件は変えない。

## 対象外

16 channel、16本の独立pad、trigger、圧縮、hardware delimiter(pulse / level)、host download、external clock、ring・chunk sizeのtuning。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9。外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

RX設定rate、TX設定rate、各API結果、copied、callbacks、dequeues、max_queue、queue overflow、max_inflight_bytes、ring_bytes、ring_overrun、runs、run長min/max、seq_violations、最初の違反sample位置、capture_us、sampling rate(callback byte由来)、spool rate(copied由来)。

## 完了条件

各rateについてring未読量と連番違反を記録し、dropなし境界を確定する。ring超過と連番違反が一致しない場合もそのまま記録する。TXとRXの共存が成立しない場合は、どのAPIで外れたかを特定して完了とする。

## 影響

[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のraw rate行の根拠を、duty/edge検証からsample単位検証へ差し替える。以降の全実験の信号源とdrop判定の基準を決める。E032・E033の律速の帰属も見直す。

## 結果

実施日: 2026-09-09

採用run: `_runs/E036_20260909T025817Z_default/test_parlio_rate_seq_verify/dut.log`

| RX設定 | TX設定 | sampling | spool | inflight最大 | ring超過 | 連番違反 | 最初の違反 | run長 | capture_us |
|---:|---:|---:|---:|---:|:-:|---:|---:|---:|---:|
| 20 MHz | 5 MHz | 19.985 MB/s | 19.947 MB/s | 0 | — | 0 | — | 4〜4 | 52,567 |
| 80 MHz | 20 MHz | 79.702 MB/s | 79.552 MB/s | 4,032 | — | 0 | — | 3〜5 | 13,181 |
| 96 MHz | 24 MHz | 95.627 MB/s | 95.446 MB/s | 4,928 | — | 0 | — | 3〜5 | 10,986 |
| 100 MHz | 25 MHz | 99.643 MB/s | 97.951 MB/s | 20,160 | — | 0 | — | 3〜5 | 10,705 |
| 104 MHz | 26 MHz | 103.730 MB/s | 98.034 MB/s | 60,480 | — | 0 | — | 3〜7 | 10,696 |
| 112 MHz | 28 MHz | 111.811 MB/s | 98.236 MB/s | 142,912 | **超過** | 18 | 601,536 | 1〜5 | 10,674 |
| 120 MHz | 30 MHz | 119.560 MB/s | 97.099 MB/s | 244,608 | **超過** | 27 | 433,600 | 1〜5 | 10,799 |

全7条件でPARLIO TXとRXは同一group・同一GPIOで共存し、全APIが`ESP_OK`だった。`queue_overflow`は全条件で0、`result`も全条件で`ESP_OK`である。20 MHzではrunが厳密に262,144本、run長は4固定で、信号源と検証器がsample単位で一致した。

sampling rate(callback byte ÷ capture_us)は120 MHzまで設定の99.6%を維持した。一方spool rate(copied ÷ capture_us)は100 MHz以上で97.1〜98.2 MB/sに張り付いた。ring未読byteの最大値は設定rateとともに単調に増え、112 MHzでring 65,536 byteを超えた。

burst budget、つまりringが吸収できる時間からsample数を予測すると、違反の出た位置とほぼ一致した。

| RX設定 | sampling − spool | ring枯渇まで | 予測sample数 | 実測の最初の違反 |
|---:|---:|---:|---:|---:|
| 100 MHz | 1.692 MB/s | 38.7 ms | 約3.87 Mi | 1 Miでは違反なし |
| 104 MHz | 5.696 MB/s | 11.5 ms | 約1.20 Mi | 1 Miでは違反なし |
| 112 MHz | 13.575 MB/s | 4.83 ms | 約0.54 Mi | 0.57 Mi |
| 120 MHz | 22.461 MB/s | 2.92 ms | 約0.35 Mi | 0.41 Mi |

予測はやや小さめに出るが、ringが空から埋まる初期は差分が小さいためで、桁と順序は一致している。

方法上の記録を2点残す。dut.logの先頭には、host側がbannerを待つ前に一度triggerされたrunの後半(112 / 120 MHz)が入っている。同一boot内の2回目のtriggerがpytestの解析対象で、1回目の112 / 120 MHzはring超過1・違反14 / 35と、採用runの18 / 27に対して同じ結論を出した。またring超過条件ではrun長の最小が1になる。これは上書きされたdataが隣接しない値として現れるためで、違反数と合わせて壊れ方の指標になる。

## 判定

**8 channel triggerなしbatchの律速はsampling側ではなくring→PSRAM copy側であり、drop判定は「未読byteがring容量を超えたか」で決まる。1,048,576 sampleのburstでは104 MHzまでdropなしで成立し、112 MHzで失敗する。**

三つの独立した数値に分かれた。

1. **sampling上限** — 内部clock源はPLL_F160Mが最上位なので構造上160 MHzが天井である。8 channelでは少なくとも120 MHzまで設定どおりsamplingしている。E032・E033の「実効rateが設定へ追従しない」は入力側の限界ではない
2. **持続spool帯域** — 約98 MB/s。これを超えるrateは持続できない
3. **burst深度** — sampling超過分をringが吸収できる間だけ成立する。ring 64 KiBでは、104 MHzで約1.2 Mi sample、112 MHzで約0.54 Mi sampleが上限になる

したがって「8 channelの上限は100 MHz」という単一の値は成立しない。E033が100 MHzを成立、104 MHzを不成立としたのは、判定にend-to-endのspool rateが設定rateへ追従することを求めたためで、**104 MHzはdropなしで1 Mi sampleを取得できていた**。逆に持続captureでは100 MHzでも約3.87 Mi sampleで破綻するので、E030の16 MiB deep captureに100 MHzを適用してはならない。

E031〜E033の`overflows == 0`と`result == ESP_OK`は、112 / 120 MHzでsampleを失っている条件でも成立した。firmware側のdrop検出はring容量ではなく64段のqueueが満杯になる点を見ているため、ring容量の約3.8倍まで見逃す。host側判定の`max_queue < 16`はring容量のproxyとして実際に機能していたが、この実験のinflight_maxが直接の量である。

またdutyとedge数による検証は、112 / 120 MHzで連番違反が18 / 27件あってもE031と同じ形の検証を通過する。定常・周期的な信号源では上書き後も同じ波形が見えるためで、sample単位の検証には非周期または連番の信号源が必要である。

## 事実・候補・未決

**事実**

1. PARLIO TXとRXは同一group・同一GPIOで共存し、data_width 8のgray code rampを信号源にできた。20 MHzでrun 262,144本・run長4固定・違反0。
2. sampling rate(callback byte由来)は120 MHz設定で119.560 MB/s、設定の99.6%を維持した。spool rate(copied由来)は100 MHz以上で97.1〜98.2 MB/sに飽和した。
3. ring未読byteの最大値は100 / 104 / 112 / 120 MHzで20,160 / 60,480 / 142,912 / 244,608 byteだった。ring 65,536 byteを超えた112 / 120 MHzだけ連番違反が18 / 27件出た。104 MHzは違反0。
4. 112 / 120 MHzは連番違反があっても`result=ESP_OK`、`overflows=0`だった。
5. 最初の違反位置(0.57 / 0.41 Mi sample)は、sampling超過分でringが枯渇するまでのburst budget(0.54 / 0.35 Mi sample)と同じ桁で一致した。

**候補**: capabilityを「sampling上限」「持続spool帯域」「ring容量から決まるburst深度」の三つに分けて申告し、rate単独の上限値を出さない。drop判定はinflight最大 > ring容量とし、queue段数では判定しない。信号源はgray code rampを標準にする。

**未決**: 持続spool帯域のring・chunk・core配置による改善余地 / 16 channelの同条件再検証 / 深度を変えたときのburst境界の実測(現在は1 Miの1点とmodelだけ) / 104〜112 MHz間の1 Mi境界 / trigger・圧縮を加えたときのinflight。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): 限界matrixの8 channel行と「調べる機能」のrate記述を、sampling / spool / burstの三つに分ける
- [LEDGER](../LEDGER.ja.md): E036の節
- E032・E033のレポートは書き換えず、本実験からの追記で律速の帰属を訂正する
