# captures — 検証用 capture の取り方と参照 fixture

プロトコル項目を `verified` に上げる根拠は **実機 capture**。ここでは取り方・記録形式・replay 検証を置く。方法論の全体は [../guides/advanced.ja.md](../guides/advanced.ja.md) §B。

## 取り方

- **Linux usbmon**: `/sys/kernel/debug/usb/usbmon/` を Wireshark で開き、USB bulk を丸ごと記録。純正ツール(WCHISPTool / WCH-LinkUtility)や wlink/probe-rs の往復を観察するときに使う。
- **ツール内蔵 record**: ch32rv は `--capture <file>` で自分の USB 往復を **NDJSON** に記録する(自作ツールにも同じ機構を持たせると回帰検証が楽)。

## 記録形式(NDJSON)

1 行 = 1 USB 転送:

```json
{"seq":12,"t_us":34567,"chan":"cmd","dir":"out","len":4,"ok":true,"data":"810d0101"}
```

- `chan`: `cmd`(EP 0x01/0x81)/ `data`(EP 0x02/0x82)。
- `dir`: `out`(PC→probe)/ `in`(probe→PC)。
- `data`: 16 進のペイロード。
- 先頭に `_meta`(ツール版・操作)と `_device`(VID/PID/serial/topology/product/ports)の行を置く。

## replay(オフライン検証)

記録した NDJSON は **replay** できる: 記録された応答を per-(chan,dir) FIFO として返し、実機なしでツールのパーサ/状態機械を回す。用途:

- **回帰テスト**: fixture を commit し、パース/デコードが壊れていないか CI で確認。
- **divergence 検出**: ツールが記録と同じ順序でコマンドを出しているか。flash のような決定的経路は byte 一致するはず。
- `verified` の根拠として「この capture を replay して一致」を残せる。

## 参照 fixture

[`fixtures/`](fixtures/) に実機 capture を置く。命名例: `<操作>-<target>-fw<版>.ndjson`。firmware 版で挙動が変わる項目(消去済みセルの read 値など)は**版ごとに**記録する。

### `fixtures/target-info-v307.ndjson`(attach + identify、LinkE fw2.22 → CH32V307)

`target info` 相当の全往復。[pc-to-link.ja.md](../protocols/pc-to-link.ja.md) の各コマンドが実バイトでどう並ぶかの worked example(cmd EP、`81`=OUT/`82`=IN):

| seq | dir | data | 意味([pc-to-link.ja.md](../protocols/pc-to-link.ja.md)) |
|---|---|---|---|
| 0/1 | out/in | `810d01ff` / `820d01ff` | **DetachChip**(セッション前クリア、§4 `0x0d 0xff`) |
| 2/3 | out/in | `810d0101` / `820d04 02 16 12 00` | **GetProbeInfo**(§4)。応答 `[02,16,12,00]` = fw **2.22**・variant `0x12`(LinkE)・mode 0(RISC-V) |
| 4/5 | out/in | `810c02 01 01` / `820c0101` | **SetSpeed**(§4 `0x0c`)。attach 前なので family=`0x01` placeholder、speed high=`0x01` |
| 6/7 | out/in | `810d0102` / `820d05 06 30700528` | **AttachChip**(§4 `0x0d 0x02`)。応答 = family **`0x06`**(V30x)+ chip_id `0x30700528` |
| 8/9 | out/in | `81110105` / (20B) `ffff 0120 7bbed00a9c1c5054 e339e339 30700528` | **ChipInfo**(§4 `0x11 0x05`、frame 無し生 20B)。flash_kb be16=`0x0120`=**288 KiB**、UUID `7bbed00a9c1c5054`、protection `e339e339`、chip_id `30700528` |
| 10/11 | out/in | `810d01ff` / `820d01ff` | **DetachChip**(解放) |

注: seq 6→7 の応答が ~62ms 後(`t_us` 5338→67590)。attach は target を掴むため時間がかかる。この fixture は ch32rv の replay 統合テストにも使われる決定的シーケンス。

### `fixtures/linke-iap-update-fw212-to-222.ndjson`(probe firmware 更新、2.12 → 2.22)

**WCH-LinkUtility V3.00** で WCH-LinkE の firmware を更新したときの USBPcap capture から、**流れが分かる 14 転送だけを抜いたもの**。仕様は [pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §10b。

`len` は **USB 転送全体**の長さ。IAP mode の frame は `cmd | len | off_lo | off_hi | data…` なので、**転送 64 B = ヘッダ 4 B + データ 60 B**。

| seq | ep | dir | data | 意味 |
|---:|---|---|---|---|
| 0 | `0x01` | out | `810d0101` | GetProbeInfo |
| 1 | `0x81` | in | `820d04 02 0c 12 00` | fw **2.12**・variant `0x12`(LinkE)・mode 0 |
| 2 | `0x01` | out | `810f0101` | **IAP entry**。**応答は返らない**(probe が即再起動) |
| 3 | `0x02` | out | `81020000` | 開始 |
| 4 | `0x82` | in | `0000` | ack |
| 5 | `0x02` | out | `803c0000` + 60 B | 書込 off=`0x0000`。**60 B は image 先頭と一致** |
| 6 | `0x82` | in | `0000` | ack(以降すべての転送に付く) |
| 7 | `0x02` | out | `803c3c00` + 60 B | 書込 off=`0x003c`(= 60。stride が 60 と分かる) |
| 1831 | `0x02` | out | `803cfcd5` + 60 B | 中盤 off=`0xd5fc` |
| **3655** | `0x02` | out | `802cbcab` + 44 B | **書込 pass の最後**。`len` が端数 `0x2c`=44、off=`0xabbc` |
| **3657** | `0x02` | out | `823c0000` + 60 B | **照合 pass の最初**。cmd が `0x82` になり off が `0x0000` へ戻る |
| 7307 | `0x02` | out | `822cbcab` + 44 B | 照合の最後(書込と同じ off・同じ端数) |
| 7308 | `0x82` | in | `0000` | ack |
| 7309 | `0x02` | out | `83020000` | **終了**。**応答は返らない**(probe が app へ jump) |

- **`ep` を明示的に記録している。** 通常 mode は `chan` の `cmd`/`data` が EP 0x01/0x02 に対応するが、**IAP mode には cmd/data の役割分担が無い**(4 本ある bulk のうち `0x02`/`0x82` の 1 組しか使わない)ため、`chan` だけでは通常 mode の役割を含意してしまう。
- **応答が返らないコマンドが 2 つある**(`810f0101` = IAP entry、`83020000` = 終了)。どちらも直後に device が消える。`_trim.no_reply_to` にも記録。

**絞ってある**ので、そのままでは replay で全体を再現できない。省略していない事実は先頭の `_trim` に数値で持たせてある:

- `total_bulk_transfers` 7,310 のうち **14 を採用**。`seq` は**元の位置のまま**なので、飛んでいる箇所が省略点
- `write_transfers` / `verify_transfers` = 各 **1,826**、`write_bytes` / `verify_bytes` = 各 **109,544**
- `image_sha256` = 転送された image(`FIRMWARE_CH32V305.bin`)の SHA-256。**image 自体は WCH のものなのでこの repo には置かない**。同じものを持っているかはこのハッシュで確認する

`_device` は phase ごとに 3 行(更新前 `bcdDevice=0x0212` / IAP mode / 更新後 `0x0222`)。**この capture には IAP mode の device descriptor が含まれない**ため VID:PID は記録していない(config descriptor から分かる interface 構成のみ `note` に置いた)。

> **形式の拡張**: この fixture は既存形式に `ep`(生の endpoint)・`phase`(どの列挙か)・`_trim`(省略の明示)を足している。**複数の列挙をまたぐ capture では、生の endpoint と、省略した事実を数値で残すこと**を規約とする。

以降、実機 capture(flash / erase / DMI / ISP / DAP など)を追加する。
