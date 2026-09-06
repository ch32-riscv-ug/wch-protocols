# bootloader 横断調査 — データセット

調査設計は [../../bootloader-survey-plan.ja.md](../../bootloader-survey-plan.ja.md)、分析結果は [../../bootloader-survey.ja.md](../../bootloader-survey.ja.md)。

## 再実行

```sh
export WCH_ROOT=<各 repo を clone した親ディレクトリ>
python3 extract.py    # P1: projects / memory_map / series_memory / constants / files
python3 extract2.py   # P2: protocol / entry_exit / flash_ops / usb / clock_uart
python3 extract3.py   # P3: stubs / stub_args / stubs_hex / stub_disasm
```

`extract3.py` は RISC-V の objdump を使う。既定は
`$WCH_ROOT/tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-objdump`。
別のものを使うなら `RISCV_OBJDUMP=<path>` を渡す。

`reg_ops.csv` / `equiv_groups.csv` / `findings.csv` は**人手で作った**(`basis` を参照)。

### 必要な repo

`WCH_ROOT` の直下に、[§1 の対応表](../../bootloader-survey-plan.ja.md#1-調査対象インベントリ)の名前で clone されていること。

- `CH32V003` `CH32V006` `CH32V103` `CH32V205` `CH32V20x` `CH32V307` `CH32V407`
  `CH32X035` `CH32X315` `CH32L103` `CH32M030` `CH32H417` — `github.com/ch32-riscv-ug/<name>`
- `ch32fun` — `github.com/cnlohr/ch32fun`
- `rv003usb` — `github.com/YuukiUmeta-UIAP/rv003usb`(fork)
- `ch32_user_bootloader_flasher` — `github.com/YuukiUmeta-UIAP/ch32_user_bootloader_flasher`
- `ch32-device-data` — `github.com/ch32-riscv-ug/ch32-device-data`(join 用。抽出には不要)

## 規約

- **絶対パスを出力しない**。`repo` 列 + repo 相対の `path` 列に分ける。
- すべての行に `source_file` + `source_line`(または `basis` に `(L…)`)を持たせる。
- 数値は**式と評価値の両方**を残す(`64K-24K` と `40960`)。
- 末尾 3 列は `#,confidence,basis`(姉妹 repo `ch32-device-data` と同じ)。
  `confidence` の語彙は wch-protocols 側(`verified` / `attested` / `single-source` / `conflict` / `todo`)。

## テーブル

| ファイル | 行数 | 主キー | 中身 |
|---|---:|---|---|
| `projects.csv` | 29 | `project_id` | 1 行 1 project。`.template` 由来の `tmpl_*`(実型番・書込先・出力形式)と `ld_ref_path`/`ld_present` を含む |
| `memory_map.csv` | 51 | `project_id`,`region` | linker script の `MEMORY`。`constrains_size` = 1 なら series 既定より狭い(実際に縛っている)、0 なら既定と同値で無制約、空なら `.ld` が無い |
| `series_memory.csv` | 40 | `series`,`core`,`variant_label` | `EVT/EXAM/SRC/Ld/Link.ld` の series 既定。**`active=0` はコメントアウトされた品種別構成**(捨てずに残す) |
| `constants.csv` | 602 | `project_id`,`name` | `iap.h/iap.c/flash.*/main.c` の `#define` 全ダンプ。無選別。`category` は後付けラベルなので誤分類しても `value_expr` から再分類できる |
| `protocol.csv` | 103 | `project_id`,`cmd_name` | sync head・command / error 定数・`isp_cmd` の構造体レイアウト |
| `entry_exit.csv` | 13 | `project_id` | BL に留まる条件と抜け方。`blank_pattern` / `polarity`(**`==` か `!=` か**)/ `marker_addr` / `exit_method` / `deinit_steps` / `watchdog` |
| `flash_ops.csv` | 47 | `project_id`,`api`,`call_args` | flash の erase / program / buffer 呼び出しを**引数そのまま**。`granularity_bytes` はマスクや `Size_*` 引数から導出 |
| `usb.csv` | 15 | `project_id` | descriptor の形式(生バイト / マクロ)・VID・PID(vendor/HID)・`DEF_USB_IAP_MODE` |
| `clock_uart.csv` | 15 | `project_id` | UART port / baud / BRR 直値 / printf の baud |
| `stubs.csv` | 51 | `stub_id` | 拡張可能な stub。`source_form` / `is_generated` / `generated_from` / `blob_bytes` / **`reg_set`** / **`rv32ec_safe`** / `active` |
| `stub_args.csv` | 2 | `stub_id`,`scratchpad_offset` | scratchpad の引数配置(ソース中のコメント由来。**未完**) |
| `reg_ops.csv` | 41 | `impl_id`,`seq` | **言語をまたぐ比較の共通座標系**。C / asm / hex を MMIO 操作列に正規化。検証セットのみ |
| `equiv_groups.csv` | 11 | `equiv_group`,`impl_id` | 同一機能の別形態を束ねる |
| `files.csv` | 467 | `path` | 解析した全ファイルの `bytes` / `lines` / `sha256`。EVT 更新時の差分検出用 |
| `findings.csv` | 24 | `finding_id` | 所見。`axis` は調査設計 §2 の軸 ID |
| `stubs_hex/*.hex` | 34 | — | stub の生バイト(space 区切り 16 進)。**劣化なし** |
| `stub_disasm/*.asm` | 34 | — | 上を `riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric` した結果 |

## 既知の穴

- `stub_args.csv` は 2 行しか埋まっていない。minichlink の stub 引数配置は `pgm-b003fun.c` の
  呼び出し側コードを読む必要があり、P3 では未着手。
- `reg_ops.csv` は検証セット(V003 の 64 B fast program、5 実装)だけ。全 project 展開は未。
- `flash_ops.csv` の `granularity_bytes` は program 側が空の project がある
  (`FLASH_BufLoad` ループ回数から導出していないため)。
- 副対象(ETH_IAP / HOST_IAP / BLE IAP / BootAsUser)は未収録。
