# E106 実captureからmixed-rate USB連続転送

状態: **40 Msps長時間PASS。45 / 47 / 48 Mspsは持続時にring追越し**

## 問い

E105で個別に確認した16-bit codecとUSB連続転送を、実際のPARLIO captureと同時に動かしたときの成立境界はどこか。

## 構成

```
PARLIO RX 16-bit / 40 Msps (80 MB/s)
  -> 64 sample単位codec (3 raw + 8 hold D=64)
  -> 25 byte/block (15.625 MB/s)
  -> 8 MiB PSRAM FIFO
  -> USB HS vendor IN
```

内部PARLIO TXの8 GPIOをRX lane 0..7と8..15へ複製する。したがって16-bit取り込み、codec、USBの結合試験だが、11本の独立外部GPIOの電気試験ではない。

PCは`E6 + uint64_le(blocks)`でcaptureを開始する。8192 blockでwire 204,800 byteとなり、codec blockとUSB packetの両方で端数が出ない。hostは受信内容についてGray codeの進行、4 sample周期、複製laneの一致を検査する。

USBだけの経路予算は`EP + uint64_le(bytes)`で測る。deviceは既知patternを最大速送信し、hostは実測Mbpsとその90%を`recommended_90pct_mbps`として表示する。capture生成rateで測らないため、USB経路上限とP4内部codec上限を分離できる。

## 結論

3 raw＋8 hold(D=64)の実capture→codec→PSRAM→USB結合経路は、**40 Mspsで持続PASS**した。wireは15.625 MB/s = 125 Mbpsである。13,107,200 byte / 524,288 blockを全検査し、次がすべて0だった。

- device側raw Gray連番違反
- 複製lane不一致
- PARLIO callback queue overflow
- PSRAM FIFO overflow
- PC側mixed-rate sequence不一致

captureは838.965 msで、67,108,864 raw byte / 80 MB/s = 838.861 msという理論時間と一致した。

45 Mspsは平均処理時間には追いついたが、長時間中の瞬間的なring遅れでraw連番284件、47 Mspsは974件となった。48 Mspsも短時間からringを追い越した。**現実装の11 logical lane構成は40 Mspsを安全点とし、45 Msps以上を持続対応とはしない。**

最初のE105 codec値145.899 MB/sは固定global入力だったため、コンパイラが4-byte `memcpy`を直接loadへ畳んだ値だった。runtime DMA pointerでは実関数呼び出しがblockあたり32回残り37 MB/sまで落ちた。aligned 32-bit direct loadへ修正すると停止中codecは約142〜149 MB/sへ戻ったが、実captureではPARLIO callbackとPSRAM spoolのjitterも含めて判定する必要がある。

codecとPSRAM copyを同じcoreで直列化すると48 Mspsに届かなかった。最終構成はRX/USB/spoolをcore 0、codecをcore 1へ分け、4個の内部RAM stageを介してcodecとPSRAM copyを並列化した。

USBの`short`はFIFOが空になったときにhostの大きなURBが早期完了した回数で、欠損ではない。40 Msps試験では全byteを受信できたがcallback負荷になるため、後続でdirect armまたはgateway側URB再投入を詰める。

## 再現

```sh
arduino-cli compile --profile esp32p4_device
arduino-cli upload --profile esp32p4_device --port /dev/ttyUSB0
uv run --with libusb1 python host_capture.py --periods 64 --depth 8
uv run --with libusb1 python host_capture.py --probe-bytes 64000000 --depth 8
```
