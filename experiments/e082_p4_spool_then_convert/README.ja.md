# E082 一時ファイルへ受けてから `.sr` へ変換する

状態: **完了 — [E080](../e080_p4_pulseview_gapless/README.ja.md) が落ちた条件がそのまま通る。受け側は律速でなくなった**(2026-09-13)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E080](../e080_p4_pulseview_gapless/README.ja.md)(**律速は `srzip` の書き出しだった**)、[E078](../e078_p4_continuous_stream/README.ja.md)(device 側は 86 Msps)

## 問い

**capture 中は packed のまま一時ファイルへ落とし、終わってから `.sr` へ変換すると、どの rate・どの深さまで通るか。**

## なぜこの問いか

[E080](../e080_p4_pulseview_gapless/README.ja.md)で、**継ぎ目は消えたが受け側が律速になった**。256 M sample を 86 MHz で取ると `-O srzip` では `fifo_overflow=31,506` で落ち、`-O binary` や捨てる client なら通る。**`srzip` の圧縮が capture と同時に走るのが問題**で、しかも**出力の総量で重くなる**(64 MB は通り、256 MB は 16 Msps でも落ちる)。

**capture 中に `.sr` を作る必要はない。** 受けている間は **packed のまま追記するだけ**にして、**展開と zip は capture が終わってから**やればよい。

## 仮説

**device 側の上限(86 Msps)まで戻る。** capture 中の仕事は **21.5 MB/s で file へ追記するだけ**になり、これは NVMe でも WSL の filesystem でも余裕がある。**変換は後から好きなだけ時間をかけられる。**

## 反証条件

1. 一時ファイルへの書き出しが間に合わない(`fifo_overflow` が出る)
2. 通るが、変換に現実的でない時間がかかる
3. 変換した `.sr` を sigrok が読めない、または波形が合わない

## 方法

[E080](../e080_p4_pulseview_gapless/README.ja.md)の server 経路を使わず、**device から直接受ける tool** にする([`stream_to_sr.py`](stream_to_sr.py))。

1. console で `S <bytes> <rate>`、bulk IN を **URB 4 本 in-flight** で読む
2. **展開せず、packed のまま file へ追記する**(transfer callback の中の仕事は `write` だけ)
3. 完了後に **chunk ごとに展開して `.sr` を書く**。`srzip` は chunk を `logic-1-N` として並べる形式なので、**全体を memory に載せずに済む**
4. 圧縮は既定で **`ZIP_STORED`**(無圧縮)。`--compress` で deflate も選べる
5. 判定は [E074](../e074_p4_2ch_capture_to_sr/README.ja.md) と同じ**立ち上がり edge の間隔**を head / tail で見る

### 記録する数値

- spool の MB/s、device 側の `fifo_overflow` / 占有 / `stalls`
- **変換の所要時間**と出力サイズ(stored / deflate)
- 周期 min / max
- `sigrok-cli` が読み戻せるか

## 対象外

- PulseView から live で引くこと([E080](../e080_p4_pulseview_gapless/README.ja.md))。**本実験は「後から開く」用途**
- trigger、4 / 8 channel
- 268 M sample を超える深さ(firmware の 1 回の上限)

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`([E078](../e078_p4_continuous_stream/README.ja.md) の firmware)、HS port を usbipd で WSL へ
- host: `uv run --with libusb1 --with numpy`、`sigrok-cli`

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**[E080](../e080_p4_pulseview_gapless/README.ja.md) が落ちた条件(86 MHz × 256 M sample)を、欠落なく `.sr` にする。** 変換の所要も記録する。

## 影響

- [E080](../e080_p4_pulseview_gapless/README.ja.md) の未決「受け側が律速」
- [PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) 経路 D(`.sr` を書く)

## 結果

**[E080](../e080_p4_pulseview_gapless/README.ja.md) が sample を落とした条件**(86 MHz × 256 M sample = 64 MiB packed)を、そのまま流した。

| | [E080](../e080_p4_pulseview_gapless/README.ja.md)(live で `srzip`) | **E082(一時ファイル → 後変換)** |
|---|---|---|
| spool | 5.29 MB/s | **21.48 MB/s** |
| `fifo_overflow` | **31,506** | **0** |
| 弾性 FIFO 最大占有 | **8 MiB(満杯)** | **57 KB** |
| 周期 | 60〜1392(**欠落**) | **860 / 860** |
| 判定 | **SAMPLES LOST** | **sample-accurate** |

**device 側は 21.48 MB/s で、[E078](../e078_p4_continuous_stream/README.ja.md) の単体測定(21.4〜22.4 MB/s)と同じ。** 占有 57 KB は「追いついている」側の値である。**反証条件 1 は否定された。**

### 変換は後からいくらでもやれる

| 方式 | 所要 | 出力 |
|---|---:|---:|
| **`ZIP_STORED`(既定)** | **0.51 秒** | 245 MB |
| `ZIP_DEFLATE`(`--compress`) | 9.5 秒 | **2.6 MB** |

どちらも `sigrok-cli` が読み戻す(`Samplerate 86000000` / `sample count 256000000` / `unitsize 1`)。**反証条件 2・3 も否定。**

**この信号(PWM)は 94 倍に縮む。** 実際の logic capture でも、変化の少ない channel ほど効く。**圧縮は capture の外に出したので、どれだけ時間をかけても sample には影響しない** — これが [E080](../e080_p4_pulseview_gapless/README.ja.md) との本質的な差である。

## 事実

1. **受け側を後処理にすると、律速は device 側へ戻る。** 86 MHz × 256 M sample が `fifo_overflow=0`・占有 57 KB で通る。[E080](../e080_p4_pulseview_gapless/README.ja.md) では同条件で 8 MiB 満杯 + 31,506 回の overflow だった。
2. **capture 中の仕事は「packed のまま追記」だけでよい。** 展開(4 倍)も圧縮も capture の外へ出せる。
3. **変換は安い。** 無圧縮なら 64 MiB → 245 MB を **0.51 秒**。deflate でも 9.5 秒で、**245 MB が 2.6 MB(94 分の 1)**になる。
4. **深さの上限は firmware の 1 回の上限(268,435,456 sample)に戻った。** 受け側ではなくなった。
5. **live で見る用途と、保存する用途は分けたほうがよい。** [E080](../e080_p4_pulseview_gapless/README.ja.md) の経路は PulseView で**見る**ため、E082 は**残す**ため。

## 候補

- **`.sr` に残すなら一時ファイル経由にする。** live で `srzip` へ流し込まない
- **保存時は deflate を使う。** 9.5 秒で 94 分の 1
- **PulseView で live に見たいときは [E080](../e080_p4_pulseview_gapless/README.ja.md) の経路**を使い、**深さは 64 M sample 程度に抑える**

## 未決

- **268 M sample を超える深さ** `—`。firmware の 1 回の上限
- **spool 先が遅い媒体だったら** `—`。WSL の filesystem でしか測っていない
- **変換の並列化** `—`。chunk ごとに独立なので分割できるはず

## 影響

- [E080](../e080_p4_pulseview_gapless/README.ja.md) の未決「受け側が律速」— **用途を分けることで解決**
- [PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) 経路 D
