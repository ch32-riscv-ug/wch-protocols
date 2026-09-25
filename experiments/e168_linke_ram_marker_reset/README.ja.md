# E168 LinkE の操作で target の電源が落ちたか・reset されたかを RAM の目印で判定する

状態: **完了(2026-09-25、TX / RX つきの配線)**。TX / RX を外した試験は、配線の変更待ち(LEDGER の候補 `linke-special-erase-with-target`)。

## 問い

E165〜E167 で、LinkE 2.22 は `probe power 3v3 / 5v` でも特殊消去でも 3V3 を切らず(E167、target なし)、RST 未接続の X035 に reset の証拠も無かった。これを、つながった target の側から直接判定する。

**RAM は reset では消えず、電源が落ちると崩れる**ので、RAM の目印で電源の断を、app の loop counter と DMSTATUS の havereset で reset / 再起動を見分ける。

## 手順

- 機材: WCH-LinkE fw 2.22 `FC928F068181` → CH32X035C8T6。
  - 外部との接続は 3V3・SWDIO・SWCLK・TX・RX・GND で、RST は未接続。3V3 は LinkE から。
  - app は E162 の clockwatch(core の既定の clock)。loop counter `cw_seq` は `0x2000000c` にある。
- [`ram_marker.py`](ram_marker.py) の流れ(操作ごとに clockwatch を焼き直してから行う):
  1. `0x20003000` に 256 byte の目印(固定 seed の乱数)を `ch32rv write` で書く。読み戻して確かめ、`cw_seq` も読む。
  2. 1 つの USB session の中で、次を続けて送る。
     - SetSpeed。
     - 操作(何もしない / `81 0d 01 0a` → 1 s → `09` / `81 0d 01 0c` → 1 s → `0b` / 特殊消去 `81 0d 02 0f 0d`)。
     - 0.3 s 待つ。
     - AttachChip。
     - DMSTATUS(havereset = bit 18/19)を読む。
     - abstract memory read で `cw_seq` と目印の先頭 word を読む。
  3. `ch32rv read` で目印の 256 byte と `cw_seq` を読む。

## 結果(TX / RX つき)

| 操作 | RAM の目印 | DMSTATUS(操作の後の AttachChip の直後) | `cw_seq`(前 → 後) |
|---|---|---|---|
| 何もしない | 256/256 一致 | `0x00000382`(havereset なし) | 125,450 → 393,920 |
| 3V3 off → 1 s → on | 256/256 一致 | `0x00000382` | 126,966 → 397,344 |
| 5V off → 1 s → on | 256/256 一致 | `0x00000382` | 127,151 → 396,510 |
| 特殊消去(応答 `82 0d 01 0f`) | 256/256 一致 | `0x00000382` | 126,466 → **131,938 で止まる**(その後も同じ) |

- どの操作でも、RAM は残り、havereset は立たず、`cw_seq` は 0 からやり直していない。**電源は落ちておらず、reset もされていない。**
- 特殊消去では、`cw_seq` が約 5,500 進んだところ(操作から約 20 ms)で止まった。LinkE が halt を取った時点で app が止まり、そのまま flash が消されたと読める。
- 生の値と USB の往復は `out/withuart_*.json`。

## 止まった状態での試験([`stopped_state.py`](stopped_state.py))

ユーザーの提案で、X035 を E164 の止まった状態に落としてから、同じ判定をした。

- **対照**: 目印を書く → 既定の clockwatch を `ch32rv flash --reset run` で焼き直す → 目印を読む。256/256 残った(`out/control.json`)。書込みの stub(`0x20000000`〜`0x20002800`)も、app の起動処理も、`0x20003000` の目印を壊さない。
- **本番**(`out/stopped_1.json`):
  1. 目印を書く。
  2. HPRE `1001` の clockwatch を焼く。
  3. `target info` で止める(UID `-`)。
  4. 同じ USB session の中で、特殊消去を応答が `0f` になるまで最大 4 回送る → **4 回とも `82 0d 01 00`(失敗)**。
  5. AttachChip は成功し、DMSTATUS は **`0x000c0382`(havereset が立っている)**。
  6. abstract memory read で読んだ目印の先頭 word は `d4a418ca` で、書いた値と一致した(abstractcs は `cmderr 6` を示していた)。
- **復旧**: 別の USB session で特殊消去を送ると、1 回目 `00`、2 回目 `0f` で戻った。その後、**目印は 256/256 残っていた**(`out/stopped_1.after_recovery.bin`)。

読み取れること:

- 止める → 特殊消去の失敗 5 回 → 成功 → 復旧、の間ずっと RAM が残っていた。**今の配線では、電源は一度も落ちていない。**
- それでも、止まった状態では havereset が立った。**電源が落ちないまま reset がかかっている。** RST は未接続なので、出どころは debug 経由(化けた DMCONTROL の書込みに ndmreset が乗る、など)か、target 自身のどちらか。
- 「1 回目は `00`、次で `0f`」は、次の流れで説明できる(推定)。
  1. 止まった状態(ACTLR が `0xffffffff` など)では、LinkE は halt を取れず `00` になる。
  2. その間に何かの reset で app が起動し直し、普通に動く状態に戻る(HPRE `1001` のままでも、LinkE が接続しなければ止まらない)。
  3. 次の特殊消去では halt が取れて `0f` になる。

## 結論(現時点)

- TX / RX がつながった状態では、LinkE 2.22 の `probe power 3v3 / 5v` も特殊消去も、X035 の電源を落とさない。正常な X035 は reset もされず、特殊消去は「halt → 消去」である。
- 止まった X035 では、電源が落ちないまま reset(havereset)がかかる。出どころは未確定。
- 残る穴は、TX / RX からの回り込み(back-power)で RAM が保たれた可能性。E167 で、LinkE の 3V3 出力は target なしでも切れていなかったので、可能性は低い。ただし、つながった target では確かめていない。

## 未決

- (TX / RX を外す試験は不要と判断した。E167 で LinkE の 3V3 出力そのものが切れないため、back-power を疑う理由が無い。)
- 止まった X035 の reset の出どころ。E165 の 2c で過去の収録を洗い直すと、線上に reset 操作(ndmreset・PFIC)は無く、時刻は特殊消去の 136〜142 ms に絞れた。LinkE が繰り返す線の初期化か、target 自身かは未確定。
