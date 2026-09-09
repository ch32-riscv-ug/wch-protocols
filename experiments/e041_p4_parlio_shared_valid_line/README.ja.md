# E041 ESP32-P4 valid線をdata線と同一GPIOで共有する

状態: **完了 — 共有は成立。hardware triggerはchannelを消費しない**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E037](../e037_p4_parlio_pulse_trigger/README.ja.md)・[E038](../e038_p4_parlio_pulse_trigger_rate/README.ja.md)

## 問い

**`valid_gpio_num`をdata線のいずれかと同一GPIOに設定して、8 channelすべてをdataとして残したままhardware edge triggerを使えるか。**

## 仮説

E037・E038のhardware triggerはvalid線に専用のGPIOを割り当てており、「channelを1本payする」という前提で能力を整理した。しかしPARLIOのpin割り当てはGPIO matrix経由であり、matrixは**一つのGPIOを複数のperipheral入力信号へfan-outできる**。したがって`valid_gpio_num`をdata線と同じGPIOにすれば、その線はdataとして記録されつつtriggerにも使えるはずである。

成立すれば意味が大きく変わる。triggerの代償が「channel 1本」から「どのchannelでtriggerするかを選ぶ」だけになり、8 channel + hardware edge triggerが8本のpinで成立する。これはE038で「data_width 8のhardware triggerはvalid線込みで9線必要なので現在のpin宣言では作れない」とした未決を、pinを増やさずに解く。

`valid_sig_line_id`はdata lineと衝突しない内部slotを指す番号なので、data_width 8に対しては8以上を選ぶ。GPIOの共有と内部slot番号は別の話である。

信号源はbit 0〜6に7-bit gray ramp、bit 7にtrigger levelを載せる。bit 7はdata channel 7であり同時にtrigger線でもある。frameがbit 7のhigh区間の内側に収まるようにgate幅を取れば、frame中に追加のedgeは来ない。

## 反証条件

- driverが`valid_gpio_num`とdata線のGPIO重複を拒否する
- triggerが発火しない
- 共有したdata線(bit 7)の値が取得dataに正しく現れない
- bit 0〜6のgray rampにstep違反が出る
- frameの先頭が想定位置(gate開放位置)からずれる

## 方法

- TX: data_width 8、GPIO 2〜9、`output_clk_freq_hz` = RX rate ÷ 4、16,384 wordを`loop_transmission`
  - bit 0〜6: `gray7(index & 0x7F)`(周期128 word)
  - bit 7: index 2048から2,048 wordだけhigh(gate)。1 loopに立ち上がり1回
- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、**`valid_gpio_num` = GPIO 9(data線7と同一)**、`valid_sig_line_id` = 8
- delimiter: pulse delimiter、`eof_data_len` 4,096 byte(data_width 8なので4,096 sample = 1,024 source word)、`timeout_ticks` 0、`has_end_pulse=false`、`pulse_invert=false`
- 受信は`partial_rx_en=false`の有限transaction、payloadは8 KiB internal RAM
- RX rate: 20 / 80 / 160 MHz

frame 1,024 source wordはgate 2,048 wordの内側なので、frame中にbit 7の立ち上がりは来ない。

検証は二つ行う。

- bit 0〜6を`gray7`として復元し、run崩し後のstepが+1 mod 128であることを確認する
- **全sampleでbit 7が1であることを確認する。** 共有した線がdataとしても正しく取得されている証拠になる

先頭sampleは`gray7(2048 & 0x7F)` = 0にbit 7を立てた値、つまり`0x80`が期待値である。

APIの不成立も結果として最後まで記録する。

## 対象外

level delimiterでの共有、16 channelでの構成、有限frameの持続byte rate(frameが4,096 byteと短くburstに収まるため、この実験では帯域を測らない)、`eof_data_len` 65,535超、pre-trigger、gate境界のsample精度、共有線をtriggerにしたときのjitter。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

RX設定rate、TX設定rate、各API結果、`wait_all_done`結果、receive_done回数、受信byte、run数、run長min/max、gray7 step違反数、bit 7が0だったsample数、先頭4 byte、経過us。

## 完了条件

valid線とdata線のGPIO共有が成立するか否かを確定する。成立する場合は、共有線のdataが正しく取得されることと、triggerが期待位置で発火することを記録する。拒否される場合はどのAPIで外れたかを特定して完了とする。

## 影響

成立すれば[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のTrigger節から「消費channel 1」が消え、hardware triggerは「どのchannelでtriggerするかを選ぶ」機能になる。8 channel + hardware triggerが8 pinで成立するので、E038の未決(9線必要)も解消する。

## 結果

実施日: 2026-09-09

採用run: `_runs/E041_20260909T065115Z_default/test_parlio_shared_valid_line/dut.log`

| RX設定 | TX設定 | wait | receive_done | 受信byte | runs | run長 | gray7違反 | bit 7が0のsample | 先頭4 byte | 経過 |
|---:|---:|---|---:|---:|---:|---:|---:|---:|---|---:|
| 20 MHz | 5 MHz | `ESP_OK` | 1 | 4,096 | 1,024 | 4〜4 | 0 | **0** | `80 80 80 81` | 625 us |
| 80 MHz | 20 MHz | `ESP_OK` | 1 | 4,096 | 1,024 | 3〜5 | 0 | **0** | `80 80 80 81` | 164 us |
| 160 MHz | 40 MHz | `ESP_OK` | 1 | 4,096 | 1,024 | 3〜5 | 0 | **0** | `80 80 80 81` | 87 us |

**成立した。** `valid_gpio_num`をdata線7と同じGPIO 9に設定しても、`parlio_new_rx_unit`はGPIOの重複を拒否せず、3 rateすべてで全APIが`ESP_OK`だった。受信byteは`eof_data_len` 4,096と完全一致し、`gray7`のstep違反は0件である。

**共有した線はdataとしても正しく取得されている。** bit 7が0だったsampleは全条件で0件だった。gate区間の内側でframeを取っているのでbit 7は常に1が期待値であり、それが4,096 sample全部で満たされた。つまりGPIO 9は同時にdata channel 7とtrigger線として機能している。

**trigger位置も期待どおりである。** 先頭4 byteは`80 80 80 81`で、`0x80` = `gray7(2048 & 0x7F)` = 0にbit 7を立てた値、つまりgate開放位置(source index 2048)の値である。先頭runが4ではなく3 sampleなので、pulse検出から取得開始までのずれは1 sample以内で、E037と同じ挙動である。3 rateで先頭値が完全に一致した。

run長は20 MHzで厳密に4固定、80 / 160 MHzで3〜5に散った。**これはE038の観測と一致しない。** E038はdata_width 4で、160 MHzの整数分周(20 / 40 / 80 / 160 MHz)ではrun長が4固定、非整数分周(100 / 120 MHz)で3〜5と分かれていた。本実験は80 / 160 MHzがどちらも整数分周であるにもかかわらず3〜5である。したがって**整数分周であることはrun長の均一性の十分条件ではない。** 二つの独立した分周器の位相関係がarmごとに変わることが実際の変数だと考えられるが、本実験では切り分けていない。step違反は0なので取得の正しさには影響しない。

## 判定

**hardware triggerはchannelを消費しない。** `valid_gpio_num`をdata線と共有できるので、代償は「どのchannelでtriggerするかを選ぶ」ことだけである。8本のpinで8 channel + hardware edge triggerが成立し、E038で「data_width 8のhardware triggerはvalid線込みで9線必要」とした未決はpinを増やさずに解けた。

`valid_sig_line_id`はGPIOではなく内部のline slot番号なので、data_width 8に対して8を指定すればdata lineと衝突しない。GPIOの共有と内部slot番号は独立に決められる。

副産物として帯域の下限が一つ分かった。data_width 8の160 MHzはpacking後160 MB/sであり、4,096 byteのframeを25.6 usにわたってstep違反0で取得できた。**internal RAMへのDMA write経路は少なくとも4 KiB burstで160 MB/sを通す。** E036の約98 MB/sはinternal ring → PSRAMのtask copy段の限界であって、DMA write自体の限界ではないことがこれで裏付けられた。ただし25.6 usは持続測定ではないので、持続帯域は別に測る必要がある。

## 事実・候補・未決

**事実**

1. `valid_gpio_num`をdata線7と同一のGPIO 9に設定しても`parlio_new_rx_unit`は受理し、20 / 80 / 160 MHzの3条件すべてで全APIが`ESP_OK`だった。受信byteは`eof_data_len`と完全一致、`gray7` step違反0件。
2. **bit 7が0だったsampleは全条件で0件。** 共有したGPIOはdata channel 7としてもtrigger線としても機能している。
3. 先頭4 byteは3条件すべて`80 80 80 81`で、gate開放位置の値と一致した。先頭runが3 sampleなので取得開始のずれは1 sample以内。
4. run長は20 MHzで4固定、80 / 160 MHzで3〜5。**E038でdata_width 4の80 / 160 MHzが4固定だったことと一致しない。** 整数分周はrun長均一性の十分条件ではない。原因は切り分けていない。
5. data_width 8の160 MHz、つまり160 MB/sで4,096 byteを25.6 usにわたり違反0で取得した。internal RAMへのDMA writeは4 KiB burstで160 MB/sを通す。

**候補**: trigger能力の申告から「消費channel 1」を外し、「trigger源となるchannelを1つ選ぶ」に変える。hardware trigger付き8 channel構成を8 pinの標準構成にする。

**未決**: run長の均一性を決めている条件(分周比か位相か。E038と本実験で結果が分かれた) / internal RAMへのDMA writeの持続帯域(4 KiB burstより長い取得で98 MB/sを超えられるか) / 共有線をlevel delimiterのgateに使う構成 / 16 channelでの共有(data_width 16では`valid_sig_line_id`に空きslotが無い可能性) / 共有時のtrigger jitter / `eof_data_len` 65,535超。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): Trigger節の「消費channel」を1から0へ直し、trigger源のchannel選択という表現へ変える。E038のrate上限にdata_width 8の160 MB/s burstを追記する
- [E038](../e038_p4_parlio_pulse_trigger_rate/README.ja.md): 整数分周とrun長の関係、および9線必要という記述を追記で限定する
- [LEDGER](../LEDGER.ja.md): E041の節
