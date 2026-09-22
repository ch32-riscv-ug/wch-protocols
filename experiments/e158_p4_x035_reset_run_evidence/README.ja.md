# E158 ESP32-P4 → CH32X035: debug reset 後に「user code が走っている」証拠は DM から取れるか。UART 無応答の回は何が違うか

状態: 完了（2026-09-22）

## 問い

OEP v0 probe（oep-probe-arduino `Ch32Dm::reset()`: halt → ndmreset → 解放確認 → dmactive 再投入 → running 待ち → 線解放 → 再 attach）で
X035F8U6 を reset したとき、

1. **DMSTATUS だけを見る reset（confirm=0）** は、100 回中何回 sketch の UART banner（`<name> READY`）が出ないか。
2. banner が出ない回、hart は何をしているか — halt できるか、dpc はどこか（flash 内の user code か、0 か）、`ch32_millis_counter` は進むか、
   RCC / USART4 / GPIOB / FLASH の register は banner が出た回と何が違うか。
3. **PC sample で確認する reset（confirm=1: 解放後に attach → halt → dpc → resume を最大 3 回、失敗なら reset 列をやり直す）** にすると、
   banner 欠落は 0/100 になるか。ならないなら「確認は通ったが banner が無い」回の register 差分は何か。

## 仮説

- confirm=0 は 1〜3/100 で banner が出ない（これまでの 43/44、runner 14/14 の残り）。
- banner が出ない回は halt が成功し dpc は flash 内（過去の hang_pc 観測: `loop()` 内）で、**hart は走っている**。違いは UART/GPIO/RCC 側の register にある
  （debug reset が power-on reset と同じ初期状態を作っていない候補: `RCC_RSTSCKR` の reset 源 flag、`RCC_CFGR0` の clock 選択、USART4 `BRR`/`CTLR1`）。
- confirm=1 の PC sample は「hart が走っている」証拠にはなるが、banner 欠落を 0 にはしない（原因が hart 停止ではないなら）。

## 反証条件

- banner 欠落回で halt が失敗する、または dpc が 0 / boot ROM に留まる → 「hart が走らない」が正しく、PC sample が完了条件として妥当。
- confirm=0 で 100/100 banner → 残存不良は再現せず、LA 観測（候補 `x035-ndmreset-hart-not-running`）へ。

## 方法

probe firmware は oep-probe-arduino の `examples/Esp32P4X035Probe` そのもの（本 dir の `.ino` は include shim、`sketch.yaml` の `libraries: dir:` で
隣の checkout を指す）。pytest-embedded-arduino-cli が **毎回 build して転送**してから測る。host 側は oep-client-python（`.env` の `TEST_OEP_CLIENT_SRC`）。

1. DUT sketch は ArduinoCore-CH32 `tests/sketches/basic/core_api` を `CH32_SERIAL_DEFAULT=4` で build（`oep_smoke.build` を流用）。ELF から
   `ch32_millis_counter` の address を取る。`program_image` で転送・CRC 検証。
2. fixture.uart を rx=12 / tx=6 で lease、115200。
3. 1 cycle = `target.control reset(mode=0, confirm=C)` → banner を 600 ms 待つ → halt → dpc / mcause / mepc / mstatus / mtvec と
   `ch32_millis_counter`、`RCC_CTLR/CFGR0/APB1PCENR/APB2PCENR/RSTSCKR`、`USART4 STATR/BRR/CTLR1`、`GPIOB_CFGLR`、`FLASH_ACTLR` を読む → resume →
   banner 欠落回だけ、さらに 600 ms 待ち（halt/resume で動き出すか）→ confirm=1 の reset で回復を試みる。
4. Phase A: C=0 × 100。Phase B: C=1 × 100。
5. 各 cycle を 1 行（`CYCLE phase= i= ok= flags= attempts= pc= banner_ms= dpc= millis= ...`）と JSON（`cycles.json`、`_runs/` に退避）で残す。
   Phase ごとに `SUMMARY phase= banner=n/100 confirmed= recovered= reset_ms_median=`。
6. 終了時 reset（confirm=1）して target を走らせたまま終わる。

## 対象外

LA 観測（線上で ndmreset の何が違うか）。他の reset 手段（PFIC SYSRST、NRST pin）。

## 必要な環境

profile `esp32p4_x035`（fixture P4 `30eda0e31108` + X035F8U6、GPIO2/54、USART4 PB0/PB1 ↔ GPIO12/6）。flash は `core_api` image で上書きする（保全不要）。

## ベンチ種別

一時（既設 fixture、配線変更なし）。

## 記録する数値

CYCLE 行（上記）、SUMMARY 行、`cycles.json`。

## 完了条件

Phase A / B の SUMMARY と、banner 欠落回（あれば）の register 差分表。

## 結果

状態: 完了（2026-09-22）。run: `_runs/E158_20260922T025334Z_default/`（firmware = 確認を「PC が読めた」だけで通す版）、
`_runs/E158_20260922T025541Z_default/`（確認を「PC ≠ 0」に締めた版。以降の oep-probe-arduino はこれ）。probe firmware は
oep-probe-arduino `examples/Esp32P4X035Probe`（esp32 3.3.12 pin）、DUT は ArduinoCore-CH32 `core_api`（9,780 byte、`CH32_SERIAL_DEFAULT=4`）。
attach 時の実測 SWCLK（`max_clock_hz`）は 6.29 / 6.42 MHz（half 0 ns）。

| phase | run 1 banner | run 2 banner | 確認 | 「駐留」検出（flags bit2） | reset 所要 median |
|---|---:|---:|---:|---:|---:|
| A `confirm=0`（DMSTATUS だけ） | 96/100 | 98/100 | — | — | 66 / 59 ms |
| B `confirm=1`（PC sample） | 100/100 | 100/100 | 100/100 | 5 / 5 | 76 / 64 ms |

banner が出なかった 6 cycle（A 4 + 2）はすべて同じ状態だった:

| register | banner あり（196 cycle 全て） | banner なし（6 cycle 全て） |
|---|---|---|
| dpc | flash 内（`0x1a4`〜`0xf28`） | **`0x00000000`** |
| mtvec / mstatus | `0x3` / `0x88` | **`0x0` / `0x0`**（reset 値） |
| RCC_CFGR0 / APB1PCENR / APB2PCENR | `0x00` / `0x80000` / `0x9` | **`0x50` / `0` / `0`**（reset 値） |
| USART4 BRR / CTLR1、GPIOB_CFGLR、FLASH_ACTLR | `0x1a1` / `0x202c`、`0x4444448b`、`0x2` | **`0` / `0`、`0x44444444`、`0`**（reset 値） |
| RCC_RSTSCKR | `0x10000000` | `0x10000000`（同じ） |
| `ch32_millis_counter` | 進む | 前回 run の残り値（RAM は消えない） |

つまり **hart は reset vector（PC 0）に駐留して 1 命令も実行していない**。DMSTATUS は allrunning=1。この 6 cycle 全部で、diagnostic の
halt → resume の直後 1.3〜11 ms で banner が出た（`AFTER=`）。run 1 の phase B では PC sample が `0x00000000` を返した cycle が 5 回あり、
その resume が hart を解放して banner が出ていた（確認は通ったが証拠ではない）。

## 事実 / 候補 / 未決

- **事実**: P4 probe の debug reset 列の後、X035F8U6 の hart は **約 3〜5 %**（6/200）で reset vector に駐留する。DMSTATUS は running を返す。
  UART / clock / GPIO の問題ではない（周辺 register は全て reset 値、1 命令も走っていない）。
- **事実**: 駐留した hart は **haltreq → resumereq で 100 %（15/15: diagnostic 6 + phase B 5 + run 2 phase B 5 の再 sample）解放**される。
  線解放 → 再 attach（従来の回復、43/44）より確実。
- **事実**: 「halt できて dpc が読めた」は証拠にならない（駐留中でも halt は成功し dpc=0 が読める）。**dpc ≠ 0** が証拠。
- **候補（採用: oep-probe-arduino）**: target.control reset は `confirm=1` で、解放後に attach → halt → dpc → resume を行い、dpc=0 なら
  「駐留を解放した」（flags bit2）として再 sample、dpc≠0 で完了（bit1）。200/200。
- **未決**: なぜ ndmreset 後に駐留するか `—`（DM 側の hart reset 解放条件か、haltreq/ndmreset の順序か）。LA で SWCLK/SWDIO を見ても
  DM 内部の状態は見えないので、DMCONTROL の書込み順序を変える実験（`x035-ndmreset-hart-not-running` を「原因」の問いに改題）で追う。

## 反映

台帳 §1 に E158、候補 `x035-ndmreset-hart-not-running` を「原因」の問いへ改題。oep-spec registry の target.control reset（`confirm`、
`flags/attempts/pc`）、guidelines §6-8「完了条件は証拠」。oep-probe-arduino `Ch32Dm::reset(confirm)`、rebuild plan 5/6 完了。
