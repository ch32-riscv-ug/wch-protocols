# E061 ESP32-P4 回収を別coreへ移すとdrainは上がるか

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 調査地図: [P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md) / 先行実験: [E060](../e060_p4_drain_batch_coalesce/README.ja.md)・[E059](../e059_p4_drain_breakdown/README.ja.md)

## 問い

**PARLIOのISRが走るcoreとmemcpyするcoreを分けると、window中のdrain帯域は上がるか。**

## 仮説

[E059](../e059_p4_drain_breakdown/README.ja.md)でdrain低下の原因が1 chunkあたり3.6〜5.2 usの固定costだと分かり、[E060](../e060_p4_drain_batch_coalesce/README.ja.md)で`xQueueReceive`のまとめ取りもmemcpyのまとめも効かないことが分かった。memcpyの累積時間は3条件でほぼ一定だったので、固定costはdriver側のISRが支配している。**task側で残る手は回収を別coreへ移すことだけである。**

P4は`SOC_CPU_CORES_NUM` = 2でHP coreが2基ある。ESP-IDFのinterruptは`esp_intr_alloc`を呼んだcoreに割り当てられるので、driverの生成・enableをArduinoのloop task上で行えばISRはそのcoreに載る。回収loopだけを`xTaskCreatePinnedToCore`で別coreへ置けば、**ISRの実行とmemcpyが別coreに分かれる。**

ISRがmemcpyを止めている分だけdrainが上がるはずである。[E059](../e059_p4_drain_breakdown/README.ja.md)の160 MHzではmemcpy占有率が77%だったので、ISRの分がまるごと消えればdrainはmemcpy帯域107 MB/sへ近づく。

ただし二つのcoreは同じcacheとPSRAM controllerを共有するので、memcpy帯域そのものは上がらないか、cache競合で下がる可能性もある。

case:

| # | driver生成・ISR | 回収loop | 分かること |
|---:|---|---|---|
| 1 | loop taskのcore | **同じcore** | [E060](../e060_p4_drain_batch_coalesce/README.ja.md)基準の再現 |
| 2 | loop taskのcore | **core 0へpin** | 片方のcoreへ寄せた場合 |
| 3 | loop taskのcore | **core 1へpin** | もう片方へ寄せた場合 |

Arduinoのloopがどのcoreにいるかは`xPortGetCoreID()`で記録する。case 2と3のどちらかがloop taskと同じcoreになるので、その組が実質case 1の対照になり、もう一方が分離条件になる。

sample rateは160 MHz、window byte長は96,000、回収はper-chunk copy(まとめ取りもmemcpyまとめもしない)に固定する。[E060](../e060_p4_drain_batch_coalesce/README.ja.md)で基準のISR側未読最大は54,656、drainは82.3 MB/s、memcpy帯域は107.3 MB/sだった。

## 反証条件

- 3条件でISR側未読最大とmemcpy帯域が変わらない(core分離が効かない)
- 分離するとdrainが下がる(cache競合が勝つ)
- pinしたcoreでtaskが起動しない、またはwatchdogが出る
- どれかの条件で飛びの急増やqueue overflowが出る

## 方法

[E060](../e060_p4_drain_batch_coalesce/README.ja.md)の構成から回収loopをtaskへ切り出し、pinするcoreだけを変える。captureの設定は変えない。

- RX: data_width 8、`data_gpio_nums[0..7]` = GPIO 2〜9、`valid_gpio_num` = GPIO 9(data線7と同一)、`valid_sig_line_id` = 8、160 MHz
- delimiter: level delimiter、active high、`eof_data_len` = 0、`partial_rx_en=true`。`max_recv_size`と受信sizeは62,720
- TX: data_width 8、GPIO 2〜9、5 MHz。1 loopは6,000 wordで先頭3,000だけbit 7をhigh(duty 50%、window 96,000 byte)
- queue深さ64、回収は100 ms、destinationは4 MiB PSRAM、未読はISR内で標本化
- driverの生成・enable・start、およびstop・検証・報告はArduinoのloop task上で行う。**回収loopだけをtaskへ出し、case 1は同じcore、case 2 / 3はcore 0 / 1へpinする**
- 回収taskの優先度は3条件とも5で揃える
- `xPortGetCoreID()`でloop taskのcoreと回収taskのcoreを記録する

APIの不成立も結果として最後まで記録する。

## 対象外

ISR自体のcore affinityを明示的に変える方法、ISRの実行時間の直接測定、優先度の掃引、sample rateとwindow長の掃引、destinationをinternal RAMへ置く構成、triggerなしspool経路での同じ測定。

## 必要な環境

- 32 MiB PSRAM搭載ESP32-P4 rev 1.3
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5
- `TEST_PARLIO_PINS`のGPIO 2〜9のみ。外部配線不要

ベンチ種別: **一時・配線なし**

## 記録する数値

case、loop taskのcore、回収taskのcore、各API結果、memcpyの累積時間・累積byte・呼び出し回数、memcpy帯域、ISR側とtask側の未読最大、そこから逆算したdrain、queue overflow、飛びの総数と期待境界数、階差一致数、回収byte、経過us。

## 完了条件

3条件のdrainとmemcpy帯域を記録し、core分離がdrainを上げるか否かを確定する。上がる場合はその量を記録する。

## 影響

上がるなら、gated captureのdrainは実装のcore配置で決まることになり、条件2の`drain(rate)`は「ISRと回収を分けたときの値」として申告できる。上がらないなら、drainの改善手段は尽きたことになり、条件2の`drain(rate)`表がそのまま上限として確定する。[P4 logic analyzer予備調査](../../references/p4-logic-analyzer-investigation.ja.md)のdrain表が閉じる。
