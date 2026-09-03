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

以降、実機 capture(flash / erase / DMI / ISP / DAP など)を追加する。
