# E039 ESP32-P4 level delimiterによるhardware gating

状態: **完了 — gatingは成立、`eof_data_len`=0の可変長は不成立**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E037](../e037_p4_parlio_pulse_trigger/README.ja.md)・[E038](../e038_p4_parlio_pulse_trigger_rate/README.ja.md)

## 問い

**level delimiterで、enable線がactiveな間だけ取得するhardware gatingが成立するか。`eof_data_len` = 0のとき、enableの無効化でframeが終わって可変長になるか。**

## 仮説

E037・E038で確立したhardware triggerはpulse delimiterによるもので、pulseがframeを開始し`eof_data_len`が終わらせる固定長方式だった。level delimiterは同じvalid線を「dataが有効な区間」として扱う。`parlio_rx_level_delimiter_config_t`は`eof_data_len`のコメントに「0にするとEOFはenable信号が無効化されたときだけ起きる」と書いており、`flags.active_low_en`で極性を選べる。

したがってlevel delimiterは次の二つを与えるはずである。

- **hardware gating** — enableがactiveな区間だけをsampleする。chip selectがassertされている間だけ取る、といった条件をCPU負荷なしで実現できる
- **可変長frame** — `eof_data_len` = 0でenable幅がそのままframe長になる。取得量を事前に決めなくてよい

pulse delimiterと同じ配線制約が効く。data_width 4 + valid線1本で5線なので、`TEST_PARLIO_PINS`の8線の内側に収まる。

## 反証条件

- `parlio_new_rx_level_delimiter`が受理されない
- enableがactiveでもframeが始まらない、またはenable区間外のsampleが混ざる
- `eof_data_len` = 0でframeが終わらない、または受信byteがenable幅と対応しない
- `active_low_en`が効かない
- enableが一度もactiveにならないのにframeが始まる
- 受信したframeがgray rampとして復元できない

## 方法

RX rateは20 MHzに固定する。問いはrateではなくgating経路の成立だからである。source構成はE037と同じで、valid線に載せるのを短いpulseではなく幅のあるlevelに変える。

- RX: data_width 4、`data_gpio_nums[0..3]` = GPIO 2〜5、`valid_gpio_num` = GPIO 6、`valid_sig_line_id` = 4、`exp_clk_freq_hz` 20 MHz
- TX: data_width 8、GPIO 2〜9、`output_clk_freq_hz` 5 MHz、16,384 wordを`loop_transmission`
  - bit 0〜3: `gray4(index & 0xF)`
  - bit 4: index 2048から2,048 wordだけhigh(gate幅)、他はlow
- delimiter: level delimiter、`sample_edge` POS、`bit_pack_order` LSB、`timeout_ticks` 0
- 受信は`partial_rx_en=false`の有限transaction、payloadは16 KiB internal RAM

gate幅2,048 TX wordはRX 20 MHzで8,192 sample、data_width 4なので4,096 byteに対応する。

case:

| # | active_low_en | eof_data_len | pattern上のgate | 期待 |
|---:|---|---:|---|---|
| 1 | false | 2,048 byte | あり | gate区間内で`eof_data_len`到達、2,048 byte |
| 2 | false | 0 | あり | gate無効化で終了、約4,096 byte |
| 3 | true | 2,048 byte | あり | lowの区間で取得、2,048 byte |
| 4 | false | 2,048 byte | なし(常にlow) | frameが始まらない |

case 3のlow区間はgate以外の14,336 wordなので、`eof_data_len` 2,048 byteは区間内に収まる。

APIの不成立も結果として最後まで記録する。

## 対象外

pulse delimiterの`has_end_pulse`・`pulse_invert`、level delimiterの`timeout_ticks`、gating時の最大sample rate、data_width 8 / 16(必要pin数が宣言を超える)、gate幅の掃引、PSRAMへの直接受信、pre-trigger、連続frameのsoak。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、`active_low_en`、`eof_data_len`、gate有無、各API結果、`wait_all_done`結果、receive_done回数、受信byte、期待byteとの差、gray step違反数、run数、run長min/max、先頭4 sample、経過us。

## 完了条件

level delimiterによるhardware gatingと、`eof_data_len` = 0による可変長frameが成立するか否かを確定する。成立しない場合はどのAPI・どの段階で外れたかを特定して完了とする。

## 影響

[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のTrigger節に、hardware側の窓の切り方(pulseによる固定長 / levelによるgating・可変長)を追加する。成立すれば「enable線がactiveな区間だけ取る」がCPU負荷なしのcapabilityとして立つ。

## 結果

実施日: 2026-09-09

採用run: `_runs/E039_20260909T031756Z_default/test_parlio_level_gate/dut.log`

| # | active_low_en | eof_data_len | gate | delimiter | wait | receive_done | 受信byte | 期待 | runs | run長 | 違反 | head | 経過 |
|---:|---|---:|---|---|---|---:|---:|---:|---:|---:|---:|---|---:|
| 1 | false | 2,048 | あり | `ESP_OK` | `ESP_OK` | 1 | 2,048 | 2,048 | 1,023 | 4〜4 | 0 | `00000000` | 625 us |
| 2 | false | **0** | あり | `ESP_OK` | **`ESP_ERR_TIMEOUT`** | **0** | **0** | 4,096 | — | — | — | — | 499,805 us |
| 3 | true | 2,048 | あり | `ESP_OK` | `ESP_OK` | 1 | 2,048 | 2,048 | 1,024 | 4〜4 | 0 | `0000bbaa` | 251 us |
| 4 | false | 2,048 | なし | `ESP_OK` | `ESP_ERR_TIMEOUT` | 0 | 0 | 0 | — | — | — | — | 500,465 us |

**固定長のhardware gatingは成立した。** case 1では受信byteが`eof_data_len`と完全一致し、runは1,023本・run長4固定・gray step違反0で、frameはgate区間内の連続sampleだった。先頭4 sampleは0,0,0,0で、gateがsource index 2048から始まり`gray4(2048 & 0xF)` = 0であることと一致する。E037のpulse delimiterと同じ位置決めである。

**`active_low_en`も効いた。** case 3はlowの区間で2,048 byteを取得し、違反0だった。ただしheadは`0000bbaa`(採用run)と`00008999`(同一boot内の先行run)で異なった。low区間はgate以外の14,336 wordと長いので、armした時点で既にlowであり開始位置が定まらない。経過が251 usとframe本体205 usに近いことも、armしてすぐ取得が始まったことを示す。**極性反転は動くが、この構成では開始位置の再現性が無い。**

**gateが無ければframeは始まらない。** case 4は誤trigger0で、`wait_all_done`が500 msでtimeoutした。

**`eof_data_len` = 0による可変長frameは成立しなかった。** case 2はdelimiter生成も`parlio_rx_unit_receive`も`ESP_OK`だったが、`on_receive_done`が一度も発火せず、`wait_all_done`が500 msでtimeoutした。headerは「0にするとEOFはenable信号が無効化されたときだけ起きる」と書いているが、この組み合わせ(`partial_rx_en=false`の有限transaction、data_width 4)ではEOFが来ない。

ただし本実験の計測では、**frameが開始しなかったのか、開始したがEOFが来なかったのかを区別できない。** payloadの中身は`wait_result == ESP_OK`のときだけ検証する設計にしたため、case 2ではpayloadを見ていない。区別は後続の問いに回す。

## 判定

**level delimiterはhardware gatingとして成立する。valid線1本で「enableがactiveな区間だけ取る」がCPU負荷なしに実現でき、極性も選べる。一方`eof_data_len` = 0による可変長frameはこの構成では成立しない。**

これでhardware側の窓の切り方が二つ揃った。どちらもvalid線1本を消費し、pre-triggerは取れない。

| delimiter | 開始 | 終了 | 開始位置の再現性 | 用途 |
|---|---|---|---|---|
| pulse | 専用線のpulse edge | `eof_data_len`のみ | 1 sample以内で再現([E037](../e037_p4_parlio_pulse_trigger/README.ja.md)) | 単発eventのtrigger |
| level (active high) | enableのassert | `eof_data_len`のみ | 1 sample以内で再現 | chip select等がassertされている区間の取得 |
| level (active low) | enableのdeassert | `eof_data_len`のみ | **再現しない**(armした時点で既にactiveなら即開始) | 極性が逆の信号 |

**終了は現状どちらも`eof_data_len`だけである。** 16 bitで最大65,535 byteという制約([E018](../e018_p4_parlio_psram_log_suppression/README.ja.md)と同じ)が、hardware完結のframeの深度上限をそのまま決める。level delimiterで「enableが切れるまで取る」ができないので、gate幅が不定な信号を取るには`eof_data_len`を上限として置き、実際の有効長はhostまたはsoftware側で判定することになる。

active lowの開始位置が再現しないのは異常ではなく、「armした時点でenableがactiveならその瞬間から始まる」という仕様どおりの帰結である。逆に言えば、level delimiterはpulse delimiterと違って**armとenableの前後関係に依存する**。armする前からenableがactiveな信号を狙うときは、開始位置が信号の位相ではなくarmのタイミングで決まる。

## 事実・候補・未決

**事実**

1. `parlio_new_rx_level_delimiter`は`valid_sig_line_id` = 4、data_width 4で`ESP_OK`だった。
2. active high + `eof_data_len` 2,048 byteで受信byteが完全一致し、runは1,023本・run長4固定・gray step違反0。先頭4 sampleはgate開始位置のgray値と一致した。
3. `active_low_en` = trueでも2,048 byteを違反0で取得した。ただしheadは2 runで`0000bbaa`と`00008999`と異なり、開始位置は再現しない。経過251 usはarm直後に開始したことを示す。
4. gateが無いcaseはframeが始まらず、`wait_all_done`が500 msで`ESP_ERR_TIMEOUT`を返した。誤triggerは0。
5. **`eof_data_len` = 0ではdelimiter生成と`receive`が`ESP_OK`でも`on_receive_done`が発火せず、500 msでtimeoutした。** frameが開始しなかったのか、開始してEOFが来なかったのかは本実験では区別していない。

**候補**: hardware側の窓をpulse(単発event)とlevel active high(区間取得)の二つに限り、終了は常に`eof_data_len`とする。level active lowはarmとenableの前後関係で開始位置が決まることを明記して別扱いにする。可変長取得はhardwareに期待せず、`eof_data_len`を上限に置いてsoftware側で有効長を判定する。

**未決**: `eof_data_len` = 0でframeが開始しているのか否か(payloadが書かれるかで判定できる) / `partial_rx_en=true`との組でのlevel delimiter / level delimiterの`timeout_ticks`(enable無効化から数えるので、こちらは効く可能性がある) / gating時の最大sample rate / gate幅の掃引とgate境界のsample精度 / pulse delimiterの`has_end_pulse`による停止 / data_width 8 / 16での構成。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): Trigger節にhardware側の窓の切り方3種と、終了が`eof_data_len`だけであることを追加する
- [LEDGER](../LEDGER.ja.md): E039の節
