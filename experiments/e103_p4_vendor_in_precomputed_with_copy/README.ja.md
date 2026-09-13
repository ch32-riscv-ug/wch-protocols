# E103 事前生成のみでTX producer費用を分離する

状態: **完了 — 事前生成だけで34.275 MB/s**(2026-09-14、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E101](../e101_p4_vendor_in_callback_chain/README.ja.md)、[E102](../e102_p4_vendor_in_zero_copy_precomputed/README.ja.md)

## 問い

**callback-chain direct TXでpatternだけを事前生成し、internal endpoint bufferへの8 KiB memcpyは残すと何MB/sか。**

## 仮説

E101の18.396とE102の36.159の中間になる。E101→E103がproducer生成、E103→E102がinternal memcpyの費用になる。

## 反証条件

E101と同じなら生成は無関係。E102と同じならmemcpyは無関係。

## 方法

E101へ`E101_PRECOMPUTED_PATTERN=1`だけを追加。zero-copy patchのdefineは立てず、TinyUSBの`memcpy(epbuf, buffer, 8192)`を残す。E090 host sweepを3 run。

## 対象外

cache clean単独、公開API設計。

## 必要な環境 / ベンチ種別

P4 2枚OTG HS直結。一時。

## 記録する数値 / 完了条件

各run最大MB/sと完全性。E101/E102との差を算出する。

## 影響

TX側の約1.5倍差をproducer生成とinternal copyへ定量配分する。

## 結果

生ログ: `_runs/E103_20260914T011600JST_esp32p4_host/`。3 runとも最大 **34.275 MB/s**、全条件PASS。

同じdepth 2 / 8 KiBで1 MiBに要した時間はE101（毎回pattern生成＋copy）57 ms、E103（事前生成＋copy）31 ms、E102（事前生成zero-copy）29 ms。128回の8 KiB submitなので、差分は概算で **pattern生成203 us / chunk、internal memcpy 16 us / chunk**。

**事実**: E101→E102の改善の大半はzero-copyではなく、benchmarkのbyte-by-byte pattern生成を完了後のcritical pathから外した効果。internal memcpy除去の上積みは34.275→36.159 MB/s（+5.5%）。buffered TXはproducerをDMAと並行させるため、同じ生成処理を含んでも25.575 MB/sまで隠せていた。
