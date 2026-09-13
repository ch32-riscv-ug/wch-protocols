# E105 channelごとに時間解像度を変えるmixed-rate形式

状態: **進行中 — 一般budget計算と60 M SPI例のhost codecはPASS、P4実装待ち**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E086](../e086_p4_8ch_stream/README.ja.md)、[E104](../e104_p4_windows_continuous_bulk/README.ja.md)

## 問い

**PCがchannelごとの時間解像度と縮約policyをcapture前に指定し、高速信号はそのまま、CS / INT / button等だけ粗くしてUSB・保存予算内へ収められるか。** 60 Msps SPIでCSだけ8:1にする形は最初の具体例であり、固定仕様ではない。

## 仮説

各channelを`raw`、`decimate_hold`、`any_active`、`edge_latch`のいずれかと倍率Dで表し、共通block内でbitplane化できる。有限長batchならPCが先に出力byte予算を渡し、P4がcapture後にin-place変換して返せる。

## 反証条件

- 高速3chが全sample一致で戻らない。
- CSが定義した8-sample bucket規則どおり戻らない。
- P4上の変換速度がraw入力30 MB/sを下回り、continuous pipelineに使えない。
- USBへ同時送出したときFIFOが時間に比例して増える。

## 一般形式

PARLIOの全laneは同じbase rateでcaptureする。laneごとのhardware sample clockは変えられないので、低rate化はcapture後のcodecで行う。

PCから各channelへ次を指定する。

| mode | 意味 | 向く信号 | 注意 |
|---|---|---|---|
| `raw` | base sampleを全部保持 | CLK、MOSI、MISO、data bus | lossless |
| `decimate_hold(D, phase)` | D sampleごとに1点 | button、長時間変化しないstatus | D未満のpulseを見逃す |
| `any_active(D, polarity)` | bucket内にactiveが1点でもあればactive | CS、IRQ/INT | pulseは残るがedge位置はbucket幅へ広がる |
| `edge_latch(D, polarity)` | edge有無とbucket末尾levelを残す | 短いINT、wake/event | format overheadが増えるが見逃しにくい |

PC側は低rate channelをbase sample gridへhold展開する。sigrok/PulseViewには通常の等間隔sampleとして渡せるが、粗くしたchannelのedge時刻は元に戻らない。その損失はmetadataにも残す。

block sample数`B`は全Dについて`B/D`が8の倍数になる値（power-of-two Dなら通常`B = 8 * max(D)`）を選ぶ。plane-majorでchannelごとに連続格納し、channel cのpayloadは`B / D[c] / 8` byte。概算wire rateは

```
wire_Bps = base_rate_hz / 8 * sum(1 / D[c]) + framing
```

となる。modeが`edge_latch`ならlevel/edge用bitを追加する。

## 具体例（固定仕様ではない）

1 block = base 64 sample:

- byte 0..23: 各sampleのlane 0..2を3 bit/sample、LSB-firstで連結（一般形式では3本のbitplaneにしても同じ24 byte）。
- byte 24: 8 sampleごとのCSをbit 0..7へ格納。
- reference codecは比較用に`sample0`と`active_low_any`の両方を実装する。実用上のCS既定は`active_low_any`が安全。

PCで復元するとCSは各bitを8 sampleへholdする。高速3chはlosslessだが、CS edge位置は最大7 sample量子化され、8 sample未満のpulseは`sample0`では見逃しうる。

## 予算protocol

PCはcapture前に少なくとも`base_rate_hz`、`sample_count`、`output_budget_bytes`、channelごとの`mode / D / polarity / phase`を送る。deviceはblock size、必要wire byte、raw capture byte、復元metadataを計算して`ACCEPT`応答し、合わなければcapture開始前にrejectする。60 M・4ch例では

```
blocks = sample_count / 64
wire_bytes = blocks * 25
raw_capture_bytes = sample_count / 2
```

で検算する。`wire_bytes > output_budget_bytes`またはrawがPSRAM上限を超えればcapture前にrejectする。予算だけ渡して無限に取り続ける形にはしない。

raw 32 byte/blockからoutput 25 byte/blockへ前向きin-place変換できる。各blockのraw 32 byteだけlocal scratchへ退避すれば、後続blockを壊さずPSRAM追加領域を要しない。

PCが伝える「予算」は特定の23.4 MB/s等ではなく、**その要求で許容する総byte数または最大持続byte rate**。deviceは設定から必要量を毎回計算する。有限batchなら総byte数、continuousなら実測済みの安全なrateも必要になる。

## 方法

1. 60 M・4ch例のhost reference codecでrandom/edge位置全パターンをround-tripし、高速3ch完全一致とCS bucket規則を確認する。
2. 一般化したchannel descriptorとblock size/必要byte計算を追加する。
3. 同じcodecをP4のharvest後段へ実装し、変換のみのMB/sを測る。
4. finite captureを変換後にWindows WinUSBへ返し、byte数・各channel規則を照合する。
5. continuousは別条件として、FIFO占有がdurationで増えないか測る。E104のIN低速状態では成立しない設定をcapture前にrejectする。

## 完了条件

host codec、P4 codec、Windows batch返送が一致し、60 Mspsでoverflow 0。continuousを主張する場合は64 MiB以上でFIFO占有が増えないこと。

## 中間結果

`codec.py`に一般channel descriptor（mode / decimation）とblock/byte/rate計算、60 M SPI例のreference encode/decodeを実装した。random 16,448 sampleで高速3ch完全一致、`sample0` / `active_low_any`のCS規則一致。`pytest` 1件PASS。

- raw×3 + active-low-any(D=8): block 64 sample / 25 byte / **23.4375 MB/s**
- raw×2 + any-active(D=8) + edge-latch(D=64) + button hold(D=1024)の例: block 8192 sample / 2209 byte / **16.179199 MB/s**

後者のようにchannel構成を変えれば必要rateは毎回計算し直される。23.4375 MB/sは仕様値ではない。

### 何分の1まで使えるか

codec上はD=65536までbudget計算を通した。3 raw + 1 slow、base 60 Mspsの結果は`decimation_sweep.py` / `_runs/E105_20260914T074947JST_host_codec/decimation_sweep.txt`。

| D | 独立blockに必要なbase sample | block時間 | wire MB/s | raw 30 MB/sからの削減 |
|---:|---:|---:|---:|---:|
| 1 | 8 | 0.133 us | 30.000 | 0% |
| 2 | 16 | 0.267 us | 26.250 | 12.5% |
| 4 | 32 | 0.533 us | 24.375 | 18.75% |
| **8** | **64** | **1.067 us** | **23.438** | **21.875%** |
| 16 | 128 | 2.133 us | 22.969 | 23.438% |
| 64 | 512 | 8.533 us | 22.617 | 24.609% |
| 1024 | 8192 | 136.533 us | 22.507 | 24.976% |
| 65536 | 524288 | 8.738 ms | 22.5001 | 25.000% |

slow channelをどこまで落としても3 raw channelだけで22.5 MB/s必要なので、削減上限は25%。D=8ですでに上限25%の87.5%を回収しており、それ以上はblock/latencyだけ増えて利得が小さい。E104 high側24.2 MB/sへ入る最小power-of-twoはD=8。low側16.8 MB/sへはDを無限にしても入らない。

独立blockにするなら最低`8D` sampleが要る。bit accumulatorをchunk間で持てば小さいchunkでも符号化できるが、途中chunkからのrandom accessとエラー復帰が難しくなるため、USB/TCPへ渡す単位はblock境界に揃える。

3 raw + 1 slowの先頭1 blockだけを見ると、圧縮後は`3D+1` byte、rawは`ceil(N/2)` byteなので、padding込みで得になる要求長は概ね`N >= 6D+2` sample。D=8なら50 sample以上、D=1024なら6146 sample以上。PulseViewの通常のk/M sample要求では無視できるが、数十sampleだけ返すcontrol用途では圧縮しない方が小さい。

### PulseViewへの出し方

stock BeagleLogic protocolは`get`時に要求sample数をserverへ通知しない。clientは必要数を読んだ時点で`close`する（E077で確認済み）。したがってserverが要求数を知ってから厳密にround-upすることはできない。

採る形は次の二段にする。

1. device→serverはcodec block境界で、継ぎ目のない大きめcapture/streamを送る。
2. serverはblockごとに復元してPulseViewへ小分け送信し、`close`を受けた時点で停止する。最後に先行capture/転送済みの余剰があれば捨てる。

finite batchならserverの`--samples`を想定するPulseView要求以上へ置き、`ceil(samples / B) * B`だけdeviceへ要求する。余剰は最大`B-1` sample。別batchを繋ぐと実時間のgapが入るので、PulseView要求より小さいbatchを反復する形には戻さない。
