# E099 callback内FIFO readでloop待ちだけを外す

状態: **完了 — callback readでも33.7 MB/s**(2026-09-14、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E098](../e098_p4_vendor_out_fifo_flush_callback/README.ja.md)

## 問い

**buffered RXの二つのcopyを両方残したままFIFO readを完了callback内へ移すと、E098の32.8 MB/sと通常loopのE095 34.95 MB/sのどちらに一致するか。**

## 仮説

E098付近になる。E098の低下がFIFO clear特有ではなく、USB taskのcallback内で16 KiBを処理してrearmする構造の費用なら、通常readでも同じになる。E098との差だけがFIFO → application copyの費用になる。

## 反証条件

E095付近ならE098の`read_flush()`経路が特有の低下を作った。E098よりさらに低ければ2回目copyの費用を数値化できる。

## 方法

- E098と同じ一時patch、RX FIFO 32 KiB / arm 16 KiB、host条件
- payload callback内で`tud_vendor_n_read()`を16 KiB bufferへ実行
- pattern検査はせず、byte countだけ行う
- 3 run、完全性照合

## 対象外

製品API設計、DWC2 trace。

## 必要な環境 / ベンチ種別

P4 2枚OTG HS直結。一時。

## 記録する数値 / 完了条件

各run最大MB/s。E095 / E098 / E097と比較して二つのcopyと実行場所を分離する。

## 影響

buffered経路の改善点がcopy除去かtask間の受け渡しかを決める。

## 結果

生ログ: `_runs/E099_20260914T005800JST_esp32p4_host/`。各run最大33.777 / 33.729 / 33.734 MB/s、中央値 **33.734**、完全性PASS。別taskで後段readを重ねる通常buffered(34.906)は一部をpipelineできるが、完了callback内の最初のcopyは隠せない。
