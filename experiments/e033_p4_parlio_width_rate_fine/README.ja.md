# E033 ESP32-P4 PARLIO 8/16 channel rate精密探索

状態: **計画**

規則: [実測の規則](../README.ja.md) / 先行実験: [E032](../e032_p4_parlio_width_rate_coarse/README.ja.md)

## 問い

**triggerなしbatch captureの安定境界は、8 channelの84〜120 MHz、16 channelの44〜80 MHzのどこか。**

## 仮説

E032では両幅とも約80 MB/sまで成立したため、channel数ではなくpacking後byte rateに対応し、8chと16chの境界byte rateは近くなる。

## 反証条件

- 成立/不成立がrate順に単調でない
- 8chと16chの境界byte rateが大きく異なる
- E032の成立点または不成立点を再現しない

## 方法

E032と同じ1,048,576 sample、PWM、PSRAM spool、成立条件を使う。8chは84〜120 MHz、16chは44〜80 MHzを4 MHz刻みで測る。

## 対象外

trigger、deep capture、外部pad、ring/chunk tuning。

## 必要な環境

32 MiB PSRAM搭載ESP32-P4、既存stable port、配線不要。ベンチ種別: **一時・配線なし**。

## 記録する数値

設定/実効sample rate、byte rate、queue最大、overflow、data検証。

## 完了条件

各幅で最大成立点と最初の不成立点を4 MHz幅で確定する。

## 影響

raw batch capabilityの上限と、後続trigger/圧縮試験の最大入力rateを決める。
