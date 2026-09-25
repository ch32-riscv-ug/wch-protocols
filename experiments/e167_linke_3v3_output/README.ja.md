# E167 target をつながない WCH-LinkE の 3V3 出力と RST(power 命令と特殊消去)

状態: **完了(2026-09-25)**

## 問い

E166 では、`probe power 3v3 off` を送っても、LinkE から給電された X035 は動き続けた。一方、特殊消去では target に reset がかかっていた(E165)。

- LinkE の 3V3 出力そのものは、これらの命令で本当に切れるのか。
- 特殊消去の中で、LinkE は何をしているのか。

target をつながず、UART や pull-up からの回り込み(back-power)が無い状態で測る。

## 手順

- LinkE: WCH-LinkE fw 2.22 `497E8F06CE2E`(target なし)。
- 測定器: ESP32-P4 `esp32-series-30eda0e343c6`。E163〜E165 の capture 機に、[`p4_rail_logger`](p4_rail_logger/p4_rail_logger.ino) を焼いた(Arduino ESP32 3.3.12、`esp32:esp32:esp32p4:PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=cdc`)。
  - すべての pin を入力のまま使い、何も駆動しない。
  - ADC(`analogReadMilliVolts`、11 dB 減衰)で 3V3 を、digital で RST / SWDIO / SWCLK / RX / TX を読む。
  - 1 点あたり約 0.29 ms(約 3.5 kHz)。
- 配線(LinkE の端子 → P4 の GPIO): RST 16、3V3 17 と 21、GND 18 と 22、SWDIO 19、SWCLK 20、RX 23、TX 39。
  - P4 の端子はすべて 3.3 V まで使える。GPIO39 は SD 用の線で VO4 の pull-up につながるが、VO4 は既定で Hi-Z(ユーザーの確認)。
- [`measure.py`](measure.py): P4 に記録開始(`r<ms>`)を送り、0.3 s 後に LinkE へ生のコマンドを送る。結果は `out/<名前>.csv`(変化点と 1 ms ごとの点)と `out/<名前>.usb.json`。

## 結果

| 送ったコマンド | LinkE の応答 | 3V3(GPIO17 / 21 の平均) | RST / SWDIO / SWCLK |
|---|---|---|---|
| `81 0d 01 0a` → 1 s → `81 0d 01 09` | `82 0d 01 0a` / `82 0d 01 09` | 前後とも中央値 3.24 V(最小 3.12 V) | 変化なし |
| `81 0d 01 0c` → 1 s → `81 0d 01 0b`(5V) | 受理 | 中央値 3.24 V(最小 3.11 V) | 変化なし |
| 特殊消去 `81 0d 02 0f 0d` | `82 0d 01 00`(2.14 s、target なしで失敗) | 中央値 3.24 V(**最小 3.02 V、2.5 V 未満の点は 0**) | **RST が動く**(下記)。SWDIO / SWCLK も動く |

特殊消去の中の RST(時刻はコマンドを送った時点から):

- 約 43 ms で 0 → 1 になる。
- 約 0.60〜2.35 s の間に、low の pulse が 67 回入る(幅の中央値 4.3 ms、最大 36 ms。始まりの間隔の中央値 30 ms)。
- 約 2.45 s で 0 に戻る。
- 待機中の RST は、P4 の入力で 0 と読めた。LinkE が駆動していない(open-drain で浮いている)ためと見られる(推定。P4 側には pull を掛けていない)。

## 結論

- LinkE fw 2.22 のこの個体では、`probe power 3v3` / `5v` の命令で **3V3 の出力は切れない**(target をつないでいなくても)。E166 で X035 が動き続けたのは、back-power のせいではなく、そもそも出力が切れていないためだった。
- 特殊消去(ch32rv の `recover --method power-off`)も、**target が無いときは電源を切らず、RST に pulse を入れながら接続を試す**。3V3 は 3.0 V 以上を保った。
- E165 で X035 に reset がかかった仕組みは、まだ分からない。X035 の RST は LinkE につながっていないので、この pulse ではない。
  - 候補: target が見つかったときだけ電源を切る。DM の ndmreset を使う。
  - 確かめるには、target をつないだ状態で 3V3 と RST を同時に測る。

## 未決

- target がつながっているときの特殊消去で、3V3 は切れるか(X035 を LinkE 497E につなぐか、X035 の 3V3 を ADC で測る)。
- `probe power 3v3` が効く LinkE の条件(firmware の版、基板の jumper など)。
- 待機中の RST が 0 と読めるのが、浮いているのか、LinkE が low に引いているのか(P4 側で pull-up を掛けて読めば分かる)。
