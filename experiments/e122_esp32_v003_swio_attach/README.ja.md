# E122 ESP32 から CH32V003 の SWIO に attach する

状態: **完了 — 応答未確認**

## 問い

`esp32-d0wd-v3-0070070d9394` の GPIO16 と UIAPduino Pro Micro CH32V003 の PD1 が接続された現ベンチで、ESP32 から単線 SWIO を使って DMI 設定レジスタを読み出せるか。

## 仮説

[E008](../e008_wire_swio_frame/README.ja.md) で検算した `start + addr7 + rw + data32` の送信形式と、既存の ESP32-S2 参照実装の read turn-around を classic ESP32 の GPIO16 に移せば、`DMCFGR` の上位 16 bit として書き込んだ `0x5aa5` を読み戻せる。

## 反証条件

- GPIO16 を入力 pull-up にした静止状態が LOW で、送信を安全に開始できない。
- 参照実装で有効とされた timing coefficient 9〜10 を含む範囲で、`DMCFGR & 0xffff0000 == 0x5aa50000` を一度も読み戻せない。

## 方法

1. ESP32 の GPIO16 を input + pull-up にし、静止レベルを確認する。LOW なら以後の操作を中止する。
2. GPIO16 を input/output open-drain にする。HIGH は line release、LOW だけを駆動する。
3. SWIO の `DMSHDWCFGR` と `DMCFGR` に `0x5aa50400`、`DMCONTROL` に `1` を各 2 回書く。
4. `DMCFGR`、`DMSTATUS`、`DMHARTINFO` を読み、raw 値と timeout を記録する。
5. coefficient 8〜12 を順に試し、最初に signature が一致した条件を成功とする。

書くのはデバッグ回路を有効にする DMI レジスタだけとする。halt request (`DMCONTROL=0x80000001`)、abstract command、target memory、flash controller は操作しない。

## 対象外

- CPU halt、register/memory access、flash read/write/erase
- SWIO pulse-width の電気的な境界測定 (`swio-threshold`)
- OEP transport と SEDIO service の wire format
- GPIO16 以外の配線探索

## 必要な環境

- probe: classic ESP32 D0WD-V3、`/run/board-identify/by-id/esp32-d0wd-v3-0070070d9394`
- target: UIAPduino Pro Micro CH32V003、PD1/SWIO
- 配線: ESP32 GPIO16 ↔ V003 PD1、共通 GND、3.3 V logic
- 計測器: 不要。外付け 10 kΩ pull-up があれば望ましいが、最初は ESP32 内蔵 pull-up で静止レベルを検査する

## ベンチ種別

**一時**。ユーザーが配線済みと申告した現ベンチを変更せず使う。

## 記録する数値

- attach 前の GPIO16 level
- coefficient ごとの `DMCFGR` read result / timeout
- 成功時の `DMCFGR`、`DMSTATUS`、`DMHARTINFO` (32-bit hex)

## 完了条件

signature を読み戻して成功、または安全検査で中止、または coefficient 8〜12 の全条件で timeout/不一致を記録したら完了とする。

## 影響

成功すれば [link-to-target.ja.md](../../protocols/link-to-target.ja.md) の SWIO read phase と、ESP32 を SEDIO の target-link backend にできるという候補に実機根拠を与える。1 台・1 配線の確認なので仕様 status は動かさない。

## 結果

run: `_runs/E122_20260918T010222Z_default/`

| 項目 | 観測値 |
|---|---:|
| GPIO16 idle | 1 (HIGH) |
| coefficient 10 | status=0, `DMCFGR=0x00000000` |
| coefficient 9 | status=0, `DMCFGR=0x00000000` |
| coefficient 11 | status=0, `DMCFGR=0x00000000` |
| coefficient 8 | status=0, `DMCFGR=0x00000000` |
| coefficient 12 | status=0, `DMCFGR=0x00000000` |

pytest はログ取得後、結果行を取り出す harness の `re.Match` 取扱い誤りで failed になった。上表はその前に device log へ保存された実機出力であり、firmware の実行自体は `ATTACH END` まで完了している。harness の誤りは修正した。

## 事実

1. GPIO16 は入力 pull-up 状態で HIGH だったため、安全検査を通過し SWIO frame を送信した。
2. 内蔵 pull-up + 常時 open-drain の条件では、coefficient 8〜12 のすべてで signature を読み戻せなかった。
3. read は timeout せず全 bit が 0 と判定された。line は各 bit の timeout 内には HIGH へ戻るが、sample 時点では LOW だった。

## 候補

内蔵 pull-up の立ち上がりが read の sample 点に間に合っていない可能性がある。参照実装の `R_GLITCH_HIGH` と同じ短い HIGH recharge を別実験で試す。

## 未決

- GPIO16 と PD1 が実際に導通しているか
- V003 が給電され、SWIO が有効か
- 外付け pull-up または参照実装の HIGH recharge で応答するか

## 反映

仕様 status は変更しない。E123 で参照実装との電気的な差を切り分ける。
