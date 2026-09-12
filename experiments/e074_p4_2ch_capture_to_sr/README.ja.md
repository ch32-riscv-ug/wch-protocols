# E074 ESP32-P4 2 channel capture から sigrok `.sr` まで通す

状態: **完了 — 160 Mspsまでsample精度で取れ、`.sr`にしてsigrokが読み戻せる。16 Mi sampleの深さも通った**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E067](../e067_p4_usb_vs_capture_core/README.ja.md)(capture経路)、[E031](../e031_p4_parlio_channel_width/README.ja.md)(channel幅とpacking) / 設計: [PulseView / sigrok 連携](../../references/pulseview-integration.ja.md)

## 問い

**ESP32-P4で2 channelのPARLIO captureを取り、hostで sigrok の `.sr` に変換して sigrok / PulseView が読み戻せるところまで通るか。どのsample rateまでsample単位の欠落なしに取れるか。**

## 仮説

**通る。rateは160 MHzまで行ける。**

[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)の限界matrixで、1 / 2 / 4 channelは**内部clock源の上限である160 MHzまで成立**し、packing後のbyte rateも2 channelなら40 MB/sで持続spool帯域(約98 MB/s)に余裕がある、とされている。**ただしその検証はdutyとedge数によるもので、sample単位の欠落は検出できない**と[E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)が実証している。**この実験はそこをsample単位で埋める。**

## 反証条件

1. 160 MHzでoverflowまたは取りこぼしが出る
2. **周期がずれる**(= sampleが落ちている)
3. `.sr`をsigrokが読めない、channel数やsample rateが食い違う
4. 深い capture(16 Mi sample)が通らない

## 方法

### capture

- PARLIO RX、**data_width 2**、sample rateを振る
- 信号源は**LEDC PWM 100 kHz**を`PARLIO_PINS`の先頭2本(GPIO 2, 3)へ出し、**GPIO matrix経由でPARLIO RXが読み戻す**。**外部配線は不要**
- **duty 25%(64/256)と50%(128/256)**を別channelに割り当て、host側で識別できるようにする
- internal DMA ring 64 KiB → `on_partial_receive` → queue → harvest が PSRAM へ `memcpy`

### download

**USB-Serial-JTAG(console)で raw binary を流す。** `DUMP bytes=<n>` の1行の直後に、他に何も挟まず n byte を送る。

> OTG HS port は[E072](../e072_p4_hs_device_to_host_native/README.ja.md)/[E073](../e073_p4_hs_hid_throughput/README.ja.md)のために2枚目のboardへ配線されているので、この実験ではconsole経由にした。**経路を差し替えれば[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)の10.74 MB/sがそのまま使える。**

### host側

`capture_to_sr.py` が

1. `C <bytes> <rate>` で capture させ、`D` で download する
2. **packed 2 bit/sample(4 sample/byte、LSB first)を 1 byte/sample へ展開する**
3. `.sr`(zip: `version` / `metadata` / `logic-1-1`)を書く
4. **立ち上がりedgeの間隔**を測る

**展開はhost側で行う。** `.sr` は 1 sample = 1 byte なので2 channelでは**4倍に膨らむ**。**線の上はpackedのまま運ぶ**のが要点([PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) §5)。

### 欠落の判定

**dutyでは判定しない。** 周期的な信号源ではsampleが落ちてもdutyはほぼ変わらない([E036](../e036_p4_parlio_rate_seq_verify/README.ja.md))。**立ち上がりedgeの間隔**を全部測り、**min / max / mean が期待値(`rate ÷ 100 kHz`)と一致するか**で見る。**1 sampleでも落ちればどこかの間隔が短くなる。**

## 対象外

- 3 channel以上、trigger、pre/post、circular ring
- OTG HS経由でのdownload(経路の差し替えだけ。[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md)で測定済み)
- PulseViewのGUIで開く操作(`sigrok-cli`が読めれば同じlibsigrokの経路)
- IP経由(`beaglelogic`のTCP emulation。[PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) §2)
- 外部信号の取り込み(信号源は内部PWM)

## 結果

### rate掃引(524,288 sample = 131,072 packed byte)

| sample rate | overflow | timeout | D0 duty | D1 duty | **周期 min / mean / max** | 期待周期 |
|---:|---:|---:|---:|---:|---|---:|
| 32 MHz | 0 | 0 | 25.01% | 50.01% | **320 / 320.00 / 320** | 320 |
| 64 MHz | 0 | 0 | 25.01% | 50.01% | **640 / 640.00 / 640** | 640 |
| 128 MHz | 0 | 0 | 25.00% | 50.02% | **1280 / 1280.00 / 1280** | 1280 |
| **160 MHz** | 0 | 0 | 24.99% | 50.02% | **1600 / 1600.00 / 1600** | 1600 |

**どのrateでも min = mean = max = 期待値。524,288 sample の中に欠落が1つも無い。**

### 深いcapture

```
CAP status=0 rate_hz=160000000 bytes=4194304 samples=16777216 overflow=0 timeout=0 elapsed_us=104973
  D0: high  25.00%  period mean  1600.00 min 1600 max 1600 samples (expected 1600)
  D1: high  50.00%  period mean  1600.00 min 1600 max 1600 samples (expected 1600)
```

**16,777,216 sample(4 MiB packed)を160 Mspsで104.9 ms、欠落0。**

### `.sr` の読み戻し

```
$ sigrok-cli -i p4_2ch_deep.sr --show
Samplerate: 160000000
Channels: 2
- D0: logic
- D1: logic
Logic unitsize: 1
Logic sample count: 16777216
```

32 MHzのファイルを `--samples` で覗くと、両channelが同時に立ち上がり、**D0は80 sample後、D1は160 sample後に落ちる**(320 sampleの周期に対し25%と50%)波形がそのまま見える。

### download

| 取得量 | 所要 | 実効 |
|---:|---:|---:|
| 131,072 B | 0.16 s | 0.80 MB/s |
| 4,194,304 B | 5.82 s | **0.72 MB/s** |

USB-Serial-JTAG(FSのCDC)経由。**[harness-channels](../../references/harness-channels.ja.md)の「native USB CDC ×1 = ~1 MB/s(FS)」という見積りと整合する。**

`.sr` のファイルサイズは 16 Mi sample で **102,787 byte**(zip deflate)。周期的な信号なのでよく縮む。

## 事実

1. **2 channelのcaptureは160 Mspsまでsample単位で正確である。** 32 / 64 / 128 / 160 MHzすべてで**周期のmin = max = 期待値**、overflow 0、取りこぼし0。**反証条件1・2はいずれも否定された。**
2. **160 MHzはP4の内部clock源(PLL_F160M)の上限**であり、[限界matrix](../../references/p4-logic-analyzer-investigation.ja.md)の「1 / 2 / 4 channelは160 MHz成立」に**sample単位の裏付けが付いた**([E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)が未検証と断っていた行)。
3. **深さは16 Mi sample(104.9 ms @ 160 Msps)まで通る。** PSRAMは32 MiBあるので、firmwareの4 MiB上限を上げればさらに伸びる。
4. **`.sr` は sigrok が正しく読み戻す。** channel数・sample rate・sample数・波形すべて一致。
5. **packedのまま運んでhostで展開する方式が成立する。** 2 channelでは`.sr`が4倍に膨らむので、**線の上でpackedのままにするだけで転送量が1/4になる**。
6. download は console(FS CDC)で **0.72〜0.80 MB/s**。**深さを使い切ると律速はここになる** — 4 MiBに5.8秒。

## 近直の目標への到達

| 目標 | 状態 |
|---|---|
| **2chで数十Msps** | **160 Msps で達成**(目標の約5倍)。sample精度も確認済み |
| **ローカルで`.sr`保存** | **達成**。`capture_to_sr.py` が書き、`sigrok-cli`が読む |
| **ch単位の詰め替え** | **packedのまま運んでhostで展開**する形で達成。転送量は1/4 |
| PulseViewで開く | `.sr`を開くだけ。**IP経由は[別途](../../references/pulseview-integration.ja.md)** |

**残る律速はdownloadだけ。** consoleの0.72 MB/sをOTG HSのvendor bulk(**10.74 MB/s**、[E071](../e071_p4_hs_vendor_fifo_depth/README.ja.md))へ差し替えれば、**4 MiBは5.8秒から0.39秒**になる。

## 候補

- **capture側は2 channelについては完了扱いでよい。** 160 Mspsはhardwareの天井で、そこまでsample精度が取れている
- **downloadをOTG HSへ差し替える**(採用の方向)。経路を変えるだけで15倍
- **packedのまま運ぶ**(採用)。展開はhost

## 未決

- **OTG HS経由でのdownloadと組み合わせた通し** `—`。いまHS portは2枚目のboardへ配線されている
- **連続streaming**(batchではなく流しっぱなし) `—`。[E067](../e067_p4_usb_vs_capture_core/README.ja.md)の釣り合い点は約29.7 Msps(CDC)、vendor bulkなら約43 Msps
- **4 MiBを超える深さ** `—`。PSRAM 32 MiBのうち使っているのは4 MiB
- **外部信号** `—`。信号源は内部PWMで、配線した実信号は未検証
- **3 channel以上での同じ検証** `—`
- trigger / pre-post / circular ringとの組み合わせ `—`

## 影響

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) 限界matrix — **2 channel行の「160 MHz成立」にsample単位の裏付け**が付いた(※印の注記を外せる)
- [PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) §4 `.sr`を書く — **実装して通した**
- 近直の目標「2chで数十Msps、ローカルで`.sr`保存」— **達成**
