# E143 RVSWDでX035を刺激してP4側の配線を探索する

状態: **完了**（2026-09-20）

## 問い

E142で確定したP4 GPIO2=SWDIO、GPIO54=SWCLKを使い、X035側GPIOを1本ずつLOW/HIGHにしたときP4のどの安全GPIOが反応するか。配線済み20本の対応表を仮定なしで発見できるか。

## 方法

1. RVSWDでX035をhaltする。
2. abstract commandのprogram bufferでX035 peripheral word read/writeを行う。
3. 各GPIOの設定とOUTDRを退避し、10 MHz push-pull出力へ一時変更する。
4. LOW/HIGHそれぞれでP4の安全GPIO46本を入力観測し、変化したpinを記録する。
5. 各pinを即時復元し、最後にX035をdebug resetして通常起動へ戻す。

X035のPC16/17（USB）とPC18/19（RVSWD）は刺激対象外。P4のGPIO24–27（USB）、34–38（strap/UART）も観測対象外。target flashとoption bytesは変更しない。

## 結果

RVSWD haltとabstract commandによるword read/writeが成立した。開始時の代表値は`DMSTATUS=0x00030382`、`RCC_APB2PCENR=0x0000001c`。各target pinをLOW→HIGH→LOW→HIGHと2往復させ、両往復で同じP4 pinだけが追従する条件で次を得た。

| CH32X035 | ESP32-P4 | 備考 |
|---|---:|---|
| PA0 | GPIO46 | |
| PA1 | GPIO47 | |
| PA2 | GPIO48 | |
| PA3 | GPIO49 | |
| PA4 | GPIO53 | |
| PA5 | GPIO4 | |
| PA6 | GPIO11 | |
| PA7 | GPIO5 | |
| PB0 | GPIO12 | |
| PB1 | GPIO6 | |
| PB3 | GPIO13 | |
| PB11 | GPIO9 | |
| PB12 | GPIO14 | |
| PC10 | GPIO50 | |
| PC11 | GPIO52 | |
| PC14 | GPIO10 | |
| PC15 | GPIO15、GPIO45 | **P4の2本が同じtarget pinへ接続** |
| PC18 / SWDIO | GPIO2 | E142で確定 |
| PC19 / SWCLK | GPIO54 | E142で確定 |

17本の通常target GPIOにP4側18本が対応し、RVSWD 2本を加えると、配線したという**P4側20本すべて**を説明できる。

逆向きにもP4 GPIOを1本ずつ駆動してX035 INDRを読んだ。主要対応とGPIO15/45→PC15は再現した。一方、target側をfloating inputへ一括変更した条件では未bond pin・内部alias由来とみられる追加反応（例: GPIO4→PA2/PA5）が出た。このためcanonical mappingは、targetがpush-pullで能動駆動した上表とする。

## 事実

- RVSWDだけでX035をhaltし、周辺レジスタをread/writeし、GPIO配線を探索できた。
- target flash、option bytes、USB PC16/17には触れていない。
- 各pinのGPIO設定とOUTDRを復元し、最後にdebug resetを実施した。
- P4側のUSB、strap、UART pinは探索・駆動対象から除外した。

## 未決

- PC15へGPIO15/45を二重接続したことを、今後のfixtureで意図的冗長として使うか、一方を未使用扱いにするか。
- USB PC16/17はtarget USBのまま残しており、P4とのGPIO配線には含めない。
