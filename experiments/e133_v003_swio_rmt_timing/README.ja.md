# E133 ESP32 RMTでV003 SWIO実波形を測る

状態: **完了（2026-09-20）**

## 問い

software SWIOの固定係数10〜12でread/write結果が変わる原因を、同じGPIO16へ重ねたRMT RXの
実測pulse幅から切り分けられるか。

## 方法

- ESP32 GPIO16 → V003 PD1/SWIOの既存配線だけを使う。
- RMT RXをGPIO16へ入力として追加し、80 MHz（12.5 ns/tick）でedge間隔を取得する。
- CPUは従来と同じGPIO register直書きPHYでDMCFGRをreadする。
- coefficient 8〜14について、decode値とRMT symbol列を記録する。
- captureを付けない同じreadも直後に行い、RMT併用自体が結果を変えないか確認する。

flash、BOOT領域、option byteは変更しない。GPIO23 resetも使用しない。

## 完了条件

係数ごとのhost pulse幅、turnaround、target response pulse幅を取得し、成功値と化けた値を比較する。

## 未決

- RMT RX入力追加がsoftware SWIOの立上りへ与える影響
- 最適条件を固定係数にするか、起動時校正にするか

## 結果

GPIO16へのRMT RX追加はsoftware GPIO outputと共存し、63 symbolのread transactionを
12.5 ns/tickで取得した。最初のhost LOWは係数8/9/10で21/23/25 tick
（262.5/287.5/312.5 ns）、長LOWは代表値69/81/85 tick
（862.5/1012.5/1062.5 ns）だった。

各係数を10回測ると、capture付き/直後の通常readでDMCFGRが一致した回数は次のとおりだった。

| coefficient | capture read | plain read |
|---:|---:|---:|
| 8 | 10/10 | 10/10 |
| 9 | 10/10 | 10/10 |
| 10 | 10/10 | 10/10 |
| 11 | 9/10 | 10/10 |
| 12 | 9/10 | 8/10 |
| 13 | 10/10 | 7/10 |
| 14 | 9/10 | 1/10 |

DMI DATA1へ100個の既知値を書き、係数8で独立検証したwrite一致回数は係数8〜14で
100、100、100、99、100、98、98だった。同じ係数でreadした一致回数は
100、100、99、99、93、95、93だった。遅くすれば安定する関係ではない。

係数8をOEP backendへ適用するとDMI単発試験は通ったが、abstract memory/flash sequenceは
成立せず、全page要求が診断03で失敗した。固定係数の単発DMI成功率だけではflash条件を選べない。
また同じDMI registerの多重readとabstract read後のDATA1照合は、既存sequenceを全失敗にしたため
撤回した。次はflash register sequence全体をRMT captureし、どのtransactionで逸脱するかを測る。
