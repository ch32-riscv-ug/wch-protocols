# E100 TinyUSB direct TXでdevice → hostの天井を測る

状態: **完了 — 単純direct TXは18.09 MB/sへ低下**(2026-09-14、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E090](../e090_p4_dwc2_double_buffer/README.ja.md)、[E097](../e097_p4_vendor_out_direct_rx/README.ja.md)

## 問い

**device → hostもnon-buffered direct TXにすると、25.575 MB/sの天井はdirect RX側の39.7 MB/sへ近づくか。**

## 仮説

上がる。buffered TXはsketchからTX FIFOへcopyし、transfer開始時にFIFOからendpoint bufferへ再copyした後cache cleanしてDMAする。direct TXはsketch bufferからendpoint bufferへの1 copyだけになる。

## 反証条件

25.6 MB/s付近のままなら、software FIFO copyではなくIN token応答、DWC2 DMA/cache clean、単一transferの再arm、またはproducer側が天井である。

## 方法

- E090のhost sketchと1 MiB sweepをそのまま使用
- deviceは`CFG_TUD_VENDOR_TXRX_BUFFERED=0`、TX endpoint buffer 8 KiB
- hostの5 B stream commandはdirect RX callbackで処理
- pattern生成、8 KiB chunk、`waitWritable()`はE090と同じ
- 3 run、hostのramp検査と全byte数を必須にする

## 対象外

pattern事前生成、複数TX buffer、DWC2 trace。

## 必要な環境 / ベンチ種別

P4 2枚OTG HS直結。一時。

## 記録する数値 / 完了条件

各run最大MB/s、host bad/errors。E090の25.575とE097の39.737を比較する。

## 影響

1.55倍の方向差がbuffered TX copyか、それより下のIN処理かを決める。

## 結果

生ログ: `_runs/E100_20260914T010200JST_esp32p4_host/`。各run最大18.099 / 18.077 / 18.090 MB/s、中央値 **18.090**、全run PASS。copyを一段減らしても、単一endpoint buffer化で先行queue/pipelineを失うため25.575より遅い。
