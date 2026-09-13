# E102 zero-copy・事前生成direct TXのIN上限

状態: **完了 — zero-copyで36.159 MB/s**(2026-09-14、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E101](../e101_p4_vendor_in_callback_chain/README.ja.md)

## 問い

**8 KiB patternを事前生成し、そのbufferをDCD DMAへzero-copyでcallback chainすると、device → hostは25.575 MB/sを超えてdirect OUT 39.7 MB/sへ近づくか。**

## 仮説

上がる。E101で残る完了間の仕事は8 KiB pattern生成、internal endpoint bufferへのmemcpy、cache cleanである。事前生成＋zero-copyはcache cleanとregister armだけを残す。

## 反証条件

25.6 MB/s以下ならIN側DWC2/cache clean/token応答が天井。35 MB/s以上なら方向差の大半はTX producer/copy直列化である。

## 方法

- E101のcallback chainとE090 hostを固定
- cache lineに整列した静的8 KiB rampをsetupで一度生成
- [一時patch](tinyusb-vendor-zero-copy.patch)でnon-buffered vendor writeのinternal memcpyを外し、渡されたbufferを直接`usbd_edpt_xfer()`へ渡す
- 3 run、host byte数・ramp・errorsを照合

## 対象外

一般向けzero-copy APIのownership設計、複数buffer producer。

## 必要な環境 / ベンチ種別

P4 2枚OTG HS直結。一時。

## 記録する数値 / 完了条件

各run最大MB/s。E090 / E100 / E101 / E097と比較する。

## 影響

同じDWC2で方向差を作るsoftware workを確定する。

## 結果

生ログ: `_runs/E102_20260914T010700JST_esp32p4_host/`。各run最大36.159 / 36.159 / 36.158 MB/s、中央値 **36.159**。全条件でramp `bad=0`、errors=0。

| device TX経路 | 最大値中央値 |
|---|---:|
| buffered 8 KiB (E090) | 25.575 MB/s |
| direct、task rearm (E100) | 18.090 MB/s |
| direct、callback chain (E101) | 18.396 MB/s |
| direct、事前生成＋internal copy (E103) | 34.275 MB/s |
| **事前生成zero-copy、callback chain (E102)** | **36.159 MB/s** |
| direct RX host→device (E097) | 39.737 MB/s |

**事実**: 事前生成＋zero-copyでbuffered比+41.4%、単純direct比ほぼ2倍。調整後の方向比は39.737 / 36.159 = **1.10倍**。E103の追加対照により、改善の大半はbenchmarkのpattern生成を完了後のcritical pathから外した効果で、internal memcpy除去単独の上積みは34.275→36.159 MB/s（+5.5%）と分かった。残る約9%にはIN cache clean、IN token応答、8 KiBごとの完了/rearmが含まれる。

**候補**: 製品化するならbuffer ownershipを明示したzero-copy TX APIと、DMA中に次bufferを準備できるping-pong queueが必要。今回の一時patchをそのまま公開APIにはしない。
