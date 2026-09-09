# E037 ESP32-P4 PARLIO pulse delimiterによるhardware trigger

状態: **完了 — hardware trigger成立。arm待ちtimeoutはsoftwareが必要**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E036](../e036_p4_parlio_rate_seq_verify/README.ja.md)・[E025](../e025_p4_sump_trigger_rate_boundary/README.ja.md)

## 問い

**PARLIO RXのpulse delimiterで、4 data channel + 1 valid lineの構成をとり、hardware pulseでframeを開始して`eof_data_len`で停止できるか。pulseが来ないときはhardware timeoutが出るか。**

## 仮説

E023〜E028のtriggerはすべてsoftwareで全sampleを走査する方式で、8 channelでは基本trigger 24 MHz、4-stage 16 MHzが天井だった。CPUがsampleを1つずつ見る限り、raw rateの100 MHzには追従できない。

一方`parlio_rx.h`にはsoft delimiterのほかにpulse delimiterとlevel delimiterがあり、次をhardwareで持つ。

- `parlio_rx_unit_config_t::valid_gpio_num` — valid信号のGPIO
- `parlio_rx_pulse_delimiter_config_t::valid_sig_line_id` — valid信号が占める内部data line slot。data lineとの衝突を避けるためdata widthより上を指定する
- `eof_data_len` — frame長をhardwareで決める。0ならend pulseで終わる
- `timeout_ticks` — source clock tick数によるhardware timeout
- `flags.pulse_invert` — pulseの極性。rising / fallingの選択に相当
- `flags.has_end_pulse` / `end_bit_included` — hardwareでの停止

つまり1 channelをtrigger線に払う代わりに、CPU走査なしでedge triggerとpost長とtimeoutが成立する可能性がある。成立すればtrigger tierはsoftwareの20 MHz級とhardwareのraw rate級に分かれる。

配線は変えられないので、`TEST_PARLIO_PINS`のGPIO 2〜9の内側で構成する。PARLIO TXをdata_width 8で作り、bit 0〜3に4-bit gray ramp、bit 4にtrigger pulseを載せる。RXはdata_width 4でGPIO 2〜5をdata lineにし、GPIO 6を`valid_gpio_num`にする。E036でTXとRXが同一group・同一GPIOで共存することは確認済みである。

`valid_sig_line_id`の許容範囲はheaderのコメントが`(data_width, MAX]`と書いており、data_width 4に対して4が入るか5からかが読み取れない。両方試して受理される側を確定する。

## 反証条件

- `parlio_new_rx_pulse_delimiter`がどちらのline idでも受理されない
- `parlio_rx_unit_receive`がpulse delimiterで`ESP_ERR_INVALID_ARG`等を返す
- pulseがあってもframeが始まらない、または`eof_data_len`で止まらない
- pulseが無いのにframeが始まる
- 受信できたframeがgray rampとして復元できない

## 方法

RX rateは20 MHzに固定する。E036で20 MHzはsample単位で健全と確認済みで、この実験の問いはrateではなくtrigger経路の成立だからである。

- TX: data_width 8、GPIO 2〜9、`output_clk_freq_hz` 5 MHz、16,384 wordを`loop_transmission`で連続送出
  - bit 0〜3: `gray4(index & 0xF)`
  - bit 4: index 2048から4 wordだけhigh、他はlow。frame長より周期を長くして、1 frame中にpulseが二度来ないようにする
  - bit 5〜7: 0
- RX: data_width 4、`data_gpio_nums[0..3]` = GPIO 2〜5、`valid_gpio_num` = GPIO 6、`exp_clk_freq_hz` 20 MHz、`io_loop_back=false`
- delimiter: pulse delimiter、`eof_data_len` 16,384 byte(width 4なので32,768 sample)、`sample_edge` POS、`bit_pack_order` LSB、`start_bit_included=false`、`has_end_pulse=false`、`pulse_invert=false`
- 受信は`partial_rx_en=false`の有限transactionとし、payloadは32 KiB internal RAMにする。PSRAMのcache境界問題(E016〜E019)をこの問いへ持ち込まない
- 完了待ちは`parlio_rx_unit_wait_all_done`の500 ms software timeoutで行い、hangしないようにする

case:

| # | valid_sig_line_id | trigger pulse | timeout_ticks |
|---:|---:|---|---:|
| 1 | 4 | あり | 0 |
| 2 | 5 | あり | 0 |
| 3 | 4 | なし | 60,000 |
| 4 | 5 | なし | 60,000 |

APIの不成立も結果として最後まで記録する。

## 対象外

hardware trigger成立時の最大sample rate、level delimiter、`has_end_pulse`による停止、`pulse_invert`による極性、8 / 16 channelでの構成、pre-trigger、software triggerとの併用、PSRAMへの直接受信。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、valid_sig_line_id、pulse有無、timeout_ticks、各API結果、`parlio_rx_unit_wait_all_done`の結果、receive_done回数、hardware timeout回数、受信byte数、先頭4 sampleの値、gray step違反数、run数、run長min/max、経過us。

## 完了条件

pulse delimiterによるhardware trigger開始と`eof_data_len`停止が成立するか否かを確定し、成立する`valid_sig_line_id`とtimeoutの挙動を記録する。成立しない場合はどのAPI・どの段階で外れたかを特定して完了とする。

## 影響

成立すれば[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のtrigger節に、software走査tierとは別のhardware trigger tierが立つ。trigger能力の申告方法(消費channel数、条件種、最大rate)が変わる。不成立ならsoftware走査tierだけが残り、E023〜E028の20〜24 MHzがtrigger付き公称値として確定する。

## 結果

実施日: 2026-09-09

採用run: `_runs/E037_20260909T030730Z_default/test_parlio_pulse_trigger/dut.log`

| # | valid_sig_line_id | pulse | timeout_ticks | delimiter | wait | receive_done | timeout_events | 受信byte | runs | run長 | 違反 | 先頭4 sample | 経過 |
|---:|---:|---|---:|---|---|---:|---:|---:|---:|---:|---:|---|---:|
| 1 | 4 | あり | 0 | `ESP_OK` | `ESP_OK` | 1 | 0 | 16,384 | 8,192 | 4〜4 | 0 | 0,0,0,1 | 2,058 us |
| 2 | 5 | あり | 0 | `ESP_OK` | `ESP_OK` | 1 | 0 | 16,384 | 8,192 | 4〜4 | 0 | 0,0,0,1 | 2,058 us |
| 3 | 4 | なし | 60,000 | `ESP_OK` | `ESP_ERR_TIMEOUT` | 0 | 0 | 0 | — | — | — | — | 499,798 us |
| 4 | 5 | なし | 60,000 | `ESP_OK` | `ESP_ERR_TIMEOUT` | 0 | 0 | 0 | — | — | — | — | 499,869 us |

pulseのある2 caseは、全APIが`ESP_OK`で`on_receive_done`が1回だけ発火し、受信byteは`eof_data_len`と完全に一致した。復元したframeはrun 8,192本、run長が4固定、gray step違反0件だった。frame長32,768 sampleをsource分周比4で割った値がちょうど8,192なので、frame全域が欠落なく連続している。

pulseの無い2 caseはframeが一度も始まらず、`parlio_rx_unit_wait_all_done`が500 msで`ESP_ERR_TIMEOUT`を返した。誤triggerは無い。

`valid_sig_line_id`は4と5の両方が受理され、結果も同一だった。headerのコメントは範囲を`(data_width, MAX]`と書いているが、data_width 4に対してid 4は使える。条件は「data lineと衝突しない」ことである。

先頭4 sampleは両caseで0,0,0,1だった。pulseはsource index 2048から始まり、そのwordのgray値は`gray4(2048 & 0xF)` = 0である。frame先頭のrunが4ではなく3 sampleなので、pulse検出からsample取得開始までのずれは1 sample(50 ns)以内に収まっている。2 caseで同一値なので再現する。

経過時間2,058 usは、frame本体1,638 us(32,768 sample / 20 MHz)に、armしてから次のpulseが来るまでの待ち約420 usを足した値である。source loopは16,384 word / 5 MHz = 3,277 us周期なので待ち時間はこの範囲に収まる。

**`timeout_ticks` = 60,000を設定してもpulseが来ない間は`on_timeout`が発火しなかった。** frameが始まる前のarm待ちはhardware timeoutの対象ではない。

## 判定

**PARLIO RXのpulse delimiterはhardware triggerとして成立する。valid線1本を払えば、CPUがsampleを走査せずにframe開始と`eof_data_len`停止が得られ、誤triggerもない。**

これはE023〜E028のsoftware走査tierとは別の能力である。software triggerはdata線上のpattern / mask / edge / occurrence / multi-stageを条件にできるが、全sampleをCPUが見るため8 channelで24 MHz、4-stageで16 MHzが天井だった。pulse delimiterは条件が「専用線のpulse」1種類に限られる代わりに、CPU負荷がゼロなのでrateはraw captureの限界(E036のsampling / spool / burst)と同じところまで行けるはずである。したがってtrigger能力は次の二段で申告する。

| tier | 条件 | 消費channel | rateの決まり方 |
|---|---|---|---|
| software走査 | pattern / mask、edge、occurrence、multi-stage | 0 | CPUの走査能力。8 channelで24 MHz、4-stage 16 MHz |
| hardware pulse delimiter | 専用線のpulse 1本(極性は`pulse_invert`) | 1 | raw captureの限界と同じはず。**本実験は20 MHzでの成立確認までで、上限は未測定** |

この構成には次の制約がある。

- `eof_data_len`は16 bitで最大65,535 byteである(E018と同じ制約)。これを超えるpost長は`partial_rx_en`とsoftware停止に戻るため、E026と同じdescriptor粒度の停止誤差が復活する
- pulse delimiterはpulseでframeを開始するので、**pre-trigger dataは取れない**。pre/post windowが必要ならE027のcircular ring方式であり、hardware triggerとpre-triggerは同時に成立しない
- 条件はdata線上のpatternではない。SUMP的なpattern triggerの代替にはならず、外部trigger入力またはsingle-line edge triggerに相当する
- arm待ちのtimeoutはsoftwareで持つ必要がある。`timeout_ticks`はarm待ちには効かない

## 事実・候補・未決

**事実**

1. `parlio_new_rx_pulse_delimiter`は`valid_sig_line_id`が4でも5でも`ESP_OK`で、data_width 4との組で動作した。結果は両者同一。
2. pulseがあるとき`on_receive_done`が1回発火し、受信byteは`eof_data_len` 16,384と完全一致した。frameはrun 8,192本・run長4固定・gray step違反0で、全域が連続していた。
3. pulseが無いときframeは一度も始まらず、`wait_all_done`が500 msで`ESP_ERR_TIMEOUT`を返した。誤triggerは0。
4. `timeout_ticks` = 60,000でも、frame開始前のarm待ちでは`on_timeout`が発火しなかった。
5. frame先頭のrunが3 sampleなので、pulse検出から取得開始までのずれは1 sample(50 ns)以内で、2 caseで再現した。

**候補**: trigger能力をsoftware走査tierとhardware pulse tierの二段で申告し、hardware tierはvalid線1本の消費とpre-trigger不可を明記する。arm待ちtimeoutはsoftwareで持つ。

**未決**: hardware trigger時の最大sample rate / level delimiterによるgating / `has_end_pulse`での停止 / `pulse_invert`の極性 / 8・16 channelでの構成(valid線を含めて9・17線が必要になるためpin数の確認が要る) / `eof_data_len` 65,535超のpost長 / circular ringとの併用可否。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): Trigger節をsoftware走査tierとhardware pulse tierの二段に分ける
- [LEDGER](../LEDGER.ja.md): E037の節
