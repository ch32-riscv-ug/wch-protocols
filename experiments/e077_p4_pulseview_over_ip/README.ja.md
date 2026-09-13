# E077 stock の sigrok / PulseView から IP 経由で P4 の capture を取る

状態: **完了 — stock の `sigrok-cli` が server 経由で実機から取れる。4 M sample が0.45秒でsample精度。1回の capture を超える要求は継ぎ目が出る**(2026-09-12)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E076](../e076_p4_capture_hs_download/README.ja.md)(capture → HS download)、[E074](../e074_p4_2ch_capture_to_sr/README.ja.md)(`.sr`) / 設計: [PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) 経路B

## 問い

**BeagleLogic の TCP protocol を演じる Python server を1本置くと、stock の sigrok / PulseView が P4 の capture を IP 経由で取れるか。**

## なぜこの問いか

[近い目標の4番目](../../references/pulseview-integration.ja.md)が「Python で IP 待ち受けして PulseView から capture する」である。[連携メモ](../../references/pulseview-integration.ja.md)は**経路B(BeagleLogic を演じる)だけが stock 環境で動く**と結論し、**接続と最初の command が `version\n` であることまで**実地確認して止まっている。残りは「`version` に何を返せば先へ進むか」「`get` 以降の data 形式」で、**libsigrok の driver source を読めば決まる**。

[E076](../e076_p4_capture_hs_download/README.ja.md)で capture → download が 0.48 秒で通るようになったので、**server の背後に実機を置ける。**

## 仮説

**取れる。** driver source(`beaglelogic_tcp.c` / `protocol.c` / `api.c`、upstream master)から読める要件は次だけである。

| command | server が返すもの |
|---|---|
| `version` | **`BeagleLogic` で始まる文字列**(`strncasecmp(resp, "BeagleLogic", 11)`) |
| `memalloc` / `samplerate` / `sampleunit` / `triggerflags` / `bufunitsize` | **10進整数1個** |
| 上記 + 引数 | **`ok` で始まる文字列** |
| `get` | **同じ socket に raw sample を流す**。1 sample = `sampleunit` が 1 なら 1 byte、0 なら 2 byte。bit n = channel n |
| `close` | 停止 |

終了条件は **`bytes_read >= limit_samples × unitsize`** なので、**要求された byte 数をちょうど送れば driver が自分で終わる**。

## 反証条件

1. `--scan` で device として見えない
2. 見えるが capture が始まらない / 終わらない
3. 取れるが**波形が E074 の `.sr` と一致しない**(channel の対応、sample 数、rate)
4. PulseView(GUI)では動かない

## 方法

### server

`bl_server.py` を書く。**stock の libsigrok 0.5.2 に対して動かす**(手元にあるもの。upstream の driver source と 0.5.2 の `.so` の command 語彙が一致することは[連携メモ](../../references/pulseview-integration.ja.md)で確認済み)。

- `--source sim` : 内部生成の矩形波。**server 単体の適合性**を見る
- `--source usb` : [E076](../e076_p4_capture_hs_download/README.ja.md)と同じ経路で実機から取る。**console で `C` / `D`、data は OTG HS の vendor bulk**
- packed 2 bit/sample の展開は server 側([連携メモ](../../references/pulseview-integration.ja.md) §5 のとおり線の上は packed のまま)

### 確認

1. `sigrok-cli --driver "beaglelogic:conn=tcp-raw/127.0.0.1/<port>" --scan` で見えるか
2. `--samples N` で取れるか。**sample 数がちょうど N か**
3. **同じ信号を[E074](../e074_p4_2ch_capture_to_sr/README.ja.md)の `.sr` 経路でも取り、波形を突き合わせる** — 立ち上がり edge の間隔が `rate ÷ 100 kHz` で一致するか
4. `sigrok-cli -o ... -O srzip` で `.sr` に落として読み戻す

### 記録する数値

- scan の応答、capture の sample 数と rate
- **channel ごとの立ち上がり edge 間隔(min / mean / max)と duty**
- capture 要求から data 到着までの所要
- server が送った byte 数と driver が受け取った sample 数

## 対象外

- **trigger**(driver 側の soft trigger は効くが、P4 の hardware trigger を BeagleLogic の語彙へ写すのは別問題)
- 連続 streaming(gap のない stream。本実験は1回の capture を送り切る)
- 経路A(COM port への SUMP)と経路C(TCP SUMP)
- PulseView GUI の操作確認(**`sigrok-cli` が通れば同じ libsigrok の経路**。GUI は別途)
- BeagleLogic の sample rate 表現(10 Hz〜100 MHz)を超える rate の扱い

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`(E076 の firmware をそのまま使う。**再ビルド不要**)
- HS port を usbipd で WSL へ
- `sigrok-cli` 0.7.2 / libsigrok 0.5.2
- host: `uv run` の pyusb / pyserial

## ベンチ種別

board(単体、内部 PWM を信号源とする。外部配線なし)

## 完了条件

**`sigrok-cli` が server 経由で実機の capture を取り、E074 と同じ周期判定を通す。** 取れない場合は**どの command で止まったか**を記録する。

## 影響

- [PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) — 経路Bの「未確認」を埋める
- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) — host 側の出口が1つ増える

## 結果

### 通った

| 段 | 結果 |
|---|---|
| `--scan` | **`beaglelogic - BeagleLogic 1.0 with 14 channels`** として見える |
| capture(4 M sample @ 80 MHz、2 channel) | **0.44 / 0.46 / 0.46 秒**(3回)、**sample 数はちょうど 4,000,000** |
| 周期判定 | **3回とも D0 / D1 ともに 800 / 800.00 / 800**(期待 `80 MHz ÷ 100 kHz`)、duty 25.00% / 50.00% |
| sample unit 16 bit(14 channel 全有効) | **通る**。1 M sample、周期 800 一致 |
| `.sr` 出力 | `sigrok-cli -O srzip` が書き、読み戻せる |

内訳(server 側の log、4 M sample):

```
device: 1000000 packed bytes, overflow=0 timeout=0 in 50.1 ms
download: 1000000 bytes in 0.103 s = 9.72 MB/s
sent 4000000 bytes
```

**packed 1 MB を線の上で運び、server で 4 MB へ展開している**([連携メモ](../../references/pulseview-integration.ja.md) §5 のとおり)。

### protocol の実体(driver source から確定)

[連携メモ](../../references/pulseview-integration.ja.md)が「未確認」としていた部分は次で確定した。

| command | server が返すもの | 備考 |
|---|---|---|
| `version` | **`BeagleLogic` で始まる文字列** | `strncasecmp(resp, "BeagleLogic", 11)` だけを見る |
| `memalloc` | 10進整数 | **要求 sample × unit byte がこれを超えると driver が capture を切り詰める**。大きく返す |
| `samplerate` / `sampleunit` / `triggerflags` / `bufunitsize` | 10進整数 | 引数付きなら **`ok`** |
| `get` | **同じ socket に raw sample** | 1 sample = `sampleunit` が 1 なら 1 byte、0 なら 2 byte。**bit n = channel n** |
| `close` | **返答不要** | 下記 |

**driver は「何 sample 欲しいか」を server に伝えない。** `limit_samples` は host 側だけに留まり、driver は**必要な byte 数を受け取った時点で `close` を送って読むのをやめる**。server 側は**client が止めるまで送り続ける**のが正しい実装になる。

### 刺さった2つ

1. **`close` を受けて server が socket を閉じてはいけない。** 閉じると `sigrok-cli` が **CPU 100% で回り続けて終わらなくなる**(2分以上回して kill した)。driver は `close` を送ったあと **25 ms の drain をしてから自分で閉じる**ので、**server は待つ**。閉じた側の pollfd が `G_IO_HUP` で即座に返り続けるためと読める
2. **`numchannels` は scan option として driver にあるが、`sigrok-cli` 0.7.2 は conn 文字列の中で受け付けない**(`Unknown device option 'numchannels'`)。**channel 数を 8 に落とすには `--channels P8_45,P8_46` で index 8 以上を無効にする** — driver は**index 8 以上が1つでも有効なら sample unit を 16 bit にする**ので、これが 1 byte/sample への道になる

### 1回の capture を超える要求は継ぎ目が出る

16 M sample を要求すると server は 4 M の batch を**4回 capture して繋ぐ**。結果、**継ぎ目にだけ周期の乱れが出る**。

| | 結果 |
|---|---|
| 所要 | 3.09 秒 |
| 周期が 800 でない箇所 | **4 箇所のみ**。sample offset 3,999,837 / 7,999,487 / 11,999,549 / 12,000,000 |
| batch 境界 | 4,000,000 / 8,000,000 / 12,000,000 |

**乱れはすべて境界にある。batch の内側は全部 800 である。** これは欠落ではなく、**別々の capture を時間軸上で繋いだことによる実際の空白**で、`--samples` を server の batch 以下にすれば出ない。

## 事実

1. **stock の sigrok / PulseView から IP 経由で P4 の capture が取れる。** driver の追加も libsigrok の入れ替えも要らない。→ **[連携メモ](../../references/pulseview-integration.ja.md) 経路Bが成立した。反証条件1・2は否定。**
2. **波形は [E074](../e074_p4_2ch_capture_to_sr/README.ja.md) の判定を通る。** 3回とも周期 800 が min = mean = max。**反証条件3も否定。**
3. **4 M sample @ 80 MHz が 0.45 秒。** capture 50 ms + download 0.10 秒 + TCP 送出。[E076](../e076_p4_capture_hs_download/README.ja.md)の経路をそのまま使っている。
4. **driver は sample 数を server に伝えない。** server は client が `close` を送るまで送り続ける設計になる。**`close` を待って次の batch を捕るようにすると、満足した client に余分な capture を起こさせずに済む。**
5. **`close` で socket を閉じると `sigrok-cli` が終わらなくなる。** libsigrok 0.5.2 の挙動。
6. **16 bit sample unit(14 channel 全有効)でも通る。** server 側で 2 byte に広げるだけ。

## 候補

- **PulseView から使うときは `--samples` を server の batch 以下にする。** 超えると継ぎ目が入る
- **rate は 80 MHz を既定にする。** driver の samplerate list が 100 MHz までなので、その内側で切りのよい値を採る(**「PARLIO は 160 MHz ÷ 整数」と書いていたのは誤り。下記訂正**)
- **BeagleLogic を演じる server は `close` で socket を閉じない**

## 未決

- **PulseView(GUI)での確認** `—`。`sigrok-cli` が通ったので同じ libsigrok 経路だが、GUI の操作は未確認
- **継ぎ目のない連続 streaming** `—`。capture しながら降ろす経路が要る
- **trigger** `—`。driver 側の soft trigger は効くはずだが未確認。P4 の hardware trigger を BeagleLogic の語彙へ写すのは別問題
- **160 Msps を PulseView へ出す** `—`。driver の rate list が 100 MHz までなので、**rate を偽って渡す**しかない
- **`.sr` の chunk 順序** — `srzip` は `logic-1-1` … `logic-1-10` … と分割するので、**文字列順に並べると継ぎ目で偽の edge が出る**。数値順に並べること(本実験で一度踏んだ)

## 影響

- [PulseView / sigrok 連携](../../references/pulseview-integration.ja.md) — **経路Bの「未確認」が埋まった**。protocol の実体と2つの落とし穴を反映する
- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) — host 側の出口が `.sr` に加えて **PulseView 直結**になった

## 訂正 — 「PARLIO は 160 MHz ÷ 整数」は誤り(2026-09-13)

候補に「PARLIO は 160 MHz ÷ 整数なので 80 MHz が上限」と書いたが、**PARLIO RX の分周器は integer(1〜256)+ numerator / denominator の分数分周**である(`parlio_ll_rx_set_clock_div()` が `hal_utils_clk_div_t` の 3 フィールドを書く。source の既定は `PLL_F160M` = 160 MHz)。

**実証**: [E078](../e078_p4_continuous_stream/README.ja.md) / [E084](../e084_p4_transfer_tuning/README.ja.md) が **86 / 90 / 94 MHz で周期 860 / 900 / 940 をちょうど**出している。整数分周なら 80 MHz へ丸められて周期 800 になっていたはずで、そうなっていない。

**80 MHz を選ぶ理由は driver 側の list 上限(100 MHz)だけ**であり、PARLIO 側の制約ではない。
