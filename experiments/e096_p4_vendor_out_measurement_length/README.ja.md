# E096 256 KiB測定ならDL-165の38.2 MB/sへ一致するか

状態: **完了 — 256 KiBでも約34.9 MB/s**(2026-09-13、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E095](../e095_p4_vendor_out_data_zlp_cost/README.ja.md)

## 問い

**E095をDL-165の元テストと同じ1条件256 KiBに短縮すると、4 MiBでの34.953 MB/sより高くなり38.2 MB/sへ一致するか。**

## 仮説

短時間測定ではtimer量子化や開始直後のburstにより高く見える可能性がある。相手以外の測定条件を元テストへ揃える。

## 反証条件

256 KiBでも約35 MB/sなら、残差は測定長ではなくDL-165とEspUsbDeviceの実装差である。

## 方法

- E095のRX 16 KiB、command-only ZLP、pattern検査ありを固定
- payloadだけ4 MiBから256 KiBへ変更。元の`vendor_bulk_throughput`と同量
- 3 run、全条件でhost/device byte数とpatternを照合

## 対象外

DL-165の再測定、TinyUSB direct RX。

## 必要な環境 / ベンチ種別

P4 2枚OTG HS直結。一時。

## 記録する数値 / 完了条件

各run最大MB/sと完全性。3 runのmin/median/maxを4 MiBおよび38.2と比較する。

## 影響

38.2との差が測定法か実deviceの受信経路かを確定する。

## 結果

生ログ: `_runs/E096_20260913T110619Z_esp32p4_host/`。各run最大34.799 / 34.911 / 34.906 MB/s、中央値 **34.906**、完全性PASS。4 MiBのE095と差がなく、DL-165元試験と同じ256 KiB測定長は38.2との差を説明しない。
