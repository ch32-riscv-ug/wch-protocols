# E014 ESP32-P4 PARLIO内部capture

状態: **中断 — 選んだ初期化方法は反証**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md)

## 問い

**ESP32-P4で8本のGPIOをLEDCのPWM出力に使ったまま、同じGPIOをPARLIO RXの8-bit入力へ接続し、外部配線なしで8本すべてを同時captureできるか。**

## 仮説

できる。

ESP32-P4のGPIO matrixはperipheral出力をGPIOへ接続しながら入力経路を別peripheralへ接続できる。ESP-IDFのPARLIO RXには、GPIOへ出力した内部信号を入力経路へ戻す`io_loop_back`がdebug/test用として用意されている。Espressifのlogic analyzer例も、GPIOが生成する内部信号をPARLIO RXで並列captureする構成を示している。

Arduino標準APIにはPARLIOのclassはないため、Arduino sketchからLEDCにはArduino API、PARLIO RXには同梱されたESP-IDF driver APIを直接使用する。

## 方式の選定

### 信号源

最初の信号源には**LEDC PWM**を使う。

| 候補 | 利点 | 欠点 | この実験での扱い |
|---|---|---|---|
| LEDC PWM | hardwareだけで周期動作する。Arduino標準APIから設定できる。P4に8 channelあり、同一周波数・異なるdutyで8 laneを識別できる | 独立timerは4本なので8種類の独立周波数は作れない。複雑なpatternには向かない | **採用**。共通周波数・異なるdutyを使う |
| UART | 波形が既知でprotocol decoderでも確認しやすい | 1本のserial信号なので8-bit並列経路の確認には弱い | 後の実用例候補 |
| RMT TX | pulse列を正確に作れ、可変長patternにも向く | P4のTX候補は4 channelで、8 lane同時確認には足りない | edge型captureとの比較用 |
| PARLIO TX | 高速な8/16-bit既知patternをDMAで反復できる | RXと同じPARLIO blockを使うため、「別peripheralを動かしながら観測する」という最初の問いがぼやける | PARLIO速度掃引用の信号源候補 |
| GPIO software loop | 実装が最小 | task割込みやCPU負荷のjitterが混ざり、capture側の評価と分離しにくい | 不採用 |

LEDCの出力設定後にPARLIO RXを同じGPIOへ接続する。これにより、Arduinoの通常機能を利用するapplicationにcapture機能を後付けできるかを直接確認できる。

### capture engine

最初のcapture engineには**PARLIO RX**を使う。

| 候補 | 得られるデータ | 向く用途 | 限界 |
|---|---|---|---|
| PARLIO RX | 固定sample clockごとの1/2/4/8/16-bit GPIO状態 | 汎用logic analyzer、複数laneの同期capture | data量がsample rateに比例する。P4ではRX unitが1本 |
| RMT RX | levelと継続時間の列 | PWM、UART、SWD等のedgeが疎な信号 | RX候補は4 channel。全laneの同時sampleではなくchannel間の共通時刻表現も必要 |
| MCPWM capture | edge発生時のtimer値 | 周期・pulse幅の高精度計測 | ISR中心のedge eventであり、任意のlogic状態列を保存できない |
| GPIO interrupt | edge event | 低速な状態変化の通知 | 高速・多channelではCPU負荷と取りこぼしが支配的になる |
| SPI/I2S/camera系RX | 各bus固有のframe | 対応busが明確な専用capture | clock、frame、pin配置等の制約が強く、汎用pin samplerにはしにくい |

PARLIOは固定間隔のraw sampleを得る主capture engineとし、RMT等はraw sample量を減らせる**別形式のcapability**として後で比較する。PARLIOの代替として一つへ統一する前提にはしない。

## 反証条件

次のいずれかが起きたら、そのままでは成立しない。

1. Arduino-ESP32 3.3.11でPARLIO RX APIを含むsketchをbuildまたはlinkできない
2. 8本のLEDC PWMを設定できない
3. PARLIO RXを同じ8本へ接続すると、先に設定したLEDC出力が解除される
4. PARLIO RXの有限長DMA captureが完了しない
5. captureした8 laneのいずれかが固定値になり、設定したPWMのhigh/lowを両方含まない
6. 各laneの観測dutyが設定値から許容差を超えて外れる

## 方法

1. Arduino-ESP32 3.3.11の`esp32:esp32:esp32p4` profileでbuild・uploadする
2. `.env`で指定する8本のGPIOへ、LEDCの8 channelを同一周波数・異なるdutyで設定する
3. 同じ8本をPARLIO RXのdata line 0〜7へ割り当て、内部clockと`io_loop_back`を有効にする
4. 8-bit幅の有限長sample bufferをDMAで3回captureする
5. 各laneについてhigh数、low数、edge数、high比率を出力する
6. host側pytestがAPIの成功、全laneの遷移、duty比率を検査する
7. GPIO構成dumpをログへ残し、LEDC出力と入力enableが同時に維持されているか確認する

PWMは各laneの識別ができるよう異なるdutyとする。capture開始位相は同期しないため、波形の先頭値やedge位置の完全一致は要求せず、十分な周期数に対するhigh比率で判定する。

## 対象外

- PARLIO RXの最高sample rateと長時間連続動作。routing成立後に別実験で掃引する
- 外部targetからの電圧、立ち上がり、skew、noise
- probe用GPIOの最終pin assignment
- PSRAMへのDMAと、USB/Ethernetへの同時streaming
- PARLIO以外のcapture peripheralとの性能比較
- PSRAMの単体帯域と、internal RAMからPSRAMへ退避する連続capture pipeline

## 必要な環境

- ESP32-P4 rev 1.3、MAC `e8:f6:0a:e0:aa:24`、32 MB flash
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11
- 外部配線・target・logic analyzerは不要
- GPIOはstrapping pin 34〜38とUSB-JTAG pin 24/25を避け、初回候補を2〜9とする

portとGPIO番号は`experiments/.env`だけに置く。upload・monitorはdevice lockを持つpytest harness経由でのみ実行する。

## ベンチ種別

**一時・配線なし**。内部loopbackだけを使用し、既存の常設ESP32-S3 peer配線には触れない。

## 記録する数値

| 項目 | 単位・回数 |
|---|---|
| requested / actual PWM frequency | Hz、laneごと |
| requested duty | sample比、laneごと |
| PARLIO requested sample rate | sample/s |
| capture size | byte、3回 |
| high / low count | sample、laneごと・3回 |
| edge count | edge、laneごと・3回 |
| observed high ratio | %、laneごと・3回 |
| PARLIO API result | `esp_err_t`、各段階 |
| GPIO configuration | GPIO dump、初期化後 |

## 完了条件

成功・失敗のどちらでもraw logが`_runs/`へ残り、次を判断できること。

- Arduino sketchからESP-IDF PARLIO RX APIを利用できるか
- peripheral出力とPARLIO入力を同じ8 GPIOへ同時接続できるか
- 配線なしの内部信号で8-bit並列captureの開発・回帰確認ができるか

## 影響

[Arduino向けprobe protocol実現性](../../references/arduino-probe-protocol-feasibility.ja.md)のESP32-P4 logic capture候補。成功しても外部信号の電気的なcapture性能は`verified`にしない。

## 後続実験との境界

E014は同一GPIOでのperipheral出力とPARLIO入力の**共存可否だけ**を決める。後続は台帳の未採番候補として保持し、E014と各実験の結果を見て、次に実行する一件だけを採番する。次の並びは依存関係の概要であり、実行順や実施を確定するものではない。

1. `p4-parlio-rate` — internal RAMへの有限長captureでPARLIO自体の速度上限を測る
2. `p4-psram-bandwidth` — PSRAM単体の容量とcopy帯域を測る
3. `p4-parlio-psram-spool` — internal DMA bufferからPSRAMへ退避する連続pipelineの上限を測る
4. `p4-rmt-capture` — edge表現がraw sampleより有利になる範囲を測る
5. `p4-adc-continuous` — analog入力の連続DMA captureを独立に確認する

先行結果によって不要になった候補、方法を変更すべき候補にはIDを消費しない。後続のID、実行順、完了条件は、それぞれを着手する直前のreviewで確定する。

P4 hardwareはPSRAMへDMA accessできるが、Arduino-ESP32 3.3.11同梱構成では`CONFIG_PARLIO_RX_ISR_CACHE_SAFE=y`であり、PARLIO RX driverはpayloadにinternal RAMを要求する。このため、PSRAM実験ではdirect DMAが通ると仮定せず、まず拒否条件を確認し、基本案をinternal DMA bufferからPSRAMへのcopyとする。

---

## 結果

2026-09-08、`esp32p4_parlio` profileをpytest harnessから1回実行した。

run: `_runs/E014_20260908T093607Z_default/test_internal_parallel_capture/dut.log`

| 項目 | 結果 |
|---|---|
| build | pass、flash 353,704 B、global 31,232 B |
| upload | pass、ESP32-P4 rev 1.3、MAC `e8:f6:0a:e0:aa:24` |
| LEDC設定 | Arduino APIは8 channelとも成功を返した |
| PARLIO初期化 | `ESP_OK` |
| PARLIO有限長capture | 8,192 byte × 3回、APIはいずれも`ESP_OK` |
| sample | 全3回・全8 laneで`high=0`、`low=8192`、`edges=0` |
| pytest | 1 failed、27.23 s |

GPIO dumpでは全8 pinについて`InputEn: 1`で、PARLIO data lineに対応する`GPIO Matrix SigIn ID: 188`〜`195`が設定されていた。一方、出力は全pinとも`GPIO Matrix SigOut ID: 256 (simple GPIO output)`であり、LEDCのsignal IDは残っていなかった。したがって、全sampleが0だった直接の理由は、PARLIO RXが読めなかったことではなく、capture時点で観測対象のLEDC出力がGPIOへ接続されていなかったことと整合する。

各capture開始時に、PARLIO driver内部からcache line alignmentに関する次のerrorも出た。ただしtransaction APIは`ESP_OK`を返した。

```text
cache: esp_cache_msync(...): start address ..., or the size: 0x38 is(are) not aligned with cache line size (0x40)B
```

### 事実

1. Arduino-ESP32 3.3.11からPARLIO RX driverをbuild・linkでき、8-bit・8,192 byteの有限長DMA transactionは完了する。
2. 計画した`LEDC設定 → PARLIO設定(io_loop_back=true)`では、PARLIO入力を追加した後にLEDC出力接続が維持されなかった。
3. PARLIO入力の8 signalはGPIO matrixへ設定されていたが、入力値は全lane・全runで0だった。
4. 実装はlane 7のdutyに`256/256`を指定しており、反証条件5の「全laneがhigh/lowを両方含む」を満たせない試験vectorだった。全lane 0という今回の主症状とは別だが、このvectorのまま追試には使えない。

### 候補

- `io_loop_back`を使わず、PARLIO側はinput enableとinput matrix接続だけを行う
- PARLIOを先に初期化し、その後にLEDC outputを接続する
- LEDC後とPARLIO後の二段階でGPIO dumpを取り、どのAPIでoutput signalが変わるか特定する

これらは計画した方法の変更になるためE014内では試さない。未採番計画[`p4-parlio-routing-order`](../plans/p4-parlio-routing-order/README.ja.md)へ分離する。

### 未決

- 同じGPIOでLEDC出力とPARLIO入力を共存できるか → `p4-parlio-routing-order`
- `esp_cache_msync()`のalignment errorがdata integrityまたは高sample rateに影響するか → 共存成立後の`p4-parlio-rate`
- 外部padの実電圧とPARLIO入力値が一致するか → 今回は配線なしのため未測定

## 反映

選んだ初期化方法は反証されたが、PARLIO方式全体は不成立と判断しない。後続の速度・PSRAM実験は、未採番のrouting追試が成立してから採番可否を判断する。
