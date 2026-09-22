# E159 ESP32-P4 → CH32X035: ndmreset 後に hart が reset vector に駐留するのは DMCONTROL の書込み順序で変わるか

状態: 完了（2026-09-22）

## 問い

E158 で、probe の debug reset 列の後に X035F8U6 の hart が約 3〜5 % で reset vector（dpc=0、mtvec=0）に駐留し、DMSTATUS は
allrunning を返すことが確定した。駐留率は reset 列の**どの要素**で決まるか。haltreq を reset 越しに保持する（debug spec の
「haltreq 中に reset → halt して出てくる」）か resethaltreq を使えば駐留は 0 になるか。

## 仮説

- 駐留は「ndmreset 解除の瞬間に hart が run 状態へ遷移する」ところの race で、**haltreq を保持したまま reset を通し、halted で
  出てきたところを resumereq で走らせる**列（`haltreq_through`）と `resethaltreq` 列は 0/100 になる。
- 現行列（`baseline`）の付属要素（dmactive 0→1、再 attach）は駐留率にほとんど効かず（`no_dmactive_cycle` / `no_reattach` ≈ baseline）、
  最小列（`plain`）も同程度。走行中 hart への reset（`running`）は E158 以前の観測どおり数十 % で駐留する。

## 反証条件

- `haltreq_through` / `resethaltreq` でも駐留が出る → race は hart 側の reset 解放にあり、DM の順序では消せない（PC sample 契約が唯一の手段）。
- 全 variant が 0/100 → E158 の駐留は OEP firmware 固有の要因（線の解放タイミング等）で、この sketch では再現しない。

## 方法

E156 の `rvswd_dedic.h`（dedicated GPIO PHY、attach/halt/resume）を使う独立 sketch。X035 の flash は E158 が残した `core_api` のまま（書かない）。

1. `P` で開始。attach → halt → half period の margin check（DMSTATUS 1000 read、E157 と同じ選び方）。
2. 7 variant を round-robin で各 100 cycle。1 cycle = 「reset 列」→ 線解放 → 20 ms 待ち → attach → DMSTATUS 記録 → halt → dpc / mtvec を
   abstract command で読む → resume → 線解放 → 20 ms 待ち。**駐留 = dpc==0 && mtvec==0**（E158 の署名）。
3. variant:
   - `baseline`: oep-probe-arduino `Ch32Dm::resetOnce` と同じ列（halt 済みから: ndmreset → 解除 read-back → dmactive 0→1 → running 待ち（halted なら resume）→ ackhavereset → dmactive 0 → 解放 → 再 attach → 解放）。
   - `no_reattach`: baseline から最後の再 attach を除く。
   - `no_dmactive_cycle`: baseline から dmactive 0→1 を除く。
   - `plain`: ndmreset → 解除 read-back → dmactive 0 → 解放。
   - `haltreq_through`: haltreq を保持したまま ndmreset（0x80000003）→ haltreq 保持で解除（0x80000001）→ allhalted 待ち → ackhavereset → resumereq → resumeack 待ち → dmactive 0 → 解放。
   - `resethaltreq`: setresethaltreq（bit3）→ ndmreset → 解除 → allhalted 待ち → clrresethaltreq（bit2）→ ackhavereset → resumereq → 解放。
   - `running`: 走行中（resume 済み）の hart に ndmreset → 解除 → dmactive 0 → 解放（E158 以前の「約半分」の参照）。
4. `CYCLE variant= i= parked= dpc= mtvec= dmstatus=`（駐留の行だけ全項目、それ以外は集計）、`SUMMARY variant= cycles=100 parked= halt_fail= sample_fail=`。

## 対象外

LA 観測。駐留を解放する手段の比較（E158 で haltreq→resumereq 15/15 は確定）。

## 必要な環境

profile `esp32p4_x035`（fixture P4 + X035F8U6、GPIO2/54）。flash は書かない。

## ベンチ種別

一時（既設 fixture、配線変更なし）。

## 記録する数値

variant ごとの parked / 100、halt 失敗数、sample 失敗数、選んだ half period。

## 完了条件

7 variant × 100 cycle の SUMMARY。

## 結果

状態: 完了（2026-09-22）。採用 run: `_runs/E159_20260922T031805Z_default/`（150 cycle）と `…T032111Z`（300 cycle）。
`…T031526Z`（100 cycle）も同じ測り方で有効。それ以前の 2 run は測定側の不良で無効（下記）。

測定側の教訓（計画外、2 回のやり直し）:

1. **attach は遅い half で行う。** 選んだ half 0 ns のまま `configureBus`（初期化 100 clock）+ dmactive 書込みをやり直すと、sample の 25〜44 % が
   ゴミになった。OEP の PHY が attach だけ half 500 ns にしているのはこのため（`RvswdPhy::attach`）。
2. **half は attach ごとに選び直す。** run 冒頭で 1 回選ぶだけだと DMI の 43 %（76,845 / 179,852）が parity 不一致になった。attach ごとに
   1000 read の margin check をすると sample_fail は全 variant で 0。

| variant | 100 cycle | 150 cycle | 300 cycle | 合計 parked | 備考（300 cycle run の step 失敗） |
|---|---:|---:|---:|---:|---|
| `baseline`（現 `Ch32Dm::resetOnce`） | 4 | 3 | 12 | **19 / 550（3.5 %）** | 解除 read-back 失敗 17 |
| `no_reattach` | 4 | 5 | 34 | 43 / 550（7.8 %） | 26 |
| `no_dmactive_cycle` | 2 | 2 | 17 | 21 / 550（3.8 %） | 13 |
| `plain` | 4 | 3 | 20 | 27 / 550（4.9 %） | 35 |
| `haltreq_through` | **0** | **0** | 7 | **7 / 550（1.3 %）** | halted で出てこない 58、その後 resume 失敗 7 |
| `resethaltreq` | 6 | 6 | 20 | 32 / 550（5.8 %） | halted 待ち失敗 151 / 300 → **X035 は resethaltreq 非対応**とみる |
| `running` | 0 | 1 | 5 | 6 / 550（1.1 %） | 解除 read-back 失敗 19 |

駐留の署名は E158 と同じ（dpc=0、mtvec=0）。駐留 cycle の DMSTATUS は `0x000c0382`（**allhalted + anyhalted + allhavereset/anyhavereset**、
version 2）— 20 ms 経っても「halted」と言っている。E158 で probe の `resetOnce` 終了時に running と読めていたのは、その直前の
resume-if-halted ループが resumeack を返させていたためで、hart は走っていなかった。

**駐留と DMI の乱れは同時に起きる。** 300 cycle run の 2,700 attach のうち half 0 で clean だったのは 1,850（69 %）で、残り 31 % は half 100〜200 ns
が選ばれた。駐留 117 cycle のうち **115（98 %）は half 100〜200 ns の attach**。150 cycle run も同じ（駐留 22 のうち 20 が half 100）。
run ごとの駐留率の違い（1〜11 %）も、その run の half 0 の clean 率と連動している。

## 事実 / 候補 / 未決

- **事実**: DMCONTROL の書込み順序で駐留率は変わる（`no_reattach` 7.8 %、`haltreq_through` 1.3 %）が、**どの順序でも 0 にはならない**（550 cycle）。
  probe 側の完了条件は E158 の PC sample（dpc≠0）のままでよい。
- **事実**: `haltreq_through`（haltreq 保持で ndmreset → halted 待ち → ackhavereset → 明示 resume）は baseline の約 1/3 の駐留率だが、
  300 cycle run では hart が halted で出てこない回が 58 あり、その一部（7）が resume に失敗して駐留した。
- **事実**: `resethaltreq`（DMCONTROL bit 3/2）は X035 では効かない（halted 待ち失敗 151/300、駐留も減らない）。
- **事実**: ndmreset 解除の write が read-back で確認できない回が 4〜12 %（`release_fail`）。OEP の read-back ループはこのために要る。
- **事実（`x035-dmi-parity-intermittent` への追加）**: reset 直後の attach では 31 % が half 0 で clean にならず 100〜200 ns を要した。
  駐留 cycle の 98 % がこれに重なる。target 側の状態（clock か DM の応答 timing）が変わっている候補。
- **候補**: 駐留の原因は「DM が halt 状態のまま hart を reset から出す」経路にあり、probe 側の DMCONTROL 順序では避けられない。**PC sample と
  haltreq→resumereq による解放**（E158）を契約とする。`haltreq_through` は追試で不採用（下記）。
- **未決**: 駐留と DMI 乱れの共通原因 `—`（target の clock 状態か。RCC_CFGR0 を駐留時と走行時で比べる: E158 では駐留時 `0x50`、走行時 `0x00`）。

## 追試: `haltreq_through` を probe firmware に入れて E158 を再測（同日）

oep-probe-arduino `Ch32Dm::resetOnce` を `haltreq_through` に書き換え、E158 を 300 + 300 cycle 回した（`_runs/E158_20260922T032523Z_default/`）。

| phase | banner | 内訳 |
|---|---:|---|
| A `confirm=0` | **150 / 300** | 欠落 150 のうち **駐留（dpc=0）は 26 だけ**。残り **124 は dpc が `loop()` 内、RCC / USART4 は設定済み**なのに banner が無く、`ch32_millis_counter` が 8〜33 で止まっていた（600 ms 後の読出し。走った cycle は 12〜128）。mstatus は両方 `0x88`（MIE=1）、mcause は両方 SysTick |
| B `confirm=1` | **300 / 300** | 駐留検出 26。確認の halt → resume の後に全 cycle で banner |

150 cycle すべてで、diagnostic の halt → resume の 1〜11 ms 後に banner が出た。つまり **reset-halt から resume で走らせた hart は、
約 4 割で「命令は実行するが割込み（SysTick）が届かない」状態になり、次の halt → resume で正常化する**。E159 の指標（dpc≠0）はこの
状態を「駐留でない」と数えるので、E159 の `haltreq_through` 1.3 % はこの失敗を含んでいない。**`haltreq_through` は不採用**、
`resetOnce` は baseline のまま（同 commit へ戻した）。

この結果は契約にも効く: **「PC が flash を指す」だけでは user code が正常に走っている証拠にならない**。E158 の確認手順（走行中の hart を
halt → dpc → resume）は sample であると同時に、割込み停止状態を解く操作でもある。証拠の強さは UART banner（外部観測）> PC sample + resume。

## 反映

台帳 §1 に E159、候補 `x035-ndmreset-hart-not-running` を「原因は DM 順序では消えない、共通原因は clock 状態か」へ更新、
`x035-dmi-parity-intermittent` に reset 直後 31 % の事実を追記。oep-probe-arduino `Ch32Dm::resetOnce` は baseline のまま（`haltreq_through` は E158 再測で不採用）。guidelines §6-8 に「PC sample だけでは不十分、外部観測が上位」を追記。
