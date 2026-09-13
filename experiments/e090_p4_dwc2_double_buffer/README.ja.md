# E090 DWC2 のハードウェア TX FIFO を 2 packet にすると天井は動くか

状態: **準備完了 — リグの空き待ち**(2026-09-13。ビルド確認済み)

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
- host 側は **[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) の `vendor_bulk_in_throughput` をそのまま**。continuous → depth {1,2,4} × 転送長 {512, 2048, 8192, 16384, 32768}
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
