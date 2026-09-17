# E119 generic codecの残りを潰す — fast部のword化、hold/8単独channel、PARLIO割り込みのcore配置（2.4.0 pin）

状態: **完了 — 4手とも実測で否定。E118の状態がスカラーコードの最適点**（fast部のword化は非整列storeが17 cycleで悪化、整列paddingでも悪化、単独channelの転置経路も悪化、割り込みのcore移動は偏りが反転するだけ）（2026-09-16）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E118](../e118_p4_generic_fast_path/README.ja.md)（worker周辺を削って16 ch hold 60 Msps、上限65。encodeは980 cycle/block: fast部690、hold/8単独channel約200、hold/64 12本の群約80）

## 問い

E118で残ったencode本体の費用（W16で980 cycle/block）のうち、**fast部（F bit gather＋byte emit、690）**と**hold/8単独channelの取り出し（200）**を削り、加えて**PARLIO RX割り込みをcore 1へ移してcore 0の負荷（usbd＋usbTask＋ISR）を分散**すると、16 ch hold構成のstreaming上限（65 Msps）と各layoutの`codec_limit`はどこまで上がるか。fast部はF=3 16-bitで8 sampleあたり約40 cycleで、byte storeとnibble分割が半分を占めると見ている。

## 仮説

- fast部を「32 sample→F word」に組み替えると（8個の4F bit gatherを定数shiftでF個のwordに詰め、wordで書く）、8 sampleあたり約25 cycle（≈3 cycle/sample）になり、W16のencodeは980→約750 cycle/block、benchは47→約60 Msps/core。上限65→75 Msps級。前提はP4の非整列word store（block先頭はW=53で非整列）がハードウェアで処理されること。起動時の自己計測（`E119_SELFTEST`）で、非整列swのcycleが整列の数倍以内なら成立、trap（数百cycle）なら`E119_UNALIGNED_WORD_STORE=0`でaligned local＋memcpyに切り替える。
- hold/8（B=16）の1 channel群はE114経路で16値×約12 cycle。laneのshiftを掛けてから`spread`で置く群経路（G=1）にしても同程度なので、ここは16-bit sampleを2本ずつwordで読み（2 bucketぶんが1 wordに入るのはD=1だけなので効かない）…効く手が薄い。**D≥8の1 channel群は、16 loadを先に集めてから並列にbit抽出する形**（load latencyの重なり）で約100に下げる。
- PARLIO RX割り込みをcore 1に置くと、core 0（usbd＋usbTask＋worker 0）の割り込み込みencode（1,400 cycle）が下がり、W16 60でのidle 3% / 11%が均される。上限＋3〜5 Msps。

## 反証条件

- 非整列swがtrapされる（自己計測で数百cycle） → aligned local＋memcpy版で測り、改善幅を記録。
- fast部のbenchが上がらない → gatherの演算数が主因でなくload / storeの帯域。その場合は32 sample群のloadを先行させる（software pipelining）。
- ISRのcore移動で全体が悪化 → core 1のworkerが割り込みを受けて同じだけ遅くなるだけで、和は変わらない。元に戻す。
- byte不一致 → wordの詰め方（little-endian、4F·kのoffset）の取り違え。host `Reference`（sample-major）が検査する。

## 方法

- firmware: E118をfork。(1) `encodeDynamicFast`を32 sample→F word版に（`dynPack8<F>`、`dynStoreWords<F>`）。(2) `E119_SELFTEST`（整列／非整列swのcycle）。(3) hold/8等の1 channel群の取り出し改良。(4) `E119_RX_ISR_CORE`（PARLIO RX unitをcore 1のtaskから作って割り込みをcore 1へ）。各段で`Q` benchとstreaming。
- 掃引: bench（E118と同じ14 layout＋fast-only F=1〜8）、streaming（製品モード）: W16 60 / 65 / 70 / 75、F4 60 / 70、F5 60 / 70、8-bit F3 100 / 108、any6 33 / 40、edge12 17 / 20、配線順不同56 / 60。byte一致は分周が正確なrate（60 / 80 / 100 / 40）で、他は欠損0＋idleで見る（E118 §5）。25 s soak: W16の新上限−5。`codec_limit`係数の再較正。
- host: E118の`host_descriptor.py`。着手前にEspUsbDevice側sessionへ板の使用を宣言。

## 対象外

any / edgeの縮約pass融合（効果はmix系layoutのみ。時間があれば追記）、固定profileの変更、USB経路。

## 必要な環境 / ベンチ種別

第三P4、PC直結usbipd/WSL、`EspUsbDevice (2.4.0)`。一時。

## 記録する数値 / 完了条件

自己計測の結果、fast部bench（F別、旧→新）、W16のencode cycle/block、streaming上限と`codec_limit`、byte一致、soak。**W16のstreaming上限が65→70以上、かつ新上限−5での25 s soak欠損0**で完了。届かなければ内訳と理由を記録して完了。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.3 / Phase D、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)、[E118](../e118_p4_generic_fast_path/README.ja.md)。

## 結果（2026-09-16、ログ `_runs/_runs/E119_20260916T075917JST_p4_direct_pin240/sweep.log`、2.4.0 pin）

| 手 | 実測 | 判定 |
|---|---|---|
| **自己計測**: 整列sw 対 非整列sw | `E119_SELFTEST aligned_sw_cycles=3 misaligned_sw_cycles=17` | 非整列word storeはハードウェアで処理されるが**約6倍**遅い |
| (1) fast部を「32 sample→F word」（非整列word store） | fast-only 16-bit F1 / F2 / F3 / F4 / F8: 73.1 / 83.8 / 66.6 / 65.4 / 53.7 → **59.9 / 60.2 / 58.5 / 47.5 / 44.0**、8-bit F3 84.1→67.8、W16 47.0→**43.4**、F4 49.6→38.6 | **悪化**。byte store 12回＋shiftより、非整列word 3回（51 cycle）＋詰めが高い |
| (1') 同上をwire blockの4 byte padding（flag `0x08`、W 53→56）で**整列**word storeに | W16 47.4→**39.1**、F4 49.3→38.2、8-bit F3 73.1→**54.1**、配線順不同46.6→37.1。streamingもW16 60で落ちる | **さらに悪化**。整列してもword詰め（8個のgatherを配列に置いて定数shiftで詰める）が高く、配列がレジスタに乗らない。byte emit（gatherの結果をそのまま数個のshiftとsbで書く）のほうが安い |
| (2) hold/8などB≥8の1 channel群を転置経路（G=1、定数offsetの展開）へ | W16 47.0→43.9、配線順不同46.1→**38.6**、3 raw＋hold/8単独は58.7→59.8 | **悪化**（E115で「1 channel群はE114経路」とした判断が正しい）。展開よりappend32とmask計算の固定費が勝つ |
| (3) PARLIO RX割り込みをcore 1へ | W16 60: idle 3% / 11% → **4.3% / 0.3%**、encode core 0 1,609→1,158、core 1 1,110→1,431、chunks 5,783 / 3,855。65は従来どおり落ちる | **効果なし**。偏りが反転するだけで総量は同じ（total idleはやや減）。core 0に戻す |

E118の状態（fast部はbyte emit、1 channel群はE114経路、割り込みはcore 0）が**スカラーコードとしての最適点**。fast部のF=3 16-bitは約5 cycle/sample（gather 4回のmasked shift＋OR、emit 3 byte）で、これ以上はP4のSIMD拡張（PIE、`xesppie`）でbit gatherをベクトル化するか、上限65 Mspsを受け入れるかになる。PIEはこの実験の対象外（inline asmとdocsの読み込みが要る。候補として残す）。

## 判定

- 仮説（fast部のword化で8 sampleあたり25 cycle）: **反証**。非整列storeは17 cycle、整列にしても詰めの費用でbyte emitに負ける。
- 仮説（1 channel群の展開で約100）: **反証**。
- 仮説（割り込み移動で＋3〜5）: **反証**。総量不変。
- 完了条件（上限65→70以上）は**未達**。理由は上表のとおり記録し、E118を製品firmwareの基点とする。

## 次の候補

- **PIE（ESP32-P4のSIMD拡張）でfast部のbit gatherを書く**（LEDGER候補 `p4-pie-bit-gather`）。8 sample×16 bitの128 bit loadから、laneごとのbit抽出をvector shift / andで一度に作れれば、fast部690→200 cycle級の余地がある。
- any / edgeの縮約pass融合（OR＋AND、rise＋fallを1 passで）。mix系layoutだけに効く。

## 追記（2026-09-17）: buildの出所を実行記録から証明できない

持ち主の方針（2026-09-17）: **`dir:`（working tree）で取った数値は再現性がないので使えない。正式値は、リリース版をpinし、その版が実際にリンクされたことを証明できるものだけ。**

この実験は`sketch.yaml`で`EspUsbDevice (2.4.0)`をpinしているが、**実行記録（`_runs/`）にライブラリ版が残っていない**。当時はhost側のログしか保存していなかった。加えて、EspUsbDevice側sessionが2026-09-17に**`build/<profile>/libraries.cache`が`sketch.yaml`のpin変更に追従せず`--clean`でも消えない**事例を実測している（別版をpinしたbuildが前の版をリンクした）。**pinを書いただけでは、意図した版がリンクされたとは限らない。**

したがって**この実験の数値は、出所を証明できない値として扱う。** [E120](../e120_p4_usb_baseline_250/README.ja.md)以降は、生成ELFの`strings`で実リンク版を実行記録に残す（[実測の規則](../README.ja.md)）。

**USB天井については[E120](../e120_p4_usb_baseline_250/README.ja.md)（2.5.0 pin、ELF証明）で取り直し済みで、device側の数値はE116と小数点以下まで一致した。** stream側（E117 / E118）の取り直しは未了である。
**訂正（2026-09-17）**: 上の注記は行き過ぎだった。**却下の対象は、当方のpatchを当てて測った数値（[E108](../e108_p4_zero_copy_stream/README.ja.md)〜[E115](../e115_p4_grouped_plane_transpose/README.ja.md)）である。素のリリース版へのpinは問題ない。** この実験はpin 1本で組んでおり、**正式値として有効**である。

残る弱点は証拠の強さだけで、実行記録にライブラリ版を残していないので成果物からリンク版を示せない。**製品の目安は現行リリースで取り直した[E120](../e120_p4_usb_baseline_250/README.ja.md) / [E121](../e121_p4_stream_baseline_250/README.ja.md)（2.5.0 pin＋ELF証明）を一次の裏付けにする。**
