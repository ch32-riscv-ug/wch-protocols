# E036 ESP32-P4 PARLIO rate境界のsample単位再検証

状態: **計画**

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
