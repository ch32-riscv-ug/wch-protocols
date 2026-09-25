# WCH-LinkE ↔ target 線上 capture（ESP32-P4 の OEP logic capture で収録、L103 / V203 / V003）

状態: **生データ収録済み・一部解析済み**（2026-09-25）。解析の途中経過は下の「分かったこと」。低速の attach 区間は L103 / V203 とも復号済み
（下の「低速区間と L103 / V203 の比較」。**LinkE は接続のたびに RCC を組み直している**。以前の版のこの README の「RCC へのアクセスは無い」は誤り）。
同じ日に、配線を変えずに追加の操作（`extra/`）と ch32rv の版ごとの比較（`versions/`）も収録した（下の「追加の収録」）。

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
| `<op>.dmi.txt` | `tools/dmi_decode.py` で RVSWD を DMI の読み書きに復号したもの（L103 は `SPLIT=two DEBOUNCE=3` で作り直し済み、低速区間を含む。V203 は未収載、V003 は SWIO なので対象外） |
| `<op>.mem.txt` | 抽象コマンドを data1 / data0 と組にしたメモリ・レジスタのアクセス一覧 |
| `read_*.bin` | ch32rv の読み出し結果 |
| `stub_flash_pattern4k.bin`（l103） | 線から組み立てた RAM stub（0x20000000、512 byte） |
| `SHA256SUMS` | このフォルダの全ファイルの SHA-256 |

操作: `target_info`、`flash_pattern4k`、`verify_pattern4k`、`read_flash_4k`（0x08000000+4096）、`read_ram_256`（0x20000000+256）、
`dbg_halt`、`dbg_regs`、`dbg_step`、`dbg_resume`、`reset`、`erase_all`、`flash_sketch`。すべて ch32rv の終了コード 0。

## 分かったこと（L103、高速区間。wch-protocols / ch32rv / ch32-device-data の各セッションの解析と一致）

- 読み出しは 54 clock（target が駆動する 1 clock 多い）、書き込みは 53 clock。データは 54 clock 中の 15 ビット目から。
  parity の誤りは全操作で 0。
- ~~RCC_CTLR / RCC_CFGR0 / FLASH_ACTLR へのアクセスは無い~~ → **誤り**。低速区間を読めていなかった。下の「低速区間と L103 / V203 の比較」。
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

- 低速区間の 85 clock の frame（201 個、L103 と V203 で bit 列が同一）の意味。
- V203 の高速区間（下）の復号。V203 の接続の後半（L103 の FLASH_CTLR / STATR 書き込みと ESIG 読み出しに当たる部分）は高速区間にあって未確認。
- V203 の frame の区切り（ひげが多い）、V003（SWIO）の復号。

## 追加の収録（2026-09-25、配線は上と同じ）

### `extra/<target>/` — 追加の操作（ch32rv 0.9.1、`tools/run_extra.sh`）

| 操作 | 内容 |
|---|---|
| `speed_{low,medium,high}_{target_info,read_ram_256,read_flash_4k}` | `--speed` を変えた接続と読み出し |
| `dmi_write_dmcontrol_01` / `dmi_read_{dmstatus,data0,abstractcs}` | `dbg dmi write 0x10 1`、`dbg dmi read 0x11 / 0x04 / 0x16` の単発 |
| `err_read_unmapped` | 0x60000000+16 の読み出し（ch32rv は 0 を返し終了コード 0） |
| `err_wrong_chip` | 違う SKU の `--chip`（ch32rv が接続後に拒否、終了コード 23） |
| `option_get` → `option_set_data`（data0 = 0x5a、data1 = 0xc3）→ `option_get_after_set` → `read_obr_after_set`（FLASH_OBR）→ `option_reset` → `option_get_after_reset` | オプションバイトの往復 |
| `protect_on` → `protect_get_on` → `protect_off` → `protect_get_off` | 読み出し保護の往復（off は全体消去を伴う） |
| `clock_flash`（`tools/clock_<target>.bin`）→ `clock_read_rcc_0` / `clock_read_actlr_0` → `clock_target_info` → `clock_read_rcc_1` → `clock_dbg_halt` → `clock_read_rcc_2` → `clock_dbg_resume` → `clock_reset` → `clock_read_rcc_3` / `clock_read_actlr_3` | 動いている target の RCC_CTLR / CFGR0（0x40021000+8）と FLASH_ACTLR（0x40022000+4）を各操作の前後で読む |
| `connect_under_reset` | `--connect-under-reset target info`（NRST は配線していない） |

- `tools/clock_sketch.ino` をビルドした image。V203 は `ch32-riscv-ug:ch32v:CH32V203:pnum=CH32V203C8T6`（PLL 96 MHz）、
  L103 は `CH32L103:pnum=CH32L103C8T6`（既定 8 MHz）、V003 は `CH32V003:pnum=ANY`（既定 24 MHz）。L103 96 MHz / V003 48 MHz は
  core が未対応でビルドできなかった。
- RCC / ACTLR の読み値（`clock_read_*.bin`、little endian）は 3 target とも全時点で同じ:
  L103 CTLR 0x03107f83 / CFGR0 0x001c040a / ACTLR 0x00000001、V203 0x03007083 / 0x0034040a / 0x00000000、
  V003 0x00004683 / 0x00000000 / 0x00000000。ただし読み出しそのものも LinkE 経由なので、接続している間だけの変化は見えない。
- `read_obr_after_set.bin`（data0/data1 = 0x5a/0xc3 を書いた後の FLASH_OBR）: L103 0x0c35acfc、V203 0x030d68fc、V003 0x030d6bdc。
- L103 は `.dmi.txt` / `.mem.txt` まで復号済み。parity の誤り 1 件が `clock_read_rcc_1` と `clock_target_info` に残る（ひげ）。

### `versions/<ch32rv 版>/<target>/` — ch32rv 0.10.0 / 0.9.0 / 0.8.0 で同じ 12 操作

- GitHub releases の x86_64-unknown-linux-gnu 版（添付の `.sha256` と一致を確認、各フォルダに `.sha256` を保存）。
  `tools/run_suite2.sh` を環境変数 `CH32RV=<その版の ch32rv>` で実行（`tools/linke_cap.py` が参照する）。
- 108 件すべて終了コード 0、区画の空白なし。どの版も `read_flash_4k.bin` = `tools/pattern-4k.bin`。
- L103 の復号結果（`.mem.txt`）の比較: target_info / reset / read_ram_256 のアクセス列は 0.8.0〜0.10.0 と 0.9.1 で一致。
  flash_pattern4k / erase_all / dbg_halt の違いは値だけ（halt した位置の dpc、復号で取れなかった「?」）で、番地と順序は同じ。
  parity の誤り（ひげ）: 0.10.0 flash_pattern4k 1・flash_sketch 1、0.9.0 dbg_regs 1、0.8.0 dbg_regs 1・flash_pattern4k 2・flash_sketch 1。

### 分かったこと（追加分から）

- **FLASH_CTLR = 0x8080 / FLASH_STATR = 0xB020 の書き込みと ESIG（0x1FFFF7E0 / E8 / EC / F0）の読み出しは LinkE の
  ファームウェアが自分で行う**。`l103/target_info.ndjson` の USB 往復は `81 0d 01 ff`、`81 0d 01 01`、`81 0c 02 01 01`、
  `81 0d 01 02`（応答 `82 0d 05 0e 10310710`、約 55 ms）、`81 11 01 05`（応答に ESIG の 20 byte）、`81 0d 01 ff` だけで、
  ch32rv はメモリ書き込みを送っていない。ch32rv の版（0.8.0〜0.10.0）で線上の列も変わらない。

## 低速区間と L103 / V203 の比較（2026-09-25 追記）

### 復号ツールの変更（`tools/dmi_decode.py`）

- `DEBOUNCE=k`: 両線に保持時間フィルタ（新しいレベルが k sample 続いて初めて変化とみなす）。V203 は 1 sample の尖りが多い。
- `SPLIT=two`: まず 250 sample（50 MHz 換算）の間で frame を区切り、既知の長さ（53 / 54 / 85 / 585）でない塊を 100 sample で区切り直す。
  低速区間は clock の周期が 105〜155 sample（L103 約 475 kHz、V203 の書き込み frame は low が 2 倍で約 320 kHz）で、
  以前の 100 sample の区切りでは frame が割れていた。閾値は `.sr` の samplerate に合わせて伸縮する。
- L103 の `.dmi.txt` / `.mem.txt`（`l103/`、`extra/l103/`、`versions/*/l103/`）は `SPLIT=two DEBOUNCE=3` で作り直した。parity の誤りは
  l103 / extra 0、versions 0.10.0 で 1、0.8.0 で 3。版の比較: target_info / read_ram_256 は 4 版で一致。flash / erase / reset の違いは 1〜2 行の
  欠け・ずれで、復号の取りこぼしと見ている（系統的な差ではない、未確定）。

### 接続時（USB `81 0d 01 02`、約 55 ms）に LinkE がすること（target_info の `.mem.txt`、L103 と V203 で同じ流れ）

1. `MEMR 0x1FFFF704`（チップ ID。L103 0x10310710、V203 0x20310500）、`REGW 0x7C0 = 0x300`（CSR 0x7C0）
2. RCC_CTLR を読んで同じ値を書く。RCC_CFGR0 を読み、**SW / 分周を 0 に**（L103 0x001c040a → 0x001c0000、V203 0x0034040a → 0x00340000）
3. CTLR の **PLLON を落とす**（0x03107f83 → 0x02107f83 → 0x00107f83）、CFGR0 = 0
4. 系統ごとの部分:
   - L103: INTR 0x40021008 = 0x009f0000、0x40023800 を読んで同じ値を書く、**FLASH_ACTLR 0x40022000 に 0x11 を書いてから 0x1 に戻す**
   - V203: CTLR を読んで同じ値を書く、INTR = 0x00ff0000、**0x4002102c = 0**、0x40023800 を読んで同じ値を書く、0x1FFFF70C を読む（0xc13e0005）
5. CFGR0 = 0x400（PPRE1 /2）、PLLMUL を**系統ごとの決まった値**に（0x001c0400 / 0x00340400）
6. PLLON を立て、PLLRDY を待つ（0x01107f83 → 0x03107f83）
7. SW = PLL（0x...0402）、SWS = PLL を確認（0x...040a）
8. （L103）FLASH_CTLR = 0x8080、FLASH_STATR = 0xB020、ESIG の読み出し

- **LinkE は元のクロックを戻さず、系統ごとの決まった値にして抜ける**（L103 CFGR0 = 0x001c040a、V203 0x0034040a）。
  reset 直後の接続（`extra/*/clock_read_rcc_0`、`clock_read_rcc_3` の `.sr` の先頭）で LinkE が最初に読む値はスケッチ自身の設定で、
  L103 は CTLR 0x00107f83 / CFGR0 0（HSI 8 MHz）、V203 は CFGR0 0x0028000a（core の 96 MHz 設定）。どちらも抜けるときには上の値になっている。
  target_info などで「読んだ値と同じ値に戻す」ように見えるのは、1 回前の接続がすでに書き換えた後だから。
- 読まずに書く値: V203 の **RCC_CFGR2（0x4002102C）= 0**（読み出し無し。HSE + PREDIV / PLL2 のアプリでは PLL の入力が変わるはず）、
  L103 の **FLASH_ACTLR は最後に決め打ちで 0x1**（読み → 読み値 | 0x10 → 読み → 0x1。reset 直後は 0 → 0x10 → 0 → 0x1）。
- `extra/` の `clock_read_*.bin` がどの時点でも同じだったのは、1 回目の読み出し自体が接続で、その時点で書き換わっていたため。
  接続中の約 10 ms は HSI で動く。（ArduinoCore-CH32 の 09-05 の観測「V307 x12 が x15 / APB1 /2 になる」とも合う形。V307 は未収録。）
- ch32rv の USB 送信には RCC の操作は無い（LinkE のファームウェアが自分で行う）。

### V203 の高速区間（`v203-100mhz/`、`v203-160mhz/`）

- V203 の書き込み・読み出しの一部で、SWCLK は **high 約 20〜30 ns、周期 60〜100 ns**（160 MHz の収録で high 3〜4 sample、low 5〜6 / 12 sample）。
  L103 の高速区間（周期約 20 sample @ 50 MHz、約 2.5 MHz）よりずっと速く、50 MHz の `v203/` はこの区間を正しく記録できていない。
- `v203-100mhz/`: 12 操作 + `clock_flash` / `clock_target_info` を 100 MHz で取り直した（すべて空白なし、rc 0、read_flash_4k = pattern）。
  ひげ除去なし（`DEBOUNCE=1`、`SPLIT=two`）で flash_pattern4k の R が 1683 件（L103 は 1646）取れ、V203 もデータは DMI で送っているとみられる。
  55 / 56 clock の frame が残り、復号は未完。
- `v203-160mhz/flash_pattern4k`: 160 MHz。格納先が足りず **空白 7 か所**（gap-marked 7、stopped reason 2）。パルス幅を見る用。
- V003（SWIO）は 1 周期 11〜14 sample @ 50 MHz（約 240 ns）で、50 MHz で足りる。
