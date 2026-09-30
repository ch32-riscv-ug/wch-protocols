# E171 WCH-LinkE は RVSWD のバスをどのレベルで休ませるか(既存の線上 capture の集計)

状態: **完了(2026-09-30)**。新しい収録はしていない。既存の 2 つの fixture を集計しただけ。

## 問い

oep-probe-arduino は 2026-09-23 に、RP2350 の治具で次のことを観測した(oep-probe-arduino `docs/pico-bench.ja.md` の「バスを待機させるレベルが決定的だった」)。

- CH32L103 は、両線 high で約 1 ms 休ませると debug link を失う。5 ms 以上では DM が reset され、halt していた hart が走り出す。SWCLK low で休ませれば、3 s たっても halt を保つ。
- CH32X035 は逆で、high で休ませるとどれだけ置いても link を保つ。frame の間を low で休ませると接続できなかった。

2026-09-24 には、これが target の性質か治具の性質かを「LinkE → L103 の capture か OEP の観測で確かめる」として保留した。WCH-LinkE は target ごとに休ませるレベルを変えているか。

## 手順

- 資料
  - [wire-linke-p4-2026-09-25/](../../captures/fixtures/wire-linke-p4-2026-09-25/README.ja.md): LinkE fw 2.22 → L103(`0E028F0692F1`)と V203(`FBC18F0680B0`)。ch32rv 0.9.1 の 12 操作。ESP32-P4 で 50 MHz。D0 = SWCLK、D1 = SWDIO。
  - [wire-flash-v003-x035-2026-09-11/](../../captures/fixtures/wire-flash-v003-x035-2026-09-11/README.ja.md) `wire-x035.sr`: LinkE fw 2.22(`FC928F068181`)→ X035。LA2016 で 50 MHz。CH0 = SWCLK、CH1 = SWDIO。
- [`idle_levels.py`](idle_levels.py) で、(SWCLK, SWDIO) が 100 µs 以上変わらない区間を、レベルごとに数えた。
  - 収録の最初や最後にかかる区間は edge として別に数えた(操作の前後なので、LinkE が線を駆動しているかどうか分からない)。
  - 反射のひげ(1〜2 sample)で区間が割れることはありうる。その場合は数が増えるだけで、レベルは変わらない。
- 結果の全行は [`out.tsv`](out.tsv)。

## 結果

| target | 操作 | 操作中(inner)の休み | 前後(edge) |
|---|---|---|---|
| L103 | 12 操作すべて | **SWCLK=0・SWDIO=1 だけ**。1 操作あたり 10〜347 区間、最長 39.9 ms(flash_pattern4k) | SWCLK=0・SWDIO=1(55〜65 ms) |
| V203 | 12 操作すべて | **SWCLK=0・SWDIO=1 だけ**。1 操作あたり 237〜670 区間、最長 2.3 ms | SWCLK=0・SWDIO=1(1 ms 未満)、または無し |
| X035 | 収録 1 本(接続・消去・書込み・検証) | **SWCLK=1・SWDIO=1 だけ**。68 区間、最長 10.3 ms | SWCLK=1・SWDIO=1(最長 393 ms) |

- どの収録にも、もう一方のレベルの区間は 100 µs 以上続くものが 1 つもない。
- L103 / V203 の 24 操作は、すべて ch32rv の終了コード 0 で終わった(各 `.json` の `rc`)。L103 は、40 ms low で休んだ後も操作を続けられている。
- X035 の収録では、10 ms high で休んだ後も frame が続いている。

## 結論

- **WCH-LinkE fw 2.22 は、RVSWD を休ませるレベルを target によって変えている。** L103 と V203 では SWCLK を low・SWDIO を high にし、X035 では両線を high にする。
- OEP の治具での観測(L103 は low で休ませる必要があり、X035 は high で休ませる必要がある)と向きが一致する。配線がまったく別の host でも同じなので、target 系統の性質と見る。
- [link-to-target](../../protocols/link-to-target.ja.md) §3「信号とアイドル」の「無トランザクション時は両方 HIGH」は、X035 にしか当てはまらない。その箇所を直した。

## 限界

- X035 の収録は、L103 / V203 とは別の LinkE 個体・別の日(09-11)のもの。同じ LinkE で target だけを替えた比較ではない。
- LinkE が何でレベルを選んでいるのか(chip ID か、family か、接続時の問い合わせの応答か)は分からない。
- 1 系統につき 1 個体だけ。V00x(1 線)は対象外で、V103 / V30x / L103 の他の個体は見ていない。
- 休みを high にしたときの L103 の振舞いは、LinkE 越しでは試せない(OEP の 09-23 の観測だけ)。

## 未決

- V103 / V30x / X035 の、同じ LinkE での収録(target だけを替える)。
- oep-spec `5bfe052`(2026-09-30、push 済み)で、oep-if-debug §3 の rvswd attach に `idle_clock`(0 = high、1 = low)が入った。線の設定は target の性質として host が持ち、probe は既定を持たない。同じ commit で、reset の線にも既定が無くなった(host が毎回 channel で明示する)。ch32rv の DB の既定値は、この E171 と OEP の観測から決めることになる。
