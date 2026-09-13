# E098 buffered RXのsketch copyとloop待ちを外す

状態: **完了 — 最初のFIFO copyが残ると32.8 MB/s**(2026-09-14、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E097](../e097_p4_vendor_out_direct_rx/README.ja.md)

## 問い

**DWC2 endpoint buffer → TinyUSB RX FIFOのcopyは残し、FIFO → sketchのpayload copyとloop待ちだけを外すと、E095の34.95 MB/sはE097 direct RXの39.7 MB/sへ近づくか。**

## 仮説

近づく。E097は二つのcopyとloop schedulingを同時に外したため、どれが約4.8 MB/sを占めるか未分離である。buffered完了callbackへ実受信長を渡し、その場でFIFOをclearすれば最初のcopyだけを残せる。

## 反証条件

約35 MB/sのままならendpoint buffer → FIFO copyが主因。39 MB/s台ならFIFO → sketch copyまたはloop待ちが主因。中間なら費用は分散している。

## 方法

- RX FIFO 32 KiB / arm 16 KiB、host 256 KiB、command-only ZLPを固定
- TinyUSBのbuffered callbackに`xferred_bytes`を渡す[一時patch](espusbdevice-buffered-rx-length.patch)を追加（先にE097のpatchを適用）
- payload callbackでは受信長を加算し、`tud_vendor_n_read_flush()`でFIFOをcopyせず捨てて即rearm
- commandだけはFIFOから最大5 B読む
- 3 run、全条件でbyte数を照合

## 対象外

callback即時処理とFIFO → sketch copyを別々に分ける追試（必要なら次実験）。製品API設計。

## 必要な環境 / ベンチ種別

P4 2枚OTG HS直結。一時。

## 記録する数値 / 完了条件

各run最大MB/sと完全性。E095 / E097と比較する。

## 影響

buffered受信経路の約12%の費用を、二つのcopyとloop schedulingへ分解する。

## 結果

生ログ: `_runs/E098_20260914T005600JST_esp32p4_host/`。各run最大32.768 / 34.897 / 32.768 MB/s、中央値 **32.768**、完全性PASS。FIFO→sketch copyを外してもdirect RXへ近づかず、endpoint buffer→FIFO copyを残す経路が主損失。
