# E083 間欠的に 1 channel が死ぬ現象 — 準備順序が原因か

状態: **完了 — 原因は準備順序だった。PARLIO receiver を先に作ると cold boot の 29% で 1 channel が死ぬ。LEDC を先に attach すれば 0%**(2026-09-13)

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E075](../e075_p4_width_sample_accuracy/README.ja.md)(現象の記録)、[E015](../e015_p4_parlio_routing_order/README.ja.md)(LEDC 出力と PARLIO RX 入力は同一 GPIO で共存する)

## 問い

**[E075](../e075_p4_width_sample_accuracy/README.ja.md) が観測した「`overflow=0` のまま特定の 1 channel だけ duty 0.00% になる」現象は、`create_receiver()` と `configure_pwm()` の順序で説明できるか。**

## なぜこの問いか

[E075](../e075_p4_width_sample_accuracy/README.ja.md)は掃引の初回に 3 回この現象を見て、**observation として残したまま未特定**にしていた。

- `overflow` は 0 のままなので、capture 経路の drop とは別
- **死ぬ channel は毎回違う**(D3 / D5)ので特定の GPIO の問題ではない
- **同じ条件を再実行すると再現しない**

疑っていたのは **test firmware 側の準備順序** — `create_receiver()`(PARLIO の GPIO matrix 入力を張る)が `configure_pwm()`(LEDC 出力を attach する)より先に走っており、**`ledcAttach` がその pin の matrix 設定を踏む**のではないか、という筋である。

## 仮説

**順序が原因。** PARLIO を先に作ると起きる、LEDC を先に attach すれば起きない。

## 反証条件

1. どちらの順序でも同じ頻度で起きる(順序は無関係)
2. どちらの順序でも起きない(再現しない)
3. 起きるが `overflow` が非 0(capture 経路の drop だった)

## 方法

[E075](../e075_p4_width_sample_accuracy/README.ja.md)の firmware に、**順序を選べる引数**と**board 上での lane 判定**を足した。

- `run_capture(bytes, rate, order)` — **order 0 = receiver 先**([E075](../e075_p4_width_sample_accuracy/README.ja.md)と同じ)、**order 1 = LEDC 先**
- 各 trial の頭で全 lane に `gpio_reset_pin()`
- **board 上で lane ごとの立ち上がり edge を数える。** 0 本なら「死んだ」。download しないので 1 trial が数 ms
- **boot 直後の 1 回**を自動で取り、結果を console へ**繰り返し出す**(reset 後に host が port を開き直すまで待てるように)

### cold boot を数える必要があった

**session 内で何回繰り返しても再現しない。** 同一 boot 内で 200 trial(各順序 100)回して **0 件**。`esp_restart()` による再起動 10 回でも **0 件**。

**再現するのは hard reset の直後の 1 回だけ**だった。そこで `esptool --after hard-reset flash-id` で hard reset をかけ、**boot ごとに 1 標本**を取る形にした。

## 対象外

- 2 / 8 channel(本実験は 4 channel、160 MHz)
- 外部信号(信号源は内部 LEDC)
- 原因のレジスタ単位の特定(IO MUX / GPIO matrix の読み出し)

## 必要な環境

- ESP32-P4 `esp32-p4-30eda0e31478`、外部配線なし
- `esptool`(hard reset 用。arduino15 に同梱のもの)

## ベンチ種別

board(単体、内部 PWM を信号源とする)

## 完了条件

**順序ごとに cold boot を 20 回以上集め、死んだ channel の数を並べる。**

## 結果

4 channel / 160 MHz / 65,536 byte、boot 直後の 1 回のみを標本とする。

| 順序 | cold boot | **1 channel が死んだ回数** | 死んだ lane |
|---|---:|---:|---|
| **order 0 — receiver → LEDC**([E075](../e075_p4_width_sample_accuracy/README.ja.md)と同じ) | **24** | **7(29%)** | 毎回 **D2**(`dead=0x04`) |
| **order 1 — LEDC → receiver** | **30** | **0** | — |

死んだ trial の例:

```
BOOT order=0 rate_hz=160000000 lanes=4 status=0 bytes=65536 overflow=0 dead=0x04 edges=83,83,0,83
```

**`overflow` は全 trial で 0**、`status` も 0。**死んだ lane 以外は正常な edge 数(82〜83)**である。

同一 boot 内の繰り返しでは、**どちらの順序でも 0 件**(各 100 trial)。`esp_restart()` でも **0 件**(10 回)。

## 事実

1. **原因は準備順序だった。** receiver を先に作ると **cold boot の 29%** で 1 channel が死に、**LEDC を先に attach すると 30 回で 0 件**。→ **反証条件 1・2 はどちらも否定された。**
2. **`overflow` は常に 0。** capture 経路の drop ではない。→ **反証条件 3 も否定。** [E075](../e075_p4_width_sample_accuracy/README.ja.md) の見立て(「capture 経路そのものの問題ではない」)は正しかった。
3. **再現するのは hard reset 直後の 1 回だけ。** 同一 boot 内では 200 trial 回しても出ず、`esp_restart()` でも出ない。**[E075](../e075_p4_width_sample_accuracy/README.ja.md) が「掃引の初回」にだけ見たのはこのため**で、「再実行すると再現しない」も同じ理由である。
4. **死ぬ lane は条件の中では一定。** ここでは常に D2 だった([E075](../e075_p4_width_sample_accuracy/README.ja.md) は別の幅・rate で D3 / D5)。**特定の GPIO が悪いのではなく、条件ごとに決まる**と見える。
5. **既存の測定は影響を受けていない。** [E074](../e074_p4_2ch_capture_to_sr/README.ja.md) / [E075](../e075_p4_width_sample_accuracy/README.ja.md) / [E078](../e078_p4_continuous_stream/README.ja.md) / [E080](../e080_p4_pulseview_gapless/README.ja.md) / [E082](../e082_p4_spool_then_convert/README.ja.md) は**すべて立ち上がり周期で判定している**ので、死んだ channel があれば必ず落ちる。実際 [E075](../e075_p4_width_sample_accuracy/README.ja.md) はこれを検出して記録に残した。

## 候補

- **信号源を先に attach してから PARLIO receiver を作る。** 内部 LEDC を信号源にする sketch は全部この順序にする
- **cold boot の 1 回目を疑う。** 同一 boot 内の再実行で「直った」ように見えても、直っていない
- **lane ごとの edge 数を board 上で数えるのは安い。** 65,536 byte で数 ms。**download せずに channel の生死を判定できる**

## 未決

- **なぜ順序で決まるのか** `—`。`ledcAttach` が matrix を踏む、という筋は立てたが**レジスタでは確認していない**
- **なぜ cold boot 限定か** `—`。`esp_restart()` との差は peripheral の reset 範囲と読めるが未確認
- **2 / 8 channel での頻度** `—`
- **外部信号(LEDC を使わない構成)で起きるか** `—`。起きないはずだが未確認

## 影響

- [E075](../e075_p4_width_sample_accuracy/README.ja.md) の未決「間欠的に 1 channel が定数 0 になる現象」— **特定できた**
- [P4 logic analyzer 予備調査](../../references/p4-logic-analyzer-investigation.ja.md) — 内部信号源を使う構成の準備順序
- [E015](../e015_p4_parlio_routing_order/README.ja.md) — 「共存する」は正しいが、**attach の順序までは見ていなかった**
