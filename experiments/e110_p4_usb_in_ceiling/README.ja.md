# E110 USB HS bulk INの転送速度をゼロベースで再測する

状態: **完了 — 天井はDWC2のbulk IN TX FIFOが1 packet分だったこと。2 packetにすると29.7→49.3 MB/s（395 Mbps、理論の93%）、Windows nativeでも47.1 MB/s。transfer長・arm深さ・URB・再arm経路は主因ではなかった**（2026-09-15） — **参考値（独自patch版library）**（EspUsbDevice 2.3.0＋E097/E101/E102/E110の一時patch。正規libraryに取り込まれるまで製品の目安には使わず、修正依頼の根拠にのみ使う）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E108](../e108_p4_zero_copy_stream/README.ja.md)、[E102](../e102_p4_vendor_in_zero_copy_precomputed/README.ja.md)、[E104](../e104_p4_windows_continuous_bulk/README.ja.md)、[E090](../e090_p4_dwc2_double_buffer/README.ja.md)

## 問い

ESP32-P4のUSB 2.0 HS bulk INは、device→PCで**理論値（512 byte×13 packet/µframe＝53.25 MB/s＝426 Mbps）に対してどこまで出て、失われている分はdevice側（transferごとの完了→再armの隙間、transfer長）とhost側（URB長、URB depth、usbipd / WinUSBの経路）のどちらにあるか。** これまでの値（buffered 24〜26 MB/s、zero-copy 27.7〜31 MB/s、P4 host相手36.2 MB/s）を前提にせず、captureとcodecを外した最小firmwareで測り直す。

## 仮説

- device側の隙間は完了割り込み→usbd task→callback→再armの数十µsで、27 KiB transfer（約0.9 ms）に対して数%。transfer長を65,024 byte（127 packet、`usbd_edpt_xfer`の16-bit上限）にしても速度がほとんど変わらなければ、隙間は主因ではない。
- host側が主因なら、URB depthを増やしても飽和し、経路（usbipd/WSL 対 Windows native）で差が出る。E104 / E107 / E108でnativeがusbipdより遅いのは、WinUSB経路の性質と見る。
- device単体の天井はP4 hostとの直結で見えたE102の36.2 MB/s以上で、PC相手の27.7〜31 MB/sはhost側で決まっている。

## 反証条件

- transfer長を512→65,024 byteへ変えて速度が大きく変わる → device側の再arm隙間が主因。
- URB長・depthで速度が大きく変わる → host側のqueue深さが主因。
- どれを変えても同じ値 → 線上（NAK率、xHCIのtoken発行）が天井。

## 方法

- firmware: E108のarm ring（zero-copy、callback chain）だけを残し、PARLIO / codec / PSRAMを外す。64 KiBの256周期patternをdevice側transfer長T（512の倍数、512〜65,024）で繰り返し送る。commandでT、総byte、arm ring深さ（1〜4）、spin負荷計測を指定する。
- device側計時: armから完了までの時間（min / avg / max）、完了から次のarm完了までの隙間（avg / max）、完了回数、最初のarmから最後の完了までの時間。spin taskでcore 0のISR込み負荷。
- host: 64 MBを1 MiB×8のURBで受け、patternを全照合。URB長（64 KiB / 256 KiB / 1 MiB / 4 MiB）とdepth（1 / 2 / 4 / 8 / 16）を掃引。
- 掃引: (a) T ∈ {512, 2048, 8192, 27136, 65024} × arm深さ {1, 2, 4}、host 1 MiB×8。(b) T=65,024・深さ4でhost URB長×depth。(c) 各点3回。経路はusbipd/WSLとWindows nativeの両方。
- 結果は理論値53.25 MB/sに対する比と、µframeあたりの平均packet数（rate / 512 / 8000）で表す。

## 対象外

OUT方向（E092〜E099）、hub経路、複数endpointの並列送出（結果次第で次の候補にする）。

## 必要な環境 / ベンチ種別

第三P4、PC直結。両経路の手順はE109と同じ。一時。

## 記録する数値 / 完了条件

各点のMB/s、理論値比、packet/µframe、device側の隙間とtransfer時間。隙間の寄与とhost側の寄与が数値で分離できれば完了。

## 影響

[P4 USB HSまとめ](../../references/p4-usb-hs-summary.ja.md)（vendor bulk約24 MB/sの記述）、[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.2 / §1.3、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)のUSB予算。

## 再現

```sh
arduino-cli compile --profile esp32p4_device && arduino-cli upload --profile esp32p4_device --port /run/board-identify/by-id/esp32-p4-80f1b2d0b261
usbipd.exe attach --wsl --busid 1-7
uv run --with libusb1 python host_probe.py --xfer 65024 --arm-depth 4 --transfer-size 1048576 --depth 8 --bytes 64000000 --repeat 3
```

## 結果

生ログ: `_runs/E110_20260915T090222JST_p4_direct/sweep.log`。firmwareはpattern送出だけの最小構成（E108 arm ring、zero-copy）。理論値は512 byte×13 packet/µframe＝53.248 MB/s＝426 Mbps。特記なければusbipd/WSL直結、host URB 1 MiB×8、64,000,000 byte、pattern全照合PASS、`arm_failures=0`。

### 1. TX FIFO 1 packet（EspUsbDevice 2.3.0の既定）: device側transfer長とarm深さ

| transfer長 | arm深さ | host MB/s | packet/µframe | 理論比 | 再arm隙間avg | endpoint未arm時間 |
|---:|---:|---:|---:|---:|---:|---:|
| 512 | 1 | 8.6〜8.9 | 2.1 | 16% | 19 µs | 32% |
| 512 | 4 | 11.7 | 2.85 | 22% | 1 µs | 4% |
| 2,048 | 1 | 19.9 | 4.9 | 37% | 19 µs | 18% |
| 2,048 | 2〜4 | 25.1〜25.6 | 6.2 | 48% | 1 µs | 2.2% |
| 8,192 | 1 | 26.6〜26.7 | 6.5 | 50% | 19 µs | 6.1% |
| 8,192 | 2〜4 | 27.8〜28.0 | 6.8 | 52% | 1 µs | 0.6% |
| 27,136 | 1 | 28.7〜28.9 | 7.0 | 54% | 19 µs | 2.0% |
| 27,136 | 2〜4 | 29.3〜29.5 | 7.2 | 55% | 1 µs | 0.2% |
| 65,024 | 1 | 29.4〜29.6 | 7.2 | 55% | 19 µs | 0.9% |
| **65,024** | **2〜4** | **29.6〜29.8** | **7.25** | **56%** | 1 µs | **0.1%** |

各3回。transfer長≥27 KiB・arm深さ≥2ではendpointが99.8〜99.9%の時間armedで、完了→再armの隙間は1 µs（callback chain）。それでも**armed中のpacket間隔が平均17.7 µs**（65,024 byte = 127 packetが2,254 µs）で、理論の9.6 µsの約半分の頻度しかpacketが出ていない。device側の隙間は主因ではない。

### 2. host側: URB長×depth（transfer 65,024 / arm 2）

| URB | depth 1 | depth 2 | depth 4 | depth 8 | depth 16 |
|---:|---:|---:|---:|---:|---:|
| 64 KiB | 20.5 | 28.6 | 28.5 | 28.5 | 28.7 |
| 256 KiB | 22.3 | 29.7 | 29.9 | 29.7 | 29.5 |
| 1 MiB | 24.4 | 29.3 | 29.5 | 29.6 | （4 MiB以降失敗） |
| 4 MiB | `LIBUSB_ERROR_NO_MEM` | | | | |

depth 1だけURB完了→再投入の隙間で落ち、depth≥2は飽和する。WSL（usbip vhci）では4 MiB URBが確保できない。hostのqueueは主因ではない。

### 3. 再arm経路とcore 0負荷

- task再arm（callback chainなし）: 隙間18 µs、65,024 byteで29.5 MB/s、8,192 byteで26.6 MB/s。chainと同値（65 KiB）〜0.3 MB/s差（8 KiB）。
- spin法のcore 0空き: 29.3 MB/s送出中に**98.1%**。device CPUはほぼ関与していない。

### 4. TX FIFOを2 / 4 packetにする

`dcd_dwc2.c`の`dfifo_alloc()`は bulk IN に `packet_size / 4` words＝**1 packet**のTX FIFOしか割り当てない（`_tud_cfg.bm_double_buffered`が立っていれば2倍、EspUsbDeviceは立てていない）。P4 HSのDFIFOは1,024 words: RX 304、EP0 16、DMA EPInfo 32を除く672 wordsが余っており、1つのbulk INに最大5 packet分を使える。[patch](dcd-dwc2-in-fifo-packets.patch)で`E110_IN_FIFO_PACKETS`倍にした。

| TX FIFO | transfer長 | usbipd/WSL | packet/µframe | 理論比 | Windows native | 理論比 |
|---:|---:|---:|---:|---:|---:|---:|
| 1 packet | 65,024 | 29.6〜29.8 MB/s | 7.25 | 56% | 27.7 MB/s（E108/E109） | 52% |
| **2 packet** | **65,024** | **49.0〜49.3 MB/s（392〜395 Mbps）** | **12.0** | **92〜93%** | **47.1 MB/s（377 Mbps）** | **88%** |
| 2 packet | 27,136 | 47.8 MB/s | 11.7 | 90% | 45.6 MB/s | 86% |
| 2 packet | 8,192 | 41.4〜41.6 MB/s | 10.1 | 78% | 39.5 MB/s | 74% |
| 4 packet | 65,024 | 49.0〜49.2 MB/s | 12.0 | 92% | （2と同値のため未測） | |
| 4 packet | 27,136 / 8,192 | 47.3〜47.6 / 41.2〜41.3 | | | | |

各2回、pattern全照合PASS。2 packetで**1.66倍**になり、4 packetは2 packetと同じ。65,024 byte transferの所要時間は2,254→1,365 µsで、armed中のpacket間隔は10.7 µs（理論9.6 µs）になった。残り7〜12%はtransfer境界（8 KiBで22%）とhost側で、transfer長を伸ばすほど理論値に寄る。

### 5. streaming firmwareへの適用（E109＋2 packet FIFO、`stream/`）

| 項目 | FIFO 1 packet（E108 / E109） | FIFO 2 packet |
|---|---:|---:|
| USB-only probe（usbipd/WSL） | 247 Mbps、90%予算222 Mbps | **389 Mbps、90%予算350 Mbps** |
| 8-bit 60 Msps | PASS、退避0 | PASS、退避0、codec 81.1% |
| 8-bit 72 Msps | PASS、退避8.6 MB（bounce 332） | PASS、**退避0**、codec 95.5% |
| 8-bit 76 Msps | FAIL（codec 100%、queue overflow） | **PASS**（1回、codec 97.1%、退避0） |
| wide 40 / 52 / 56 Msps | PASS / PASS / PASS（56はcodec 99.4%） | PASS / PASS / PASS（56はcodec 99.2%） |

USB帰路は8-bit 76 Msps（237.5 Mbps）でも予算の68%で、退避FIFOが一度も使われない。76 MspsがPASSに変わったのは1回だけの観測で、退避copyによるPSRAM / bus競合がなくなりcore 1のcodecに約3 pointの余裕が出たと見るが、再現回数は足りない。

### 6. 手順上の注意

- 最初の掃引はhostのpattern照合に`memoryview`を保持したまま`bytearray`を縮めるbugがあり全run失敗した。修正後に再flashして測り直した。
- host側でrunが途中失敗するとdeviceは送りっぱなしになり、2 s後にstatusを書いて次のcommandへ進むため、以後数runがtimeoutで連鎖する。E110 hostは失敗時に読み捨てを試みるが、確実な復帰は再flash（reset）だった。
- FIFO 2 / 4 packetの`build_opt.h`は`-DE110_IN_FIFO_PACKETS=N`。最終状態は2。

## 事実 / 候補 / 未決

**事実**

1. P4のUSB HS bulk INの天井は、TinyUSB `dfifo_alloc()`がbulk INに割り当てるTX FIFOが1 packet分だったことにある。2 packetにすると同じhost・経路・firmwareで29.7→49.3 MB/s（理論53.2 MB/sの93%）、Windows nativeで27.7→47.1 MB/s（88%）。4 packetは2と同じ。
2. 1 packetのときはendpointが99.9%の時間armedでもpacket間隔が理論の約2倍（17.7 µs）で、device側の再arm隙間（1〜19 µs）、transfer長、host URB長・depth（≥2）、callback chainの有無は主因ではない。device CPUはこの間98%空いている。
3. E090が「hardware TX FIFOは天井原因ではない」としたのは、当時の律速がbuffered送信のcopy（25.6 MB/s）だったため。zero-copy後はFIFOが唯一の天井になる。
4. streaming firmwareに適用するとUSB-only 389 Mbps（90%予算350 Mbps）、8-bit 76 Msps・wide 56 Mspsまで退避なしで通る。残る律速はcodec（core 1）だけ。

**候補**

- EspUsbDeviceへの改修依頼: bulk IN endpointに2 packet分のTX FIFOを与える（TinyUSBの`tud_configure()` / `bm_double_buffered`で正規に指定できる）。P4 HSのDFIFO予算内で、他classと同居しても1つのbulk INなら収まる。
- 通常仕様のUSB予算は「probe→90%」のまま。値は経路で350 Mbps（usbipd/WSL）／339 Mbps（native）級になり、8-bit 3.125 bit/sampleなら約110 Msps、wide 3.3125 bit/sampleなら約105 Mspsまで予算内。上限を決めるのはcodecになる。
- device側transfer長は27 KiB以上、arm深さ2以上。8 KiBは78%に落ちる。

**未決**

- 残り7〜12%（transfer境界の1 packet分と、native側の差）。65,535 byteの16-bit上限を外す（TinyUSBの`usbd_edpt_xfer`はuint16）か、複数endpointの交互送出で埋まるかは未測。
- hub経路、P4 host（E102の36.2 MB/sはFIFO 1 packetの値。2 packetでの再測は別リグ）。
- 8-bit 76 MspsのPASSが安定するかは3回以上の再測が要る。

### 追記（2026-09-15、EspUsbDevice側sessionの独立実測）

ライブラリ側がCR-13（bulk IN TX FIFO 2 packet、DFIFO収支が収まるときだけ自動有効）を実装し、同じP4（esp32-p4-80f1b2d0b261、usbipd/WSL）で**buffered経路**（`waitWritable(writeCapacity())`＋`write()`、FIFO 4096/4096、pyusb 32 MB×3）を測った結果: 1 packet 22.98 / 22.61 / 21.63 MB/s → 2 packet **28.74 / 28.86 / 28.93 MB/s（+26%）**。本実験のzero-copy経路（29.7→49.3 MB/s）と伸び幅が違うのはcopy律速のぶんで、「FIFOだけで取れる分」がライブラリ既定構成での値。先方は採用を決めた（増えるのはDFIFOという固定資源だけで、収まらない構成では従来どおり1 packetに倒れる）。
