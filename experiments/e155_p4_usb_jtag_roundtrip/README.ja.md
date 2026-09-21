# E155 ESP32-P4 USB-Serial/JTAG: request 往復時間と帯域は frame 長と in-flight 数でどう変わるか

状態: 計画

## 問い

fixture P4 の USB-Serial/JTAG（`303a:1001`、Arduino `Serial` = HWCDC）を OEP の transport にしたとき、
host → device → host の echo 往復は、frame 長 {8, 64, 512, 4096} byte、in-flight {1, 4, 16} で
1 frame あたり何 µs、実効何 kB/s か。

## 仮説

- in-flight 1 の往復は USB full-speed の polling と HWCDC の実装で **1〜3 ms** 級。現 OEP は stop-and-wait なので
  full verify 722 往復 ≈ 0.7〜2 s がこれに消えている。
- in-flight を 4〜16 にすると往復待ちが隠れ、帯域は frame 長で決まる。4 KiB frame で数百 kB/s 以上。
- 64 byte（現 OEP の 96-byte 上限に近い）は in-flight 16 でも 4 KiB より一桁遅い。

## 反証条件

- in-flight を増やしても frames/s が増えない（device 側 HWCDC が直列化している）。
- 4 KiB frame で echo が壊れる（HWCDC buffer 上限）。

## 方法

1. device は `Serial.setRxBufferSize/TxBufferSize(8192)` で起動。`?` に銘板と `ECHO READY` を返し、以後は binary。
   frame = `len16 LE` + payload。受けた frame をそのまま返す。`len=0` で binary を抜け `ECHO END frames= bytes=` を出す。
2. host（pytest）は銘板の後に redirect thread を止め、pyserial で直接送受信する。reader thread が echo を回収・照合し、
   main thread は outstanding が in-flight 数を超えないように送る。
3. size × depth の 12 条件、各 frame 数は 4 KiB: 64、512: 256、他: 512。開始から最終 echo 受信までを測る。
4. 終了後 `len=0` を送り、redirect thread を戻して `ECHO END` を確認する。

## 対象外

HS USB（Phase B）。P4 → host 片方向 streaming。Windows 側 driver。

## 必要な環境

profile `esp32p4_x035`（fixture P4）。target 不要。同じ PC で他 session が USB を使っていないこと（§4.4）。

## ベンチ種別

一時（配線変更なし）。

## 記録する数値

`E155 size= depth= frames= total_s= us_per_frame= kB_s=`（kB/s は payload+header の片方向換算）。全 echo の byte 一致数。

## 完了条件

12 条件すべての行が得られ、echo 不一致 0。不一致があればその条件を記録して完了。

## 影響

oep-spec v0 draft の frame 上限と pipelining window（rebuild plan S1）。仕様 status は動かない。

## 結果

状態: 完了（2026-09-22）。採用 run: `_runs/E155_20260921T231205Z_default/`（銘板 `git=20229b1+dirty`、RX/TX ring 8192）。
host は WSL2 + usbipd 経由（HS 直結ではない）。

ハーネス上の注意（run 履歴）: (1) pytest-embedded の port を close → 再 open すると **USB-Serial/JTAG の DTR/RTS で P4 が reset** され、
firmware が text mode に戻って binary が捨てられる。binary 区間は harness の pyserial 実体を借り、reader thread は `stop_reading()` で止めるだけにする。
(2) reader の `read(65536)` は timeout（20 ms）まで待つため往復が 21 ms に見えた。`read(1)` + `in_waiting` で解消。
(3) main thread の `sleep(0)` spin は GIL 争いで 2〜3 ms を足す。Condition 変数で待つ。

参照（同期 write → blocking read、reader thread なし、100 回）:

| size | median | min | p95 |
|---:|---:|---:|---:|
| 8 B | 1,318 µs | 355 µs | 11,695 µs |
| 512 B | 2,523 µs | 2,092 µs | 5,273 µs |

echo 行列（frame = len16 + payload。kB/s は片方向、header 込み）:

| size | depth | frames | µs/frame | kB/s | 備考 |
|---:|---:|---:|---:|---:|---|
| 8 | 1 | 512 | 2,238 | 4.5 | |
| 8 | 4 | 512 | 2,004 | 5.0 | |
| 8 | 16 | 512 | 1,497 | 6.7 | |
| 64 | 1 | 512 | 2,558 | 25.8 | |
| 64 | 4 | 512 | 1,453 | 45.4 | |
| 64 | 16 | 512 | 1,529 | 43.2 | |
| 512 | 1 | 256 | 4,267 | 120.5 | |
| 512 | 4 | 256 | 1,637 | 314.0 | |
| 512 | 16 | 256 | 1,489 | **345.3** | |
| 4096 | 1 | 64 | 19,431 | 210.9 | |
| 4096 | 4 | 64 | — | — | **stall**: 8 送信後 4 受信、mismatch 1。16 KiB outstanding が RX/TX 8 KiB を超え、HWCDC の FIFO 置換で欠落 |
| 4096 | 16 | — | — | — | 未測定 `—`（stall 後は device が frame 途中で止まるため） |

## 事実 / 候補 / 未決

- **事実**: この経路の request 往復は最小 0.36 ms だが中央値 1.3 ms、p95 11.7 ms と jitter が大きい。stop-and-wait（現 OEP、full verify 722 往復）は jitter に直接支配される。
- **事実**: 帯域は 512 B × in-flight 4 以上で **約 320〜345 kB/s** に飽和し、frame を 4 KiB にしても上がらない（depth 1 で 211 kB/s）。
- **事実**: outstanding byte 数が device の ring（8 KiB）を超えると **HWCDC はデータを落とし、echo が欠落・不一致になる**。USB 自体は無損失でも、この transport は backpressure 下で無損失ではない。
- **候補（S1 へ）**: frame 上限は 512 B〜1 KiB、window は **frame 数ではなく byte 数**で probe が宣言し（例: 4 KiB）、client は outstanding byte がそれを超えないよう送る。62 KiB read の transport 分は ≈ 0.2 s、E153 の DMI 分 ≈ 0.3 s と合わせ full verify ≈ 0.5 s が見込み。
- **候補**: 低スペック profile（64 B、in-flight 1）と高スペック profile（512 B〜、window 4 KiB）は同じ frame 形式で値だけ違う（guidelines §5）。
- **未決**: 4 KiB × in-flight 2（8 KiB = ring と同量）の境界 `—`。HS USB での同じ行列 `—`（Phase B）。Windows native / usbipd 無しの jitter `—`。

## 反映

台帳 §1 E155 を完了。oep-spec `docs/development-guidelines.ja.md` §1・§5 に「window は byte 数で宣言」「transport は backpressure 下で落とす前提」を追記。
