# DMI Bridge Protocol(`dmibridge/1`)— host ↔ 汎用 probe

状態: **draft**(この repo で唯一の**自前設計**。解読ではなく仕様策定。実装・実測は未)。層は L1 datagram + L2 多重化 + L3 コマンド。

任意の MCU(RP2040 / ESP32 / ESP32-S3 / CH32 …)を CH32 RISC-V 用の probe に仕立てるための、**transport 非依存の host↔probe protocol**。設計の前提と背景は [../references/generic-probe-design.ja.md](../references/generic-probe-design.ja.md)、エコシステム上の位置づけは [../references/ecosystem-any-hardware.ja.md](../references/ecosystem-any-hardware.ja.md)。

**実装(この仕様の両端)**

| 側 | 実体 | 備考 |
|---|---|---|
| host | `ch32rv-dmibridge` crate(`ch32rv` workspace) | `ch32rv-wchlink`(WCH-Link USB protocol)と同格の backend |
| probe | `CH32RVProbe`(Arduino ライブラリ、repo `ch32rv-probe`) | この仕様の実装の 1 つ。他実装も同じ crate で繋がる |

既存 backend が **probe : protocol = 1 : 1**(WCH-Link / factory ISP / custom BL)なのに対し、本仕様は **1 : N**(1 つの protocol を多数の board が喋る)。ゆえに protocol 名と製品名を分離している。

## 0. 設計原則

1. **probe は chip を知らない**。運ぶのは RISC-V の DMI トランザクション `(addr7, data32, op2)` だけ([link-to-target.ja.md §3](link-to-target.ja.md))。family 別 FLASH 手順・DM 操作・stub・GDB は全部 host 側([riscv-debug-module.ja.md](riscv-debug-module.ja.md) / [pc-to-link.ja.md](pc-to-link.ja.md) §5–6)。**新 chip 対応は host の更新だけで済む**。
2. **protocol は上限を持たない。数値はすべて `caps` が申告する**。lane 数・未応答数・datagram サイズ・batch 長のいずれも、仕様上の固定値を置かず probe が宣言する。
3. **transport 間の差は「境界」と「完全性」の 2 つだけ**。それを L1 adapter が吸収し、L2 以上は 1 bit も変わらない(§2)。
4. **正体判定は handshake で行う**。USB VID/PID は候補を絞るフィルタにすぎない([ecosystem-any-hardware.ja.md §4.2b](../references/ecosystem-any-hardware.ja.md) の CMSIS-DAP の教訓)。
5. **ピンは protocol で設定しないが、protocol で申告する**(§5.3)。

## 1. 層モデル

```
L4 アプリ         chip DB / flash algo / DM 操作 / GDB / semihosting      ← host のみ。probe に無い
──────────────────────────────────────────────────────────────
L3 コマンド       hello/caps/info, dmi_read/write, batch, power, uart …   ← 本書 §4
L2 多重化         type(req/resp/event) + lane + tag                        ← 本書 §3
L1 datagram       境界の復元 + 完全性の担保(transport ごとに差し替え)      ← 本書 §2
L0 transport      UART / USB CDC / USB HID / USB bulk / TCP / WebSocket
──────────────────────────────────────────────────────────────
線層(probe 内部)  SWIO 1 線 / RVSWD 2 線 → DMI トランザクション          ← link-to-target.ja.md
```

- **L2 以上は transport を一切知らない**。L1 の契約(§2.1)だけが境界。
- 線層は probe 内部の実装詳細で、本仕様には現れない。1 線/2 線の選択は `lane_attach` の引数(§4.2)。

## 2. L1 — datagram 契約と transport adapter

### 2.1 契約

L1 adapter は上位に対し、次を満たす **datagram 送受信**を提供する:

| 要件 | 内容 |
|---|---|
| 境界保持 | 送った 1 datagram が 1 datagram として届く(結合・分割されない) |
| 順序保持 | 送信順に届く |
| 欠落なし | 届いたものは化けていない。化けたものは**届かない**(捨てる)。上位は再送で回復する(§6.3) |
| 重複なし | 同じ datagram が 2 回届かない |
| 最小 mtu | **64 byte 以上**。実値は `caps.mtu` |

上位は L1 の実装を知らない。逆に L1 は datagram の中身を解釈しない。

### 2.2 adapter 一覧

| transport | 境界 | 完全性 | adapter がやること | mtu 目安 |
|---|:--:|:--:|---|---|
| **UART / USB CDC** | 無 | 無 | magic + len + payload + **CRC16** + 再同期(§2.3) | 256–1024 |
| **USB HID** | 有(64 B 固定) | 有(USB CRC) | 64 B report への分割・再組立(§2.4) | 分割で無制限 |
| USB bulk / WebUSB | 有 | 有 | ほぼ素通し。`len % wMaxPacketSize == 0` のとき ZLP | 512+ |
| **TCP** | 無 | 有 | `u16 len` prefix のみ。**CRC 不要** | 大きく取れる |
| **WebSocket** | 有(message) | 有 | **何もしない**(binary message = datagram) | 大きく取れる |

**BLE は本仕様の対象外**。Bluetooth SIG の Declaration/QDID の手続きが USB VID/PID より重く、MTU 20–244 B・接続間隔 10–30 ms で flash 用途に向かないため。ただし GATT notify は §2.1 の契約を満たすので、**必要になれば adapter を 1 つ書くだけで足りる**(本仕様の変更は不要)。層を分けた利益がここに出る。

### 2.3 UART / USB CDC フレーミング

```
0xA5  0x5A   len_lo len_hi   <payload: len byte>   crc_lo crc_hi
```

- `len` = payload 長(1..mtu、little-endian)。CRC は **CRC-16/CCITT-FALSE**(poly 0x1021、init 0xFFFF)を `len`(2 B)+ `payload` に対して計算。
- 長さ前置なので **escape 不要**(SLIP/COBS を使わない)。低速 MCU での実装量と RAM を最小化するための選択。
- **再同期**: `0xA5 0x5A` を走査 → `len > mtu` なら破棄して走査再開 → CRC 不一致なら magic の次の byte から走査再開。
- probe は magic 以前の任意のゴミを許容しなければならない。これは host の port scan 安全性([ecosystem-any-hardware.ja.md §4.5](../references/ecosystem-any-hardware.ja.md))と、**DTR auto-reset 直後の起動メッセージ**を吸収するため。
- magic `0xA5 0x5A` は ASCII 範囲外かつ ardulink 互換モードの先頭文字(`w` `r` `?` `p` `P`)と衝突しない(§7)。

### 2.4 USB HID フラグメンテーション

64 B report、byte0 がヘッダ:

```
byte0: bit7 = more(1 なら後続あり) | bit6..0 = この report の payload 長(0..63)
byte1..: payload
```

vendor-defined usage page(`0xFF00`)を使う。CMSIS-DAP と同じく、**識別は VID/PID ではなく interface/product string と `hello` 応答**で行う。

## 3. L2 — 共通ヘッダ

全 datagram の先頭 4 byte:

| off | field | 内容 |
|---:|---|---|
| 0 | `type` | `0x00` req(host→probe) / `0x01` resp(probe→host) / `0x02` event(probe→host、非要求) |
| 1 | `lane` | 対象 lane(0..N-1)。**`0xFF` = probe 全体**(hello/caps/info/ping/set_baud/reset) |
| 2 | `tag` | req が採番。resp は echo。event は `0x00` |
| 3 | `cmd` | コマンド番号(§4)。resp は echo |

resp の payload 先頭は必ず `u8 status`(§6.2)。

- **`lane` をヘッダに置く**のが本仕様の要点の 1 つ。firmware はコマンドを解析する前に phy インスタンスへ振り分けられ、event も lane を名乗れる。lane を各コマンドの引数にすると batch のたびに分岐が入り、event が lane を持てない。
- protocol 版は datagram ごとに持たない。`hello` で 1 度だけ合意する(§4.1)。
- **多 byte 整数はすべて little-endian**。⚠ [pc-to-link.ja.md](pc-to-link.ja.md) の `DmiOp` は data が **big-endian** なので、host 側で両 backend を跨ぐときに変換が要る。

## 4. L3 — コマンド

### 4.1 probe 全体(`lane = 0xFF`)

| cmd | 名前 | req payload | resp payload |
|---|---|---|---|
| `0x01` | `hello` | `u16 host_proto_max` | `status, u16 proto_ver, u8 flags` |
| `0x02` | `caps` | — | `status,` TLV(§5) |
| `0x03` | `info` | `u16 offset` | `status, u8 more, ` UTF-8 テキスト断片(§5.3) |
| `0x04` | `ping` | `u32 cookie` | `status, u32 cookie` |
| `0x05` | `set_baud` | `u32 baud` | `status`(**応答送出完了後に**切替) |
| `0x06` | `reset_probe` | — | `status`(応答後に再起動) |

`hello` は host が対応可能な最大版を送り、probe が実際に使う版を返す。**`hello` が成立して初めて他のコマンドを送ってよい**。port scan は `hello` を撃って応答が無ければ閉じる。

### 4.2 lane

| cmd | 名前 | req payload | resp payload |
|---|---|---|---|
| `0x10` | `lane_attach` | `u8 wire`(0 auto/1 swio/2 rvswd)`, u32 speed_hint` | `status, u8 wire_actual` |
| `0x11` | `lane_detach` | — | `status` |
| `0x12` | `line_reset` | — | `status` |
| `0x20` | `dmi_read` | `u8 addr7` | `status, u32 data, u8 dmi_status` |
| `0x21` | `dmi_write` | `u8 addr7, u32 data` | `status, u8 dmi_status` |
| `0x22` | **`batch`** | §4.3 | §4.3 |
| `0x30` | `power` | `u8 rail`(1=3v3/2=5v)`, u8 on` | `status` |
| `0x31` | `nrst` | `u8 act`(0 deassert/1 assert/2 pulse)`, u16 ms` | `status` |
| `0x50` | `uart_open` | `u32 baud, u8 config` | `status` |
| `0x51` | `uart_close` | — | `status` |
| `0x52` | `uart_write` | bytes | `status, u16 accepted` |
| `0x60` | `autopoll_set` | `u8 addr7, u32 mask, u32 expect, u16 interval_us` | `status` |
| `0x61` | `autopoll_clear` | — | `status` |

- `wire = 0`(auto)は 1 線/2 線の自動判別(`rvswdio_programmer` の `opmode` 実績)。判別結果を返す。
- `dmi_status` は RISC-V DTM の 2 bit(0 ok / 2 fail / 3 busy)をそのまま通す。**frame の `status` と混同しない**: `status = OK` は「probe が実行した」、`dmi_status` は「線がどう答えたか」。
- **busy(3)の再試行は probe が吸収する**(`caps.busy_retry` 回まで)。host を単純に保つための選択で、[generic-probe-design.ja.md §4](../references/generic-probe-design.ja.md) の未決事項に対する回答。

### 4.3 `batch` — latency 対策の核

req payload: `u8 count`, 続けて `count` 個の可変長 op。

| op | 名前 | 長さ | 形式 |
|---|---|---:|---|
| `0x00` | nop | 1 | `op` |
| `0x01` | read | 2 | `op, addr7` |
| `0x02` | write | 6 | `op, addr7, u32 data` |
| `0x03` | delay_us | 5 | `op, u32 usec` |
| `0x04` | **poll** | 14 | `op, addr7, u32 mask, u32 expect, u32 timeout_us` — `(data & mask) == expect` まで読み続ける |
| `0x05` | **write_rep** | 3+4n | `op, addr7, u8 n, n × u32` — 同一 addr へ n 回書込 |
| `0x06` | **read_rep** | 3 | `op, addr7, u8 n` — 同一 addr から n 回読出 |

resp payload:

```
u8 status        (0 = 全 op 完了)
u8 executed      (完了した op 数)
u16 read_bytes
u32 × k          (read / read_rep / poll の結果を発生順に)
```

- **最初の誤りで停止**する。`executed` がどこで止まったかを示すので、op ごとの status は持たない(密度優先)。
- `poll` があることで **BUSY 待ちが往復を発生させない**。1 page の program が 1 batch に収まる。
- `write_rep` / `read_rep` は **DMDATA0 の autoincrement 経路**([riscv-debug-module.ja.md](riscv-debug-module.ja.md) の abstract autoexec)を想定した bulk primitive。word あたり 6 → 4 byte に縮む。

**RAM stub に専用コマンドは要らない。** stub の load(PROGBUF/DMDATA への書込)・起動(abstract command)・回収は**すべて DMI read/write でしかない**ので、host が `batch` に詰めれば済む。stub 本体の handshake(target 側 stub が DMDATA0 を待つ)も `write_rep` + `poll` で表現できる。→ [generic-probe-design.ja.md §4](../references/generic-probe-design.ja.md) が挙げていた `stub_load` / `stub_run` / `stub_poll` は**削除**。probe に DM の知識を一切入れずに済み、設計原則 1 が完全に守られる。

### 4.4 event(probe → host、非要求)

| cmd | 名前 | payload |
|---|---|---|
| `0x80` | `uart_data` | `u16 dropped,` bytes(該当 lane の target UART 受信) |
| `0x81` | `autopoll_hit` | `u32 value` |
| `0x82` | `log` | `u8 level,` UTF-8 テキスト |
| `0x83` | `lane_status` | `u8 reason`(電源断・線切断・target reset 検出など) |

`dropped` は前回 event 以降に捨てた byte 数。**黙って落とさない**(原因不明の欠落を防ぐ)。

`autopoll` は「DMI reg を周期的に読み、条件成立で event を上げよ」という**汎用のレジスタ監視**であり chip 知識ではない。SDI print(dmdata、[serial-and-print.ja.md §3](serial-and-print.ja.md))の往復を probe 内に閉じ込めるために使う。既定は無効。

## 5. `caps` と `info`

### 5.1 符号化

TLV(`u8 type, u8 len, len byte`)。未知の type は読み飛ばす → **版を上げずに項目を足せる**。

### 5.2 probe 全体の項目

| type | 項目 | 内容 |
|---|---|---|
| `0x01` | `proto_ver` | u16 |
| `0x02` | `impl_name` | str(例 `CH32RVProbe`) |
| `0x03` | `impl_ver` | str |
| `0x04` | `uid` | bytes(chip UID。個体識別。USB serial string と同源) |
| `0x05` | `mtu` | u16 |
| `0x06` | `max_inflight` | u8(host が持てる未応答 req 数。既定 1) |
| `0x07` | `max_batch_ops` | u16 |
| `0x08` | `event_queue` | u16(byte) |
| `0x09` | `busy_retry` | u8 |
| `0x0A` | `flags` | u8(bit0 ardulink 互換 / bit1 autopoll / bit2 set_baud) |
| `0x0B` | `board` | str(例 `ESP32-S3-DevKitC-1`) |
| `0x0C` | `build` | str(版・ビルド日時・git hash) |
| `0x20` | `lane` | 入れ子 TLV(lane ごとに 1 つ) |

lane の入れ子:

| type | 項目 |
|---|---|
| `0x01` | `id` u8 |
| `0x02` | `wires` u8(bit0 swio / bit1 rvswd) |
| `0x03` | `features` u8(bit0 power3v3 / bit1 power5v / bit2 nrst / bit3 uart / bit4 vref sense) |
| `0x04` | `uart_max_baud` u32 |
| `0x05` | `label` str(人間向け。例 `V003 socket`) |
| `0x10` | `pin` 入れ子(`u8 role, str name`)を役割ごとに繰り返す |

`role`: `1 swio` / `2 swclk` / `3 swdio` / `4 nrst` / `5 pwr_en` / `6 uart_tx` / `7 uart_rx` / `8 vref`。

### 5.3 ピンは「設定しない、申告する」

**実行時のピン割り当ては protocol に持ち込まない。** 理由は 2 つ:

- ESP32 の RMT channel / RP2040 の PIO SM は有限で、「lane 2 は lane 1 が bit-bang のときだけ使える」式の資源依存を `caps` で表現し始めると破綻する。**スケッチが構築できた構成だけが `caps` に現れる**なら矛盾が起きない。
- host から任意ピンを割り当てられると、配線ミスでショートさせられる。

一方で **「焼いた後に、どのピンをどの機能に割り当てたか分からなくなる」**問題は実在する。これは設定ではなく**申告**で解く:

- `caps` の `pin`(役割 + 名前文字列)で機械可読に
- `info` で人間可読に(下記)

ピン名は **probe 側が文字列で返す**。生のピン番号は board の命名規則(`GP2` / `IO4` / `PD1` / `D2`)を知らないと意味を成さず、それを知っているのは firmware だけだから。

**申告は実物から自動導出する**(手書きの表は必ず腐る)。スケッチでピンを与える箇所でそのまま文字列化する:

```cpp
// マクロがピン式をそのまま文字列化するので、配線と申告がずれない
LANE_SWIO (lane0, IO2,        .power = IO10, .nrst = IO11, .uart = {IO12, IO13}, "V003 socket");
LANE_RVSWD(lane1, IO4, IO5,                  .nrst = IO14,                        "V307 board");
LANE_SWIO (lane2, IO6,                                                            "breadboard");
static CH32RVProbe probe({&lane0, &lane1, &lane2}, transport);
```

`info` はこれを整形した UTF-8 テキストを返す(`mtu` を超えるので `offset` で分割取得):

```
CH32RVProbe 0.1.0  (dmibridge/1)
board  : ESP32-S3-DevKitC-1
build  : 2026-09-04T11:20+09:00  git:a1b2c3d
uid    : 7C:DF:A1:E3:04:B8
lane0  swio  IO2          pwr=IO10(3v3)  nrst=IO11  uart=IO12/IO13   "V003 socket"
lane1  rvswd IO4/IO5      pwr=-          nrst=IO14  uart=-           "V307 board"
lane2  swio  IO6          pwr=-          nrst=-     uart=-           "breadboard"
transport: ws://0.0.0.0:3333   (UART0/1/2 は target monitor に空き)
```

`build` を含めるのが要点。**「どのピンに割ったか」と「どの firmware を焼いたか」は同じ問題**で、片方だけ分かっても復元できない。host からは `ch32rv probe info`、Wi-Fi probe なら同じ内容を Web ページでも出せる。

## 6. フロー制御と誤り処理

### 6.1 方向で規則が違う(非対称)

| 方向 | 規則 |
|---|---|
| **host → probe** | 未応答の req は `max_inflight` 個まで(既定 1)。`tag` で対応付け |
| **probe → host** | **event はいつでも送ってよい**。ただし線の critical section 中は送らずキューに積む |

低速 MCU で壊れるのは「割り込みを止めている間の**受信**」だけで、送信は probe 自身がタイミングを選べる。この非対称のおかげで、`max_inflight = 1` の probe でも **target UART モニタが push で流せる**。

`max_inflight > 1` を申告する probe(DMA/割り込み駆動の phy を持つ板)では、host は req をパイプライン化でき、**複数 lane の同時書込**が可能になる。host 側の実装は `tag` で対応付けるだけで変わらない。

### 6.2 `status`

| 値 | 名前 | 意味 |
|---|---|---|
| `0x00` | OK | |
| `0x01` | EBADCMD | 未知のコマンド |
| `0x02` | EBADLANE | 存在しない lane |
| `0x03` | EBADARG | 引数・長さ不正 |
| `0x04` | EBUSY | `max_inflight` 超過、または lane が実行中 |
| `0x05` | EWIRE | 線の誤り(dmi_status が fail、busy 再試行を使い切った、target 無し) |
| `0x06` | ENOSUP | `caps` に無い機能 |
| `0x07` | ETOOBIG | mtu / max_batch_ops 超過 |
| `0x08` | ESTATE | lane が attach されていない等、手順違反 |

**probe は必ず応答を返す**(実行不能でも status を返す)。無応答は L1 の欠落だけを意味する。

### 6.3 再送と冪等性

UART で CRC 不一致の datagram は捨てられるので、**host はタイムアウト + 再送**で回復する。ただし `dmi_write` は一般に冪等でない(abstract command の起動など)。

→ **probe は lane ごとに直前の `(tag, 応答)` を 1 組だけ保持し、同じ `tag` の req が再来したら再実行せずキャッシュした応答を返す**。host は再送時に `tag` を変えない。RAM 消費は応答 1 個分で、AVR 級でも払える。

### 6.4 タイムアウトの所在

タイムアウトは **host が持つ**。probe は `batch` の `poll` op を除いて無限待ちをしない。`poll` の上限は op 自身の `timeout_us`。

## 7. ardulink 互換モード

[ch32fun の `minichlink/ardulink.c`](https://github.com/cnlohr/ch32fun/blob/master/minichlink/ardulink.c) が既に使っている 6 byte protocol を、**受信 1 byte 目で自動判別**して受け付ける(任意機能、`caps.flags` bit0 で申告):

| 先頭 byte | 意味 |
|---|---|
| `0xA5` | native(§2.3) |
| `'?'` | ardulink 同期 → `'!'` を返す |
| `'w'` | `w reg d0 d1 d2 d3` → `'+'` |
| `'r'` | `r reg` → `d0 d1 d2 d3` |
| `'p'` / `'P'` | target 電源 on/off → `'+'` |

利点は **minichlink が host 側を 1 行も変えずに day 1 で使える**こと。制約: lane 0 のみ、event 無し、batch 無し。native モードに入ったら reset まで戻らない。

## 8. 適合水準

### 8.1 機能プロファイル

| profile | 必須コマンド |
|---|---|
| **Core**(必須) | `hello` `caps` `info` `ping`、`lane_attach` `lane_detach` `line_reset`、`dmi_read` `dmi_write`、`batch`(`nop`/`read`/`write`/`delay_us`、8 op 以上) |
| **Bulk** | + `batch` の `poll` / `write_rep` / `read_rep` |
| **Bench** | + `power` `nrst` `uart_*` `autopoll_*` と event |
| **Multi** | + lane 2 本以上 |

Core だけで flash / read / debug は成立する(遅いだけ)。Bulk が実用速度の下限。

### 8.2 電気的水準

| 水準 | 内容 |
|---|---|
| **3.3 V**(基準) | v1 で実装を出す水準。RP2040 / ESP32・S3 / CH32。SWIO / RVSWD 両方 |
| **5 V board** | **SWIO のみ**。SWIO は「LOW に引く/開放して target の pull-up で HIGH」の open-drain 運用なので probe 側は HIGH を駆動せず、直列抵抗のみで電気的に安全。**RVSWD は SWCLK が push-pull なのでレベル変換必須 → 対象外** |
| **AVR profile** | protocol 上は成立する(`mtu` 64 / `max_inflight` 1 / event はキュー)。v1 では実装を出さない。Uno R3 の価値は V003 = SWIO の bootstrap であり、上の「5 V board = SWIO のみ」と一致するので、後から phy を足すだけでよい(protocol 変更不要) |

`mtu` 64 / `max_inflight` 1 が正当な値であることを仕様が保証する限り、低資源 MCU への移植は **firmware 側の作業だけ**で済む。

## 9. 未決事項

1. `wire = auto` の判別手順(`rvswdio_programmer` の `opmode` 実装を確認して確定させる)。
2. SWIO のタイミング係数を `lane_attach` の `speed_hint` でどう表現するか(target clock 依存。自動調整を probe に持たせるか host が掃引するか)。→ [coverage.ja.md](../coverage.ja.md) P3-6 と同時に解決する。
3. `set_baud` の切替手順(応答送出完了の検出、失敗時の 115200 への自動復帰)。
4. IP transport の認証(LAN の誰でも書けてしまう問題。[ecosystem-any-hardware.ja.md §4.5](../references/ecosystem-any-hardware.ja.md))。トークンを `hello` に載せるか、TLS/WSS にするか。
5. mDNS のサービス名(`_dmibridge._tcp` を候補とする)。
6. `caps` TLV type 番号の正式割当(本書は draft の暫定値)。

## 参照

- 設計の背景・transport 比較・性能見積り: [../references/generic-probe-design.ja.md](../references/generic-probe-design.ja.md)
- エコシステム上の位置づけ・USB ID 方針・T0〜T3: [../references/ecosystem-any-hardware.ja.md](../references/ecosystem-any-hardware.ja.md)
- 既存の自作 probe と host ツール: [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md)
- 運ぶ中身(DMI トランザクション): [riscv-debug-module.ja.md](riscv-debug-module.ja.md) / 線上の符号化: [link-to-target.ja.md](link-to-target.ja.md)
- 同格の既存 backend: [pc-to-link.ja.md](pc-to-link.ja.md)(WCH-Link USB protocol)
- target 側 print の中身: [serial-and-print.ja.md](serial-and-print.ja.md)
