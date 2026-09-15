# E115 generic codecの縮約取り出しを群ごとのbit行列転置にする — 16 ch hold構成を固定profile並みに戻せるか

状態: **完了 — bench条件は達成（W16 F3 32→47.9 Msps/core、8-bit F3 55→75）、streaming 60 Mspsは未達（16 ch hold構成は50でbyte一致、55で落ちる）。残りはfast部とper-block overhead**（2026-09-15） — **参考値（独自patch版library）**（EspUsbDevice 2.3.0＋E097/E101/E102/E110の一時patch。正規libraryに取り込まれるまで製品の目安には使わず、修正依頼の根拠にのみ使う）

## 問い

[E114](../e114_p4_dynamic_descriptor/README.ja.md)のgeneric codecは、fast部（F bit gather）は固定profile並みだが、縮約channelのbucket値を**channelごとに1 bitずつ**取り出す部分が費用を決めていて（hold 1値あたり6〜8 cycle）、16 ch hold構成で32 Msps/core、固定profile（wide 72 / four 68 Msps、2 worker）の1.4〜1.8倍の費用だった。any_active / edge_latchは0.2〜0.4倍。**縮約channelを（mode, D, phase, polarity）の群にまとめて連続laneへ置き、群ごとにbucket×channelのbit行列を転置して取り出すと、generic codecは固定profile並みの費用（16 ch hold構成でbench 45 Msps/core以上、2 worker streamingで60 Msps）に戻るか。any / edgeも同じ形で固定並みに上がるか。**

## 仮説

- 固定profileが速いのは、D=64の8 channelをwordの**bit-spread演算で一度に**作るからで、channel数に比例するのは取り出し後のappendだけになっている。genericでも、群（同じmode・D・phase・polarityのchannel群、G本）の値はsample word `s[k·D+phase]`（hold）または縮約level配列 `Bucket[log2 D][k]`（any / edge）を`>> lane0`して`G` bit取れば、bucket kごとに1 wordで済む。B = 128/D個のG bit行を「channel c → B bit」へ転置すれば、出力はchannel-majorのplaneそのもの（c0のB bit、c1のB bit、…）になる。
- 転置は「行をB倍に広げて（spread: bit iをbit B·iへ、log2 Bのmask段）ORで重ねる」だけで、費用はB·log2 B程度の演算。D=64（B=2）の12 channelは約8演算、D=32（B=4）の12 channelは約16演算で、E114 v3の168 / 336 cycleから1桁下がる。D=8（B=16）はG本を2本ずつ32 bitに詰めて転置する。
- edge_latchの2 bit値（level | edge<<1）は、levelとedgeを別々に転置してbitを交互に置く（stride 2のspread）。
- deviceがlane順を群順に並べ替えるのはE114の並べ替え（rawを下位、縮約を上位）の延長で、wire plane順も群順にして返答に`order=`（plane順のdescriptor index）を返せばhostは追随できる。E105 referenceの意味は変わらない（channelの並び替えは仕様の範囲）。
- 期待値: W16（3 raw＋hold/8＋12 hold/64）はfast 640 cycle＋hold/8群（G=1, B=16）約80＋hold/64群（G=12, B=2）約20で約750 cycle/block ＝ 約48 Msps/core → 2 worker streamingで60 Msps。F4＋12 hold/32、F5＋11 hold/32も同程度。any6 / edge12は縮約passが残るので固定の0.7倍以上を目標にする。

## 反証条件

- W16 F3のbenchが45 Msps/coreに届かない → 転置の演算数の見積りが甘い、または群ごとのsample読み（B個のword load）がcache律速。取り出し以外（fast部、縮約pass）の内訳をbenchの内訳計測で分ける。
- any / edgeが0.7倍に届かない → 律速は取り出しでなく縮約pass（quantityごとのword pass＋2分木）。passの融合（OR / ANDを同時に、riseとfallを同時に）へ切り替える。
- referenceとの不一致 → 群順の並べ替えでhost側plane順がずれている。`order=`の意味を疑う。

## 方法

- firmwareはE114をforkし、`buildDynamicProfile`で縮約channelを（mode, log2 D, phase, active）でsortして連続laneに置き、群表（lane0, G, mode, D, phase, active）を作る。返答に`groups=`と`order=`を足す。
- `encodeDynamicBlock`の縮約部を群loopに替える: hold群は`s[k·D+phase] >> lane0 & maskG`をB個集めて転置、any群は`BucketOr/And[log2 D][k] >> lane0`、edge群はlevel（`s[k·D+D−1]`）とRise/Fallを別々に転置してstride 2で重ねる。B∈{1,2,4,8,16,32,64,128}のtemplate。G=16、B=128（D=1のany / edge）は転置せず従来経路に落とす。
- `reduce_model_check.py`を群順・転置に合わせて更新し、random descriptorでE105 referenceと照合してから実機へ。
- 計測: `Q`のbenchでE114 v3と同じ14 layout（W16 F3 / F4 / F5 / F6 / F8、any12、edge12、8-bit F1〜F7、配線順不同、mix、any6、edge6、素通し）を比較。streamingはW16 40 / 50 / 60、F4 40 / 60、F5 40 / 60、8-bit F3 60 / 80、any6 30 / 40、edge12 15 / 20、mix8 10 / 20をbyte一致で、`codec_limit`の係数（bench比）を較正し直す。hostはE114の「callback内で処理しない」版。GPIO 10〜17はpull-down。
- 5分soak: W16 60（通れば）とF4 60をgenericで。

## 対象外

D>128、host gateway（Phase B）、外部16 GPIOの電気試験、loopback源の8-bit 58〜60 Msps問題（Gray進行checkで代替）。

## 必要な環境 / ベンチ種別

第三P4（esp32-p4-80f1b2d0b261）、PC直結 usbipd/WSL、`/tmp/EspUsbDevice-e110`。一時。

## 記録する数値 / 完了条件

layoutごとのbench（E114 v3との比）、streaming上限とbyte一致、codec_limit係数、W16 / F4 60 Mspsのsoak。**W16 F3のbench ≥ 45 Msps/coreかつ2 worker streaming 60 Msps byte一致で完了**。any / edgeは記録のみ（0.7倍未満なら反証条件の切り分けを追記）。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.3 / §2 / Phase D、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)の追記（genericの目安）、[E114](../e114_p4_dynamic_descriptor/README.ja.md) §8。

## 結果（2026-09-15。ログ `_runs/_runs/E115_20260915T164809JST_p4_direct/sweep.log`）

firmware `e115_p4_grouped_plane_transpose.ino`（E114 fork）。deviceは縮約channelを（mode, log2 D, phase, active）で安定sortして連続laneに置き、返答に`groups=`と`order=`（laneごとのdescriptor index）を足した。hostは`order`でplane順を追随する。`reduce_model_check.py`（群順＋転置の鏡像）はE105 referenceとrandom descriptor 1,500本で全一致。

### 1. bench（単core、Msps。E114 v3 → E115。E115は3版: v1＝転置のみ、v2＝1 channel群はE114経路＋fold gather、最終＝群関数をalways_inline）

| layout | E114 v3 | E115 v1 | v2 | **最終** | 倍率 |
|---|---:|---:|---:|---:|---:|
| 16-bit 3 raw＋hold/8＋12 hold/64（W16） | 32.1 | 43.8 | 42.6 | **47.9** | 1.49 |
| 16-bit 4 raw＋12 hold/32（F4） | 30.3 | 47.7 | 48.8 | **49.8** | 1.64 |
| 16-bit 5 raw＋11 hold/32（F5） | 31.6 | 46.7 | 46.5 | **47.4** | 1.50 |
| 16-bit 6 raw＋10 hold/64 | 37.9 | 53.9 | 53.5 | **54.6** | 1.44 |
| 16-bit 8 raw＋8 hold/64 | 36.6 | 47.5 | 50.5 | **51.2** | 1.40 |
| 16-bit 3 raw＋hold/8＋12 any/32/low | 17.1 | 20.7 | 20.4 | **21.8** | 1.27 |
| 16-bit 3 raw＋hold/8＋12 edge/32/high | 11.4 | 14.0 | 14.0 | **14.3** | 1.25 |
| 8-bit 1 raw＋7 hold/64 | 52.5 | 69.6 | 81.7 | **85.8** | 1.63 |
| 8-bit 3 raw＋5 hold/64 | 54.6 | 63.3 | 71.8 | **75.0** | 1.37 |
| 8-bit 5 raw＋3 hold/64 | 54.2 | 56.3 | 55.9 | **56.8** | 1.05 |
| 8-bit 7 raw＋1 hold/64 | 55.5 | 52.1 | 50.7 | **55.3** | 1.00 |
| 8-bit 配線順不同（5群、各1 channel） | 42.7 | 32.2 | 32.8 | **46.7** | 1.09 |
| 8-bit mix（any×2、edge×2、hold/4、hold/128） | 9.4 | 8.5 | 8.6 | **9.5** | 1.01 |
| 8-bit 2 raw＋6 any/8/low | 21.3 | 24.7 | 26.4 | **27.6** | 1.30 |
| 8-bit 2 raw＋6 edge/16/high | 17.8 | 20.5 | 21.7 | **21.8** | 1.22 |

内訳の切り分け（8-bit F=3）: fast部だけ（3 raw＋hold/128）72.6 Msps＝634 cycle/block。5本を1群で転置すると73.2（縮約の費用≈0）、5本を別phaseで1 channelずつ（E114経路）だと39.0。v1/v2で配線順不同が下がったのは、この1 channel経路が別関数になって呼び出し1回あたり約100 cycle（レジスタ退避と参照渡し）掛かっていたため。always_inlineで46.7に戻り、E114より上になった。

### 2. streaming（2 worker、`--no-codec-limit`、byte一致、hostは検証をcapture後に回す版）

| layout（bench） | 通った | 落ちた | idle（通った上限） |
|---|---|---|---|
| W16（47.9） | 40、**50**（171 Mbps） | 55、60 | 50で0.2% / 0.25% |
| F4（49.8） | 40、**50**（225 Mbps） | 55、60 | 50で0.2% / 0.4% |
| F5（47.4） | 40（221 Mbps） | 50、60 | |
| 8-bit 3 raw＋5 hold/64（75.0） | 60、70、**80**（261 Mbps） | | 80で0.4% / 1% |
| 配線順不同（46.7） | 40、**50**（166 Mbps） | | 50で10% / 18% |
| any6（27.6） | 30、**40**（114 Mbps） | 50 | 40で0.6% / 3% |
| edge12（14.3） | 15、**20**（80 Mbps） | 25、30 | 20で3% / 9% |
| mix8（9.5） | 10、**15**（42 Mbps） | 20 | 15で8% / 12% |

streaming上限はhold系でbenchの1.0〜1.15倍、any / edgeで1.4〜1.6倍。取り出しが安くなったぶん、streaming固有の費用（Gray check、DMA ringからの読み、stage書き出し）の比率が上がり、E114の1.25〜1.56倍から下がった。`codec_limit`の係数は**1.0×0.9**にした（W16 42、F4 44、8-bit F3 67、配線順不同42、any6 24、edge12 12、mix 8 Msps）。

### 3. 判定

- **bench条件（W16 F3 ≥ 45 Msps/core）は達成**（47.9）。仮説どおり、縮約の取り出しは群ごとの転置で費用がほぼ消えた（1群5本で≈0）。
- **streaming条件（16 ch hold構成で60 Msps）は未達**。50でbyte一致、55で落ちる。固定wide 72 Mspsに対して0.7倍（E114は0.55倍）。
- 残りの費用はfast部（F=3 16-bitで約5 cycle/sample＝約640 cycle/block）とper-block overhead。fold gatherは8-bit F≤4で効いた（F1 69.6→81.7、F3 63.3→71.8）が、16-bitでは4 shift版と差がない。固定profileのfast部がどこまで速いかは同じbench harnessで測っていない（`benchmarkCodec()`はlegacy 64-sample blockで95 Msps/core相当）。次はfast部の内訳（disassembly、8 sample→F byteのemitのstore回数）と、streaming固有費用（check() 7〜8 point、DMA ring読み）の分離。
- any / edgeは1.2〜1.3倍。律速は取り出しでなく縮約pass（量ごとのword pass）に移った（反証条件2の切り分けどおり）。passの融合が次の手。
- referenceとの不一致は出なかった（群順の`order=`でhostが追随）。**注意**: loopback源はW16 45、配線順不同60、F4 50（v2）で「開始位相が見つからない」ことがあり、同じrateで別runは通る。8-bit 58〜60でE114が見たのと同じ源側の限界で、run単位のflake。

### 4. 次

- fast部の内訳計測とemitの見直し（E116候補）。固定wide / eightのfast部を同じharnessで測って差を数字にする。
- 縮約passの融合（OR＋AND、rise＋fallを1 passで）。
- loopback源の位相flake: 外部clock同期の源、または検証をGray進行checkに切り替える。
