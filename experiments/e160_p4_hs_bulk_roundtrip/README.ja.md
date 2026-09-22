# E160 ESP32-P4 HS vendor bulk で OEP frame の往復は 1 frame 何 µs・何 kB/s か（E155 の HS 版）

状態: 完了（2026-09-22）

## 問い

E155 は USB-Serial/JTAG（HWCDC、usbipd/WSL 経由）で OEP frame の往復を測り、frame 512 B × in-flight ≥ 4 で ≈ 320〜345 kB/s、
往復 median 1.3 ms（8 B）だった。同じ echo を **P4 の HS OTG（EspUsbDevice vendor bulk、WinUSB flat）** で行うと、往復と帯域は
どうなるか。OEP v0 の transport 抽象（frame = len16 + message、byte window pipelining）は bulk の上でも同じ host client で動くか。

## 仮説

- 往復 median は 1 ms を切る（bulk 1 往復は usbip 経由でも数百 µs、E081）。
- 512 B × in-flight 4 で 2 MB/s 以上、4096 B で 10 MB/s 台（E104 の連続 bulk 24〜30 MB/s に対し、往復待ちがあるので 1/2〜1/3）。
- frame 境界: bulk は packet 単位だが device 側で byte stream として扱えば E155 と同じ framing で全数一致する。

## 反証条件

- 8 B の往復が 1.3 ms 以上（usbip の往復が支配的で HS の利点が無い）。
- 4096 B で 4 MB/s 未満、または不一致 / stall。

## 方法

常設 USB bench の P4（console `esp32-p4-80f1b2d0b261` = CH340 UART、native USB = usbipd bind 済みの `303a:4021` / `e104-p4-windows-v1`）。
**識別子（VID/PID/serial）は E104 と同じものを名乗る**（Windows 側の bind は device instance で決まるので、変えると管理者権限で再 bind が要る）。

1. device: `EspUsbDeviceVendor`（buffered、HighSpeed controller）で E155 と同じ echo（len16 + payload をそのまま返す、len 0 で終了）。
   console（UART）に銘板と `ECHO END frames= bytes=` を出す。EspUsbDevice 2.5.0 pin、esp32 3.3.12 pin。
2. host: pyusb（libusb、usbip 経由）で bulk IN/OUT。E155 と同じ行列: frame 8 / 64 / 512 / 4096 B × in-flight 1 / 4 / 16、
   参照として同期往復（write → read）100 回の median / min / p95。
3. 数値は E155 の表と並べる。

## 対象外

direct（unbuffered）転送、capture streaming、OEP endpoint そのものの移植（結果を見て S1 の transport 抽象へ）。

## 必要な環境

profile `esp32p4_hs`。target 無し。`303a:4021` が WSL に attach されていること（`usbipd.exe attach --wsl --busid <busid>`、bind 済みなので管理者不要）。

## ベンチ種別

常設（USB bench）。

## 記録する数値

`E160 REF size= median_us= min_us= p95_us=`、`E160 size= depth= frames= received= mismatch= us_per_frame= kB_s= stalled=`。

## 完了条件

REF 2 行と行列 12 行、不一致 0。

## 結果

状態: 完了（2026-09-22）。run: `_runs/E160_20260922T06*_default/`（A: frame ごとに flush、B: burst 終端で flush、C: host が frame を束ねて送る）。
device は HS で enumerate（bulk IN 0x81 / OUT 0x01、512 B、`speed=3`）、EspUsbDevice 2.5.0 / esp32 3.3.12 pin、全 run 不一致 0。

同期往復（write → read、100 回）:

| size | E160 median | min | p95 | E155（HWCDC）median | min | p95 |
|---:|---:|---:|---:|---:|---:|---:|
| 8 B | 982〜1,167 µs | 760 | 1,730〜2,382 | 1,318 | 355 | 11,695 |
| 512 B | 923〜1,124 µs | 641 | 1,173〜4,201 | 2,523 | 2,092 | 5,273 |

frame 1 個ずつ URB に載せる（A、B はほぼ同じ）:

| size | depth | µs/frame | kB/s | E155 kB/s |
|---:|---:|---:|---:|---:|
| 8 | 16 | 331 | 30 | 6.7 |
| 64 | 16 | 302 | 219 | 43 |
| 512 | 4 | 383 | 1,343 | 314 |
| 512 | 16 | 393 | 1,308 | — |
| 4096 | 4 | 906 | 4,524 | ≈345 |
| 4096 | 16 | 910 | 4,502 | — |

**host が depth 個の frame を 1 回の bulk write に束ねる（C）**:

| size | depth（= 束ね数） | µs/frame | kB/s |
|---:|---:|---:|---:|
| 64 | 4 | 112 | 588 |
| 64 | 16 | 48 | **1,372** |
| 512 | 4 | 215 | 2,391 |
| 512 | 16 | 99 | **5,208** |
| 1024 | 4 | 274 | 3,748 |
| 1024 | 16 | 177 | **5,810** |

## 事実 / 候補 / 未決

- **事実**: HS bulk（usbip/WSL 経由）の往復 median は 0.9〜1.2 ms で HWCDC（1.3 / 2.5 ms）より短く、p95 は 1/3〜1/5。min は 640〜840 µs で
  HWCDC の 355 µs より長い（usbip の URB 往復が支配、E081 と一致）。
- **事実**: frame を 1 個ずつ URB にすると frame あたり 300〜900 µs が天井で、depth を 4 → 16 に増やしても伸びない。device 側の flush 方針（A/B）は無関係。
- **事実**: **host が frame を束ねて 1 URB にすると 512 B × 16 で 5.2 MB/s、1 KiB × 16 で 5.8 MB/s**（束ねない場合の 4 倍、HWCDC の 15〜17 倍）。
  device は byte stream として受けて frame 境界を自分で切るだけでよい。
- **事実**: buffered build の `EspUsbDeviceVendor::write()` は FIFO が EP サイズに達するまで IN 転送を arm しない。応答が短い frame は `flush()` が要る。
- **候補（S1 transport）**: OEP client の pipelining は「window 内で送れる request を連結して 1 回の transport write にする」。byte window は URB の
  自然な上限で、HS では 4 KiB → 16 KiB 程度へ上げてよい（1 KiB frame × 16 が最良点）。device 側は burst 終端で flush。
- **未決**: usbip を介さない native host（Windows / Linux 直結）での往復 `—`。direct（unbuffered）転送での上限 `—`（E104 の 24〜30 MB/s に近づくか）。

## 反映

台帳 §1 E160。oep-spec S1（transport 抽象: 束ね送信、window）と rebuild plan Phase B。oep-probe-arduino の bulk Stream adapter と
oep-client-python の `BulkTransport`（束ね送信）はこの数値を根拠に作る。
