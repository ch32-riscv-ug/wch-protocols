# E106 8/16-bit実captureからmixed-rate USB連続転送

状態: **完了 — 8-bit安全値60 Msps、16-bit wide安全値40 Msps、現USB経路は32 Mspsで連続転送PASS**

## 問い

E105で個別に確認した16-bit codecとUSB連続転送を、実際のPARLIO captureと同時に動かしたときの成立境界はどこか。

## 構成

```
PARLIO RX 8/16-bit
  -> mixed-rate codec
     8-bit: 3 raw + 5 hold D=64
     16-bit legacy: 3 raw + 8 hold D=64 (64 sample -> 25 byte)
     16-bit wide: 3 raw + 1 D=8 + 12 D=64 (128 sample -> 53 byte)
  -> 8 MiB PSRAM FIFO
  -> USB HS vendor IN
```

内部PARLIO TXの8 GPIOをRX lane 0..7と8..15へ複製する。したがって16-bit取り込み、codec、USBの結合試験だが、11本の独立外部GPIOの電気試験ではない。

PCは16-bit legacyを`E6`、8-bitを`E8`、16-bit wideを`EW`で開始する。USBを除いた内部sink試験は先頭を`I`へ変えた`I6` / `I8` / `IW`である。後ろは共通して`uint32_le(rate_hz) + uint64_le(blocks)`である。firmwareを書き換えずprofileとrateを掃引できる。hostは受信内容についてGray codeの進行、4 sample周期、16-bit時の複製lane一致、8-bit時のpadding zeroを検査する。

USBだけの経路予算は`EP + uint64_le(bytes)`で測る。deviceは既知patternを最大速送信し、hostは実測Mbpsとその90%を`recommended_90pct_mbps`として表示する。capture生成rateで測らないため、USB経路上限とP4内部codec上限を分離できる。

## 結論

### 8-bit: 3 fast＋5 slow

USBを律速から外し、codec出力をPSRAMへ循環書込みする内部持続試験を行った。167,772,160 sampleを各rateで処理した結果、60 Mspsと61 Mspsは連番違反、callback queue overflowともに0で、61 Mspsは3回連続PASSした。62 Mspsではqueue overflow 200、連番違反390が発生し、以降は処理時間が約2.726秒で頭打ちになった。したがって現実装の成立境界は61 Msps、安全設定は**60 Msps**とする。

| base rate | raw入力 | wire生成 | 判定 |
|---:|---:|---:|---|
| 60 Msps | 60 MB/s | 23.4375 MB/s | 3回PASS |
| **61 Msps** | **61 MB/s** | **23.828125 MB/s** | **3回PASS** |
| 62 Msps | 62 MB/s | 24.21875 MB/s | FAIL |
| 64〜70 Msps | 64〜70 MB/s | 25〜27.34375 MB/s | FAIL |

現在のhub 2段＋usbipd/WSL経路でも、8-bit / 32 Mspsを65.536 MB連続送信し、PC復元、raw連番、padding、queue/FIFO overflowのすべて0を確認した。8-bitでも全8 channelをbase rateで送るのではなく、3 fast＋5 slowへ縮約することでwireは3.125 bit/base sampleとなる。

### 16-bit wide: 3 fast＋1 D=8＋12 D=64

製品説明の16 channel代表構成を128 sample→53 byteで実装した。40 Msps時の論理・wire帯域はいずれも`3×40 + 40/8 + 12×40/64 = 132.5 Mbps`で、block末尾paddingはない。

最初の1-bitずつ配置する実装は39.5 Mspsまで成立し、40 Mspsでringを追い越した。D=64の2個の12-bit snapshotを固定bit-spread演算で交互配置すると、40 Msps / 167,772,160 sampleを3回連続PASSした。各回ともraw連番、複製lane、callback queue、PSRAM sink overflowは0だった。これにより**16 channel通常上限40 Msps**は代表profileでも維持できる。

現在のhub 2段＋usbipd/WSLでは、32 Msps / wire 106 Mbpsを69,468,160 byte連続転送し、PC復元、device raw連番、複製lane、queue/FIFO overflowのすべて0を確認した。codecの53-byte境界をそのままUSB transfer終端にするとshort packetが連発しusbipdがtransfer errorになったため、PSRAM FIFO上で境界を分離し、最終回以外は512 byte単位でUSBへ渡すようにした。codecは大きな論理blockでpackingし、USBは独立して分割する構成が必要である。

### 16-bit legacy: 3 fast＋8 slow

3 raw＋8 hold(D=64)の実capture→codec→PSRAM→USB結合経路は、**42 Mspsで65.536 MBを長時間PASS**した。wireは16.40625 MB/s = 131.25 Mbpsである。次がすべて0だった。

- device側raw Gray連番違反
- 複製lane不一致
- PARLIO callback queue overflow
- PSRAM FIFO overflow
- PC側mixed-rate sequence不一致

raw入力335,544,320 byteのcaptureは3.994659 sで、84 MB/sから求める理論3.994575 sと一致した。最大ring未読は37,184 / 65,536 byteだった。

同じ13.1072 MBを掃引すると、40 Mspsは最大ring未読12,992 byte、42 Mspsは21,056 byteでPASSした。43 Mspsは3回中1回だけPASSし、他は65,408 / 163,968 byteまで遅れてFAILした。44 Mspsは486,976 byte、46 / 48 / 49 Mspsはさらに大きくringを追い越した。**現実装の11 logical lane構成は42 Mspsを持続上限、40 Mspsを余裕を持つ設定とする。**

| base rate | raw入力 | wire | 最大ring未読 | 判定 |
|---:|---:|---:|---:|---|
| 40 Msps | 80 MB/s | 15.625 MB/s | 12,992 B | PASS |
| **42 Msps** | **84 MB/s** | **16.40625 MB/s** | 21,056 B、65.536 MB時37,184 B | **PASS** |
| 43 Msps | 86 MB/s | 16.796875 MB/s | 28,224〜163,968 B | 不安定 |
| 44 Msps | 88 MB/s | 17.1875 MB/s | 486,976 B | FAIL |
| 46 Msps | 92 MB/s | 17.96875 MB/s | 1,151,040 B | FAIL |
| 48 Msps | 96 MB/s | 18.75 MB/s | 2,258,368 B | FAIL |
| 49 Msps | 98 MB/s | 19.140625 MB/s | 4,717,184 B | FAIL |

16-bit rawをPSRAMへ移すだけのE042は48 Msps / 約96 MB/sまで成立したため、mixed-rateの詰め替えpipeline追加による低下は48→42 Msps、約12.5%である。停止中codec演算単体は約142〜149 MB/s相当なので、律速はbit演算だけではなくPARLIO callback、64-sample block境界、stage queue、PSRAM spoolの合成である。USB直結が30 MB/s以上なら今回のwire上限16.40625 MB/sを上回るため、P4内部pipelineが先に律速する。

最初のE105 codec値145.899 MB/sは固定global入力だったため、コンパイラが4-byte `memcpy`を直接loadへ畳んだ値だった。runtime DMA pointerでは実関数呼び出しがblockあたり32回残り37 MB/sまで落ちた。aligned 32-bit direct loadへ修正すると停止中codecは約142〜149 MB/sへ戻ったが、実captureではPARLIO callbackとPSRAM spoolのjitterも含めて判定する必要がある。

codecとPSRAM copyを同じcoreで直列化すると48 Mspsに届かなかった。最終構成はRX/USB/spoolをcore 0、codecをcore 1へ分け、4個の内部RAM stageを介してcodecとPSRAM copyを並列化した。

USBの`short`はFIFOが空になったときにhostの大きなURBが早期完了した回数で、欠損ではない。40 Msps試験では全byteを受信できたがcallback負荷になるため、後続でdirect armまたはgateway側URB再投入を詰める。

### 現在のUSB経路を含む選択

HS hub 2段＋usbipd/WSL＋buffered送信で`EP`を64 MB実行した。

| byte | host実測 | 90%推奨予算 | pattern |
|---:|---:|---:|---:|
| 64,000,000 | 120.860 Mbps | **108.774 Mbps** | bad 0 / short 0 |

40 Msps構成は125 Mbpsなので、このUSB予算ではrejectする。fallbackの32 Mspsは3.125 bit/base sample×32 Msps = **100 Mbps**となり予算内である。直結で30 MB/s以上出る経路ならUSBより内部42 Msps上限が先に効く。

32 Mspsで65,536,000 wire byte / 2,621,440 blockを連続captureした。raw入力335,544,320 byte、capture 5.243010 sで、理論5.242880 sと一致した。raw連番、複製lane、queue/FIFO overflow、PC側sequenceはすべて0だった。したがって現在の接続に対する実用設定は32 Mspsである。

説明では分かりやすく「probe実測120 Mbpsなら90%の108 Mbpsを予算とし、100 Mbps構成を選ぶ」と丸めてよい。別のPC/直結で200 Mbps出た場合は180 Mbpsを予算とする。

## この実験の境界

E106で完了したのは、固定した8-bit / 16-bit代表profileについて、内部capture→codec→PSRAM上限とUSB経路予算を分離し、実連続転送まで成立させること。任意channel descriptor、probe後の自動ACCEPT / fallback、mixed-rate PulseView gateway、16本独立外部GPIOは後続課題とし、[P4ロードマップ](../../references/p4-probe-roadmap.ja.md)で管理する。

## 再現

```sh
arduino-cli compile --profile esp32p4_device
arduino-cli upload --profile esp32p4_device --port /dev/ttyUSB0
uv run --with libusb1 python host_capture.py --width 8 --internal --rate-mhz 60 --periods 320
uv run --with libusb1 python host_capture.py --width 8 --rate-mhz 32 --periods 320 --depth 8
uv run --with libusb1 python host_capture.py --width 16 --wide-profile --internal --rate-mhz 40 --periods 160
uv run --with libusb1 python host_capture.py --width 16 --wide-profile --rate-mhz 32 --periods 160 --depth 8
uv run --with libusb1 python host_capture.py --rate-mhz 42 --periods 64 --depth 8
uv run --with libusb1 python host_capture.py --probe-bytes 64000000 --depth 8
```
