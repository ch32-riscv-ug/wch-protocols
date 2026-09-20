# E135 V003 RAM flash loaderによるOEP書き込み

状態: **完了（2026-09-20）**

## 問い

E134で実測したLinkEと同じ処理単位、すなわちターゲットRAM上のloaderに消去・書込み・照合を
実行させることで、ESP32 software SWIOからリセット後も残るフラッシュ書込みができるか。

## 実装

`oep-probe-arduino`のV003 backendへ次を試作した。

- ch32-rs/wlink由来のRV32EC V003 loader（498 byte）を`0x20000000`へ配置
- 64 byteの入力を`0x20000200`へ配置
- `a0=0x1d`（unlock/erase/program/verify）、`a1=flash address`、`a2=64`
- `sp=0x20000800`、`dpc=0x20000000`を設定してresume
- DMSTATUS poll後、取りこぼした場合もfresh attachを完了fenceにする
- loaderの`a0=0`と、fresh attach後の64 byte read-backの両方を成功条件にする

PHY係数はE133とE134の比較から8とした。実測262.5/862.5 nsで、LinkEの240/860 nsに最も近い。
DMI frame後の8 us待ちは維持した。LinkE実測の通常frame間隔中央値は6.70 us、DMSTATUS pollは
9.20 usであり、同じ桁にある。

## 実機結果

対象はUIAPduino Pro Micro CH32V003、ESP32 GPIO16からPD1/SWIOへの既存配線である。GPIO23の
外部RESETは使っていない。

最初の係数10試験ではAPI上は完了poll timeoutだったが、独立readでは64 byte全体が書けていた。
完了fence追加後はsuccessになった。係数10ではsoftware reset後のread値に単発bit化けがあったが、
係数8では同じ内容の連続read 5/5が一致した。

続いて異なる5 patternをflash末尾page `0x08003fc0`へ順に書いた。各回について
`program-page64`がsuccessとなり、`normalize-user`によるsoftware reset後、32 byteずつ2 requestで
取得した64 byteが期待値と完全一致した。直前の単独patternも含め、loader方式は6回連続で成功した。

## 結論

以前のhost-driven flash controller逐次操作で起きた部分書込みとreset後不一致は、この試験では
再現しなかった。V003 prototypeの書込み経路はRAM loader方式を採用し、TargetFlashを再公開する。
ただし電源再投入、全image、複数個体は未試験であり、製品品質を示す結果ではない。
