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

## 2026-09-22 追記（oep-probe-arduino v0 stack への移植時）

「loader の `a0=0` を成功条件にする」は誤りだった。`dcsr.ebreakm` を立てずに resume すると loader 末尾の `ebreak` は debug mode に
入らず例外として mtvec（未設定なら 0 = reset vector）へ飛び、application が再起動して RAM（loader と入力 buffer）を上書きする。
その後の attach/halt で読んだ a0 は application のものだった。page 自体は ebreak 前に書き終わっているので read-back は一致し、
「完了 poll を取りこぼす」「fresh attach を fence にする」という観察もこれで説明できる。

`dcsr.ebreakm`（CSR 0x7b0 bit15）を立ててから resume すると loader は ebreak（loader 先頭 + 0x15c）で確実に halt し、a0 は 0x10
（処理した word 数）を返す。完了判定は「dpc == loader の ebreak 番地」と read-back で行う。この形で 216 page を 4.26 s
（約 20 ms/page、loader は halt 中常駐させ再注入しない）、CRC 一致、reset 後に sketch の banner を確認した。
