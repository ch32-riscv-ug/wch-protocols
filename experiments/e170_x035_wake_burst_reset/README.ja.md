# E170 RVSWD の wake burst や生の clock 列は X035 を reset するか(X035 の治具)

状態: **完了(2026-09-26)**。止まった X035 の reset の出どころは、まだ特定できていない。

## 問い

E169 で、E164 の止まった X035 は LinkE の特殊消去によって reset される、と分かった。E165 の 2c では、線上に reset の操作(ndmreset・PFIC)が無いこと、止まった target に対して LinkE が線の初期化(SWDIO high の 99 clock)を 17〜37 回繰り返すことを見た。

oep-probe-arduino の `RvswdPhy::configureBus()` には、「100 clock の wake burst は debug interface だけでなく target を reset する」という記録がある(CH32L103、2026-09-23)。これが X035 でも起きるか、また SWDIO を low にしたまま長く刻む列はどうかを、RVSWD を細かく制御できる治具で 1 種類ずつ確かめる。

## 手順

- 機材: X035 の治具。
  - P4 `esp32-series-30eda0e31108`。SWDIO = GPIO2 → X035 PC18、SWCLK = GPIO54 → X035 PC19。NRST は未接続で、電源は P4 から。
  - CH32X035F8U6。app は core の例 HelloDMSeq(core の既定の clock)。
- P4 に [`wake_probe`](wake_probe/wake_probe.ino) を焼いた。oep-probe-arduino `8abd078` の `RvswdPhy` で DMI を行い、生の clock の列は library と同じ dedicated GPIO の bundle で出す。
  - 最初の版は `pinMode` / `digitalWrite` で生の列を出していた。その後 library が pin を駆動できなくなり、通信が切れた(P4 を reset すると戻った。target は無事)。
- host: [`run.py`](run.py)(`/run/board-identify/by-id/esp32-series-30eda0e31108`)。判定は DMSTATUS の havereset(bit 18/19)。処置の前に ackhavereset を書いておく。
- 陽性の対照: ndmreset(DMCONTROL ← `0x3` → `0x1`)を書くと havereset が立ち、ack で消えることを確かめた。

## 結果

### 1. 正常な X035(`out/healthy_*.json`)

| 処置 | 回数 | havereset |
|---|---|---|
| 何もしない(100 ms) | 3 | 0 |
| wake なしの bring-up(`resync`) | 3 | 0 |
| **wake burst**(SWDIO high の 100 clock + low の 1 bit + STOP、library の `wakeBus()`) | 6 | **0** |
| SWDIO high のまま 100 / 200 clock + STOP(half 約 1 µs) | 3 / 3 | 0 / 0 |
| SWDIO low のまま 100 / 236 clock + STOP | 3 / 3 | 0 / 0 |

- **正常な X035 は、wake burst でも SWDIO low の長い列でも reset されない。** 通信も保たれた。library の「wake burst は target を reset する」は L103 の観測で、X035 には当てはまらない。

### 2. 治具で模した止まった状態(`out/stopped_slow_manual.txt`、`out/stopped_marginal.txt`)

- halt して CFGR0 ← `0xd0`(HPRE `1101` = /64)、ACTLR ← `0xffffffff` を書き、resume した。ACTLR は書込みが有効な bit だけ残って `0x83` になった。
- この状態の DMSTATUS は、half 周期によって次のように読めた。
  - **2 µs 以上(SWCLK 250 kHz 以下)**: 安定して読める。
  - 1 µs: ときどき化ける。
  - 0.5 µs 以下: 化ける。
  - LinkE の接続時の区間(約 475 kHz、half 約 1.05 µs)は、ちょうど化けるかどうかの境目にあたる。
- 次のどれでも reset はかからず、haltreq は普通に効いた。
  - 化けやすい速度(half 1 µs)で `W dmcontrol 0x80000001` を 50 回。
  - 同じ速度で wake burst を 5 回。
  - 最速で `W dmcontrol 0x80000001` を 50 回。
- ただし、この模擬は LinkE が作る止まった状態を再現できていない。LinkE の場合は、AttachChip がまったく通らず、target が SWDIO を low に張り付かせる(E164・E165)。模擬では遅い速度なら普通に通信でき、core も走り続けた。
- 最初の試みでは、最速のまま ACTLR を書いた直後に通信が化け、以後の読出し(`07ffffff` など)を havereset と誤って判定した。遅い速度で読み直すと、実際には halt 中で havereset は無かった。

### 3. 片付け

- 遅い速度で ndmreset を書くと、X035 は既定の clock に戻り、app も走った。
- P4 は `examples/Esp32P4X035Probe` に焼き戻した(oep-probe-arduino `8abd078`、`arduino-cli compile --clean` → USJ から upload)。

## 結論

- X035 では、RVSWD の wake burst(100 clock)も、SWDIO を high / low にしたまま長く刻む列も、正常な target を reset しない。
- 止まった X035 に特殊消去で reset がかかる出どころは、線の初期化そのものではない見込みである。ただし、治具では LinkE の止まった状態を再現できていないので、確定ではない。
- 残る候補は次のとおり。
  - LinkE の止まった状態に固有の何か(CFGR0 の化けた値。E165 では `0x80000000` の書込みもあった)。
  - target が化けた書込みを受け取った結果。線上の host の bit は正しくても、target の中では別の値として受理される(E165 では parity の不一致が多数あった)。
  - 特殊消去の中の、まだ見ていない動作。

## 未決

- LinkE が作る止まった状態を、治具で正確に再現する(E165 の化けた書込みの列を、そのまま遅い速度で再生する)。
- 再現できたら、E169 と同じく、処置を 1 種類ずつ与えて reset の出どころを確かめる。
