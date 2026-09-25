# E164 HPRE を変えた X035 に WCH-LinkE で接続すると target が止まる条件

状態: **完了(2026-09-25)**

## 問い

E162 で、HPRE を /2 にして動かした X035C8T6 に `target info` で接続した後、target が応答しなくなった。そのときは USER option byte が `0x07` で、RST_MODE が `00` だった。

- 同じことは再現するか。
- option byte は接続で変わるか。
- 速度設定、分周比、HPRE の field の値のどれで決まるか。
- X035 に固有か。

## 手順

- 機材: WCH-LinkE fw 2.22 `FC928F068181` → CH32X035C8T6(UID `1ff9abcd880ebc48`)。
  - X035 は LinkE の 3V3 から給電される。外部との接続は UART・RVSWD・3V3・GND だけで、NRST は未接続。
  - capture の配線の外なので、線は見ていない。
  - L103 は LinkE `0E028F0692F1`。
- sketch は E162 の [`clockwatch.ino`](../e162_linke_attach_clock_uart/clockwatch/clockwatch.ino)。`-DCW_HPRE=<field> -DCW_HDIV=<分周>` を付けると、起動時に CFGR0 の HPRE を書き換え、USART1 の BRR を合わせ直す。image は `clockwatch-CH32X035-hpre*.bin`。
- [`sequence.py`](sequence.py) の流れ:
  1. `ch32rv flash --reset run` で書く。
  2. 接続の前に UART を 2 s 受ける(`/run/board-identify/by-id/wch-link-<serial>`)。
  3. `target info` を 2 回行い、そのたびに UART を 2 s 受ける。
- 復旧: E162 の特殊消去(`81 0d 02 0f 0d`)を送り、応答が `82 0d 01 0f` になるまで繰り返す(`../e162_linke_attach_clock_uart/raw_seq.py`)。
- option byte は、試験の前と、止まった後の特殊消去の窓の中で読んだ(`x035_fix_user.py` の読むだけの mode)。

## 結果

| 起動時の HPRE(field / 分周) | 接続の速度 | 接続前の UART | 1 回目の `target info` | その後 |
|---|---|---|---|---|
| `1000` / 2 | high | 正常(CFGR0 `0x80`) | chip ID は正しいが、ESIG が読めない(UID `-`) | **UART が止まり、2 回目は no target** |
| `1000` / 2 | low | 正常(CFGR0 `0x80`) | UID `00001fffffffffff`、flash 0 KiB(化ける) | **同上** |
| `1001` / 4 | high | 正常(CFGR0 `0x90`) | UID `ffffffff00000000`、flash 65535 KiB | **同上** |
| `0001` / 2 | high | 正常(CFGR0 `0x10`) | 正常 | 動き続ける(UART は clock の変化で化ける)。2 回目も正常 |
| `0101` / 6 | high | 正常(CFGR0 `0x50`) | 正常 | 同上(09-11 fixture と同じ条件) |

- **止まるのは、HPRE の最上位 bit(CFGR0 の bit 7)が 1 のとき。** 分周比ではない(同じ /2 でも `0001` は止まらず、`1000` は止まる)。速度設定にもよらない(接続時の区間は設定に関係なく約 475 kHz)。
- 止まったときの USB: AttachChip は成功し(48 ms、chip ID `0x03510601`)、直後の ChipInfo は ESIG の部分がすべて 0 だった(`out/10_high_attach0.ndjson`)。AttachChip の中の処理(clock の組み直し)で target が止まったと見られる。
- **option byte は変わらなかった**。試験の前も、止まった後の窓の中でも `e01f5aa5 ff00ff00 00ff00ff 00ff00ff`(USER `0x1f`)。E162 のときに USER が `0x07` だった理由は、この試験では説明できない(接続で option byte が書き換わる証拠は出なかった)。
- 止まった後の特殊消去の窓では、DMSTATUS は `0x000c0382`(reset の直後で halt 中)だが、abstract memory read は `cmderr 6` で失敗した。特殊消去の応答は、1 回目が `82 0d 01 00`(約 2.1 s)で、消去されていなかった。2 回目は `82 0d 01 0f`(約 0.2 s)で消去され、以後は通常どおり接続できた。E162 の復旧でも同じ「1 回目 `00`、2 回目 `0f`」の形だった。正常な X035 では、power-off 消去は 1 回目で `0f` を返した(0.2 s。ch32rv-c8 セッションの測定)。「1 回目は `00`」は、止まった状態に固有と見られる。
- **L103 では起きない**。HPRE `1000`(CFGR0 `0x80`)で動く L103 に接続しても、target は動き続けた(UART は clock の変化で化ける)。

## 結論

- WCH-LinkE fw 2.22 の AttachChip は、CFGR0 の HPRE の bit 3(CFGR0 の bit 7)が 1 の状態で動いている X035 を止める。その後は、LinkE の電源を切って入れ直さない限り(特殊消去で可)接続できない。原因の線上の操作は見ていない。
- 回避: X035 のアプリは HPRE に `0xxx` の符号化を使う(`/2` なら `0001`)。
- ch32rv など host 側の対処の候補: 止まっても、特殊消去(flash は消える)で戻せる。応答が `0f` になるまで繰り返す。

## 未決

- LinkE が X035 の CFGR0 に何を書いて止めるのか(線を見るには、X035 を capture の配線につなぐ必要がある)。
- E162 の USER `0x07` の由来。
- 止まった状態で、特殊消去の 1 回目が `00` で失敗し、2 回目で成功する理由(正常な状態では 1 回目で成功する)。
