# E104 Windows PCとの連続bulk転送

状態: **完了 — Windows nativeで512 MiB×両方向×3 run、OUT 29.721 / IN 24.200 MB/s、不一致0**（2026-09-14）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E097](../e097_p4_vendor_out_direct_rx/README.ja.md)、[E102](../e102_p4_vendor_in_zero_copy_precomputed/README.ja.md)

## 問い

**P4のUSB HS deviceをWindows PCのWinUSBから実際に連続駆動したとき、PC→device（bulk OUT）とdevice→PC（bulk IN）は、データ完全性を保って何MB/sで持続するか。**

## 仮説

P4同士でcritical pathを揃えたE097/E102と同様、方向差は1.2倍以内になる。PC host controllerとWinUSB/libusbのURB投入が十分なら、OUTは約40 MB/s、INは約36 MB/sに近づく。

## 反証条件

一方向でも欠落・pattern不一致が出る、または3 runの中央値で方向差が1.2倍を超える。短いburstだけ速く見えるのを避けるため、各方向512 MiB以上を連続転送する。

## 方法

- WindowsにHS接続された第三のESP32-P4（UART consoleは`esp32-p4-80f1b2d0b261`）だけへdevice firmwareを書き込む。P4同士の直結ペアとS3のフルテスト環境は触らない。
- USBIPD/WSLへattachせず、Windows上のWinUSBをPyUSB + libusb-1.0 backendから直接使用する。
- 1 runにつきOUT 512 MiB、IN 512 MiB。hostは転送中に既知の0..255 patternを逐次照合し、全量をRAMへ保持しない。
- deviceはOUTをdirect RX（32 KiB arm）で逐次照合し、INは事前生成32 KiB bufferをcallback-chain zero-copyで送る。Windowsでは8 KiBより32 KiBの方が速かったため、短い校正後に固定した。
- 3 runを行い、host/deviceそれぞれのbyte数・elapsed・不一致数、Windows上のPnP service、USB endpoint MPSを保存する。

同期APIの対照は`uv run --with pyusb --with libusb python host_windows.py`、本測定は複数transferを常時投入する`uv run --with libusb1 python host_windows_async.py`で起動する。どちらもdevice driverはWindowsが自動bindしたWinUSBのままにする。

## 対象外

同時双方向、USBIPD経由、P4 hostとの再比較、公開API化、CPU使用率・消費電力。

## 必要な環境 / ベンチ種別

Windows PCへOTG HS接続されたESP32-P4 1枚。一時。USBIPDの管理者bindは不要。

## 記録する数値 / 完了条件

両方向3 runのhost MB/s、device MB/s、転送byte数、pattern不一致、中央値/min/max、OUT/IN中央値比。全runで512 MiB完全一致し、PnP serviceがWinUSB、MPS=512なら完了。

## 影響

P4同士で得た方向差1.10がPCの実用経路でも成立するかを確定し、host実装固有の投入性能とdevice側critical pathを分離できる。

## 結果

生ログ: `_runs/E104_20260914T071954JST_windows_native/`。対象は`80:f1:b2:d0:b2:61`、Windowsでは`303a:4021`、`speed=3`（high speed）、bulk IN/OUTともMPS 512。PnPは`Status=OK` / `CM_PROB_NONE` / `Service=WINUSB`。USBIPD bindは行っていない。

### 完全照合ありの連続転送

hostは1 MiB transferをdepth 16でqueueした。各runはOUT 512 MiB + IN 512 MiBで、合計3 GiB。

| run | PC→P4 OUT host | P4時計 | P4→PC IN host | P4時計 | short / mismatch |
|---:|---:|---:|---:|---:|---:|
| 1 | 29.757 | 29.751 | 24.089 | 24.084 | 0 / 0 |
| 2 | 29.721 | 29.716 | 24.224 | 24.213 | 0 / 0 |
| 3 | 29.704 | 29.698 | 24.200 | 24.195 | 0 / 0 |
| **median** | **29.721 MB/s** | **29.716** | **24.200 MB/s** | **24.195** | **0 / 0** |

host中央値のOUT/IN比は**1.228倍**。仮説の1.2倍以内を2.3%超えたため、反証条件が成立した。run間の幅はOUT 0.053、IN 0.135 MB/sで、512 MiB中の失速や時間依存低下は無い。

hostとP4自身の時計はOUTで0.02%、INで0.05%以内に一致する。Pythonの照合や結果処理を分母へ誤って入れた速度ではない。

### host queue depth

64 MiB、完全照合あり、1 MiB/transferの校正:

| depth | OUT MB/s | IN MB/s |
|---:|---:|---:|
| 1 | 29.123 | 22.706 |
| **2** | **29.709** | **24.117** |
| 4 | 29.739 | 24.146 |
| 8 | 29.727 | 24.197 |
| 16 | 29.772 | 24.144 |

**depth 2で飽和する。** 4〜16を積んでも変わらないため、残る方向差を「Windows user-modeがURBを間に合わせられない」で説明できない。

### 約1.5倍差の再現と、INの二状態

OUTの全byte照合はdirect RX completion callback内で行うので、その間は次の受信をarmできない。照合を外した同一構成では、再列挙後の64 MiB校正で**OUT 34.960 / IN 24.029 MB/s = 1.455倍**となり、問題にしていた約1.5倍差を再現した。完全照合ありではOUTだけ29.7へ下がるため、見かけの比率が1.23へ縮む。

同じ照合なしbinaryを変更せずhard reset・再列挙したところ、INは次の2状態になった。

| 同一binary | OUT | IN | OUT/IN |
|---|---:|---:|---:|
| 列挙A、連続2回 | 34.822 / 34.909 | **16.834 / 16.802** | 2.07 |
| hard reset後の列挙B | 34.960 | **24.029** | **1.455** |

OUTはどちらでも35 MB/s前後だが、INだけ約16.8 / 24.0 MB/sに分かれ、その列挙中は安定する。firmware byte列を変えずresetだけで遷移したので、producer生成やmemcpyではない。これは従来の短いrunで見えていた大きなばらつきを、**列挙単位の状態差**として再現したもの。

最後に全byte照合ありfirmwareへ戻して再flashした4 MiB smokeでも、OUT 29.664に対しINはhost 15.361 / device時計16.644 MB/sとなり、**照合ありbinaryでもlow側へ遷移した**。したがって二状態は照合defineやbinary layoutの違いではない。`final_firmware_smoke.log`に保存した。

### 手順上の除外

最初の校正は`CFG_TUD_VENDOR_RX_EPSIZE`だけを32 KiBにし、`CFG_TUD_VENDOR_RX_NEED_ZLP=1`を付け忘れた。その場合TinyUSBの実armはendpoint MPSの512 Bのままで、OUTは約8.2 MB/sだった。consoleに表示した`RX_EPSIZE`は実arm長ではない。この値は比較から除外し、以後は`RX_NEED_ZLP=1`を固定した。

## 事実

1. **PCとのnative WinUSB連続転送は成立する。** 3 GiB、全runでhost/device byte数一致、short 0、pattern不一致0。
2. **完全照合を含む実用経路はOUT 29.721 / IN 24.200 MB/s、1.228倍。** P4同士の1.10倍までは縮まらない。
3. **転送だけなら約1.5倍差を再現する。** high側列挙でOUT 34.960 / IN 24.029 = 1.455倍。
4. **host queueはdepth 2で十分。** depth 4〜16にしても増えないので、Python/libusbの同期・投入間隔は主因ではない。
5. **INには列挙単位の約16.8 / 24.0 MB/sという二状態がある。** 同一binary・同一配線・同一host process条件でもhard reset後に遷移した。OUTには同じ遷移がない。

## どこに差があるか

現時点で差の場所は二つに分かれた。

- **約35→30 MB/s（OUTのみ）**: deviceのdirect RX callback内で行う全byte照合。callbackが戻るまで次のOUT armが遅れるapplication critical path。
- **約35対24 MB/s（1.45倍）の土台**: depth 2でhost queueが飽和し、host/P4時計も一致するため、Windows user-modeより下。さらに同一binaryのresetでINだけ16.8/24.0を遷移するため、**USB reset/enumeration時に決まるP4 DWC2 TX側状態、またはWindows xHCIのbulk IN token発行状態**が残る候補。USBPcapが見せるのはURB境界でmicroframe内tokenそのものではないため、どちらか一方への確定はまだできない。

512 B packet換算では、照合なしhigh側のOUT 34.960は平均**8.54 transaction/microframe**、IN 24.029は**5.87**、low側IN 16.802は**4.10**に相当する。これはpacketを直接captureした値ではなく帯域からの換算だが、INの二状態がほぼ「6対4 transaction/microframe」に見えることは、reset時のendpoint/controller scheduling状態という候補と整合する。

P4 host相手ではdirect RX 39.737 / zero-copy TX 36.159 = 1.10倍だったのに、Windows相手では同じ方向の差が1.45倍ある。したがって**固定的なP4 PHYの方向非対称ではない**。次に決着させるなら、同一device firmwareをresetごとに複数回測りながらDWC2 endpoint/FIFO registerをdumpし、16.8/24.0状態間で差があるかを見る。差が無ければWindows xHCI側のtoken scheduleが残る。
