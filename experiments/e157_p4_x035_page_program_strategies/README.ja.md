# E157 ESP32-P4 → CH32X035: 256-byte page program は word 単位 DMI と autoexec writer で何倍違うか

状態: 計画

## 問い

E153 の PHY（1 DMI ≈ 10 µs）で X035F8U6 の 256-byte physical page を erase + program + 読み戻し検証するとき、
次の 2 方式の page あたり所要時間はいくらで、検証は一致するか。

- **A `word_dmi`**（現 OEP 相当の host 主導）: 各 word を scalar store（program buffer `sw` を abstract command で実行、≈ 8 DMI）で
  page buffer 窓へ書き、`CTLR = FTPG|BUFLOAD` を scalar store、STATR BSY を scalar read で待つ。
- **B `autoexec_writer`**: program buffer に 5-word writer（`loader.S`: address 読出し → data store → BUFLOAD → BSY 待ち → address 加算）を置き、
  DMDATA0 への write を autoexec にして **1 DMI / word** で流す。

erase、`FLASH_ADDR`、`FTPG|STRT`、最終 BSY 待ちは両方式で同じ（pc-to-link の X035 Buffered 方式、STRT は 256 B page ごとに 1 回）。

## 仮説

- A は 64 word × (8 + 8 + 11) DMI ≈ 1,700 DMI ≈ 17 ms + flash 待ち。B は 64 DMI ≈ 0.7 ms + flash 待ち。**page あたり 10 倍以上**の差。
- 両方式の読み戻しは書いた pattern と全 byte 一致する。
- 現 OEP の 440 ms/page（E145、`digitalWrite` PHY + 64-byte 単位 4 回 STRT + scalar verify）に対し B は 100 倍級。

## 反証条件

- B の読み戻しが不一致（autoexec 中の BUFLOAD 待ちが host の次 word write に追い越される）。
- A と B で erase / STRT の flash 待ち時間が支配的で差が 2 倍未満。

## 方法

1. `P<seed>` で開始。attach → halt → DMSTATUS 2000 回 read（E156 の halt 直後の不安定期間を避ける）。
2. FLASH unlock（`CTLR & (LOCK|FLOCK)` なら KEYR、MODEKEYR に KEY1/KEY2）。option byte は触らない。
3. 対象は flash 末尾 2 KiB = `0x0800F000`〜`0x0800F7FF` の 8 page（先頭の image を壊さない）。page i の pattern は `seed` と i と offset から決める。
4. 方式 A で page 0〜7、続けて方式 B で page 0〜7（seed を変えて別 pattern）。各 page: erase → program → autoexec read（E156）で 64 word 読み戻し → 不一致数。
   erase / program / verify の µs、DMI 数、retry 数を出す。
5. `CTLR=0`、resume、Hi-Z。復元はしない（保全不要）。

## 対象外

複数 page の streaming / pipelining（transport 側、S3）。probe reset 途中の回復。線上波形。

## 必要な環境

profile `esp32p4_x035`、target X035F8U6（E153 と同じ）。flash 末尾 2 KiB を破壊的に書く。

## ベンチ種別

一時（既設 fixture、配線変更なし）。

## 記録する数値

`PAGE strategy= page=0x… erase_us= program_us= verify_us= mismatch= dmi= retries=`、`SUMMARY strategy= pages=8 program_us_min/median/max= mismatch_total=`、`RESUME ok=`。

## 完了条件

両方式 8 page の行と SUMMARY、RESUME ok が得られること。不一致があればその page を記録して完了。

## 影響

oep-probe-arduino target.flash の実装（transaction = physical page、writer は autoexec）、rebuild plan E157。仕様 status は動かない。

## 結果

状態: 完了（2026-09-22）。採用 run: `_runs/E157_20260921T234304Z_default/`（銘板 `git=7164d3a+dirty`、seed 183）。
それ以前の run: `…T234004Z`（unlock 前の CTLR read が失敗）、`…T2340xxZ`（settle 30,000 read の 78 % が parity 不一致、unlock 失敗）、
`…T2342xxZ`（方式 B の実装ミス: autoexec が progbuf 実行ではなく直前の register 転送 command を再実行し、64/64 不一致）。

実装上の追加（計画外）: halt 後に half {0, 25, 50, 100, 200} ns で DMSTATUS 10,000 read の parity 不一致数を測り、最初に 0 の half を採用する
（採用 run では half 0 で 0/10,000）。方式 B は先頭 word だけ `command=0x00240000` で明示実行し、残り 63 word を autoexec で流す。

| 方式 | erase | program（8 page の min / median / max） | verify（autoexec read 64 word） | DMI / page | mismatch |
|---|---:|---:|---:|---:|---:|
| A `word_dmi`（現 OEP 相当） | 3.26〜3.28 ms | 17.21 / **17.23** / 17.26 ms | 0.66 ms | 2,600 | 0 / 512 |
| B `autoexec_writer`（1 DMI / word） | 3.30〜3.35 ms | 2.83 / **2.88** / 2.92 ms | 0.66 ms | 833 | 0 / 512 |

unlock: 初回 run の CTLR は `0x00008080`（LOCK|FLOCK）、KEYR / MODEKEYR 書込み後 `0x00000000`。以後の run では既に unlocked（re-lock していない）。

## 事実 / 候補 / 未決

- **事実**: X035 の 256-byte page は erase 3.3 ms + program 2.9 ms + verify 0.7 ms ≈ **6.9 ms**（方式 B）。program は方式 A の **6.0 倍**速い。62 KiB（248 page）換算で ≈ 1.7 s。現 OEP の 27 page 11.9 s（E145、440 ms/page）に対し約 60 倍。
- **事実**: 方式 A でも 17.2 ms/page で、現 OEP の 440 ms との差は PHY（E153）と scalar verify の排除による。
- **事実**: writer の progbuf は 5 word（`loader.S`）。現 OEP の writer も address 自動加算と BUFLOAD 待ちを progbuf 内で行っており、host 側の DATA1 write と COMMAND write（word あたり 2 DMI + poll）は不要だった。
- **事実（E156 と合わせて）**: half 0 ns の DMI read は **run によって**約 6〜8 割 parity 不一致になることがある（E156 の 2 run、E157 の 2 run）。同じ sketch・同じ配線で次の run は 10,000 read 全数一致。halt 直後の「待ち時間」では説明できない（E156 run 3 は halt 直後の最初の 1000 read から全数一致）。
- **候補（S3 の設計条件）**: PHY は session 開始時に half period の margin check（DMSTATUS 1000 read 全数一致）を行い、失敗なら half を 25 → 50 → 100 ns と上げる。DMI parity（1 bit）は garbage の半分を通すので、**読出し・書込みの正否は上位の CRC / read-back で判定する**。
- **候補**: target.flash の transaction は physical page（geometry を probe が宣言）。erase → autoexec program → autoexec read-back → CRC を probe 内で 1 request として実行し、host は page 全体を送る。
- **未決**: half 0 ns の間欠 parity 不一致の原因 `—`（P4 側のコード配置・cache・割込みか、target 側か。LA 観測が要る。候補 `x035-dmi-parity-intermittent`）。unlock 後の re-lock 方針 `—`。複数 page の pipelining と transport 込みの全域時間 `—`（S3 で実測）。

## 反映

台帳 §1 E157 を完了、候補 `x035-halt-settle` を `x035-dmi-parity-intermittent` へ改題。rebuild plan の E157 行と S3 設計条件、oep-spec guidelines §6 に「parity ではなく CRC で判定」を追記。仕様 status は動かない。
