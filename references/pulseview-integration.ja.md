# PulseView / sigrok から P4 の capture を取る経路

状態: **reference**(2026-09-13。**経路Bは[E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md)で実機まで通し、[E080](../experiments/e080_p4_pulseview_gapless/README.ja.md)で継ぎ目を消した。保存用の経路Dは[E082](../experiments/e082_p4_spool_then_convert/README.ja.md)**。手元のlibsigrok 0.5.2で実地確認した部分と、未確認の部分を分けて書く)

**用途で使い分ける** — **PulseViewでliveに見るなら経路B**(1回のcaptureとして継ぎ目なく、86〜90 Mspsまで)、**`.sr`に残すなら経路D**(packedのまま一時ファイルへ受けて、終わってから変換)。**liveで`srzip`へ流し込むと受け側が律速になり、長いcaptureでsampleを落とす**([E080](../experiments/e080_p4_pulseview_gapless/README.ja.md))。

目的は「ESP32-P4で取ったlogic captureを、**PulseViewから直接**、または`.sr`ファイル経由で見られるようにする」。経路は3つあり、**必要な実装量とhost側の前提が違う**。

## 0. 結論の要約

| 経路 | host側の前提 | device側に要るもの | 確認状況 |
|---|---|---|---|
| **A. COM portへSUMP** | **何も要らない**(stock PulseViewの`ols` driver) | USB CDC上でSUMP wire protocolを話す | **未確認**。[E023](../experiments/e023_p4_sump_basic_trigger_80mhz/README.ja.md)〜[E028](../experiments/e028_p4_sump_four_stage_trigger/README.ja.md)でtrigger側は実装済み、**wire互換は未決のまま** |
| **B. TCPでBeagleLogicを演じる** | **何も要らない**(stock PulseViewの`beaglelogic` driver) | PC側にPythonのTCP server。deviceはUSBで繋がっていればよい | **実機まで通した**([E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md)) |
| **C. TCPでSUMPを話す** | **libsigrokをgitから入れる**必要がある | Bと同じ | **現行版では不可**(下記) |
| **D. `.sr`を書く** | 不要(ファイルを開くだけ) | 無し。PC側でzipを作るだけ | **実機まで通した**([E074](../experiments/e074_p4_2ch_capture_to_sr/README.ja.md)) |

**SCPIは選択肢にならない。** sigrokのSCPI supportはoscilloscope / PSU / DMM用で、logic analyzerのdriverはSCPIを使わない。「IPで待ち受けてPulseViewから繋ぐ」を実現するのはBかCで、**stock環境で動くのはBだけ**である。

## 1. 経路A — COM portへSUMP(最短)

[E063](../experiments/e063_p4_usb_hs_enumerate/README.ja.md)で、P4のOTG HS上のCDCはWindowsに`usbser`のCOM portとして生えた(driver追加なし)。PulseViewの`ols` driverはserial portに対して話すので、**そのCOM portでSUMPを話せば、PulseViewは何の追加設定もなく繋がる**。

```console
sigrok-cli --driver ols:conn=COM8 --scan          # Windows
sigrok-cli --driver ols:conn=/dev/ttyACM1 --scan  # Linux
```

`ols` driverのscan optionは`conn`と`serialcomm`の2つだけである(実機で確認)。

残っているのは**SUMP wire protocolの互換範囲**で、これは[E028](../experiments/e028_p4_sump_four_stage_trigger/README.ja.md)の未決に「SUMP wire互換範囲」として既に立っている。SUMPはsample数が24 bit、rateがdivisorで決まるなど表現力に制限があるので、[P4 logic analyzer予備調査](p4-logic-analyzer-investigation.ja.md)の限界matrix全部は載らない。

## 2. 経路B — TCPでBeagleLogicを演じる(IP経由で唯一stockで動く)

libsigrok 0.5.2の`beaglelogic` driverは**TCP modeを持つ**。`conn`が`tcp-raw/<host>/<port>`の形を受け付けることと、接続後の最初のcommandが`version\n`であることを、**手元で実地確認した**(ダミーのPython serverを立て、`sigrok-cli --driver "beaglelogic:conn=tcp-raw/127.0.0.1/5556" --scan`が接続して`version\n`を送ってきた)。

driverが使うcommand語彙は`.so`の文字列から次が読める。

```text
version  memalloc  samplerate  sampleunit  triggerflags  bufunitsize  get  close
```

つまり**PC側にこの数個のtext commandを話すTCP serverを1本書けば、stockのPulseViewが繋がる**。deviceとの間はUSB(CDCでもvendorでも)で、serverが仲介する。

- 利点: host側に何も入れさせない。**Windows / Linux どちらでも同じ**。serverがPythonなので、[E064](../experiments/e064_p4_usb_hs_cdc_rate/README.ja.md)のreaderやchannel詰め替えをそのまま載せられる
- 制約: BeagleLogicのmodelに合わせる必要がある(sample unitは1 or 2 byte、triggerの表現はBeagleLogic流)。**P4側の機能をそのまま出せるわけではない**

### protocolの実体([E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md)で確定)

server実装は[`bl_server.py`](../experiments/e077_p4_pulseview_over_ip/bl_server.py)。

| command | serverが返すもの |
|---|---|
| `version` | **`BeagleLogic`で始まる文字列**(driverは先頭11文字しか見ない) |
| `memalloc` | 10進整数。**要求sample × unit byteがこれを超えるとdriverがcaptureを切り詰める**ので大きく返す |
| `samplerate` / `sampleunit` / `triggerflags` / `bufunitsize` | 10進整数。引数付きなら**`ok`** |
| `get` | **同じsocketにraw sample**。1 sample = `sampleunit`が1なら1 byte、0なら2 byte。**bit n = channel n** |
| `close` | **返答不要。socketを閉じないこと**(下記) |

**driverは「何sample欲しいか」をserverに伝えない。** `limit_samples`はhost側だけに留まり、**必要なbyte数を受け取った時点で`close`を送って読むのをやめる**。したがってserverは**clientが止めるまで送り続ける**。

**刺さる2点。**

1. **`close`を受けてserverがsocketを閉じると`sigrok-cli`がCPU 100%で終わらなくなる。** driverは`close`送出後に25 msのdrainをしてから自分で閉じる。**serverは待つ。**
2. **`numchannels`はdriverのscan optionだが`sigrok-cli` 0.7.2はconn文字列の中で受け付けない。** channelを8以下にする(= sample unitを1 byteにする)には **`--channels P8_45,P8_46`** で index 8以上を無効にする。

```console
uv run python e077_p4_pulseview_over_ip/bl_server.py --source usb --samples 4000000
sigrok-cli --driver "beaglelogic:conn=tcp-raw/127.0.0.1/5556" \
  --channels P8_45,P8_46 --config samplerate=80m --samples 4000000 -o out.sr -O srzip
```

**rateは80 MHzを既定にする** — PARLIOは160 MHz ÷ 整数、driverのlistは100 MHzまでで、両方が正確に表せる最大がここになる。

### 継ぎ目を消す([E080](../experiments/e080_p4_pulseview_gapless/README.ja.md))

batchを繋ぐと継ぎ目に空白が入る。**送出元を[E078](../experiments/e078_p4_continuous_stream/README.ja.md)のstreaming firmwareに替えると、1回の`get`が1回のcaptureになって継ぎ目が消える**([`bl_stream_server.py`](../experiments/e080_p4_pulseview_gapless/bl_stream_server.py)。protocolの実装はE077のものをimportして共有)。

- **86 Msps・64 M sample(0.74秒の連続capture)まで一本で通る**
- **展開はnumpyで行う。** E077のPython loopでは実時間に間に合わない(numpyなら展開後89 MB/s)
- **律速はclientの出力先である。** 256 M sampleを86 MHzで取ると`-O srzip`では欠落し、`-O binary`なら通る。**`.sr`へ落とすなら64 M sample程度まで**
- **serverの`--samples`はclientの要求に合わせる。** 超えると待ち続ける。上限はfirmwareの268,435,456 sample

### 保存が目的なら経路Dへ回す([E082](../experiments/e082_p4_spool_then_convert/README.ja.md))

**`.sr`に残すのが目的なら、liveで`srzip`へ流し込まない。** capture中は**packedのまま一時ファイルへ追記**し、**終わってから展開してzipする**([`stream_to_sr.py`](../experiments/e082_p4_spool_then_convert/stream_to_sr.py))。

- **E080が落ちた条件(86 MHz × 256 M sample)がそのまま通る** — `fifo_overflow=0`、弾性FIFOの占有57 KB、21.48 MB/s
- 変換は**無圧縮0.51秒(245 MB)/ deflate 9.5秒(2.6 MB、94分の1)**。**capture の外なので、どれだけ時間をかけてもsampleには影響しない**
- **liveで見るならE080の経路、残すならこちら**、と用途で分ける

## 3. 経路C — TCPでSUMP(現行libsigrokでは不可)

libsigrokの**serial層にTCPを足す`ser_tcpraw`は0.5.2に入っていない**。手元の`libsigrok.so.4`(0.5.2)には`tcpraw`系のsymbolが1つも無く、`.so`中の`tcp-raw`という文字列は上の`beaglelogic` driver専用のものだった。実際に`ols:conn=tcp-raw/127.0.0.1/5555`を試すと`serial-libsp: Attempt to open serial port with invalid parameters.`で止まる。

したがって**`ols`をIP経由で使うにはlibsigrokをgitから入れる**ことになる。stock環境を前提にするならBを採る。

## 4. 経路D — `.sr`を書く

`.sr`はzipで、`version`(中身は`2`)、`metadata`(INI)、`logic-1-1`以降のdata chunkから成る。**PC側でzipを組むだけ**なので、device側には何も要らない。捨てられないrawを残す用途と、PulseViewで後から開く用途に向く。

channel数が8以下なら1 sample = 1 byteで、bit位置がchannel番号に対応する。**P4のPARLIOは2 channelなら1 byteに4 sampleを詰める**ので、`.sr`へ出す前に**1 sample = 1 byteへ展開する**か、per-channel packingから組み直す必要がある。この詰め替えはPC側で行う。

**読む側の注意**: `srzip`は`logic-1-1`、`logic-1-2`…と**番号付きのchunkに分割する**。`logic-1-10`は文字列順では`logic-1-2`より前に来るので、**名前を文字列順に並べると継ぎ目で偽のedgeが出る**。**数値順に並べること**([E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md)で一度踏んだ)。

## 5. 帯域との関係

[E064](../experiments/e064_p4_usb_hs_cdc_rate/README.ja.md)でUSB HS CDCの実効帯域は約5.6 MB/s。2 channelのPARLIOは1 byteに4 sampleなので**連続streamingは約22.4 Msps相当**が上限になる。`.sr`へ出すときに1 sample = 1 byteへ展開すると**4倍に膨らむ**ので、**展開はPC側で行い、線の上はpackedのまま運ぶ**。

**現在の経路はvendor bulkで実測8.80 MB/s**([E076](../experiments/e076_p4_capture_hs_download/README.ja.md))。[E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md)は4 M sample(packed 1 MB)を**0.45秒**でPulseViewのdriverへ渡している。

## 参照

- [P4 logic analyzer予備調査](p4-logic-analyzer-investigation.ja.md) — 限界matrixと後段の設計
- [E023](../experiments/e023_p4_sump_basic_trigger_80mhz/README.ja.md)〜[E028](../experiments/e028_p4_sump_four_stage_trigger/README.ja.md) — SUMP trigger側の実装と未決
- [E063](../experiments/e063_p4_usb_hs_enumerate/README.ja.md) / [E064](../experiments/e064_p4_usb_hs_cdc_rate/README.ja.md) — HS経路の列挙と帯域
