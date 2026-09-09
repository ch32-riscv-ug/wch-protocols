# E037 ESP32-P4 PARLIO pulse delimiterによるhardware trigger

状態: **計画**

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
