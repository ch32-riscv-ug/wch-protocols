# WCH-LinkE ↔ target 線上 capture（ESP32-P4 の OEP logic capture で収録、L103 / V203 / V003）

状態: **生データ収録済み・一部解析済み**（2026-09-25）。解析の途中経過は下の「分かったこと」。低速の attach 区間は未解読。

[2026-09-11 の fixture](../wire-flash-v003-x035-2026-09-11/README.ja.md)（LA2016）と同じ 50 MHz・同じ channel の割り当て・同じ
`pattern-4k.bin` で、ch32rv の 12 の操作を target ごとに収録した。各操作の USB 往復も `--capture` で同時に記録した。

## 収録条件

| 項目 | L103 | V203 | V003 |
|---|---|---|---|
| target | CH32L103C8T6（uid 3a6dabcda282bc48） | CH32V203C8T6 | CH32V003F4P6 |
| probe | WCH-LinkE `0E028F0692F1` fw 2.22 | WCH-LinkE `FBC18F0680B0` fw 2.22 | WCH-LinkE `F90E8F067DFD` fw 2.22 |
| debug 線 | 2 線 RVSWD（PA13 / PA14） | 2 線 RVSWD（PA13 / PA14） | 1 線 SWIO（PD1） |
| channel | D0 = SWCLK（P4 GPIO14）、D1 = SWDIO（GPIO15） | D0 = SWCLK（GPIO12）、D1 = SWDIO（GPIO13） | D0 = SWIO（GPIO11） |

- 収録機: ESP32-P4（board-identify `esp32-series-30eda0e343c6`）で OEP の `oep.fixture.capture`（PARLIO、リピートモード、HS vendor
  bulk、oep-probe-arduino `OepV1Capture`）。50 MHz。すべての `.sr` で区画の間の空白（gap）なし。
- 分岐は **LinkE 側**で取った。target 側で分岐させると LinkE が target を見失った（SWDIO に T 分岐の反射）。
- **SWCLK に反射のひげが乗る**（L103 は 1〜2 sample、V203 はもっと多く 1〜4 sample）。解析の前に短いレベルを捨てる
  （`tools/dmi_decode.py` の `DEGLITCH`、既定 3 sample。V203 は 5〜6 でも frame が崩れる箇所がある）。
- ch32rv: `ch32rv 0.9.1`（`/home/mt/dev_wch/ch32rv/target/release/ch32rv`）。各操作 `--probe serial:<LinkE>` と `--capture <op>.ndjson`。
- `flash_sketch` は L103 用にビルドした 736 byte の image（`tools/l103_image.bin`）を 3 target すべてに書いた（V203 / V003 では
  動かない中身。線の記録用）。
- 収録の手順: `tools/run_suite.sh`（L103）、`tools/run_suite2.sh <dir> <LinkE> <pins> <names> <target> <image>`（V203 / V003）。
  1 操作の収録は `tools/linke_cap.py`。

## ファイル（target ごとのフォルダ）

| ファイル | 内容 |
|---|---|
| `<op>.sr` | 線の sigrok session（unitsize 1、bit k = channel k）。PulseView で開ける |
| `<op>.ndjson` | 同じ操作の ch32rv の USB 往復（captures/README の形式） |
| `<op>.json` | 収録条件、ch32rv の終了コードと出力、区画の数と開始時刻 |
| `<op>.dmi.txt` | `tools/dmi_decode.py` で RVSWD を DMI の読み書きに復号したもの（L103。V203 は復号が不完全、V003 は SWIO なので対象外） |
| `<op>.mem.txt` | 抽象コマンドを data1 / data0 と組にしたメモリ・レジスタのアクセス一覧 |
| `read_*.bin` | ch32rv の読み出し結果 |
| `stub_flash_pattern4k.bin`（l103） | 線から組み立てた RAM stub（0x20000000、512 byte） |
| `SHA256SUMS` | このフォルダの全ファイルの SHA-256 |

操作: `target_info`、`flash_pattern4k`、`verify_pattern4k`、`read_flash_4k`（0x08000000+4096）、`read_ram_256`（0x20000000+256）、
`dbg_halt`、`dbg_regs`、`dbg_step`、`dbg_resume`、`reset`、`erase_all`、`flash_sketch`。すべて ch32rv の終了コード 0。

## 分かったこと（L103、高速区間。wch-protocols / ch32rv / ch32-device-data の各セッションの解析と一致）

- 読み出しは 54 clock（target が駆動する 1 clock 多い）、書き込みは 53 clock。データは 54 clock 中の 15 ビット目から。
  parity の誤りは全操作で 0。
- **RCC_CTLR / RCC_CFGR0 / FLASH_ACTLR へのアクセスは無い**（抽象コマンドでも RAM stub でも）。
- 接続のたびに FLASH_CTLR = 0x8080（LOCK|FLOCK、リセット値）と **FLASH_STATR = 0xB020** を書き、0x1FFFF7E0 / E8 / EC / F0 を読む。
  STATR のビット 12 / 13 / 15 は L103 では予約（新しい系統では BOOT_AVA / BOOT_STATUS / BOOT_LOCK）。
- 書き込み: ch32rv の Program 0x01 で APB1PCENR = 0、KEYR と MODEKEYR の解除、**MER（全体消去）**。stub の前に
  AHB/APB2/APB1 の PCENR と SysTick CTLR を 0 にする。**戻さず**、最後の PFIC SYSRST（0xE000E048 ← 0xBEEF0080）で戻る。
  erase_all は SYSRST が無く、APB1 が止まったまま終わる。
- RAM stub は ch32rv の `stub::CH643`（488 byte）+ 0xFF の詰め物と byte 一致（ch32rv セッションが照合）。呼び出し: dcsr 0x90c3、
  dpc 0x20000000、sp 0x20002800、a0 = flag（1 解除、8 書き込み）、a1 = 番地、a2 = 長さ、buffer 0x20001000、ebreak（+0x158）で停止。
- ソフトリセット（USB `81 0b 01 01`）= DMCONTROL 0x80000001（haltreq）を 2 回 → PFIC SYSRST。ndmreset は使わない。
- ch32rv の DmiOp 1 件は線上の DMI 1 frame（reset の後の列で確認）。
- オプションバイトの鍵・領域には触れない。FLASH_OBR の読み出し値は 0x0FFFFCFC。

## 未解読・未確認

- 接続時の約 55 ms の低速区間（約 400 kHz、84 clock と 53 clock の frame）。チップ ID の読み出しなど、LinkE が自分で行う
  初期化があるはず。
- V203 の frame の区切り（ひげが多い）、V003（SWIO）の復号。
