# E040 ESP32-P4 `eof_data_len` = 0のlevel delimiterでDMAは走るか

状態: **完了 — DMAは走る。gateはdutyどおりstreamを間引く**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E039](../e039_p4_parlio_level_gate/README.ja.md)

## 問い

**`eof_data_len` = 0のlevel delimiterで、DMAは実際にpayloadへdataを書いているのか。書いているなら`partial_rx_en`との組で「hardwareでgateし、softwareで止める」modeとして使えるか。**

## 仮説

E039のcase 2は、`eof_data_len` = 0のlevel delimiterで`on_receive_done`が一度も発火せず、`wait_all_done`が500 msでtimeoutした。しかしE039はpayloadの中身を`wait_result == ESP_OK`のときだけ見る設計だったため、**frameが開始しなかったのか、開始してEOFだけが来なかったのか**を区別していない。

区別は簡単につく。payloadを0xA5で埋めておき、timeout後も無条件にM2C syncしてから中身を見ればよい。DMAが走っていれば0xA5以外のbyteが現れる。

もしDMAが走っているなら、EOFが来ないだけである。その場合は`partial_rx_en=true`にすればdataは`on_partial_receive`から取り出せるので、「hardwareがenable区間だけをsampleし、softwareが好きなところで止める」modeになる。これはE039で不成立とした可変長取得を、別の形で回収できることを意味する。

gateはsource 16,384 wordのうち2,048 wordだけhighなのでduty 12.5%である。hardware gatingが本当にstreamを絞っているなら、`partial_rx_en`で受け取るbyte rateはraw byte rateの約12.5%になるはずである。絞っていないなら100%になる。

## 反証条件

- timeout後のpayloadが全域0xA5のまま(= DMAは走っていない)
- `partial_rx_en=true`でも`on_partial_receive`が発火しない
- `partial_rx_en=true`のbyte rateがraw byte rateとほぼ同じ(= gateが効いていない)
- gateが無いのにdataが来る

## 方法

RX rateは20 MHzに固定する。source構成はE039と同じで、gate幅2,048 word / 周期16,384 word(duty 12.5%)である。

- RX: data_width 4、`data_gpio_nums[0..3]` = GPIO 2〜5、`valid_gpio_num` = GPIO 6、`valid_sig_line_id` = 4
- delimiter: level delimiter、active high、`eof_data_len` = 0、`timeout_ticks` = 0
- payloadは16 KiB internal RAM、0xA5で初期化する

case:

| # | partial_rx_en | pattern上のgate | 待ち方 | 見るもの |
|---:|---|---|---|---|
| 1 | false | あり | `wait_all_done` 500 ms | timeout後のpayloadに0xA5以外があるか。最初と最後の書き込み位置 |
| 2 | true | あり | 50 msだけchunkを回収 | callback数、byte数、byte rateがraw byte rateの何%か |
| 3 | true | なし | 50 msだけchunkを回収 | dataが来ないこと |

raw byte rateは20 MHz / data_width 4なので10 MB/sである。case 2でgateが効いていれば約1.25 MB/sになる。

case 1では、timeout・成否に関わらずpayload全域をM2C syncしてから、0xA5以外のbyte数、最初と最後の位置、先頭8 byteを記録する。

APIの不成立も結果として最後まで記録する。

## 対象外

gate境界のsample精度、gating時の最大sample rate、gate幅の掃引、pulse delimiterの`has_end_pulse`、level delimiterの`timeout_ticks`、data_width 8 / 16、PSRAMへの直接受信、回収したdataからgate境界を復元すること。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線・target・logic analyzerは不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、`partial_rx_en`、gate有無、各API結果、`wait_all_done`結果、receive_done回数、partial callback数、回収byte数、payload中の0xA5以外のbyte数、最初と最後の書き込み位置、先頭8 byte、回収window中のbyte rate、raw byte rateとの比、経過us。

## 完了条件

`eof_data_len` = 0でDMAが走るか否かを確定する。走る場合は`partial_rx_en`での回収が成立するかと、gateがstreamを絞っているかを記録する。

## 影響

DMAが走ることが分かれば、E039で不成立とした可変長取得を「hardware gate + software停止」として回収できる。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のhardware窓の記述が、「終了は`eof_data_len`だけ」から「有限transactionでは`eof_data_len`だけ、partialならsoftware停止も可」へ変わる。

## 結果

実施日: 2026-09-09

採用run: `_runs/E040_20260909T064609Z_default/test_parlio_level_open_frame/dut.log`

| # | partial_rx_en | gate | wait | receive_done | partial callback | 回収byte | payload中の書き込み | 先頭8 byte | 回収rate | rawとの比 |
|---:|---|---|---|---:|---:|---:|---|---|---:|---:|
| 1 | false | あり | `ESP_ERR_TIMEOUT` | 0 | 0 | — | **16,384 / 16,384** | `00 00 11 11 33 33 22 22` | — | — |
| 2 | true | あり | (固定window) | 0 | 20 | 65,536 | 16,384 / 16,384 | `00 00 11 11 33 33 22 22` | 1,240 KB/s | **12%** |
| 3 | true | なし | (固定window) | 0 | 0 | 0 | **0 / 16,384** | `a5 a5 a5 a5 a5 a5 a5 a5` | 0 | 0% |

**`eof_data_len` = 0でもDMAは走っている。** case 1は`on_receive_done`が発火せず`wait_all_done`が500 msでtimeoutしたが、payload 16,384 byteは全域が0xA5から書き換わっていた。E039の「不成立」は**取得が起きないことではなく、完了eventが来ないことだった。**

書かれたdataは正しい。先頭8 byteは`00 00 11 11 33 33 22 22`で、data_width 4のLSB packingを展開すると0,0,0,0,1,1,1,1,3,3,3,3,2,2,2,2である。`gray4(0..3)` = 0, 1, 3, 2に一致し、run長は4。**frame先頭はgate開放位置(source index 2048、`gray4(2048 & 0xF)` = 0)にそろっている。**

**`partial_rx_en=true`ならdataを回収できる。** case 2は52,817 usで20回のcallback、65,536 byteを取り出せた。queue overflowは0。これは「hardwareがgateし、softwareが好きなところで止める」modeとして成立する。

**hardware gatingはstreamを実際に間引いている。** case 2の回収rateは1,240 KB/sで、raw byte rate 10,000 KB/s(20 MHz / data_width 4)の**12%**だった。gateのdutyは2,048 / 16,384 = 12.5%である。gateがactiveな区間のsampleだけがDMAへ渡り、idle区間は保存されない。

回収量の内訳も一致する。52.817 msはsource loop周期3.2768 ms(16,384 word / 5 MHz)の約16周期にあたり、1周期あたりのgateは2,048 word = 8,192 sample = 4,096 byteなので、16 × 4,096 = 65,536 byteである。callbackが16回でなく20回なのは、driverのdescriptor境界(`max_recv_size` 16,384を4,032 byte単位で分割)とgate境界が一致しないためで、一部のgate windowが2 descriptorにまたがったと考えれば数が合う。ただし機構は本実験では確認していない。

**gateが無ければ何も起きない。** case 3はcallback 0、回収0で、payloadは全域0xA5のままだった。誤triggerも書き込みも無い。

case 1でpayloadが満杯になった後にDMAが停止したのか周回を続けたのかは、本実験の計測では区別できない。

## 判定

**`eof_data_len` = 0のlevel delimiterは「終わらないtransaction」であり、取得自体は正しく動く。`partial_rx_en=true`と組めば、hardware gate + software停止という可変長取得modeが成立する。** E039で「可変長は不成立」としたのは、有限transactionで完了eventを待った場合の話に限られる。

さらに重要な副産物がある。**hardware gatingはgateのdutyそのままにstreamを間引く。** idle区間のsampleはDMAへ渡らないので、保存量がdutyに比例して減る。CPU負荷はゼロである。

これはlogic analyzerとしては単なるtrigger機能ではなく、**hardwareによるcapture qualification**にあたる。chip selectやenable線がassertされている区間だけを取る使い方で、次の二つが同時に得られる。

- **有効深度の拡大** — dutyが12.5%なら、同じPSRAM容量で8倍の時間を覆える
- **帯域の緩和** — spool経路へ流れるbyte rateがdutyに比例して下がる。E036の約98 MB/sという持続限界に対して、gateがあれば見かけのsample rateをその分だけ上げられる

つまり[調査地図](../../references/p4-logic-analyzer-investigation.ja.md)の「内部圧縮」節に、CPUを使わない圧縮手段が一つ増える。RLEやtransition timestampと違って入力の性質に依存せず、最悪時膨張が無い代わりに、**qualifier線を1本必要とし、gate外の情報は完全に失われる**。

## 事実・候補・未決

**事実**

1. `eof_data_len` = 0のlevel delimiterで`on_receive_done`は発火しないが、payload 16,384 byteは全域が書き換わった。DMAは走っている。
2. 書かれたdataは`gray4`のrun長4の正しい列で、先頭はgate開放位置の値(0)にそろっていた。
3. `partial_rx_en=true`では52,817 usで20 callback・65,536 byteを回収でき、queue overflowは0だった。
4. 回収byte rateは1,240 KB/sで、raw byte rate 10,000 KB/sの12%。gate dutyは12.5%である。**gateがactiveな区間のsampleだけがDMAへ渡る。**
5. gateが無いcaseはcallback 0、回収0、payloadは全域0xA5のままだった。
6. payload満杯後にDMAが停止したか周回したかは区別していない。

**候補**: hardware窓のmodeを三つに分ける。(a) pulse + 有限transaction = 固定長・完了event有り、(b) level + 有限transaction = 固定長・完了event有り、(c) level + `eof_data_len` 0 + `partial_rx_en` = 可変長・software停止・完了event無し。(c)をcapture qualificationとして「内部圧縮」の選択肢に入れ、qualifier線1本の消費とgate外情報の喪失を明記する。

**未決**: gate境界のsample精度(gate開放・閉止のedgeに対して何sampleずれるか) / gating時の最大sample rate / gate dutyを変えたときの回収rateの線形性 / payload満杯後のDMA挙動 / callback数がgate windowと一致しない機構 / gate windowの境界をmetadataとして復元する方法(現状は連結されて境界が失われる) / `partial_rx_en`との組でのPSRAM spool。

## 反映

- [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md): hardware窓の記述を三つのmodeに直し、「内部圧縮」節にcapture qualificationを追加する
- [E039](../e039_p4_parlio_level_gate/README.ja.md): 「可変長は不成立」の判定範囲を追記で限定する
- [LEDGER](../LEDGER.ja.md): E040の節
