# E035 ESP32-P4 ADC topology・channel順

状態: **計画**

規則: [実測の規則](../README.ja.md) / 先行実験: [E034](../e034_p4_adc1_continuous_batch/README.ja.md)

## 問い

**最大aggregate rateでADC resultのchannel IDはどの順序で現れ、ADC2単独およびADC1+ADC2のcontinuous modeをanalog traceとして利用できるか。**

## 仮説

E034の最大rateではsample数は均等だが単純な逐次順にならなかった。短い先頭ID列を保存すればhardwareの並びを特定できる。P4は2 ADC unitと`SINGLE_UNIT_2` / `ALTER_UNIT` / `BOTH_UNIT`を定義するため、ADC2と両unit構成もAPI・unit IDレベルでは成立する。

## 反証条件

- 同一条件のchannel ID列がrunごとに不規則に変化する
- ADC2単独をdriverが受け付けない、またはunit/channel IDが誤る
- `ALTER_UNIT` / `BOTH_UNIT`の両方が設定または取得に失敗する
- 取得成功時にconfigured set外のID、DMA overflow、channel数の偏りが生じる

## 方法

32,768 resultの短いbatchを使い、10,000および83,333 conversion/sで次を測る。

- ADC1: 2 / 4 / 8 channel。先頭64 resultのchannel ID列と遷移を記録する
- ADC2: 1 / 2 / 4 / 6 channel。単独continuous modeの成立を確認する
- 両unit: ADC1 ch0とADC2 ch0から始め、`ALTER_UNIT`と`BOTH_UNIT`を個別に試す

APIの不成立も結果として最後まで記録する。成立条件ではunit/channel別sample数、先頭ID列、実効rate、timeout、pool overflowを確認する。入力値は判定しない。

## 対象外

電圧精度、noise、ENOB、外部信号、digital同期、長時間取得。

## 必要な環境

E034と同じESP32-P4、stable port、Arduino-ESP32 3.3.11。外部配線不要。ベンチ種別: **一時・配線なし**。

## 完了条件

ADC1最大rate時のID配列規則を記録し、ADC2単独・両unit各modeの利用可否と出力IDを確定する。

## 影響

analog capabilityの最大channel数、unit構成、sample metadataとchannel demultiplex方法を決める。
