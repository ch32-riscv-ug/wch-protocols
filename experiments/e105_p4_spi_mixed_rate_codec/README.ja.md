# E105 channelごとに時間解像度を変えるmixed-rate形式

状態: **進行中 — 3 raw＋8 slowの共有bit packing、P4 codec、Windows/WSL実転送までPASS。11 lane/60 MspsのPARLIO前段は帯域超過**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E086](../e086_p4_8ch_stream/README.ja.md)、[E104](../e104_p4_windows_continuous_bulk/README.ja.md)

## 問い

**PCがchannelごとの時間解像度と縮約policyをcapture前に指定し、高速信号はそのまま、CS / INT / button等だけ粗くしてUSB・保存予算内へ収められるか。** 特定の60 MspsやDは固定仕様ではない。

## 結論

形式と変換は成立した。低速channelをchannelごとにbyte alignmentしてはいけない。各channelのbit列を連結し、**block全体の末尾にだけ0〜7 bitをpaddingする。** 64 sample、3 raw＋8 slow(D=64)なら

```
fast = 64 sample * 3 bit = 192 bit = 24 byte
slow = 1 value * 8 channels =   8 bit =  1 byte
total                         = 200 bit = 25 byte
```

低速8本を各1 byteへ切り上げる誤った形式は32 byteになる。共有bit packingなら25 byteで、60 Msps時23.4375 MB/sである。

P4実機では固定globalの16-bit snapshotからこの25-byte blockへの専用codecが**入力145.899 MB/s、出力28.496 MB/s**を出した。ただしE106のruntime DMA pointerで生成コードを比較すると、この値は4-byte `memcpy`が直接loadへ最適化された場合に限ることが分かった。runtime pointerのままでは約37 MB/s、aligned direct load化後は約142〜149 MB/sだった。capture callbackとPSRAM copyまで含む成立点は[E106](../e106_p4_mixed_rate_capture_stream/README.ja.md)の40 Mspsである。64 MBの事前生成streamはWindows nativeとusbipd/WSLの双方で全byte照合し、`bad=0 / short=0`だった。

ただし11 physical laneはPARLIO 16-bit modeになり、60 Mspsではcapture前段が120 MB/sになる。既存実測のring→PSRAM spool上限は約98 MB/sで、16-bitは48 Msps(95.884 MB/s)まで成立、52 Msps(103.878 MB/s)でdropした([E042](../e042_p4_parlio_16ch_seq_verify/README.ja.md))。**したがって11 lane/60 Msps continuousはcodec以前のcapture前段で成立しない。11 laneなら48 Msps以下が実測済みの安全側である。**

## 一般形式

PCから各channelへ次を指定する。

通常UIで選ばせる縮約率は **1/2、1/4、1/8、1/16、1/32、1/64** とする。reference形式は1/128、1/256、1/1024以上も表現できるが、deviceの現在の64-sample専用codecは1/64までである。1/64より下では3本のraw帯域がほぼ全体を占めて削減効果が小さい一方、複数blockをまたぐ状態と待ち時間が増えるため、初期仕様には含めない。

| mode | 意味 | 向く信号 | 損失 |
|---|---|---|---|
| `raw` | base sampleを全部保持 | CLK、MOSI、MISO、data bus | lossless |
| `decimate_hold(D, phase)` | D sampleごとに1点 | button、長時間変化しないstatus | D未満のpulseを見逃す |
| `any_active(D, polarity)` | bucket内にactiveが1点でもあればactive | CS、IRQ/INT | edge位置はbucket幅へ広がる |
| `edge_latch(D, polarity)` | bucket末尾levelとactive edge有無の2 bit | 短いINT、wake/event | 正確なedge位置は失う |

block sample数`B`は全Dの倍数とする。

```
payload_bits = sum(B / D[c] * bits_per_value[c])
wire_bytes   = ceil(payload_bits / 8)
padding_bits = (-payload_bits) mod 8
```

paddingは各channel末尾ではなくblock末尾の1回だけ。`B`を省略した場合、referenceは全Dの最小公倍数`L`を求め、`L` blockの総bit数がbyte境界になる最小倍率だけBを伸ばす。低速channelが8本なら各1 bitが互いを埋めるので、D=64でも`B=64`でよい。

wire上のchannel順、bit offset、mode、D、phase、polarityはcapture metadataへ残す。referenceの一般codecはplane-major、P4の3-fast専用codecは計算量を減らすため高速3 bitをsample-majorにするが、いずれもdescriptorで一意に復元でき、総bit数は同じである。

## 3 fast＋8 slowの予算

60 Mspsで8本のslowを同じDにすると`wire_MB_s = 22.5 + 60/D`となる。

| slow D | 最小block | byte/block | wire MB/s |
|---:|---:|---:|---:|
| 1 | 8 | 11 | 82.500 |
| 8 | 8 | 4 | 30.000 |
| 16 | 16 | 7 | 26.250 |
| 32 | 32 | 13 | 24.375 |
| **64** | **64** | **25** | **23.4375** |
| 128 | 128 | 49 | 22.96875 |
| 256 | 256 | 97 | 22.734375 |
| ∞ | — | — | 22.500 |

Dを無限にしても3 rawの床22.5 MB/sは残る。INが15〜17 MB/sの低速列挙状態ではD調整だけでは成立しない。

### 16 channel / 40 Mspsの推奨例

3 channelをraw、1 channelをD=8、残る12 channelをD=64にすると、論理帯域は`3×40 + 40/8 + 12×40/64 = 132.5 Mbps`となる。64-sample blockでは212 bitを27 byteへ丸めるためwireは135 Mbpsになるが、128-sample blockなら424 bit = 53 byteでpaddingがなく、wireも132.5 Mbpsちょうどになる。150 Mbps実測の90%である135 Mbps予算に収まる構成としてreference round-tripを固定試験に追加した。

11 laneを実測済み上限の48 Mspsへ落とすと、D=64は**18.75 MB/s**となりcapture前段95.9 MB/s・USB後段の両方へ収まる。60 Mspsを維持するならphysical laneを8本以下にする、またはSPI CLKを既知周期としてPCで再構成しraw送信から外す等、3 rawそのものを減らす必要がある。

## PC予算protocol

PCはcapture前に`base_rate_hz`、`sample_count`、`output_budget_bytes`または最大持続rate、channelごとの`mode / D / polarity / phase`を送る。deviceは次を返してからcaptureを開始する。

- `block_samples / payload_bits / padding_bits / wire_bytes`
- PARLIOの実physical widthとraw capture byte rate
- 必要な総raw byte、総wire byte、PSRAM量
- 実測済みcapture上限とUSB安全rateに対する`ACCEPT / REJECT`

wire予算だけ合ってもraw capture前段が合わなければrejectする。今回の11 lane/60 Mspsがその例である。

有限batchは`ceil(requested_samples / B) * B` sampleを一度だけ継ぎ目なくcaptureし、最後の余剰をPCで捨てる。小さいbatchを反復して繋ぐとcapture間gapが入るので行わない。

## PulseViewへの出し方

stock BeagleLogic protocolは`get`時に要求sample数をserverへ伝えず、必要量を読んだclientが`close`する。gatewayはdeviceからcodec block境界で大きめに受信し、blockを復元してPulseViewへ小分け出力し、`close`で停止して先読み分を捨てる。

USB transfer境界はcodec block境界と同一である必要はない。ただし余分なshort packetを避けるなら送信周期は`LCM(codec block byte, 512)`へ揃える。25-byte blockでは12,800 byteであり、実機試験はこれを8,192＋4,608 byteのUSB armに分けた。

## 実装と検証

### host reference

`codec.py`にchannel descriptor、共有bit packing、末尾padding、encode/decode、budget計算を実装した。直接実行した3 testはすべてPASSした。

- random 3-fast＋1-CS reference: 高速lane完全一致、CS policy一致
- 3 raw＋8 hold(D=64): 31 block、各block25 byte、低速8本がbyte 24を共有
- 3 raw＋7 hold(D=64): payload 199 bit、paddingはblock末尾の1 bitだけ
- D=2 / 4 / 8 / 16 / 32 / 64: raw lane完全一致、`decimate_hold`とactive-low `any_active`の復元規則がすべてPASS

### P4 codecの深掘り

同じ262,144 blockをP4 rev 1.3で変換した。入力rateは16-bit raw換算、wire rateは25-byte出力換算。

| 実装 | input MB/s | wire MB/s | 判定 |
|---|---:|---:|---|
| 1 bitごとの汎用set | 15.905 | 3.106 | 不可 |
| lane別8-bit pack | 38.780 | 7.574 | 不可 |
| 3 plane同時pack | 41.677 | 8.140 | 不可 |
| 3-bit/sample、16-bit load | 119.658 | 23.371 | 120 MB/sに0.3%不足 |
| **3-bit/sample、32-bit pair load** | **145.899** | **28.496** | **演算単体PASS** |

高速laneをbitplane転置するより、8 samplesの3-bit値を24-bitへpackする方が約3.5倍速い。低速8本はblock末尾の1 byteを共有する。

### PCとの実連続転送

対象は第三P4 `80:f1:b2:d0:b2:61`。HSは既存のusbipd bindingを使い、Windows nativeとWSLの両方から同じfirmwareを読んだ。最終firmwareは12,800-byte patternを8,192＋4,608 byteで連続armする。

| host | byte | host MB/s | P4時計 MB/s | short / bad |
|---|---:|---:|---:|---:|
| Windows native、現在のlow列挙 | 64,000,000 | 15.195 | 15.187 | 0 / 0 |
| WSL usbipd、全byte照合 | 64,000,000 | **23.342** | **25.105** | 0 / 0 |
| WSL usbipd、長時間・照合なし | 524,288,000 | 終端計時修正前のため参考外 | **24.848** | 0 / 0 |

WSLのP4時計ではD=64の23.4375 MB/sを上回る。一方Python host wallは全byte照合時23.342 MB/sで0.4%不足し、実用marginは無い。D=128でも必要22.969 MB/sなので余裕は約1.6%。11 laneなら48 Mspsへ下げる方が堅い。

Windowsの15.2 MB/sはE104のIN二状態のlow側で、同じbinaryでも列挙状態により約15〜25 MB/sへ変わる。capture開始前のrate probeとreject/fallbackが必要である。

送信単位も「大きいほど良い」ではなかった。100 KiBは15 MB/s台、32,000 byteはshort packet条件でhost callback負荷が増えた。途中の25,600 byte試験で見えた終端stallはhostが最後も1 MiB URBを要求した計測バグで、hostは総量を先にURBへ割り当て最後のURBを正確な残量にするよう修正した。

## 未決

- [E106](../e106_p4_mixed_rate_capture_stream/README.ja.md)でPARLIO 16-bit実captureへ接続済み。40 Msps持続PASS、45 Msps以上はring追越し。
- 60 Mspsを維持する場合のCLK再構成、8-lane以内へのpin選択、または別low-speed GPIO samplerを比較する。
- Windows/WSL列挙ごとのIN rate probeと自動fallbackをprotocolへ入れる。
- `edge_latch`のedge metadataをPulseView annotationへ渡す。

## 再現

```sh
PYTHONPATH=experiments/e105_p4_spi_mixed_rate_codec python3 - <<'PY'
import e105_p4_spi_mixed_rate_codec as t
for name in sorted(n for n in dir(t) if n.startswith("test_")):
    getattr(t, name)()
PY

cd experiments/e105_p4_spi_mixed_rate_codec/device
arduino-cli compile --profile esp32p4_device
arduino-cli upload --profile esp32p4_device --port /dev/ttyUSB0

cd ..
uv run --with libusb1 python host_usb.py --chunks 5000 --depth 8
```
