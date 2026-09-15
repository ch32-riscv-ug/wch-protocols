# E090 DWC2 のハードウェア TX FIFO を 2 packet にすると天井は動くか

状態: **完了 — 2 packet 化しても天井は動かない**(2026-09-13。同一リグの A/B を各3回)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E089](../e089_p4_host_in_queue/README.ja.md)(天井は device 側と確定)、[E085](../e085_p4_transfer_size_model/README.ja.md)(`R` = 24.64 MB/s、`T` = 21.7 us)

## 問い

**device 役の約 24 MB/s(microframe あたり約 6 transaction)は、DWC2 のハードウェア TX FIFO が bulk IN に 1 packet しか割り当てていないせいか。**

## なぜこの問いか

[E089](../e089_p4_host_in_queue/README.ja.md)で **天井が device 側にある**ことは確定した(別々の host controller 2 つが同じ 24 MB/s で止まる)。**残るのは device 側のどこか**である。

**潰してあるもの**: 転送スケジューリング([E085](../e085_p4_transfer_size_model/README.ja.md) の `T` = 21.7 us。消しても `R` = 24.64 MB/s)、転送長(8 KiB 最良、16 KiB は逆に遅い)、ソフト FIFO の深さ([E084](../e084_p4_transfer_tuning/README.ja.md):8192 超で改善なし)、capture 負荷([E088](../e088_p4_usb_ceiling_idle/README.ja.md))、host 側([E084](../e084_p4_transfer_tuning/README.ja.md) / [E089](../e089_p4_host_in_queue/README.ja.md))。

**まだ触っていないものが 1 つある** — **`CFG_TUD_VENDOR_TX_BUFSIZE` は RAM 上のソフトリングで、DWC2 コアのハードウェア TX FIFO とは別物**である。後者は `dcd_dwc2.c` がこう割り当てる。

```c
uint16_t fifo_size = (uint16_t)tu_div_ceil(packet_size, 4);   // 512/4 = 128 words = 1 packet
```

**bulk IN endpoint に 1 packet 分ちょうど。** コアは **1 packet 送るたびに RAM から詰め直す**ことになる。

### 予算は余っている

| | words |
|---|---:|
| `otg_dfifo_depth`(P4 の HS) | **1024**(4 KiB) |
| EP0 IN(64 B) | 16 |
| **vendor bulk IN(512 B)** | **128 ← 1 packet** |
| 共有 RX FIFO(`13 + 1 + 2*(512/4+1) + 2*16`) | 304 |
| 使用計 | 448 |
| **空き** | **576(= あと 4 packet 分)** |

### TinyUSB には既にスイッチがある

```c
uint16_t bm_double_buffered; // bitmap of IN endpoints to be double buffered, only effective for bulk endpoints
#define CFG_TUD_CONFIGURE_DWC2_DEFAULT {.bm_double_buffered = 0, .vbus_sensing = CFG_TUD_VBUS_DETECT_HW}
```

```c
if (((_tud_cfg.bm_double_buffered & (1 << epnum)) != 0) && epnum > 0 && is_bulk) {
  fifo_size *= 2;
}
```

**既定が 0 で切られているだけ**である。**`#ifndef` ガード付きなので `build_opt.h` から立てられる** — **ライブラリを触らずに試せる**。

## 仮説

**上がる。** 2 packet staged なら、コアは 1 packet 送りながら次を詰められる。

## 反証条件

1. 変わらない → **段数ではなく DMA の詰め直しそのものが律速**
2. 下がる
3. 列挙しない / 動作しない(FIFO 配分が破綻)

## 方法

**[E089](../e089_p4_host_in_queue/README.ja.md) と同じリグ・同じ host firmware で、device 側の `build_opt.h` に 1 行足すだけ。**

```
-DCFG_TUD_CONFIGURE_DWC2_DEFAULT={.bm_double_buffered=0xFFFE,.vbus_sensing=CFG_TUD_VBUS_DETECT_HW}
```

- `0xFFFE` = EP0 以外の全 IN endpoint。**`is_bulk` かつ `epnum > 0` のときだけ効く**ので、bulk IN だけが 2 packet になる
- device 側の他の条件は [E089](../e089_p4_host_in_queue/README.ja.md) と同一(TX BUFSIZE / EPSIZE = 8192、`writeCapacity()` ぶんずつ、flush は最後のみ)
- host 側は **EspUsbHost working treeの `vendor_bulk_in_throughput` をそのまま**。実行に使ったsketch / pytest / profileのスナップショットを [host/](host/) に保存した。continuous → depth {1,2,4} × 転送長 {512, 2048, 8192, 16384, 32768}
- **[E089](../e089_p4_host_in_queue/README.ja.md) の数字がそのまま対照**になる

### 記録する数値

[E089](../e089_p4_host_in_queue/README.ja.md) と同じ(MB/s / `per_transfer` / `short` / `starved`)。**特に `per_transfer`** — 1 packet 制約が外れるなら、**device が 1 転送に詰められる量が増える**はず。

## 対象外

- 4 packet 以上への拡張(まず 2 で効くかを見る)
- FS 側([E089](../e089_p4_host_in_queue/README.ja.md) は HS のみ)
- host 側の変更

## 必要な環境

- [E089](../e089_p4_host_in_queue/README.ja.md) と同じ配線(P4 2 枚の OTG HS 直結)。**host 役は [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) 側が forced full-speed の実験に使用中**
- EspUsbDevice 2.3.0

## ベンチ種別

peer(board 2 枚、要配線)

## 完了条件

**[E089](../e089_p4_host_in_queue/README.ja.md) と同じ掃引を回し、24.45 MB/s が動くかを見る。**

## 影響

- **上がれば**: device 役の天井は「TinyUSB の FIFO 割り当て既定」であって DWC2 の物理限界ではない → EspUsbDevice へ **CR-10**(`bm_double_buffered` の露出、または bulk IN の既定を 2 packet に)
- **上がらなければ**: 段数ではなく **DMA の詰め直しそのもの**が律速 → 打ち手はライブラリの外
- どちらでも [E089](../e089_p4_host_in_queue/README.ja.md) の結論(天井は device 側)は動かない

## 結果

P4 2枚(`esp32-p4-30eda0e31478` = device / `...14f5` = host)の OTG HS を直結し、同じ host firmware のまま device だけを次の A/B で焼き替えた。各条件を3回ずつ実行した。

- **A: 1 packet** — E089 の `build_opt.h`(double buffer 指定なし)
- **B: 2 packet** — A に `CFG_TUD_CONFIGURE_DWC2_DEFAULT={.bm_double_buffered=0xFFFE,...}` を追加

生ログ: `experiments/_runs/E090_20260913T100722Z_p4_hs_ab/`(A/B 各3本)。全run・全条件で **`bad=0` / `errors=0` / pytest PASS**。

> **設定が効いていない比較ではない。** B の `dcd_dwc2.c.o` で `_tud_cfg` は `.sdata` の `fe ff 00 00`、すなわち `bm_double_buffered=0xFFFE` と確認した。A/B の sketch 差分もこの define 1行だけである。

| host read | **A: 1 packet MB/s** min / med / max | **B: 2 packet MB/s** min / med / max | `per_transfer`(両者) |
|---|---:|---:|---:|
| continuous、512 B | 8.129 / 8.158 / 8.159 | 8.128 / 8.129 / 8.159 | — |
| queue depth 1、2 KiB | 21.400 / 21.400 / 21.400 | **22.795 / 22.795 / 22.996** | 2,044 B |
| queue depth 1、8 KiB | 25.101 / 25.104 / 25.166 | 25.132 / 25.132 / 25.165 | 4,096 B |
| queue depth 1、16 KiB | 25.575 / 25.575 / 25.575 | 25.575 / 25.575 / 25.575 | 8,192 B |
| queue depth 4、512 B | 15.693 / 15.703 / 15.720 | 15.679 / 15.691 / 15.705 | 511.8 B |
| queue depth 4、32 KiB | **25.575 / 25.575 / 25.575** | **25.575 / 25.575 / 25.576** | 8,192 B |

### 事実

1. **天井は動かなかった。** 最大条件の中央値は A/B とも **25.575 MB/s**、差は測定表示の丸め以下である。したがって **bulk IN の hardware TX FIFO が1 packetだったことは、約24〜26 MB/sの天井原因ではない。** 仮説は反証条件1により外れた。
2. **小さい単発転送には局所的に効いた。** depth 1 / 2 KiB は 21.400 → 22.795 MB/s(中央値、+6.5%)。しかし 8 KiB では +0.1%、16 KiB以上では差が消える。2 packet staging は転送が短いときの詰め直し間隔を一部埋めるが、飽和rateは変えない。
3. **`per_transfer` は全条件で不変。** 16 KiB以上を要求しても 8,192 Bで頭打ちで、double bufferはTinyUSBのsoftware転送境界やZLP終端を広げない。
4. E089 の過去値24.45 MB/sと今回の25.575 MB/sは直接A/Bに使えない。同じ現在のhost firmwareで焼き戻したAも25.575 MB/sだったため、今回の結論は同時点A/Bだけから取った。

### 候補

- **CR-10は出さない。** `bm_double_buffered` を公開設定にしても最大帯域の改善にならない。
- depth 1かつ2 KiB程度の小さいtransferだけを使う用途では有効だが、host側のtransfer size/depthを上げる方が効果が大きい。

### 未決

- **約25.6 MB/sで止まるdevice側の本体は未特定。** hardware FIFOのpacket段数ではなく、DMAの供給、IN tokenへの応答間隔、またはDWC2/TinyUSBの別の経路に残る。
- 今回の1 MiB測定は41 ms単位に揃う条件が多い。細かい差を評価するなら転送量を増やす必要があるが、天井がA/Bで完全一致という結論は動かない。

## 反映

- [LEDGER](../LEDGER.ja.md) を完了へ更新。
- [P4 USB HSまとめ](../../references/p4-usb-hs-summary.ja.md) の天井候補からhardware TX FIFO 1 packet説を除外。
- EspUsbHost の `vendor_bulk_in_throughput` P4 profileを `USBMode=hwcdc,CDCOnBoot=cdc` に修正。これが無いと `Serial` がUART0へ向き、pytestのconsoleが空になる。
- 後から外部working treeの状態に依存せず再現できるよう、実行時のhost側sourceをこの実験ディレクトリに取り込んだ。ライブラリ本体は `sketch.yaml` に記載したworking treeを使う。

## 追記（2026-09-15、E110）

本実験の「hardware TX FIFOの1 packetは約24〜26 MB/sの天井原因ではない」は、当時の送信がbufferedで25.6 MB/sのcopy律速だったから見えなかっただけだった。[E110](../experiments/e110_p4_usb_in_ceiling/README.ja.md)でzero-copy送信にした上で同じFIFOを2 packetにすると、29.7→49.3 MB/s（理論の93%）へ上がり、1 packetのFIFOがzero-copy後の唯一の天井だったことが分かった。反証条件1は「その時点の他の律速を外した後で再確認する」必要があった例として残す。
