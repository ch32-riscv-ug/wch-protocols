# E168 LinkE の操作で target の電源が落ちたか・reset されたかを RAM の目印で判定する

状態: **基準(TX / RX つき)まで完了(2026-09-25)**。TX / RX を外した試験は、配線の変更待ち(LEDGER の候補 `linke-special-erase-with-target`)。

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

## 結論(現時点)

- TX / RX がつながった状態では、LinkE 2.22 の `probe power 3v3 / 5v` も特殊消去も、X035 の電源を落とさず、reset もしない。特殊消去は「halt → 消去」である。
- 残る穴は、TX / RX からの回り込み(back-power)で RAM が保たれた可能性。E167 で、LinkE の 3V3 出力は target なしでも切れていなかったので、可能性は低い。ただし、つながった target では確かめていない。

## 未決

- TX / RX を外し、3V3・GND・SWDIO・SWCLK だけで同じ 4 つを繰り返す(配線の変更待ち)。
