# E092 EspUsbDevice bulk OUT の受信 arm 長は帯域差の原因か

状態: **完了 — 8192 B armで30.84 MB/sへ上昇**(2026-09-13、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E091](../e091_p4_bulk_direction_same_peer/README.ja.md)

## 問い

**EspUsbDevice の bulk OUT 受信を 512 B から 8192 B の multi-packet transfer に変えると、同一 P4 peer の host → device 帯域は E091 の約 10.36 MB/sから上がるか。**

## 仮説

上がる。buffered vendor RX は `CFG_TUD_VENDOR_RX_NEED_ZLP=0` のとき endpoint packet size の 512 B で DWC2 transfer を完了・再 arm する。`RX_EPSIZE=8192` と `RX_NEED_ZLP=1` により、この固定処理を16 packetごとへ減らせる。

## 反証条件

8192 B arm の最大帯域が3 runのばらつきを含めて E091 と同じ、または低下する。

## 方法

- E091 と同じ device (`...1478`) / host (`...14f5`) / OTG HS直結、同じ host sketch と4 MiB payloadを使う
- Aは E091 の `RX_NEED_ZLP=0` / 512 B arm、Bは `RX_NEED_ZLP=1` / `RX_EPSIZE=8192`
- Bを3 runし、E091のA 3 runと比較する
- host async OUT depth {1,2,4,8} × transfer {512,2048,8192,16384,32768}
- host完了byte数、device受信byte数、`0xaf` patternを照合する

## 対象外

- `RX_NEED_ZLP=1` をライブラリ既定にするか。hostが短い転送の終端にZLPを送る契約が必要なため、速度とは別の設計判断にする
- DWC2 interrupt回数の直接計測。arm長A/Bで差が出なければ次に行う

## 必要な環境

P4 2枚のOTG HS直結。consoleは両方USB-Serial-JTAG。EspUsbDevice 2.3.0、EspUsbHost working tree(HR-1/HR-2入り)。

## ベンチ種別

一時(E091から配線継続)

## 記録する数値

各depth/transferのMB/s、host completed/errors/queue-full、device received/bad。Bを3 runし、各run最大値のmin/median/maxとA/B比を出す。

## 完了条件

8192 B armを3回測り、512 B armとの差とdata完全性を確定する。

## 影響

方向差をDWC2 hardwareではなくTinyUSB vendor RXのtransfer粒度で説明できるかを決める。効果があれば、用途限定のbuild optionまたはAPI化をEspUsbDeviceの変更候補にする。

## 結果

生ログ: `experiments/_runs/E092_20260913T103703Z_esp32p4_host/`。Bの3 runは全20条件でbyte数一致、`errors=0` / `device_bad=0` / PASS。

| 条件 | 各runの最大 MB/s | median | E091比 |
|---|---:|---:|---:|
| A: RX 512 B / host ZLPなし(E091) | 10.356 / 10.365 / 10.382 | **10.365** | 1.00 |
| **B: RX 8192 B / host ZLPあり** | 30.840 / 30.840 / 30.943 | **30.840** | **2.98倍** |
| 対照: RX 512 B / host ZLPあり(1 run) | 10.131 | 10.131 | 0.98倍 |

Bの代表値は depth 1 / 8 KiB = 24.2〜24.4、depth 4 / 32 KiB = 30.615、depth 8 / 32 KiB = 30.840〜30.943 MB/s。

### 事実

1. **device OUTの受信arm長が主因。** host側のZLP有無だけを変えた512 B arm対照は10.365 → 10.131 MB/sで、上昇を説明しない。512 → 8192 B armにより中央値は **2.98倍**になった。
2. **ZLP契約は必須。** 8192 B armのままhost ZLPを無効にした失敗ログでは、5 Bの開始commandがpayloadと連結され `device_received=4194309` / `device_bad=2` / FAIL。速度フラグとして透過的に既定化はできない。
3. 同一peerで比べると、tuned host → device 30.840 / device → host 25.575 = **1.21倍**。当初の1.5倍のうち大部分は比較相手の不一致とOUT側512 B armだった。
4. 38.2 MB/sにはまだ約24%届かない。**後続調査で相手はDisplayLink DL-165 (`17e9:0360`)と特定した。** 同一peerの残差はE093〜E103で再検証した。

### 候補

- continuousな大量OUTでhost側がZLP契約を守れる用途に限り、`CFG_TUD_VENDOR_RX_EPSIZE=8192` / `CFG_TUD_VENDOR_RX_NEED_ZLP=1`は有効。
- 汎用APIでは「multi-packet RXを有効にする」と「短転送をZLPで終端する」をhost/deviceの両側で対にして露出する必要がある。

### 未決

- tuned後に残る30.84対25.575 MB/s(1.21倍)の内訳。INはhostのIN tokenを待ち、OUTはhostが予めarmされたbufferへ送れるというバス方向の非対称が残る。E090によりhardware TX FIFO段数は除外済み。

## 反映

- E092にhost/device両source、profile、pytestを保存した。実装本体はE091との差を固定するため、repo内のE091 sourceをincludeしている。
- [LEDGER](../LEDGER.ja.md)と[P4 USB HSまとめ](../../references/p4-usb-hs-summary.ja.md)を更新。
