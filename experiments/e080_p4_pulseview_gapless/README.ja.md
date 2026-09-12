# E080 PulseView へ継ぎ目なく流す

状態: **完了 — 継ぎ目は消えた。86 Msps・64 M sample まで一本の capture で通る。それ以上の律速は device でも server でもなく、client が data をどうするか**(2026-09-13)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E077](../e077_p4_pulseview_over_ip/README.ja.md)(BeagleLogic の TCP を演じる。**batch を繋ぐので継ぎ目が出る**)、[E078](../e078_p4_continuous_stream/README.ja.md)(**86 Msps まで継ぎ目なく降ろせる**)

## 問い

**sigrok / PulseView が要求する sample 数を、1 回の capture として継ぎ目なく渡せるか。渡せる rate の上限は [E078](../e078_p4_continuous_stream/README.ja.md) の 86 Msps と一致するか。**

## なぜこの問いか

[E077](../e077_p4_pulseview_over_ip/README.ja.md)は PulseView から取れるところまで通したが、**1 回の capture(4 M sample)を超える要求は別々の capture を繋ぐので継ぎ目に空白が入る**。16 M sample を要求したときの周期の乱れは**すべて batch 境界にあった**。

[E078](../e078_p4_continuous_stream/README.ja.md)で **capture しながら降ろす経路が 86 Msps まで成立した**ので、**server の裏を batch から stream に差し替えれば継ぎ目は消えるはず**である。

## 仮説

**消える。** device 側は 1 回の `S <bytes> <rate>` で連続した byte 列を出すので、**server はそれを TCP へ素通しするだけでよい**。継ぎ目の元になっていた「capture を止めて次を始める」動作が無くなる。

**上限は 86 Msps より下がる**と見ている。**server が packed 2 bit/sample を 1 byte/sample へ展開する**ので、**TCP 側の byte rate は USB 側の 4 倍**になる(86 Msps なら 21.5 MB/s → **86 MB/s**)。**展開が host 側の律速になる**可能性が高い。

## 反証条件

1. 継ぎ目が消えない(batch 以外の原因で周期が乱れる)
2. 86 Msps まで通り、host 側の展開は律速にならない
3. **要求 sample 数を device 側の 1 回の上限(64 MiB packed = 268 M sample)より増やすと、結局 batch に戻る**

## 方法

[E077](../e077_p4_pulseview_over_ip/README.ja.md)の server の**送出元だけ**を差し替える。**protocol の実装(`bl_server.py`)はそのまま import して使う**ので、BeagleLogic 側の挙動は E077 と同一である。

- 送出元: [E078](../e078_p4_continuous_stream/README.ja.md) の firmware。`S <bytes> <rate>` を **1 回だけ**発行し、**届いた chunk をそのまま展開して socket へ流す**
- 展開は **numpy** で行う(E077 は Python の loop で、実時間に間に合わない)
- rate を振る: **16 / 32 / 48 / 64 / 80 / 86 MHz**
- 判定は [E074](../e074_p4_2ch_capture_to_sr/README.ja.md) と同じ**立ち上がり edge の間隔**。`sigrok-cli` が書いた `.sr` を[`check_sr.py`](../e077_p4_pulseview_over_ip/check_sr.py)で読む
- **E077 と同じ条件(16 M sample)を必ず 1 本測る** — あそこで継ぎ目が 3 箇所出た条件である

### 記録する数値

- rate ごとに: sample 数、周期 min / mean / max、**周期が期待値と違う箇所の数と位置**
- server 側の展開が間に合っているか(device が送った byte と socket へ流せた byte、遅延)
- host 側の展開 throughput(MB/s)

## 対象外

- trigger
- 268 M sample を超える要求(device 側 firmware の上限)
- 4 / 8 channel
- PulseView GUI での操作(`sigrok-cli` が通れば同じ libsigrok 経路)

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`([E078](../e078_p4_continuous_stream/README.ja.md) の firmware)、HS port を usbipd で WSL へ
- `sigrok-cli` 0.7.2 / libsigrok 0.5.2
- host: `uv run --with libusb1 --with numpy`

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**E077 で継ぎ目が出た条件(16 M sample)を、周期の乱れ 0 で通す。** 通る rate の上限を記録する。

## 影響

- [E077](../e077_p4_pulseview_over_ip/README.ja.md) の未決「継ぎ目のない連続 streaming」
- [PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) — 経路 B の制約が 1 つ減る
- [近い目標](../../references/p4-logic-analyzer-investigation.ja.md) 2 と 4

## 結果

### E077 で継ぎ目が出た条件が、そのまま通る

**16 M sample。[E077](../e077_p4_pulseview_over_ip/README.ja.md)では batch 境界に 3 箇所の乱れが出ていた条件である。**

| rate | sample 数 | 周期 min / mean / max | 判定 |
|---:|---:|---|---|
| 32 MHz | 16,000,000 | **320 / 320.00 / 320** | **継ぎ目なし** |
| 48 MHz | 16,000,000 | **480 / 480.00 / 480** | **継ぎ目なし** |
| 64 MHz | 16,000,000 | **640 / 640.00 / 640** | **継ぎ目なし** |
| 80 MHz | 16,000,000 | **800 / 800.00 / 800** | **継ぎ目なし** |
| **86 MHz** | 16,000,000 | **860 / 860.00 / 860** | **継ぎ目なし** |

**深さを 4 倍にしても通る。**

| rate | sample 数 | 周期 | device |
|---:|---:|---|---|
| **86 MHz** | **64,000,000**(0.74 秒の連続 capture) | **860 / 860.00 / 860**(74,419 周期) | 18.56 MB/s、`fifo_overflow=0`、占有 718 KB |

**反証条件 1 は否定された。**

### 律速は device でも server でもなかった

**256 M sample(64 MiB packed = firmware の 1 回の上限)を 86 MHz で要求すると壊れる** — が、原因は経路の手前ではない。**同じ run を、client 側の出力先だけ変えて 3 通り測った。**

| client 側の出力 | device の見え方 | 結果 |
|---|---|---|
| `sigrok-cli -O srzip`(`.sr` を書く) | 5.29 MB/s、**`fifo_overflow=31,506`**、占有 **8 MiB 満杯** | **sample 欠落**(周期 60〜1392) |
| `sigrok-cli -O binary > /dev/null` | **21.72 MB/s**、`fifo_overflow=0`、占有 **90 KB** | **通る** |
| protocol だけ話して捨てる client | **22.60 MB/s**(展開後 89.32 MB/s)、`fifo_overflow=0`、占有 176 KB | **通る** |

**server の展開(numpy)は 89 MB/s 出ており、律速ではない。** 詰まっているのは **`srzip` の書き出し**である。**仮説は外れた** — 展開が律速になると見ていたが、実際は client の出力先だった。**反証条件 2 は「86 Msps まで通る」側が成立し、「展開が律速」側は否定された。**

**`srzip` の遅さは rate ではなく出力の総量で効く。** 64 MB(64 M sample)は 18.56 MB/s で通るのに、256 MB では **16 Msps に落としても `fifo_overflow=1,911`** で溢れる。`srzip` は chunk ごとに zip entry を足す形式なので、**entry が増えるほど重くなる**構造と読める(未確認)。

### 1 回の capture を超える要求は、今度は繋がずに止まる

server は `get` に対して **`--samples` ぶんを 1 回だけ**流す。**client がそれより多く要求すると、足りないまま待ち続ける**([E077](../e077_p4_pulseview_over_ip/README.ja.md)は batch を繋いでいたので、代わりに継ぎ目が出ていた)。

**`--samples` を client の要求に合わせること。** 上限は firmware の 64 MiB packed = **268,435,456 sample**。それを超えるなら batch に戻すしかない。**反証条件 3 はこの形で成立する。**

## 事実

1. **継ぎ目は消えた。** [E077](../e077_p4_pulseview_over_ip/README.ja.md)で 3 箇所の乱れが出ていた 16 M sample が、**全 rate で周期完全一致**になった。
2. **86 Msps・64 M sample まで一本の capture で取れる。** [E078](../e078_p4_continuous_stream/README.ja.md)の上限がそのまま PulseView 経路の上限になっている。
3. **server の展開は律速ではない。** numpy で **89.32 MB/s**(展開後)出ており、USB 側の 22.6 MB/s に対して 4 倍の余裕がある。
4. **律速は client が data をどうするかである。** 同じ run が `-O binary` なら通り、`-O srzip` なら壊れる。**`.sr` を書きながら長時間取るのが一番きつい。**
5. **弾性 FIFO は burst を買うだけである。** 8 MiB あるので短い capture なら sink が遅くても吸収できるが、**総量が増えると必ず効いてくる**。
6. **`fifo_overflow` は sample 欠落と一致する。** 溢れた run は周期が 60〜1392 に乱れ、溢れなかった run は完全一致だった。

## 候補

- **PulseView から長く取るときは出力先を選ぶ。** `.sr` へ落とすなら **64 M sample 程度まで**。それ以上は `-O binary` か、**capture を `.sr` にせず生で受ける**
- **server の `--samples` は client の要求に合わせる。** 超えると待ち続け、下回ると余分を捨てる
- **継ぎ目が要らないなら 86 Msps まで、継ぎ目を許すなら batch で 160 Msps まで**([E074](../e074_p4_2ch_capture_to_sr/README.ja.md))

## 未決

- **`srzip` が総量に対してどう重くなるか** `—`。zip entry 数が効いていると読んでいるが未確認。libsigrok 側の話
- **PulseView GUI での挙動** `—`。GUI は `.sr` を書かずに memory へ溜めるので、`-O binary` に近いはず
- **268 M sample を超える連続 capture** `—`。firmware の 1 回の上限
- **client が要求 sample 数を伝えられないこと** `—`。BeagleLogic protocol の構造([E077](../e077_p4_pulseview_over_ip/README.ja.md))

## 影響

- [E077](../e077_p4_pulseview_over_ip/README.ja.md) の未決「継ぎ目のない連続 streaming」— **解決**
- [PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) — 経路 B の制約が「batch の継ぎ目」から「client の出力先」へ移った
- [近い目標](../../references/p4-logic-analyzer-investigation.ja.md) 2 / 4
