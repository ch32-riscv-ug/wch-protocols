# E015 ESP32-P4 PARLIO routing初期化順

状態: **完了**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行実験: [E014](../e014_p4_parlio_internal_capture/README.ja.md)

## 問い

**PARLIO RXに既存GPIO出力を操作させず入力経路だけを追加するか、PARLIOより後にLEDC出力を接続すれば、同じ8 GPIO上でLEDC PWMとPARLIO RXを共存させられるか。**

## 仮説

少なくとも一方で共存できる。

Arduino-ESP32 3.3.11に対応するESP-IDF driver sourceでは、PARLIO data pinの設定は`gpio_input_enable()`と`esp_rom_gpio_connect_in_signal()`を行う。`io_loop_back=true`のときだけ追加で`gpio_output_enable()`を呼ぶ。E014ではこの追加処理後に出力がsimple GPIOへ変わっていたため、`io_loop_back=false`またはLEDCを最後に接続することでPARLIO input mappingを残したままLEDC output mappingを設定できる可能性がある。

## 反証条件

次の両方でLEDC signalとPARLIO input signalが同時にGPIO dumpへ現れない、またはcaptureがPWM dutyを再現しなければ仮説を反証する。

1. LEDC → PARLIO、`io_loop_back=false`
2. PARLIO、`io_loop_back=false` → LEDC

## 方法

1. E014と同じboard、GPIO 2〜9、LEDC 8 channel、PARLIO RX 8-bitを使う
2. LEDCは100 kHz共通、dutyは`16, 48, 80, 112, 144, 176, 208, 240`とし、全laneが必ずhigh/lowを持つようにする
3. 各variantで初段設定後と二段目設定後にGPIO dumpを取る
4. 最終dumpでLEDC output signal IDとPARLIO input signal IDの両方を確認する
5. 8 MHzで8,192 sampleを3回captureし、各laneのhigh/low、edge数、duty比を比較する
6. build・upload・monitorはpytest harness経由だけで行う

二つのvariantを別test関数にするとfirmware uploadまで二度行われるため、同一firmwareへのhost commandでvariantを切り替え、各variantを独立に初期化・解放できる形を基本とする。driver resourceを完全に解放できない場合だけprofileを分ける。

## 対象外

- sample rate上限
- PSRAM
- 外部padの波形品質
- `esp_cache_msync()` alignment errorの原因修正
- GPIO 2〜9を製品の標準pin assignmentにする判断

## 必要な環境

- ESP32-P4 rev 1.3、MAC `e8:f6:0a:e0:aa:24`、32 MB flash
- stable port alias `/run/board-identify/by-id/esp32-p4-e8f60ae0aa24`
- Arduino-ESP32 3.3.11
- 外部配線・target・logic analyzerは不要
- GPIO 2〜9

portとGPIO番号は`experiments/.env`だけに置く。pytest harnessのdevice lockを無効化しない。

## ベンチ種別

**一時・配線なし**。E014と同じP4を使い、既存の常設ESP32-S3 peer配線には触れない。

## 記録する数値

| 項目 | 単位・回数 |
|---|---|
| variant | `ledc-first` / `parlio-first` |
| 各初期化APIの結果 | `esp_err_t`またはbool |
| requested / actual PWM frequency | Hz、laneごと |
| GPIO mapping | 初段後・二段目後、pinごとのSigOut/SigIn ID |
| capture size | 8,192 byte × 3回、variantごと |
| high / low count | sample、laneごと・3回 |
| edge count | edge、laneごと・3回 |
| observed / expected duty | ppm、laneごと・3回 |
| driver warning/error | raw serial log |

## 完了条件

- 成立variantがあれば、GPIO dumpと全laneのduty観測をraw logへ残す
- どちらも成立しなければ、公開driver APIだけでは同一pin共存ができないと判断できるログを残す
- 結果に基づき`p4-parlio-rate`を採番するか、低水準GPIO matrix操作の別候補を立てるか判断できる

## 影響

[Arduino向けprobe protocol実現性](../../references/arduino-probe-protocol-feasibility.ja.md)のESP32-P4 logic capture候補。成功しても外部padの電気的波形やsample rate上限は`verified`にしない。

---

## 結果

採用run: `_runs/E015_20260908T095518Z_default/test_routing_orders/dut.log`

pytest結果: **1 passed in 12.80 s**。

| variant | 最終SigOut | 最終SigIn | capture | duty誤差の最大値 | edge範囲 |
|---|---|---|---:|---:|---:|
| LEDC → PARLIO | 126〜133 (LEDC) | 188〜195 (PARLIO RX) | 8,192 byte × 3回、全lane成功 | 2,198 ppm | 204〜206 |
| PARLIO → LEDC | 126〜133 (LEDC) | 188〜195 (PARLIO RX) | 8,192 byte × 3回、全lane成功 | 1,832 ppm | 204〜207 |

両variantとも、最終状態ではGPIO 2〜9の`InputEn=1`、`OutputEn=[periph_sig_ctrl]`となった。LEDCの実周波数表示は全lane 100 kHz。PARLIO RXは8 MHzに設定し、各laneの観測dutyは設定値`16/256`〜`240/256`と一致した。

最初のrun (`E015_20260908T095217Z_default`) はhost側tuple indexの誤り、次のrun (`E015_20260908T095248Z_default`) は行末を待たない正規表現が`937744`を受信途中の`9377`で確定したため、判定コードを修正した。いずれもdevice側のcapture失敗ではない。採用runでは正規表現を改行まで一致させ、全48観測を完全な行として検査した。

## 事実

1. **`io_loop_back=false`なら、同一GPIOにLEDC出力とPARLIO RX入力を公開APIだけで共存させられる。** 初期化順はどちらでも成立した。
2. **E014で出力経路がsimple GPIOへ変わった原因は初期化順ではなく`io_loop_back=true`である。** 既に別peripheralが駆動するpinを内部観測するとき、PARLIO側に出力loopback設定は不要。
3. 8 MHz設定・8-bit・8,192 sampleでは、8 laneすべての100 kHz PWM dutyとedgeを3回連続で取得できた。

## 候補

既存peripheralの内部観測では、信号源を先に固定せず、PARLIO RXを`io_loop_back=false`で入力経路だけに接続する。

## 未決

- sample rate上限: 未採番候補`p4-parlio-rate`
- PARLIOからPSRAMへの直接DMA: [E016](../e016_p4_parlio_psram_direct/README.ja.md)
- PSRAM帯域とinternal RAMからの退避限界: 未採番候補`p4-psram-bandwidth` / `p4-parlio-psram-spool`
- 外部pad上の電気的波形とsample clockの実周波数は未測定。本結果は配線なしの内部経路についての`attested`であり、外部信号capture全般を`verified`にはしない

## 反映

- [実験台帳](../LEDGER.ja.md)へ結果と未採番候補を記録した。
- [Arduino向けprobe protocol実現性](../../references/arduino-probe-protocol-feasibility.ja.md)の採否は変更しない。速度・PSRAM・外部信号が未決のため。
