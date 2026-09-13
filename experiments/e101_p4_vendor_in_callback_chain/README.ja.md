# E101 direct TXを完了callbackから即時chainする

状態: **完了 — callback chainだけでは18.396 MB/s**(2026-09-14、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E100](../e100_p4_vendor_in_direct_tx/README.ja.md)

## 問い

**direct TXの次8 KiBをTX完了callbackから即時armすると、E100の18.09 MB/sからbuffered TXの25.575 MB/s以上へ戻るか。**

## 仮説

戻る。E100は完了ごとにsemaphore wakeとsketch task切替を挟むが、buffered TinyUSBは同じcallback内で次transferをrefillする。directのcopy削減を保ったまま後者と同じarm位置へ揃える。

## 反証条件

18 MB/s付近ならtask切替は原因でない。25.6 MB/s付近なら切替だけを回収しcopy削減は効かない。25.6を明確に超えればbuffered TX copyも天井へ寄与する。

## 方法

- E100と同じnon-buffered 8 KiB TX、pattern生成、E090 host sweep。[TX callback hook patch](espusbdevice-direct-tx-callback.patch)を使用
- 最初のtransferはstream command callback、以降はTX完了callbackで生成・submit
- 3 run、host byte数・ramp・errorsを照合

## 対象外

callback内producerの一般API化、二重endpoint buffer、pattern事前生成。

## 必要な環境 / ベンチ種別

P4 2枚OTG HS直結。一時。

## 記録する数値 / 完了条件

各run最大MB/s。E100 18.09 / E090 25.575 / E097 39.737と比較する。

## 影響

device → hostの差をTX再arm空白とbuffer copyへ分離する。

## 結果

生ログ: `_runs/E101_20260914T010400JST_esp32p4_host/`。3 runとも最大 **18.396 MB/s**、全run PASS。E100比+1.7%だけでtask wakeは主因でない。完了後のpattern生成＋internal memcpy＋cache cleanの直列仕事が残る。
