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

初期は capture を置いていない。実機 capture を追加したらここに fixture(と対応する操作・機材・firmware 版のメモ)を置く。firmware 版で挙動が変わる項目(§10 版差、消去済みセルの read 値)は**版ごとに**記録する。

命名例: `<操作>-<target>-fw<版>.ndjson`(例 `flash-ch32v307-fw2.22.ndjson`)。
