# E008 線: SWIO の write フレームをパルス幅で検算する

状態: **完了**(2026-09-04)    <!-- 計画 → 実行中 → 完了 / 中断 -->

規則: [../README.ja.md](../README.ja.md) / 台帳: [../LEDGER.ja.md](../LEDGER.ja.md) / 道具: [E006](../e006_tool_pulse_capture/README.ja.md)

## 問い

**SWIO(1 線)の write フレームを RMT で生成したとき、線上のパルス幅の並びが意図した bit 列に復号できるか。**

[E007](../e007_wire_rvswd_frame/README.ja.md) の SWIO 版。RVSWD はクロックがあるので bit を直接拾えたが、SWIO は**幅でしか区別できない**ので [E006](../e006_tool_pulse_capture/README.ja.md) の道具を使う。

## 仮説

**復号できる。** 根拠は 2 つ:

- **フレーム構造**([link-to-target §3](../../protocols/link-to-target.ja.md) と `rv003usb/rvswdio_programmer/bitbang_rvswdio.h` の `MCFWriteReg32` / `MCFReadReg32`): **start(1) + addr7 + R/W(write=1 / read=0) + data32 = 41 bit**。運ぶ中身は RVSWD と同じ `(addr7, data32, op)`。
- **幅**(同 `bitbang_rvswdio_ch32v003.h` の実測コメント): `1` = LOW 約 **290 ns**、`0` = LOW 約 **890 ns**、bit 周期 **1.16〜1.33 us**。[E006](../e006_tool_pulse_capture/README.ja.md) でこの 2 つが 12.5 ns 分解能で分離できることは確認済み。

## 反証条件

1. 41 symbol を送りきれない(RMT のメモリ・API の制約)
2. 受信側が幅を取りこぼす、または境界のパルスが欠ける
3. 復号した bit 列が意図と一致しない(並び順の誤り)
4. `1` と `0` の幅が実測で分離できない(E006 の結果と矛盾する)

## 方法

1. 送信側に `W<lo1>,<hi1>,<lo0>,<hi0>`(tick 単位の幅設定)と `S<addr>,<data>` を実装。**start(1) → addr7 → R/W(=1) → data32** を MSB first で並べ、各 bit を RMT の 1 symbol(LOW→HIGH)として出す。
2. 幅は 80 MHz tick で: `1` = LOW 23 tick(287.5 ns)/ HIGH 83 tick、`0` = LOW 71 tick(887.5 ns)/ HIGH 35 tick。**bit 周期は 106 tick(1325 ns)で一定**。
3. 受信側は [E006](../e006_tool_pulse_capture/README.ja.md) の取り込みで 41 symbol 分の幅を拾う。
4. host 側で LOW 幅を閾値(500 ns)で 1/0 に復号し、期待 bit 列と比較する。
5. ベクタ(各 3 回): `addr=0x11 data=0`、`addr=0x10 data=0x80000001`、`addr=0x00 data=0`、`addr=0x7F data=0xFFFFFFFF`、`addr=0x04 data=0xA5C33C5A`。

## 対象外

| 落とすもの | 行き先 |
|---|---|
| **実 CH32 が受け付ける閾値** | 候補 `swio-threshold`。**実 target が必須** |
| read フレーム(32 bit を読み返す位相) | 別実験。受信側が target を演じる必要がある |
| pull-up / open-drain の電気的挙動 | 対象外(両 board 3.3 V、push-pull) |
| 立ち上がり時間(~120 ns) | RMT では見えない。形が要るなら LA |

## 必要な環境

**peer 対 2 枚**(常設 v2)。GPIO19 の 1 線のみ。配線変更なし。実 target 不要。

## ベンチ種別

**常設 v2**。

## 記録する数値

| 項目 | 単位 | 備考 |
|---|---|---|
| ベクタごとの復号一致 | 3 回中 n 回 | |
| `1` / `0` の LOW 幅の実測分布 | ns | min / med / max。E006 と整合するか |
| 取れた symbol 数 | 個 | 41 に足りるか |

## 完了条件

- 全ベクタが 3/3 で復号一致 → **完了**
- 一致しなければ差分を事実として記録して完了

## 影響 — 主張できる範囲

- 主張できるのは **「自分の SWIO 符号器が、参照実装から読み取ったフレーム構造と幅で bit を出している」**まで。**`attested` 止まり**(送受とも自作)。
- **[coverage](../../coverage.ja.md) P3-6(SWIO の 0/1 閾値)は埋まらない。** 閾値は実 CH32 が決めるもので、ここで確認できるのは「意図した幅を正確に出せる」ことだけ。
- 仕様の status は動かさない。

---

# 結果(2026-09-04)

計画は上のまま変更していない。以下は追記。

## 結果

run: `_runs/E008_*_s3_peer/`。41 bit、80 MHz tick。

| ベクタ | addr | data | 復号一致 |
|---|---|---|:---:|
| DMSTATUS | 0x11 | 0x00000000 | **3/3** |
| DMCONTROL | 0x10 | 0x80000001 | **3/3** |
| 全 0 | 0x00 | 0x00000000 | **3/3** |
| 全 1 | 0x7F | 0xFFFFFFFF | **3/3** |
| DMDATA0 | 0x04 | 0xA5C33C5A | **3/3** |

| bit | LOW 幅 min | med | max | 標本数 |
|---|---:|---:|---:|---:|
| `1` | 287.5 ns | 287.5 | 287.5 | 213 |
| `0` | 887.5 ns | 887.5 | 887.5 | 402 |

| 反証条件 | 結果 |
|---|---|
| 1. 41 symbol を送りきれない | 起きず |
| 2. 幅の取りこぼし | 起きず(全ベクタで 41/41) |
| 3. 復号が一致しない | 起きず |
| 4. 幅が分離できない | 起きず(287.5 と 887.5、重なりなし) |

## 事実

1. **SWIO の write フレーム(start + addr7 + rw + data32 = 41 bit)をパルス幅で出し、幅から元の bit 列に復号できる。** 5 ベクタ × 3 回すべて一致。
2. **615 パルスを測って min = med = max。** ジッタが観測されない。RMT で幅を作ると、意図した値がそのまま線に出る。
3. **`1`(287.5 ns)と `0`(887.5 ns)は重なりなく分離できる。** [E006](../e006_tool_pulse_capture/README.ja.md) の結果がフレーム全体でも保たれる。
4. 参照実装から読み取った**フレーム構造と幅の両方**が、実行可能な形になった。

## 候補

- **ESP32 の SWIO phy は RMT TX で作る**(採用候補)。bit ごとに幅の違う symbol を並べるだけで、ソフトのタイミングループが要らない。[generic-probe-design §4](../../references/generic-probe-design.ja.md) の「ペリフェラル支援」段の具体形。

## 未決

- **read フレーム(32 bit を読み返す位相)は未実装。** 受信側が target を演じる必要があり、[規則 §4.4](../README.ja.md) の判断に関わる。
- **実 CH32 が受け付ける閾値は不明のまま**(候補 `swio-threshold`)。ここで確認できたのは「意図した幅を正確に出せる」ことだけ。
- 立ち上がり時間(参照実装のコメントでは ~120 ns)は RMT では見えない。

## 影響 — 主張できる範囲

- 主張できるのは **「自分の SWIO 符号器が、参照実装から読み取ったフレーム構造と幅で bit を出している」**まで。**`attested` 止まり**。
- **[coverage](../../coverage.ja.md) P3-6(SWIO の 0/1 閾値)は埋まっていない。** 閾値は実 CH32 が決める。
- 仕様の status は動かさない。
