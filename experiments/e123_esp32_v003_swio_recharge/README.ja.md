# E123 SWIO read 位相を参照実装どおり HIGH recharge する

状態: **完了 — attach 成功**

## 問い

E122 と同じ GPIO16↔PD1 配線で、ESP32-S2 参照実装の `R_GLITCH_HIGH` と同じ read 位相を使えば CH32V003 の DMI signature を読み戻せるか。

## 仮説

E122 の全 bit 0 は、内蔵 pull-up だけでは line が sample 点までに立ち上がらなかった結果である。read の途中で短時間 HIGH を駆動して line を充電し、直後に再び入力へ戻せば、target が延長する LOW と解放された HIGH を区別できる。

## 反証条件

coefficient 8〜12 のすべてで `DMCFGR & 0xffff0000 != 0x5aa50000` となる。

## 方法

送信と read turn-around を cnlohr `esp32s2-cookbook/ch32v003programmer` の bit-bang 実装に合わせる。read bit は LOW 駆動 → 出力無効 → 半区間後に短時間 HIGH 駆動 → 再び出力無効 → sample とする。その他の DMI 操作・探索範囲・安全上の対象外は E122 と同じ。

## 対象外

CPU halt、abstract command、target memory、flash 操作、電気的閾値の確定、SEDIO wire format。

## 必要な環境

E122 と同じ一時ベンチ。ESP32 GPIO16 ↔ UIAPduino CH32V003 PD1、共通 GND、3.3 V logic。

## ベンチ種別

**一時**。配線変更なし。

## 記録する数値

GPIO16 idle、coefficient ごとの DMCFGR、成功時の DMSTATUS と DMHARTINFO。

## 完了条件

signature の読出し、または全 coefficient の不一致を記録したら完了。

## 影響

成功すれば SWIO read phase と ESP32 SEDIO backend 候補に実機根拠を与える。失敗なら software timing 以外に、導通・target 給電・PD1 状態を切り分ける必要がある。

## 結果

run: `_runs/E123_20260918T010503Z_default/`

| 項目 | 観測値 |
|---|---:|
| GPIO16 idle | 1 (HIGH) |
| timing coefficient | 10（最初の条件で成功） |
| DMCFGR | `0x5aa50401` |
| DMSTATUS | `0x004c0c82` |
| DMHARTINFO | `0x002120f4` |
| pytest | 1 passed in 16.84 s（build/upload を含む） |

## 事実

1. classic ESP32 の GPIO16 から UIAPduino Pro Micro CH32V003 の PD1 へ単線 SWIO の write と read が成立した。
2. ESP32 が直前に書いた DMCFGR の signature `0x5aa5` を V003 から読み戻したため、単なる pull-up の読出しではない。
3. 別 address の DMSTATUS と DMHARTINFO も 32 bit 値を返した。
4. E122 の常時 open-drain では全 bit 0、参照実装どおりの短い HIGH recharge では最初の条件で成功した。現ベンチでは read 位相の recharge が必要だった。
5. `DMCONTROL=1` で debug module を有効にしたが、halt request、abstract command、memory access、flash 操作は行っていない。

## 候補

classic ESP32 GPIO bit-bang を SEDIO service の CH32V003 target-link backend として使える。実装時は read の HIGH recharge を PHY 要件として保持する。

## 未決

- HIGH recharge の最小幅と、外付け pull-up がある場合に常時 open-drain だけで成立する条件
- 連続 DMI transaction の誤り率と速度
- CPU halt、memory access、flash access（別の明示的な実験で扱う）

## 反映

台帳へ成功値を記録した。単一 target・単一配線の実証なので protocol 文書の status は変更しない。
