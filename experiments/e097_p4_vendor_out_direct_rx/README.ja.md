# E097 TinyUSB direct RXで残る受信コピーの費用を測る

状態: **完了 — direct RXで39.7 MB/s**(2026-09-14、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E095](../e095_p4_vendor_out_data_zlp_cost/README.ja.md)、[E096](../e096_p4_vendor_out_measurement_length/README.ja.md)

## 問い

**TinyUSB vendor RXをbuffered FIFO経由から16 KiB endpoint buffer直渡しへ変えると、P4 peerのhost → deviceは34.95 MB/sからDL-165の38.2 MB/sへ近づくか。**

## 仮説

上がる。buffered modeのOUT完了は、DWC2 DMA先からTinyUSB RX FIFOへcopyし、sketchがFIFOから再度readする。E093でarm頻度、E094でpattern検査、E095でpayload ZLP、E096で測定長を除外したため、この二重copyと再armまでの処理が残る第一候補である。

## 反証条件

direct RXでも約35 MB/sなら、残差はTinyUSB FIFO copyではなくDWC2 DMA/cache処理、host scheduling、またはDL-165固有の受信実装差にある。

## 方法

- E096 hostの256 KiB、command-only ZLP、depth/transfer sweepを固定
- deviceのRX armは16 KiBのまま、`CFG_TUD_VENDOR_TXRX_BUFFERED=0`
- `tud_vendor_rx_cb()`のbuffer/lengthをsketchへ渡し、callback内ではbyte countだけ行う
- EspUsbDevice本体は変更せず、base `b05432d`の一時worktreeへ[再現用patch](espusbdevice-direct-rx.patch)を適用
- 3 run、全条件でhost/device byte数を照合

## 対象外

direct APIの製品採用、DL-165の再測定、DWC2 register/interrupt trace。

## 必要な環境 / ベンチ種別

P4 2枚OTG HS直結。一時。

## 記録する数値 / 完了条件

各run最大MB/s、host errors、device受信byte数。3 runのmin/median/maxをE096の34.906 MB/sおよびDL-165の38.2 MB/sと比較する。

## 影響

残差約9%をsoftware copyと、それより下のDWC2/相手device差へ分離する。

## 結果

生ログ: `_runs/E097_20260914T005200JST_esp32p4_host/`。各run最大39.737 / 39.509 / 39.797 MB/s、中央値 **39.737**、完全性PASS。16 KiB direct RXはbufferedの34.906より13.8%速くDL-165の38.2も上回った。同一P4ペアの方向比は39.737 / 25.575 = **1.55倍**。

初回は`RX_NEED_ZLP`指定漏れで実armが512 Bとなり11.578 MB/sだったため比較から除外し、`_runs/E097_INVALID_512B_ARM_.../`へ保存した。
