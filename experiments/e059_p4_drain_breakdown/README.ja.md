# E059 ESP32-P4 drain低下はmemory競合かISR overheadか

状態: **完了 — memcpy帯域は一定。原因は1 chunkあたりの固定cost**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E058](../e058_p4_window_drain_vs_rate/README.ja.md)・[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)

## 問い

**window中のdrain帯域がsample rateとともに下がるのは、memcpy自体が遅くなっている(memory競合)のか、memcpyに使えるCPU時間が減っている(ISRとqueueのoverhead)のか。**

## 仮説

[E058](../e058_p4_window_drain_vs_rate/README.ja.md)で、window中のdrain帯域が100 MHz以下の100 MB/s以上から160 MHzの86.1 MB/sへ下がることを実測した。一方[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)はDMAを動かさない状態でinternal RAM → PSRAMのcopyを138.6〜182.7 MB/sと測っている。gated capture中の86 MB/sはその半分以下である。

原因は二つに割れる。

- **memory競合** — DMAがinternal ringへ書き込みながらCPUが同じringから読むため、memcpy自体のthroughputが落ちる。ならばmemcpyの中で測った帯域がrateとともに下がる
- **ISRとqueueのoverhead** — chunk通知のISRが1 chunkごとに走り、chunk rateはsample rateに比例する(160 MB/sなら4,032 byteで割って約39.7 k回/s)。memcpy自体は速いままだが、memcpyに使える時間の割合が減る。ならばmemcpyの中で測った帯域は変わらず、harvest全体に対するmemcpy時間の割合が下がる

**memcpyを`esp_timer_get_time()`で挟んで、累積時間と累積byteを記録すれば直接切り分けられる。** memcpy帯域が下がっていればmemory競合、変わらなければoverheadである。

window byte長を96,000に固定し、sample rateをE058と同じ4点で振る。E058で全条件正常に取れることは確認済みなので、測定がdata喪失に汚されない。

| sample rate | run長 | gate幅 | E058のdrain | memory競合なら | overheadなら |
|---:|---:|---:|---:|---|---|
| 80 MHz | 16 | 6,000 | ≥ 80 MB/s | memcpy帯域が高い | memcpy帯域が高い |
| 100 MHz | 20 | 4,800 | ≥ 100 MB/s | 少し下がる | 変わらない |
| 120 MHz | 24 | 4,000 | 95.9 MB/s | 下がる | 変わらない |
| 160 MHz | 32 | 3,000 | 86.1 MB/s | **大きく下がる** | **変わらない** |

## 反証条件

- memcpy帯域もmemcpy時間の割合も両方変わらない(drainが下がる理由がこの二つの外)
- memcpy帯域が[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)の138.6〜182.7 MB/sを大きく超える、または極端に下回る
- 計測を足したことでどれかの条件が破綻する(飛びの急増やqueue overflow)
- ISR側の未読最大がE058と大きく変わる(計測の負荷で regime が動いた)

## 方法

[E058](../e058_p4_window_drain_vs_rate/README.ja.md)の構成に計測を足すだけで、captureの設定は変えない。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`。`max_recv_size`と受信sizeは62,720
- TX: data_width 8、GPIO 2〜9、5 MHz固定。1 loopは`gate幅 × 2`で先頭`gate幅`だけbit 7をhigh(duty 50%)
- queue深さ64、回収は100 ms、destinationは4 MiB PSRAM、未読はISR内で標本化
- **回収loopで`memcpy`を`esp_timer_get_time()`で挟み、累積時間と累積byteを記録する**
- case: (80 MHz, gate 6,000) / (100 MHz, 4,800) / (120 MHz, 4,000) / (160 MHz, 3,000)

計測自体のoverheadを明記する。1 chunkあたり`esp_timer_get_time()`が2回増えるので、160 MHzでは約39.7 k回/s × 2で数%のCPU時間を食う。**この分だけdrainは実際より低く出る**ので、E058のISR側未読最大と突き合わせて regime が動いていないかを確認する。

APIの不成立も結果として最後まで記録する。

## 対象外

memcpyのchunk sizeやalignmentの掃引、DMA無しでのcopy帯域の再測([E020](../e020_p4_psram_copy_bandwidth/README.ja.md)で測済み)、ISR自体の実行時間の直接測定、core分離、dutyとwindow長の掃引、triggerなしspool経路での同じ測定。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

sample rate、gate幅、window byte長、各API結果、memcpyの累積時間と累積byte、そこから求めたmemcpy帯域、harvest全体時間に対するmemcpy時間の割合、ISR側とtask側の未読最大、queue overflow、飛びの総数と期待境界数、階差一致数、回収byte、経過us。

## 完了条件

memcpy帯域とmemcpy時間の割合をrateごとに記録し、drain低下がどちらに帰属するかを確定する。両方効いている場合はそれぞれの寄与を記録する。

## 影響

memory競合なら、drainを上げる手はDMAとCPUのaccess先を分ける(別のSRAM bankやPSRAM直接DMA)方向になる。overheadなら、chunk sizeを大きくしてISR回数を減らす、または回収を別coreへ分ける方向になる。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のdrain表に理由が付き、改善の方向が決まる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E059_20260909T114649Z_default/test_drain_breakdown/dut.log`

| sample rate | memcpy累積 | memcpy時間 | **memcpy帯域** | ISR側の未読最大 | 本実験のdrain | [E058](../e058_p4_window_drain_vs_rate/README.ja.md)のdrain |
|---:|---:|---:|---:|---:|---:|---:|
| 80 MHz | 4,007,808 | 31,258 us | **128.2 MB/s** | 8,064 | ≥ 80 | ≥ 80 |
| 100 MHz | 4,194,304 | 38,922 us | **107.8 MB/s** | 12,096 | 95.8 | ≥ 100 |
| 120 MHz | 4,194,304 | 39,109 us | **107.2 MB/s** | 30,464 | 92.0 | 95.9 |
| 160 MHz | 4,194,304 | 39,139 us | **107.2 MB/s** | 54,656 | 82.3 | 86.1 |

4条件すべて正常に取れた(飛び42〜44対期待43、階差15 / 15、queue overflow 0)。

**memcpy帯域は100 MHz以上で完全に一定である。** 107.8 / 107.2 / 107.2 MB/sで、rateを100から160 MHzへ1.6倍にしても変わらない。一方drainは95.8 → 92.0 → 82.3 MB/sと下がる。**したがってdrainの低下はmemcpy自体が遅くなっているためではない。**

80 MHzだけ128.2 MB/sで高い。100 MHz以上との差(−16%)はmemory競合が現れた分と読めるが、100 MHz以上では飽和しており、そこから先のdrain低下には寄与していない。なお80 MHzはdestinationが埋まりきらず(4,007,808 < 4,194,304)、chunkの構成が他と違う点は差し引いて読む必要がある。

**drain低下の正体は1 chunkあたりの固定costである。** window中にtaskがmemcpyに使えている割合を求めると次になる。

| sample rate | window所要 | window中のdrain | memcpy占有率(drain ÷ 107) | 1 chunkあたり非memcpy時間 |
|---:|---:|---:|---:|---:|
| 100 MHz | 0.96 ms | 95.8 MB/s | 89% | 4.2 us |
| 120 MHz | 0.80 ms | 92.0 MB/s | 86% | 4.7 us |
| 160 MHz | 0.60 ms | 82.3 MB/s | 77% | 5.8 us |

**window byte長を固定しているのでwindowあたりのchunk数は3条件とも23.8個で同じである。** それでもmemcpy占有率が89%から77%へ下がるのは、window所要時間が0.96 msから0.60 msへ短くなる一方、1 chunkあたりの非memcpy時間(ISR、`xQueueReceive`、loop本体、計測のtimer呼び出し)が4〜6 usとほぼ変わらないためである。**固定costが短くなったwindowの中で相対的に大きくなる。**

計測自体のcostも切り出せる。本実験の160 MHzのdrainは82.3 MB/sで、計測を入れていないE058の86.1 MB/sより3.8 MB/s低い。これはwindow時間の2.4%、1 chunkあたり約0.6 usにあたる。**timer呼び出し2回分がこの0.6 usで、差し引くと真の非memcpy時間は3.6〜5.2 us/chunkである。**

## 判定

**drainの低下はISRとqueueの1 chunkあたり固定cost(約3.6〜5.2 us)によるもので、memcpy自体の帯域低下ではない。**

- **memcpy帯域はgated capture中107 MB/s**で、100〜160 MHzで一定である。[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)がDMA無しで測った138.6〜182.7 MB/sより低いので、DMAが動いていること自体で25〜40%を失っている。ただしこの損失はDMAのrateには依存せず、100 MHz以上で飽和している
- **rate依存の部分は固定costの相対的な膨張である。** windowが短くなるほど、1 chunkあたり数usのcostが占める割合が増える

改善の方向が決まる。

- **chunk数を減らす** — 1 chunkあたりのcostが固定なので、chunkが大きくなれば効く。しかしchunk sizeは`DMA_DESCRIPTOR_BUFFER_MAX_SIZE_64B_ALIGNED` = 4,032でSoC定義から動かせない([E058](../e058_p4_window_drain_vs_rate/README.ja.md)の追記)
- **1 chunkあたりの仕事を削る** — `xQueueReceive`を1 chunkずつ呼ぶのをやめ、複数chunkをまとめて取る、あるいはqueueを介さずdescriptorの状態を直接見る
- **回収を別coreへ移す** — ISRが走るcoreとmemcpyするcoreを分ける。[E052](../e052_p4_rmt_callback_timing/README.ja.md)でCPU負荷がRMTの発火に影響しないことは確認済みだが、PARLIO側の回収については未確認
- **memcpy帯域そのものを上げる余地は小さい** — 107 MB/sはDMAが動いている限りの上限に近い

drain表に理由が付いたので、条件2の`drain(rate)`は「107 MB/s × memcpy占有率(rate)」と分解して書ける。

## 事実・候補・未決

**事実**

1. 4条件すべて正常に取れた(飛び42〜44対期待43、階差15 / 15、queue overflow 0)。
2. **memcpy帯域は100 / 120 / 160 MHzで107.8 / 107.2 / 107.2 MB/sと一定である。** drainは95.8 / 92.0 / 82.3 MB/sと下がるので、**drain低下はmemcpyの帯域低下ではない。**
3. 80 MHzのmemcpy帯域は128.2 MB/sで、100 MHz以上より16%高い。memory競合は100 MHz以上で飽和しており、そこから先のdrain低下には寄与していない。
4. **window byte長固定なのでwindowあたりのchunk数は23.8個で同じだが、memcpy占有率は89% → 86% → 77%と下がる。** 1 chunkあたりの非memcpy時間は4.2 / 4.7 / 5.8 usでほぼ一定で、windowが短くなる分だけ相対的に大きくなる。
5. 本実験の160 MHzのdrain 82.3 MB/sはE058の86.1より3.8 MB/s低い。計測のtimer呼び出し2回分で1 chunkあたり約0.6 usにあたる。差し引くと真の非memcpy時間は3.6〜5.2 us/chunkである。
6. gated capture中のmemcpy帯域107 MB/sは、[E020](../e020_p4_psram_copy_bandwidth/README.ja.md)のDMA無し138.6〜182.7 MB/sより25〜40%低い。この損失はDMAのrateに依存しない。

**候補**: 条件2の`drain(rate)`を「107 MB/s × memcpy占有率(rate)」と分解して持つ。改善は1 chunkあたりのcostを削る方向(複数chunkのまとめ取り、queueを介さない回収、別coreへの分離)で、memcpy帯域を上げる余地は小さい。

**未決**: 1 chunkあたり3.6〜5.2 usの内訳(ISR本体、`xQueueReceive`、loop本体の分離) / 複数chunkをまとめて取ると占有率が上がるかの実測 / 回収を別coreへ移した場合の効果 / 80 MHzと100 MHzの間でmemcpy帯域が飽和する理由 / triggerなしspool経路でも同じ分解が成り立つか。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): drain表に理由を付け、改善の方向を書く
- [LEDGER](../LEDGER.ja.md): E059の節
