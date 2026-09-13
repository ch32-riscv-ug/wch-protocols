# E093 vendor OUT armを16/32 KiBへ伸ばすとDL-165の38.2 MB/sへ近づくか

状態: **完了 — 16 KiBで飽和**(2026-09-13、各3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E092](../e092_p4_vendor_out_arm_size/README.ja.md)

## 問い

**EspUsbDeviceのbulk OUT受信armを8 KiBから16 / 32 KiBへ伸ばすと、同一P4 peerの30.84 MB/sはDisplayLink DL-165相手の38.2 MB/sへ近づくか。**

## なぜこの問いか

38.2 MB/sの相手はEspUsbHost `examples/Vendor/EspUsbHostDisplayDl1xx`で使ったDisplayLink DL-165 (`17e9:0360`) と確認できた。これは内部受信bufferを持つ実deviceである。E092のEspUsbDevice peerは8 KiBごとにDWC2完了割り込みからTinyUSB taskを経て再armする。残る7.36 MB/s差がこの往復回数なら、armを伸ばすほど近づく。

## 仮説

16 / 32 KiBで上がる。32 KiBなら4 MiBあたりのdata受信完了・再arm回数は8 KiBの512回から128回へ減り、DL-165との差の一部が消える。

## 反証条件

16 / 32 KiBの最大値が8 KiBの30.84 MB/sと同じ、または下がる。その場合、残差はsoftware再arm回数ではなくDWC2 OUTのbus schedulingまたはEspUsbDeviceのRX FIFO/application経路にある。

## 方法

- E092と同じP4 2枚・OTG HS直結・host ZLP有効・4 MiB payload・pattern照合
- device `CFG_TUD_VENDOR_RX_EPSIZE`だけを8192 / 16384 / 32768で比較。RX software FIFOは32768で固定
- host sweepはE092と同じdepth {1,2,4,8} × transfer {512,2048,8192,16384,32768}
- 新しい16 / 32 KiB条件を各3 run。8 KiBはE092の3 runを対照にする

## 対象外

- DL-165の再測定。現在のHS配線はP4同士で、過去の38.2 MB/sの相手銘板は文書とexampleから確定した
- library既定値の変更

## 必要な環境

E092から継続したP4 2枚のOTG HS直結。EspUsbDevice 2.3.0、EspUsbHost working tree。

## ベンチ種別

一時(E092から配線継続)

## 記録する数値

各arm長・runの最大MB/s、全sweep点のhost/device byte数とpattern error、min/median/max。

## 完了条件

16 / 32 KiBを各3回測り、arm長に対する帯域の傾きを確定する。

## 影響

38.2 MB/sとの差をdevice再arm回数で説明できるか決める。伸びるならprotocolが許す最大armが実デバイスとの差を縮める。伸びなければ残差をDWC2/bus側へ絞る。

## 結果

生ログ: `_runs/E093_20260913T105356Z_esp32p4_host/`。全条件で完全性PASS。各run最大は、8 KiB(E092) 30.840 / 30.840 / 30.943、16 KiB **33.288 / 33.288 / 33.288**、32 KiB 33.026 / 33.288 / 33.399 MB/s。**16 KiBで飽和し、再arm頻度で回収できるのは33.3 MB/sまで。**
