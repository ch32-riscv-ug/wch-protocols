# E071 ESP32-P4 device側 vendor bulkの天井は何か — 送信FIFOの深さ

状態: **完了 — FIFOは8 KiBで飽和(9.0 → 10.5 MB/s、+17%)。残りの差はFIFOではない**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)、[E070](../e070_p4_hs_vendor_stack_compare/README.ja.md) / 依頼: [EspUsbDeviceへの改修依頼](../../references/espusbdevice-change-requests.ja.md) CR-4

## 問い

**vendor bulkの送信FIFO(`CFG_TUD_VENDOR_TX_BUFSIZE`、既定512 B)を深くすると、device側の実効帯域はどこまで伸びるか。天井はFIFOの深さか、別の場所か。**

## なぜこの問いか

同じESP32-P4が**host役では36.4 MB/sを出す**ことが分かっている。

> | HS | 13 transactions × 512 B per microframe ≈ 53 MB/s | **36.4 MB/s**(ESP32-P4, async queue depth 2, 8 KB transfers) |
>
> — [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) `docs/usb-host-advanced.md`、`tests/manual/vendor_bulk_throughput`の実測

対して**device役では約9 MB/s**([E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md)・[E070](../e070_p4_hs_vendor_stack_compare/README.ja.md))。**同じchip・同じPHYで4倍の開きがある**ので、天井はhardwareではなくdevice側stackにある。

候補は2つ。

1. **送信FIFOが512 B(bulk 1 packet分)しかない** — `write()`がFIFO空き待ちで0を返す回数は4 MiBあたり3〜6万回で、1 packetあたり4回spinしている
2. **endpointごとに転送を1つしか投げていない** — host側の実測が「**queue depth 2で上限に張り付く**」と言っているのは、まさにこの軸

**この実験は1だけを振って、どこまでが1で説明できるかを決める。**

## 仮説

**FIFOを深くすれば伸びるが、36.4 MB/sには届かない。** 1 packetごとのturnaroundが減るだけで、「同時に投げる転送は1つ」という構造は変わらないため。

## 反証条件

1. FIFOを深くしても帯域が変わらない。天井はFIFOと無関係
2. FIFOを深くすると**36 MB/s級まで伸びる**。構造の問題ではなくFIFOだけだった
3. 深くすると遅くなる、または動かなくなる
4. pattern不一致が出る

## 方法

[E070](../e070_p4_hs_vendor_stack_compare/README.ja.md)のvariant B(EspUsbDevice 2.2.0)と**同じsketch・同じ条件**で、`CFG_TUD_VENDOR_TX_BUFSIZE`だけを振る。

- vendor 1 interface、endpoint 512 B、OTG HS
- 送出taskはcore 0にpin、優先度5
- 送出元はinternal RAM 64 KiBを繰り返し、転送4 MiB、consoleから`C`で開始
- host = WSL(usbip経由)+ libusb、read size 1 MiB
- 全word照合(`word[i] = i % 16384`)

### ライブラリ側の細工(再現手順)

`EspUsbDevice` 2.2.0の`src/internal/EspUsbTinyUsbConfig.h`は

```c
#define CFG_TUD_VENDOR_TX_BUFSIZE 512
```

と**`#ifndef`ガード無しで定義している**ため、`build_opt.h`の`-D`では上書きできない。**ライブラリを書き換えずに測るため、scratchへコピーして1行だけ差し替えた**。

```c
#ifndef FIFO_TX_BYTES
#define FIFO_TX_BYTES 512
#endif
...
#define CFG_TUD_VENDOR_TX_BUFSIZE FIFO_TX_BYTES
```

そのうえで`build_opt.h`に`-DFIFO_TX_BYTES=<n>`を置く。**この細工をライブラリ本体へ入れてほしい、というのが[CR-4](../../references/espusbdevice-change-requests.ja.md)**。

## 対象外

- **「同時に投げる転送を増やす」軸**。TinyUSBのclass driverの構造に手を入れる話になるので別の問い
- usbipを介さないネイティブ測定(`p4-native-vs-usbip`)
- RX側FIFOの深さ
- CDCやHIDでの同じ掃引

## 結果

### FIFO深さ(read size 1 MiB、4 MiB転送)

| `CFG_TUD_VENDOR_TX_BUFSIZE` | 回数 | min | **median** | max | `stalls` median |
|---:|---:|---:|---:|---:|---:|
| **512 B**(既定) | 25 | 6.79 | **9.03** | 10.02 | 39,746 |
| **8 KiB** | 15 | 10.27 | **10.59** | 10.76 | **28,844** |
| 16 KiB | 10 | 9.84 | **10.33** | 10.49 | 29,444 |
| 32 KiB | 10 | 10.18 | **10.39** | 10.55 | 29,672 |
| **64 KiB** | — | — | **動作せず** | — | — |

単位はMB/s。**全条件でpattern不一致0、転送失敗0**(64 KiBを除く)。

### read sizeの効き(FIFO 8 KiB)

| host read size | min | **median** | max |
|---:|---:|---:|---:|
| 64 KiB | 7.96 | **8.31** | 8.71 |
| 256 KiB | 9.37 | **9.54** | 9.77 |
| 1 MiB | 10.34 | **10.48** | 10.72 |
| 4 MiB | 10.56 | **10.74** | 11.02 |

### 事実

1. **FIFOを512 B → 8 KiBにすると帯域は9.03 → 10.59 MB/s(+17%)へ上がる。** ばらつきも6.79–10.02から10.27–10.76へ大きく縮む。**反証条件1は否定。**
2. **8 KiBで飽和する。** 16 KiB・32 KiBは10.33・10.39 MB/sで、8 KiBと差が無い(むしろ僅かに低い)。
3. **64 KiBでは動かない。** `device.begin()`は成功する(`usb_ready=1`)が**`mounted=0`のままhostがconfigureせず**、転送も全て失敗した。**深さには上限がある。**
4. **`stalls`は39,746 → 28,844で下げ止まる。** FIFOを深くしても、**1 packetあたり約3.5回のspinは残る**。
5. **36.4 MB/sには遠い。** 最良でも10.74 MB/s(FIFO 8 KiB、read 4 MiB)で、**host役の実測の約30%**。**反証条件2は否定** — FIFOの深さだけでは説明も解決もできない。
6. **read sizeは深いFIFOでもまだ効く**(64 KiBで8.31、4 MiBで10.74)。usbipの1 URBあたりのoverheadが残っていることを示す。

### 天井の内訳(現時点の理解)

| | 値 | 根拠 |
|---|---:|---|
| HS bulkの理論上限 | 53 MB/s | 13 transaction × 512 B × 8,000 microframe/s |
| **同じP4がhost役で出した実測** | **36.4 MB/s** | EspUsbHostの`vendor_bulk_throughput`(**async queue depth 2**) |
| device役・FIFO 8 KiB(usbip経由) | **10.74 MB/s** | 本実験 |
| device役・FIFO 512 B(usbip経由) | 9.03 MB/s | 本実験 / [E070](../e070_p4_hs_vendor_stack_compare/README.ja.md) |

**hardwareは36.4 MB/sを出せる。** device役で3.4倍足りない分のうち、**FIFOの深さで説明できるのは17%だけ**である。残りの候補は2つ。

- **endpointごとに転送を1つしか投げていない。** host側の実測が「**depth 2で上限に張り付く**」と言っており、device側にも同じ軸があるはず
- **usbipのoverhead。** read sizeがまだ効いていることから、ゼロではない

### 候補

- **vendor bulkのTX FIFOは8 KiBを既定にする**(採用。+17%、ばらつきも縮む。RAMは8 KiB)
- 16 KiB以上にしない(効かない)。**64 KiBは壊れる**
- 次に効くのは**FIFOではなく「同時に投げる転送の数」**

### 未決

- **同時に投げる転送を2つにしたらどこまで伸びるか** `—`。TinyUSBのvendor class driverの構造に触る話。**これが本命**
- **usbipのoverheadはどれだけか** `—`。切り分けるには**ネイティブなhost**が要る。**2枚目のP4を[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)でhostにして、HS port同士をケーブルで繋げばPCを経路から外せる**(要配線)
- **64 KiBでmountしない理由** `—`。P4のHS portのhardware FIFOは4 KB(1,024 line)で、endpoint割当と衝突している可能性
- RX側の深さ、CDC / HIDでの同じ掃引 `—`

## 影響

- [EspUsbDeviceへの改修依頼](../../references/espusbdevice-change-requests.ja.md) CR-4に実測値が付いた。**「FIFOを可変にしてほしい」に加えて「8 KiBが良い」「64 KiBは壊れる」「+17%で頭打ち」まで言える**
- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) §後段のdownload帯域 — **最良は10.74 MB/s**(2 channelなら約43 Msps相当)
- [harness-channels](../../references/harness-channels.ja.md) §物理IFの帯域表
