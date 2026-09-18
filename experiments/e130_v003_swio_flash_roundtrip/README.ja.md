# E130 V003 user flashをSWIOで往復書込みする

状態: **完了 — pattern書込み・復元・boot成功**（2026-09-18）

## 問い

ESP32 GPIO16→PD1/SWIOだけで、CH32V003 user flashの64 byte pageをerase/program/read-backでき、元データへ完全復元した後もUIAP bootloaderを起動できるか。

## 仮説

E129拡張firmwareのFLASH key/MODE key解錠と64 byte fast-program手順が成立すれば、ArduinoコアがV003対応に必要とする最小の書込み経路（read・erase・program・verify・boot entry）が一続きで成立する。

## 反証条件

退避read、既知pattern write/read-back、元データrestore/read-backのいずれかが一致しない、または復元後に`1209:b803`を起動できない。

## 方法

1. SWIO software resetでuser app状態へ正規化する。
2. user flash末尾64 byte（`0x08003FC0`）をreadしてRAM上へ退避する。
3. 決定的な64 byte patternをpage erase/programし、全byte一致を確認する。
4. `finally`で元の64 byteを必ず再programし、全byte一致を確認する。
5. E129のSWIO-only boot手順で`1209:b803`を起動する。

BOOT領域には書かない。user flash末尾は一時的に上書きするが、同一run内で元データへ復元する。

## ベンチ種別

**一時**。E129と同じ。GPIO23はHi-Zのまま。

## 完了条件

pattern/restore/bootの全検証成功、またはいずれかの失敗を記録して復元を試行したら完了。

## 結果

成功。

- 退避値: `0x08003FC0`から64 byteすべて`FF`
- 既知pattern: 64/64 byte read-back一致
- 復元: 64/64 byte read-back一致（すべて`FF`）
- 復元後のSWIO-only boot: `1209:b803`が**1.494秒**で出現（BUSID `13-3`、`Shared`）
- pytest: 1 passed、16.89秒
- run: `_runs/E130_20260918T081251Z_default/`

最初の試行ではUART試験packetの1 byteが`FA→F9`へ化けた。probeは受信値を正しくflashへ書いて内部verifyしていたため、SWIO/flashの誤りではない。書込みpacketを`address(4)+data(64)+CRC32(4)`へ変更し、CRC一致後だけerase/programすることで解消した。

また、BOOT状態の正規化は単純resetでは状態依存だったため、FLASH/MODE/BOOT_MODE keyを解錠し、BOOT_MODE bit14を明示的にclearしてからCPU software resetする37-word RAM payloadへ変更した。

## 結論

V003対応の最小経路として、SWIOだけで次が実機成立した。

1. RAM payload実行によるuser/boot切替
2. 64 byte page erase
3. 64 byte fast program
4. read-back verify
5. transport CRCによる書込前の破損拒否

BOOT領域とGPIO23外部resetは使用していない。
