# E094 OUT payloadの全word検査は33.3 MB/sの天井を作っているか

状態: **完了 — pattern検査は天井原因でない**(2026-09-13、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E093](../e093_p4_vendor_out_arm_scaling/README.ja.md)

## 問い

**E091 peerが受信payloadを32 bitごとに`0xafafafaf`と比較する処理を外すと、16 KiB armの33.288 MB/sはDL-165の38.2 MB/sへ近づくか。**

## 仮説

上がる可能性がある。33 MB/sでは毎秒約825万wordを比較しており、DL-165にはこのsoftware検査がない。USB条件とFIFO copyを固定し、検査だけを外せば寄与を測れる。

## 反証条件

全word検査なしでも中央値が33.3 MB/s付近なら、checkerは天井原因ではない。

## 方法

- E093の16 KiB armを固定。host ZLP、sweep、4 MiB payload、byte数ACKも同じ
- Aは全word検査あり(E093 RX16384)、Bは同じ`Vendor.read()`とbyte countを行うがpattern比較だけ省略
- Bを3 run。host完了byte数とdevice受信byte数は必ず一致させる

## 対象外

- software FIFO copyそのものの除去。checkerでなければ次の実験に分ける
- 破損検査を外した構成の製品採用

## 必要な環境

P4 2枚OTG HS直結、E093から継続。

## ベンチ種別

一時

## 記録する数値

各run最大MB/s、host/device byte数、errors、min/median/max。

## 完了条件

checkerなしを3回取り、E093との差を確定する。

## 影響

38.2との差が測定用application負荷か、TinyUSB/DWC2/FIFO経路かを分ける。

## 結果

生ログ: `_runs/E094_20260913T105853Z_esp32p4_host/`。検査なしの各run最大は33.288 / 33.026 / 33.288 MB/s（中央値 **33.288**）、全条件PASS。E093の検査ありと同じで、pattern比較はbackpressureを作っていない。
