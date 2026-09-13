# E091 同じ P4 device で bulk OUT / IN の方向差は残るか

状態: **完了 — 既定peerは10.365 MB/s、後続で38.2の相手も特定**(2026-09-13、3 run)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E089](../e089_p4_host_in_queue/README.ja.md)、[E090](../e090_p4_dwc2_double_buffer/README.ja.md)

## 問い

**P4 host → device の38.2 MB/sとdevice → hostの約25.6 MB/sという差は、同じEspUsbDevice peerを相手にしても残るか。**

## なぜこの問いか

38.2 MB/sはEspUsbHostの`vendor_bulk_throughput`で得た値だが、生ログと相手deviceの銘板がこのrepoに無く、USB display adapterとEspUsbDevice peerのどちらを相手にした値か固定されていない。一方、約25.6 MB/sはP4 2枚直結でEspUsbDevice peerから読む値である。**相手deviceと検査負荷が違う数字を方向差として扱うことはできない。**

## 仮説

**38.2対25.6という1.5倍差はそのまま残らない。** EspUsbDeviceのbuffered bulk OUTは既定では512 Bずつarmし直すため、host OUTの38.2 MB/sは同じpeerに対する値ではない可能性が高い。

## 反証条件

同じP4 deviceでhost → deviceが35 MB/s以上、device → hostが約25.6 MB/sのままなら、相手の違いではなく方向固有の差が残る。

## 方法

- device = `esp32-p4-30eda0e31478`、host = `...14f5`、OTG HS直結
- このディレクトリ内にhost/device両方のsketchとpytestを置く
- deviceは開始commandでcounterをresetし、bulk OUT payloadを破棄せず`0xaf`と照合する。終了commandに対して受信byte数と不一致数をbulk INで返す
- hostはasync queueのdepth {1,2,4,8} × transfer {512,2048,8192,16384,32768}を振る
- 1条件4 MiB、3 run。host完了数とdevice受信数が一致し、deviceの`bad=0`であることを必須にする
- E090のdevice → host値とは、同じ2枚・同じ配線・同じEspUsbDevice 2.3.0という範囲で比較する

## 対象外

- 差が残った場合のDWC2 register/interrupt単位の内訳。まず比較対象を同一にする
- PC/Windows hostとの比較
- hardware FIFO配分(E090で反証済み)

## 必要な環境

P4 2枚のOTG HS直結。consoleは両方USB-Serial-JTAG。EspUsbDevice 2.3.0、EspUsbHost working tree(HR-1/HR-2入り)。

## ベンチ種別

一時(peer 2枚、配線済み)

## 記録する数値

各depth/transferのMB/s、host completed/errors/queue-full、device received/bad。3 runのmin/median/max。

## 完了条件

同じEspUsbDevice peerに対するhost → deviceの最大値を3回取り、25.6 MB/sとの比を出す。

## 影響

- 38.2 MB/sをP4同士の方向差として引用できるかを決める
- 差が残れば次はdevice DCDのIN/OUT arm・DMA完了間隔を計測する
- 残らなければ38.2 MB/sの銘板不足をEspUsbHost側の文書へ反映する

## 結果

生ログ: `experiments/_runs/E091_20260913T102910Z_esp32p4_host/`。全3 run・全20条件でhost完了数とdevice受信数が一致し、`errors=0` / `device_bad=0` / PASS。

| host OUT条件 | MB/s min / median / max |
|---|---:|
| depth 1 / 512 B | 6.394 / 6.443 / 6.443 |
| depth 1 / 8 KiB | 9.823 / 9.827 / 9.829 |
| depth 1 / 32 KiB | 10.010 / 10.018 / 10.020 |
| depth 2 / 2 KiB | 10.331 / 10.356 / **10.382** |
| **各runの最大** | **10.356 / 10.365 / 10.382** |

### 事実

1. **同じEspUsbDevice peerに対するhost → deviceは約38.2 MB/sではなく、中央値10.365 MB/s。** 38.2対25.6をP4の方向差として扱う前提は崩れた。
2. 同じpeerのdevice → host 25.575 MB/s(E090)と比べると、方向差は逆で **device → hostが2.47倍**。
3. depthやhost transferを増やしても10.4 MB/sで飽和する。host queueではなくdeviceのOUT受信側に天井がある。

### 候補

- TinyUSB vendor RXは既定でDWC2 OUTを512 Bずつarmし直す。IN側の8192 B transferと非対称なため、次は[E092](../e092_p4_vendor_out_arm_size/README.ja.md)でここだけを広げる。

### 未決

- **後続調査で訂正**: 38.2 MB/sの相手はDisplayLink DL-165 (`17e9:0360`)。`EspUsbHost/tests/manual/vendor_bulk_throughput`が`0xaf`を送る試験で、`docs/usb-display-spec.md`に実deviceが記録されている。EspUsbDevice peerとの方向比較はE092〜E103で条件を揃えて再検証した。

## 反映

- host/device両方のsketch、profile、pytestをこの実験ディレクトリに保存した。
- [LEDGER](../LEDGER.ja.md)を完了へ更新し、受信arm長の切り分けをE092へ分けた。
