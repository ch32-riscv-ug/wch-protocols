# E165 X035 が止まる接続・特殊消去の窓・V103 の long 形式を線で見る

状態: **完了(2026-09-25)**。3V3 の線は GPIO13 につながっていた(ユーザーの確認)。収録では GPIO10 を 3V3 として撮ったので、電源の切断と投入の時刻は見えていない。

## 問い

- E164 で「HPRE の bit 3 が 1 の X035 は、WCH-LinkE の接続で止まる」ことが分かった。線上で LinkE は何を書いているのか。
- 止まった X035 を戻す特殊消去(`81 0d 02 0f 0d`)で、LinkE は電源を入れた後に何を送るのか(ch32rv の recover の設計の材料)。
- long 形式に応答する target(V103)では、接続がどう続くのか。

## 手順

- 配線(ユーザーが 2026-09-25 に付け替え)。ESP32-P4 `esp32-series-30eda0e343c6` の OEP logic capture に、次をつないだ。
  - GPIO12 = SWCLK、GPIO11 = SWDIO: X035C8T6(LinkE fw 2.22 `FC928F068181`、LinkE 3V3 給電)。
  - X035 の 3V3 は **GPIO13**(ユーザーの確認)。収録では誤って GPIO10 を 3V3 として撮り、GPIO10 はずっと 0 だった(何もつながっていない)。
  - GPIO14 = SWCLK、GPIO15 = SWDIO: CH32V103R8T6(**CH549 の WCH-Link fw 2.12** `434A124C5596`。LinkE ではない)。
  - どれも probe 側で分岐した。pin の割当ては、`out/00_pins_*` の edge の数から決めた。
- 収録 script は [`linke_cap.py`](linke_cap.py)。E163 の写しで、OEP v1 の新しい wire に合わせて subscribe を `cap.subscribe(0, 0)` にした。client は `oep-client-python` の `fd05776`。
- 生のコマンドは [`rawcmd.py`](rawcmd.py) で送った(`CH32RV=rawcmd.py`)。
- 止める sketch は E164 の `clockwatch-CH32X035-hpre9-div4.bin`(HPRE `1001`、CFGR0 `0x90`)。
- 解析は `captures/tools/rvswd.py`(START/STOP 区切り、`clk_bit` / `dio_bit` で線を選ぶ)。

## 結果

### 1. X035 が止まる接続(`out/10_x035_hpre9_stop`、50 MHz)

AttachChip の中の memory access(abstract command を組にしたもの)を、正常な接続(`out/00_pins_x035`、core の既定の clock)と並べた。

| 順 | 正常(CFGR0 `0x50`) | 止まる(CFGR0 `0x90`) |
|---|---|---|
| 1 | CTLR を読んで同じ値を書く | 同じ |
| 2 | CFGR0 を読む `0x50` → **書く `0x50`** | CFGR0 を読む `0x90` → **書く `0xd0`** |
| 3 | CFGR0 を読む `0x50` → 書く `0x50` | 読む `0x6a8e5694`(化けている)→ 書く `0x80000000` |
| 4 | ACTLR を読む `0` → 書く `0x10` → 読む `0` → 書く `0x2` | 読む `0x76572852` → **書く `0xffffffff`** → 読む `0x97656894` → **書く `0xffffffff`** |
| 5 | CFGR0 ← `0`、FLASH_CTLR ← `0x8080`、STATR ← `0xB020`、ESIG を読む | ESIG の読出しが化ける(ch32rv は UID `-`) |

- LinkE は、読んだ CFGR0 に **bit 6 を立てて**書き戻していると読める(`0x50` → `0x50`、`0x90` → `0xd0`)。これは推定で、`0x40` を OR するのか `0x50` を OR するのかは、この 2 例では区別できない。
- HPRE が `0xxx` なら、結果は `01xx`(/5〜/8)にとどまる。`1xxx` だと `11xx` になり、X035 の HPRE では /32〜/256(`1101` は /64)なので、HCLK は約 750 kHz 以下に落ちる。
- そのあとの DMI は化けた値を読み書きし、**ACTLR(flash の wait)に `0xffffffff`、CFGR0 に `0x80000000` を書き込んで target を止める**。
- 化けた frame は parity で 12 個検出された。LinkE はそれでも処理を続けていた。
- E162 で USER option byte が `0x07` になっていたのも、この化けた書込みが FLASH の register に当たった結果である可能性が高い(推定。E164 と E165 では option byte は変わらなかった)。

### 2. 止まった X035 への特殊消去(`out/31_x035_special_erase_50m_1`、50 MHz。`out/21〜23` は 25 MHz)

1 回目から `82 0d 01 0f` が返った(229 ms。25 MHz の 3 本も 1 回目から `0f`、207〜255 ms)。E162 と E164 で見た「1 回目は `00`」は、今回は起きなかった。

線上の流れ(時刻は収録の開始から):

1. 約 0〜120 ms は線が静か。電源を切っている区間と見られるが、3V3 が見えないので未確認。
2. **起動直後の haltreq**: 約 5.6 ms の塊を、約 11 ms 間隔で送る。中身は dmcontrol ← `0x80000001` と dmstatus の読出しの繰り返しで、最初の約 20 ms は parity の不一致が多い(target の debug module がまだ安定していない)。
3. 144.26 ms に DMSTATUS `0x00000382`(halt)を確認した。最初の塊から約 24 ms。app が HPRE を書き換える前に止めていると見られる。
4. halt した後の操作:
   - FLASH_CTLR ← `0x8080`、STATR ← `0xB020`。
   - KEYR / MODEKEYR の解錠。
   - CTLR ← MER(`4`)→ MER|STRT(`0x44`)。
   - STATR の BUSY を 288 回 poll する。
   - **この消去をもう一度くり返し**、最後に CTLR ← 0。
5. OBR・WPR と `0x08000400` を読む(消えたことの確認)。そのあと、続けて送った AttachChip の通常の列(long 形式の問い合わせ 202 回から)が始まる。

### 3. V103 + CH549 WCH-Link の接続(`out/00_pins_v103`)

- **CH549 Link は、すべての DMI を long 形式(START + 85 clock + STOP)で送る**。53 frame すべてがこの形で、short 形式は 1 つも無い。
- target 位相の番地は、host の番地をそのまま返す(53/53)。
  - write では、target 位相の data は host の data と同じ(39/39)。
  - read では、target 位相に読んだ値が載る(DMSTATUS `0x00000c82` / `0x00000382`、chip ID `0x2500410f` など)。
  - status はすべて `00`、最後の clock はすべて `0`。
- **表で parity とされている 2 bit は、parity として働いていない**。
  - host 位相の bit 41 は常に `0`。LinkE の接続時の問い合わせの frame では `1` だった。
  - target 位相の bit 83 は、write で常に `0`、read で常に `1`。
- LinkE と違い、接続時に RCC は触らない(先頭の DMI 列にそれらしい書込みは無い)。chip ID は、program buffer の routine(`lw`)で `0x1FFFF884` などを読む。

## 結論

- LinkE 2.22 は、X035 の接続中に「読んだ CFGR0 の bit 6 を立てて書く」らしい。HPRE が `1xxx` だと HCLK が /32 以下に落ち、以後の DMI が化けて ACTLR や CFGR0 に誤った値を書き、target を止める。**回避は、X035 の app で HPRE に `1xxx` を使わないこと**(E164 と同じ結論に、仕組みが加わった)。
- 特殊消去は、電源の投入の直後に haltreq を約 11 ms 間隔で送り、約 24 ms で halt を取ってから消去する。ch32rv の recover の phase 2(消去せずに直す)も、同じ時間の窓を狙える。
- 資料にある long 形式の「odd / even parity」は、CH549 Link と V103 の組合せでは parity として働いていない(host 側は常に 0、target 側は read / write の印)。link-to-target §3 の表を直す。

## 未決

- 3V3(GPIO13)を撮って、電源の切断と投入の時刻を見る。
- 「1 回目は `00`」が今回起きなかった理由。
- LinkE が CFGR0 に OR するのは `0x40` か `0x50` か(HPRE `1000` / `0011` などで確かめられる)。
- LinkE が long 形式に応答する target(V103 を LinkE につなぐ)で、short 形式に切り替えるかどうか。
