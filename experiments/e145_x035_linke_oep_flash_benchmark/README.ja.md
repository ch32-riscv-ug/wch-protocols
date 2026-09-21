# E145 X035: WCH-LinkE を書込み性能の比較基準にする

状態: **完了（初回 baseline）**（2026-09-21）

## 目的

ESP32-P4 暫定 OEP probe による CH32X035/RVSWD の flash 操作について、純正
WCH-LinkE の実測値と比較できる基準を作る。これは「LinkE より速くする」ことを
合格条件にはしない。OEP の遅い箇所を数値化し、安定性を損なわない改善の優先順位を
決めるための baseline である。

## 共通条件

| 項目 | 値 |
|---|---|
| target | CH32X035C8T6 |
| flash 範囲 | `0x08000000+63488`（62 KiB） |
| flash image SHA-256 | `a257cfac557ee3d3fa7314e7fa5b480ee034da481de82c4d028353f79b6f29fd` |
| 標準 probe | WCH-LinkE `FC928F068181`、firmware 2.22 |
| 標準 host tool | `ch32rv 0.8.0` |
| OEP probe | ESP32-P4 revision 1.3、MAC `30:ed:a0:e3:11:08` |
| OEP host tool | `oep-client-python`（TargetMemory verify） |

全 LinkE 操作は `--probe serial:FC928F068181 --chip CH32X035C8T6` を明示した。
UART の `/dev/ttyUSB*` / `/dev/ttyACM*` を選択子として使っていない。

## 既存の線上 capture

[`wire-flash-v003-x035-2026-09-11`](../../captures/fixtures/wire-flash-v003-x035-2026-09-11/README.ja.md)
は、同一 LinkE serial と X035 に既知の 4 KiB pattern を flash/verify した LA2016
50 MHz capture である。payload が線上を占める時間から、次を再計算できる。

| phase | payload | 線上時間 | 実効 payload rate |
|---|---:|---:|---:|
| write | 4 KiB | 102.041 ms | 39.20 KiB/s |
| fast readback | 4 KiB | 70.431 ms | 56.79 KiB/s |

この値は RVSWD の payload 部だけであり、attach、flash erase、USB、reset は含まない。
したがって下記の end-to-end wall-clock と混同しない。

## 2026-09-21 実機測定

すべて同じ 62 KiB raw image を対象にした。`flash` は `--verify readback --reset run
--confirm-run`、`verify` は readback comparison のみである。`--capture` の単調時刻と
`/usr/bin/time` の wall-clock を併記し、capture を取得した試行を採用した。

| 操作 | 試行 wall-clock (s) | capture end (s) | 結果 |
|---|---|---|---|
| LinkE full read | 1.23, 1.24, 1.24 | 1.235123, 1.243313, 1.239039 | 3/3 hash 一致 |
| LinkE full verify | 1.22, 1.22, 1.21 | 1.218494, 1.218634, 1.217956 | 3/3 一致 |
| LinkE full flash + readback verify + reset | 3.80, 3.81, 3.00 | 3.801177（試行 1） | 3/3 成功、target running |
| OEP full verify（旧 backend） | 25.071144 | — | image 一致、reset 0.004499 s |
| OEP full verify（session reuse + guard 0 µs） | 20.972145, 21.005331, 21.043748 | — | 3/3 image 一致、reset 成功 |
| OEP full verify（abstract autoexec sequential read） | 4.896290, 4.958516, 4.960726 | — | 3/3 image 一致、reset 成功 |

LinkE full verify の capture 平均は **1.218361 s**、全域 63,488 byte に対して
**50.87 KiB/s** である。OEP の最新 full verify は同じ範囲・同じ SHA-256 で平均
**4.938511 s**、**12.55 KiB/s** である。したがって現時点の OEP verify は LinkE の
**4.05 倍遅い**（LinkE 比 24.67 %）である。旧 backend の 25.071144 s からは **80.30 %**
短縮した。

LinkE flash 全体の 3 回目だけ 3.00 s と短く、最初の二回は 3.80/3.81 s だった。
この差を有利な最小値で代表させない。次回以降は最低 5 回、中央値・p95・失敗率を
記録する。LinkE full read の初回非-capture 試行にも 0.37 s の外れ値があったが、
capture 3 回では 1.218–1.243 s に収束したため、baseline には capture 値だけを採用した。

## 解釈と次の最適化

今回、88-byte read / 64-byte program request 間で target を halt した RVSWD session を再利用し、
X035 fixture だけは post-frame guard を 20 µs から 0 µs にした。さらに、既存の
`rv003usb` 実装で検証済みの `DMABSTRACTAUTO` / `DMDATA1` autoincrement sequence を使い、
program buffer・register・address の再設定を word ごとから連続範囲ごとへ削減した。後者は
上記 full-image readback 3 回で検証済みである。flash 終端を越える最後の speculative read は
結果を待たず、次のscalar flash操作前にcmderrをclearする。別配線では backend の保守的な 20 µs
default を使い、同じ検証を通すまで 0 µsを選ばない。

最新backendで同じimageを `--program-image --destructive` に渡した無変更経路も確認した。
差分比較 **5.110922 s**、`pages=0`、全域verify **5.104253 s**、両reset成功である。これは
書込みpageを実行していないため、flash erase/program性能の比較値ではない。256-byte物理page単位の
streaming programと、変更pageを含む反復試験が次の測定対象である。

TargetFlash revision 2 は、64-byte fragmentを4本probe RAMへstageしてから`commit-page256`を
発行する。commitは全fragmentが揃わなければ拒否し、stageだけはtarget flashを変えない。同一の
先頭256-byte pageでstage/commitを実機実行し、reset後の62 KiB hashが一致した。これは一括erase/
program経路の安全確認であって、変更pageの性能値ではない。

2026-09-21に既存PWM probe image（924-byte artifact、padded full-image SHA-256
`b6f5b99dab244417aee37c7cdc4459f3a7158ce55af63ba22bea9cb7bf1f93c4`）へ実機更新した。
現imageとの差分は27 physical pageであり、resultは`pages=27, attempts=27`だった。差分比較から
27回のstage/commitまで **9.685211 s**、reset **0.004584 s**、続く全62 KiB verify
**5.119156 s**、最後のreset **0.003012 s**で、hash一致した。従来のlogical 64-byte APIなら
同じ27 pageに最大108回のerase/programを要するところを、実測では27回へ削減できた。
ただし旧APIとの同一image・同一backend比較はまだ採っていないため、ここから削減率や速度倍率を
主張しない。

同一session内の回復も確認した。`OEP_X035_INJECT_FLASH_FAILURE=1`でphysical erase直後に
一度だけ失敗を注入すると、最初のcommitは診断`0xe1`で失敗し、独立readの先頭88 byteは全FFだった。
P4をresetせず同じstaged pageをretry commitすると成功し、target reset後の全62 KiB hashは元PWM
imageと一致した。recovery cacheはerase前imageを保持し、staged希望像で上書きしないことを実機で
確認した。この結論は同一probe sessionに限る。probe reset/電源断後のrecoveryは未解決である。

最初の64-byte program後の部分状態も同様に確認した。`OEP_X035_INJECT_FLASH_FAILURE=2`で最初の
commitは`0xe2`により失敗し、独立readでは先頭64 byteだけが希望値、続く24 byteは全FFだった。
同じstaged pageのretry commitは成功し、target reset後の全62 KiB hashは元PWM imageと一致した。

通常firmwareでのfull verify soakも実施した。同じPWM imageを20回連続verifyし、全回hash一致・
reset成功だった。verifyは平均 **5.166509 s**、中央値 **5.164288 s**、p95 **5.226030 s**、
最大 **5.249059 s**。resetは平均 **3.376 ms**、p95 **3.868 ms**、最大 **4.298 ms**。これは
read/verify gateだけの結果であり、差分program 20回とhost/client中断試験は別途必要である。

差分program soakも実施した。PWM probe imageとI2C probe imageを交互にし、各回27 physical pageを
stage/commitしてfull verifyした20回は、全回`pages=27, attempts=27`、retry 0、hash一致、両reset
成功だった。programは平均 **11.888387 s**、中央値 **11.886405 s**、p95 **11.925864 s**、最大
**11.949021 s**。続くfull verifyは平均 **5.161236 s**、中央値 **5.158991 s**、p95
**5.243148 s**、最大 **5.250146 s**。最終target imageはPWM probe imageである。

差の大半は P4 USB や flash の物理速度そのものとはまだ断定しない。OEP 暫定 backend は
software RVSWD の短い transaction を順に往復し、host request、DMI、flash page 処理、
readback を細かく同期させる構造である。一方 LinkE は線上 fast-read burst と firmware 内の
flash loader を使う。従って、まず以下を分離計測してから変更する。

1. attach/halt、1 page erase/program、read それぞれの probe 内時間。
2. OEP message 往復、P4 USB、RVSWD frame、target flash wait の内訳。
3. 物理 256-byte page 単位の streaming program/read API と、完全 page image を渡す経路。
4. 失敗時の page journal / 再送保証を保ったまま、連続 read と連続 program を batch 化する。

この benchmark はその P0 作業の回帰基準であり、書込み成功・全域 verify・reset 後の起動確認を
速度改善より優先する。OEP の release 判定は LinkE 同速ではなく、閾値と反復失敗率を別途定義する。
