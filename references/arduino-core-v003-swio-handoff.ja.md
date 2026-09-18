# Arduinoコア向け CH32V003 SWIO uploader 引継ぎ

状態: **実機でend-to-end成立した最小実装の引継ぎ**（2026-09-18）

## 1. 成立した経路

classic ESP32のGPIO16をUIAPduino Pro Micro CH32V003 V1.4のPD1/SWIOへ接続した1本だけで、Arduino sketchのbuild成果物をuser flashへ書き、実行し、製品bootloaderへ戻せた。

検証は[E123〜E131](../experiments/LEDGER.ja.md)。最終の[E131](../experiments/e131_v003_arduino_image_swio/README.ja.md)ではUIAP core 1.0.42の5,220 byte binを82個の64 byte pageとして全page照合し、`setup()`の実行をRAM markerで確認した。GPIO23/RSTとBOOT領域は使っていない。

## 2. uploaderが必要とするprimitive

1. SWIOのdebug entryとDMI read/write
2. hartのhalt
3. DMI abstract commandによる32-bit memory read/write
4. target RAMへの小さなpayload配置
5. DPCをRAM payloadへ設定してresume
6. V003 user flashの64 byte page erase/program/read-back
7. BOOT modeをuserへ明示してsoftware reset
8. BOOT mode、PD4を設定してsoftware resetし、製品bootloaderへ移行

参照実装はE129のArduino sketchに集約してあり、E130が64 byte flash round trip、E131がbin全体のhost orchestrationを追加している。

## 3. 実装上外せない防御

- host→probeのpage要求はaddress 4 byte + data 64 byte + CRC32 4 byteとし、CRC不一致なら**erase前に拒否**する。
- DMI memory操作は返ったaddressとvalueを照合し、不一致・busy・失敗を再試行する。実機試験でも一時的な誤読が観測され、再試行で回復した。
- page program後はtarget flashを全64 byte read-backして一致を確認する。不一致なら同じpageのerase/program/verify全体を上限付きで再実行する。
- user起動前に`BOOT_MODE`を明示的にclearする。単なるresetだけではboot状態が残る場合がある。
- BOOT領域には書かない。user imageの上限はboard/core定義に従い、範囲外のpage要求をprobe側でも拒否する。
- 外部reset線は必須にしない。resetはtarget CPUがRAM payloadから`PFIC_CFGR=0xBEEF0080`を書いて行う。

## 4. 受入条件

V003対応は少なくとも次を自動試験で満たす。

1. Arduino coreでfixtureをbuildできる。
2. bin全pageのerase/program/read-backが一致する。
3. software reset後にfixtureの`setup()`が実行されたことをRAM marker等で確認できる。
4. 同じSWIO線だけで製品bootloaderへ移行し、`1209:b803`を再認識できる。
5. CRC破損要求をflash erase前に拒否できる。
6. 一時的なDMI誤読を検出し、上限付き再試行または明示的失敗にできる。

## 5. まだ一般化していないもの

この結果が確定するのはV003と上記配線・動作点である。SWIO pulse幅の許容限界、pull-up値、温度・個体差、他の1-wire targetは未検証。Arduinoコアへ入れる際はV003 backendとして始め、family共通対応とは分ける。
