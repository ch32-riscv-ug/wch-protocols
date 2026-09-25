# E166 host だけで特殊消去の窓を作れるか(3V3 の off/on と、AttachChip なしの DmiOp)

状態: **完了(2026-09-25)**

## 問い

ch32rv の recover の phase 2(消去せずに直す)は、LinkE の特殊消去(`81 0d 02 0f 0d`)の中でしていることを、host が自分で行う必要がある。E165 で見た中身は、次の 2 つだった。

- target の電源を切って入れ直す。
- 起動直後に haltreq を送り続ける。

これを host から行えるかを、2 点で確かめる。

1. `probe power 3v3 off/on`(`81 0d 01 0a` / `81 0d 01 09`)で、target の電源は本当に切れるか。
2. AttachChip が成功していない状態(SetSpeed の後)でも、DmiOp は target に届くか。

## 手順

- 機材: WCH-LinkE fw 2.22 `FC928F068181` → CH32X035C8T6。LinkE の 3V3 から給電され、外部との接続は UART・RVSWD・3V3・GND だけ。
- [`power_window.py`](power_window.py) を使った。
  - `power`: 既定の clock の clockwatch(E162)を動かしたまま、UART を読みながら 3V3 を off にし、1.15 s 後に on にする。loop counter(`CW <seq>`)が途切れるか、0 からやり直すかを見る。
  - `dmiop`: SetSpeed(family `0x0d`)→ 3V3 off → 0.15 s → 3V3 on → DmiOp で dmcontrol ← `0x80000001` と DMSTATUS の読出しを繰り返す。`--attach-first` を付けると、その前に AttachChip を送る。
- USB の往復は `out/*.json`。

## 結果

1. **3V3 off で電源は切れない。**
   - off の間も UART は流れ続けた(before 11 行 / off の間 12 行 / on の後 21 行)。
   - loop counter は 184164 → 217505 … → 417556 と続けて増え、再起動していない。
   - 応答は `82 0d 01 0a` / `82 0d 01 09` で、どちらも受理されている。
   - E167 で、target をつながない LinkE でも 3V3 の出力がこの命令で切れないことを確かめた(back-power のせいではない)。
   - E165 の特殊消去では、DMSTATUS に havereset(`0x000c0382`)が立ち、target は reset されていた。LinkE は特殊消去の中で、`probe power 3v3` とは別の方法で電源を切るか reset している(方法は未確認)。
2. **AttachChip なしの DmiOp は target に届かない。** SetSpeed の後の DmiOp の応答は、data `0xffffffff`、status `3` だった。RedetectChip だけの状態(2026-09-25 fixture の `dmiop_*`)と同じ。AttachChip を先に送ると、DMSTATUS `0x00000382`、CFGR0 `0`、ACTLR `2` と正しく読めた。
   - script の halt 判定は、`0xffffffff` の bit 9 を halt と誤って数えた(結果の `halted_after_iterations: 0` は無効)。

## 結論

LinkE fw 2.22 では、host が「電源の入れ直し + 起動直後の haltreq」を自分で行うことはできない(3V3 は切れず、AttachChip なしの DmiOp は届かない)。起動直後の窓を使えるのは、LinkE の特殊消去の中だけで、そこでは flash が消える。したがって「消去せずに直す」は LinkE 単体ではできない。NRST の配線か、target の電源を host が切れる配線が要る。

## 未決

- 特殊消去の中で LinkE がどうやって target を reset しているか。E165 の 2b で 3V3(GPIO13)を撮ったが、off/on の間も特殊消去の間も digital では常に 1 だった。電圧の測定が要る。
- 失敗した AttachChip の直後の DmiOp(この試験では、成功した AttachChip の有無だけを比べた)。
