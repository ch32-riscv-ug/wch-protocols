# E131 Arduino sketch全体をV003へSWIO書込みして実行する

状態: **完了（2026-09-18）**

## 問い

UIAP Arduino coreでbuildしたCH32V003 sketchを、ESP32 GPIO16→PD1/SWIOだけでuser flashへ書込み、reset後に`setup()`の実行をRAM markerで確認し、さらにオリジナルUIAP bootloaderへ戻れるか。

## 仮説

E130で成立した64 byte page単位のerase/program/verifyをbin全pageへ適用すれば、Arduinoコアへ渡すV003 uploaderのend-to-end最小経路が成立する。

## 反証条件

UIAP core build、全page write/read-back、user mode reset、RAM marker、bootloader再entryのいずれかが失敗する。

## 方法

1. UIAP core 1.0.42、Pro Micro CH32V003 V1.4、48 MHz HSIでfixture sketchをbuildする。
2. ELFから`e131_marker`のRAM addressを取得する。
3. binを64 byte境界へ`FF` padし、全pageをCRC32付き要求でSWIO erase/program/verifyする。
4. BOOT_MODEを明示的にuserへ設定してCPU software resetする。
5. RAMをSWIO readし、`setup()`が書く`0xE131B007`を確認する。
6. SWIO-only手順で`1209:b803`へ戻る。

BOOT領域とGPIO23外部resetは使用しない。user flashはfixtureへ上書きする。

## ベンチ種別

**一時**。E130と同じ。

## 完了条件

bin全page verify、RAM marker、B803再出現がすべて成立したら完了。

## 結果

**完了条件を2回満たした。** UIAP Arduino core 1.0.42が生成した5,220 byteのbinを64 byte境界へ5,248 byte（82 page）にpadし、`0x08000000`からSWIOだけでerase/program/read-backした。両runで全82 pageが一致した。

user modeへsoftware resetした後、ELFから取得した`e131_marker=0x200000f8`をSWIOで読み、`setup()`が書く`0xe131b007`を確認した。最後にRAM payloadを実行して製品bootloaderへ切り替え、`1209:b803`が再出現した。書込み開始から再出現までは10.664秒だった。

- ESP32 GPIO16 → V003 PD1/SWIOの1本だけを使用
- GPIO23→RSTはHi-Zのまま、外部reset不使用
- BOOT領域は読書きせず、製品bootloaderを保存
- 各page要求はCRC32で保護し、probe内で書込み後read-backを実施
- SWIO/DMIの一時的な誤読はaddress/value照合と再試行で回復
- RAM payloadも全wordをread-backし、実際に検出した1-bit誤りを実行前に再送
- pageのattach/erase/program/verify失敗は同じpage全体を上限付きで再実行

成功run: [`E131_20260918T081705Z_default`](../_runs/E131_20260918T081705Z_default/)（10.664秒）、[`E131_20260918T082553Z_default`](../_runs/E131_20260918T082553Z_default/)（11.169秒）。途中runでは、消失済みUSB deviceをusbipが`Attached`のまま保持する観測上の偽陽性と、RAM/page照合失敗を得て、上記の防御を追加した。

## 帰結

ArduinoコアのV003 uploaderに必要な最小経路は、外部reset線なしで実証できた。必要なのはSWIO attach、DMI read/write、halt、RAMへのpayload注入、DPC設定・resume、64 byte単位のflash erase/program/verify、user/boot modeのsoftware resetである。実装引継ぎは[`../../references/arduino-core-v003-swio-handoff.ja.md`](../../references/arduino-core-v003-swio-handoff.ja.md)に分離した。
