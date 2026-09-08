# ESP32-P4 PARLIO routing初期化順の追試計画

状態: **未採番・review待ち**

先行実験: [E014](../../e014_p4_parlio_internal_capture/README.ja.md) / 台帳: [LEDGER](../../LEDGER.ja.md)

## 採番しない理由

E014の結果を受けた追試計画である。reviewで方法が確定し、この追試を次に実行すると決まるまではIDを消費しない。

## 問い

**PARLIO RXに既存GPIO出力を操作させず入力経路だけを追加するか、PARLIOより後にLEDC出力を接続すれば、同じ8 GPIO上でLEDC PWMとPARLIO RXを共存させられるか。**

## 仮説

少なくとも一方で共存できる。

Arduino-ESP32 3.3.11に対応するESP-IDF driver sourceでは、PARLIO data pinの設定は`gpio_input_enable()`と`esp_rom_gpio_connect_in_signal()`を行う。`io_loop_back=true`のときだけ追加で`gpio_output_enable()`を呼ぶ。E014ではこの追加処理後に出力がsimple GPIOへ変わっていたため、`io_loop_back=false`またはLEDCを最後に接続することでPARLIO input mappingを残したままLEDC output mappingを設定できる可能性がある。

## 反証条件

次のすべてでLEDC signalとPARLIO input signalが同時にGPIO dumpへ現れない、またはcaptureがPWM dutyを再現しなければ仮説を反証する。

1. LEDC → PARLIO、`io_loop_back=false`
2. PARLIO、`io_loop_back=false` → LEDC

## 方法

1. E014と同じboard、GPIO 2〜9、LEDC 8 channel、PARLIO RX 8-bitを使う
2. dutyは`16, 48, 80, 112, 144, 176, 208, 240`とし、全laneが必ずhigh/lowを持つようにする
3. 各variantで初段設定後と二段目設定後にGPIO dumpを取る
4. 最終dumpでLEDC output signal IDとPARLIO input signal IDの両方を確認する
5. 8 MHzで8,192 sampleを3回captureし、各laneのhigh/low、edge数、duty比を比較する
6. 追試もbuild・upload・monitorをpytest harness経由だけで行う

二つのvariantを別test関数にするとfirmware uploadまで二度行われるため、同一firmwareへのhost commandでvariantを切り替え、各variantを独立に初期化・解放できる形を基本とする。driver resourceを完全に解放できない場合だけprofileを分ける。

## 対象外

- sample rate上限
- PSRAM
- 外部padの波形品質
- `esp_cache_msync()` alignment errorの原因修正

## 完了条件

- 成立variantがあれば、GPIO dumpと全laneのduty観測をraw logへ残す
- どちらも成立しなければ、公開driver APIだけでは同一pin共存ができないと判断できるログを残す
- 結果に基づき`p4-parlio-rate`を採番するか、低水準GPIO matrix操作の別候補を立てるか判断できる
