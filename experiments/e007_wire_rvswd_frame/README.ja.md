# E007 線: RVSWD の host 位相フレームを検算する

状態: **完了**(2026-09-04)    <!-- 計画 → 実行中 → 完了 / 中断 -->

規則: [../README.ja.md](../README.ja.md) / 台帳: [../LEDGER.ja.md](../LEDGER.ja.md) / 道具: [E005](../e005_tool_clocked_capture/README.ja.md)

## 問い

**[link-to-target §3](../../protocols/link-to-target.ja.md) の RVSWD host 位相フレーム(addr7 + data32 + op2 + parity1 = 42 bit、MSB first)を送信器として実装し、線上に出た bit 列が仕様どおりか。**

## 仮説

**一致する。** [E005](../e005_tool_clocked_capture/README.ja.md) で、data は clock LOW で変化・clock HIGH でサンプル・MSB first の 2 線が 100 kbps まで確実に往復することを確認済み。あとは 42 bit の並べ方と parity の計算だけ。

## 反証条件

1. 送信した bit 列が host 側の期待値と一致しない(並び順・ビット数の誤り)
2. **parity の規約が確定できない** — 仕様は「Address+Data+Operation の **odd parity**」とだけ書いており、*parity bit を含めて 1 の個数を奇数にする*のか、*XOR そのもの*なのかが読み取れない
3. 42 bit(byte 境界に揃わない)の取り扱いで端が崩れる

## 方法

1. 送信側に `F<addr>,<data>,<op>,<half_us>` を実装。**addr(7) → data(32) → op(2) → parity(1)** を MSB first で並べ、E005 と同じクロック規則で出す。
2. 受信側は E005 の取り込みをそのまま使い、42 bit を拾う。
3. host 側でも同じ規則で期待 bit 列を組み立て、hex で比較する。
4. **parity の規約は「parity bit を含めて 1 の個数が奇数になる」と仮定**して実装し、その仮定を明示的に記録する(反証条件 2)。
5. 検査するベクタ(各 3 回):
   - `addr=0x11(DMSTATUS), data=0, op=1(read)`
   - `addr=0x10(DMCONTROL), data=0x80000001, op=2(write)`
   - `addr=0x00, data=0x00000000, op=0` — 全 0
   - `addr=0x7F, data=0xFFFFFFFF, op=3` — 全 1
   - `addr=0x04(DMDATA0), data=0xA5C33C5A, op=2`

## 対象外

| 落とすもの | 行き先 |
|---|---|
| **実 CH32 が受け付けるか** | 実 target が要る。ここは**自分の符号器が仕様どおりか**だけ |
| target 応答位相(7+32+2+1 のエコー)、STOP 条件、初期化の 100 clocks | 別実験(target が要る) |
| クロック周波数・波形品質 | E005 の仕様値(半周期 5 us)に従う |
| SWIO(1 線) | 別実験。道具は [E006](../e006_tool_pulse_capture/README.ja.md) |

## 必要な環境

**peer 対 2 枚**(常設 v2)。GPIO19 = SWCLK 相当、GPIO20 = SWDIO 相当。配線変更なし。実 target 不要。

## ベンチ種別

**常設 v2**。

## 記録する数値

| 項目 | 単位 | 備考 |
|---|---|---|
| ベクタごとの一致 | 3 回中 n 回 | |
| 不一致時の受信 bit 列 | hex | どこが違うかを残す |
| parity の仮定と結果 | — | 仮定が破綻したらそれ自体が事実 |

## 完了条件

- 全ベクタが 3/3 一致 → **完了**。符号器が[仕様の読み]どおりであることが実行可能な形で残る
- 一致しなければ差分を事実として記録して完了

## 影響 — 何を主張でき、何を主張できないか

- 主張できるのは **「自分の符号器が [link-to-target §3](../../protocols/link-to-target.ja.md) の読みどおりに bit を並べている」**まで。送受とも自作なので、[規則 §6](../README.ja.md) の水準では **`attested` 止まり**(独立実装ではない)。
- **仕様の status は動かさない。** §3 を `verified` にするのは、**実 CH32 が応答する**ことを見たときだけ。
- parity の規約が確定しないことは、実 target が要る穴として記録する。

---

# 結果(2026-09-04)

計画は上のまま変更していない。以下は追記。

## 結果

run: `_runs/E007_*_s3_peer/`。半周期 5 us、42 bit。

| ベクタ | addr | data | op | 期待 = 実測(hex) | 一致 |
|---|---|---|---:|---|:---:|
| DMSTATUS read | 0x11 | 0x00000000 | 1 | `220000000080` | **3/3** |
| DMCONTROL write | 0x10 | 0x80000001 | 2 | `210000000340` | **3/3** |
| 全 0 | 0x00 | 0x00000000 | 0 | `000000000040` | **3/3** |
| 全 1 | 0x7F | 0xFFFFFFFF | 3 | `FFFFFFFFFF80` | **3/3** |
| DMDATA0 write | 0x04 | 0xA5C33C5A | 2 | `094B8678B540` | **3/3** |

| 反証条件 | 結果 |
|---|---|
| 1. bit 列が期待値と一致しない | 起きず |
| 2. parity の規約が確定できない | **該当**(下記) |
| 3. 42 bit の端が崩れる | 起きず |

## 事実

1. **RVSWD host 位相の 42 bit(addr7 + data32 + op2 + parity1、MSB first)を線上に出せる。** 5 ベクタ × 3 回すべて一致。
2. **手計算とも一致する。** 例: 全 1 は ones=41(奇数)→ parity=0 → `FF FF FF FF FF 80`、全 0 は ones=0(偶数)→ parity=1 → `00 00 00 00 00 40`。**送信器・受信器・手計算の 3 者が一致**しており、送受が同じ誤りを共有している可能性は下がる。
3. **[link-to-target §3](../../protocols/link-to-target.ja.md) の bit レイアウトが実行可能な形になった。** 以後この符号器を CH32RVProbe の RVSWD phy の出発点にできる。

## 候補

- 送信は `digitalWrite` + `delayMicroseconds` の素朴な bit-bang で足りる(検証速度では)。実速度では [generic-probe-design §4](../../references/generic-probe-design.ja.md) のペリフェラル支援段へ。

## 未決

- **parity の規約が確定していない。** [link-to-target §3](../../protocols/link-to-target.ja.md) は「Address+Data+Operation の **odd parity**」としか書いておらず、*parity bit を含めて 1 の個数を奇数にする*のか*別の規約*なのかが読み取れない。**この実験は前者を仮定した**。**実 CH32 が応答するかどうかでしか決まらない** → 実 target が要る穴として記録。
- target 応答位相(7+32+2+1 のエコー)、STOP 条件、初期化の 100 clocks は未実装・未検証。

## 影響 — 主張できる範囲

- 主張できるのは **「自分の符号器が [link-to-target §3](../../protocols/link-to-target.ja.md) の読みどおりに bit を並べている」**まで。送受とも自作なので [規則 §6](../README.ja.md) の水準では **`attested` 止まり**。
- **仕様の status は動かさない。** §3 を `verified` にするのは**実 CH32 が応答したとき**だけ。
- [coverage](../../coverage.ja.md) P3-7(RVSWD の bit フレームを実測で verify)は**まだ埋まっていない**。埋まったのは「符号器側の実装が仕様の読みと一致する」ことだけ。
