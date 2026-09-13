# E095 payloadごとの自動ZLPを外すとDL-165の38.2 MB/sへ届くか

状態: **完了 — payload ZLPは約1.7 MB/sの費用**(2026-09-13、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E093](../e093_p4_vendor_out_arm_scaling/README.ja.md)、[E094](../e094_p4_vendor_out_validation_cost/README.ja.md)

## 問い

**開始・終了commandだけを明示ZLPで区切り、payload transferごとのauto-ZLPを止めると、16 KiB armの33.288 MB/sはDL-165相手の38.2 MB/sへ近づくか。**

## 仮説

上がる。E093は32 KiB payload transfer 128本それぞれにZLPを追加し、host completionは256回だった。DL-165の38.2測定はauto-ZLPなし。device armが16 KiBなら4 MiB payloadは自然に256 armで完了するため、payload中のZLPは不要である。

## 反証条件

payload ZLPを外しても33.3 MB/s付近なら、ZLP転送とhost callback回数は残差の原因ではない。

## 方法

- E093 RX 16 KiB、pattern検査ありを固定
- `B` / `E` commandは同期write直後に`vendorWriteZlp()`を明示して境界を保証
- async payloadはauto-ZLPなし。depth / transfer sweepと4 MiB照合は同じ
- 3 run。host `completed`がpayload transfer本数へ半減することも確認

## 対象外

- command framing方式の採用判断
- DL-165実機の再配線・再測定

## 必要な環境

P4 2枚OTG HS直結、E094から継続。

## ベンチ種別

一時

## 記録する数値

各run最大MB/s、host completed/errors、device byte数/bad、min/median/max。

## 完了条件

payload auto-ZLPなしを3回測り、DL-165との差を再計算する。

## 影響

残る4.9 MB/s差がprotocol framingの余分なZLPか、device/DWC2固有かを決める。

## 結果

生ログ: `_runs/E095_20260913T110359Z_esp32p4_host/`。各run最大34.953 / 34.953 / 35.076 MB/s、中央値 **34.953**、完全性PASS。payload auto-ZLPを外すとE093比+5.0%。32 KiB条件のcompletionは256→129へほぼ半減した。
