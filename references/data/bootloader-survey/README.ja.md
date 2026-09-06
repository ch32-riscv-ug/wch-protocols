# bootloader 横断調査 — データセット

調査設計は [../../bootloader-survey-plan.ja.md](../../bootloader-survey-plan.ja.md)、分析結果は [../../bootloader-survey.ja.md](../../bootloader-survey.ja.md)。

## 再実行

```sh
export WCH_ROOT=<各 repo を clone した親ディレクトリ>
python3 extract.py    # P1: projects / memory_map / series_memory / constants / files
python3 extract2.py   # P2: protocol / entry_exit / flash_ops / usb / clock_uart
python3 extract3.py   # P3: stubs / stubs_hex / stub_disasm
python3 extract4.py   # P3: stub_args / stub_framing(extract3 の出力に依存)
python3 extract5.py   # 依頼 0005: wlink 系 loader の取り込み(ch32rv を読むだけ)
python3 extract6.py   # U6: HOST_IAP 13 project
python3 extract7.py   # U5: SDK の flash 関数を MMIO 操作列へ正規化
```

`extract3.py` は RISC-V の objdump を使う。既定は
`$WCH_ROOT/tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-objdump`。
別のものを使うなら `RISCV_OBJDUMP=<path>` を渡す。

`reg_ops.csv` / `equiv_groups.csv` / `findings.csv` / `subordinate_targets.csv` は**人手で作った**(`basis` を参照)。
`build_sizes.csv` は**実際にビルドして測った**(手順は §ビルド実測)。

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
| `stubs.csv` | 56 | `stub_id` | 拡張可能な stub。`source_form` / `is_generated` / `generated_from` / `blob_bytes` / **`reg_set`** / **`rv32ec_safe`** / `active` |
| `stub_args.csv` | 105 | `stub_name`,`scratchpad_offset` | scratchpad の引数配置。`ResetOp`→`WriteOpArb`→`WriteOp4`→`CommitOp` を追って**計算した offset**。`meaning` にはソースのコメント原文を残してあるが、**write_block 系はコメント側が誤り**(`@76` と書いてあるが実際は `@108`。逆アセンブルで確認) |
| `stub_framing.csv` | 13 | `pad_size_bytes` | HID feature report の pad サイズと report ID(`0xAA + pad_size/1024`)の対応 |
| `build_sizes.csv` | 17 | `project_id`,`config`,`toolchain` | **rv003usb BL の実測サイズ**。機能フラグ 14 構成 × 予算 1,916 B、および toolchain 3 種の比較 |
| `caladdr_validity.csv` | 12 | `project_id` | `CalAddr` を **Code FLASH 総容量**と突き合わせた結果(U1)。`parts.csv` の `flash_bytes` は零等待領域なので使わない |
| `wlink_stub_comparison.csv` | 17 | `question` | 依頼 0005 の Q1〜Q3 の突き合わせ(同一性・使用レジスタ・共通接頭辞)を機械計算したもの |
| `host_iap.csv` | 13 | `project_id` | HOST_IAP 13 project(USB host が `/APP.BIN` を読む経路)の controller / image 名 / 書込先 / APP ld |
| `host_iap_constants.csv` | 900 | `project_id`,`name` | 同 13 project の `#define` 全ダンプ(GB18030 も読む) |
| `iap_reserve_compare.csv` | 9 | `series` | UART/USB IAP と HOST_IAP の BL 予約サイズ比較 |
| `reg_ops_sdk.csv` | 593 | `impl_id`,`function`,`seq` | **SDK の flash 関数を MMIO 操作列へ正規化**(U5)。12 series × 10 関数 |
| `reg_ops_signature.csv` | 10 | `function` | 上の署名比較。**どの series が同じ操作列か**= driver class の根拠 |
| `port_matrix.csv` | 13 | `series` | **統一 BL の移植パラメータ 1 枚**。protocol/entry は本調査、chip の事実は `ch32-device-data` から join(`*_cdd` 列)。→ [unified-bootloader-design.ja.md](../../unified-bootloader-design.ja.md) |
| `subordinate_targets.csv` | 9 | `project_id` | 副対象(ETH_IAP 2 / BLE IAP・OTA 3 / HOST_IAP 1 / BootAsUser 3)の領域構成と magic |
| `reg_ops.csv` | 41 | `impl_id`,`seq` | **言語をまたぐ比較の共通座標系**。C / asm / hex を MMIO 操作列に正規化。検証セットのみ |
| `equiv_groups.csv` | 16 | `equiv_group`,`impl_id` | 同一機能の別形態を束ねる |
| `files.csv` | 467 | `path` | 解析した全ファイルの `bytes` / `lines` / `sha256`。EVT 更新時の差分検出用 |
| `findings.csv` | 43 | `finding_id` | 所見。`axis` は調査設計 §2 の軸 ID |
| `stubs_hex/*.hex` | 34 | — | stub の生バイト(space 区切り 16 進)。**劣化なし** |
| `stub_disasm/*.asm` | 34 | — | 上を `riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric` した結果 |

## ビルド実測(`build_sizes.csv` の再現)

`rv003usb` の fork を作業ディレクトリへ複製し、`ch32v003fun` submodule の位置に
`cnlohr/ch32fun` を置いてから `bootloader.c` の機能 `#define` を切り替えてビルドする。

```sh
make bootloader.elf PREFIX=riscv-none-elf     # FLASH: <n> B / 1916 B が出る
```

toolchain は `$WCH_ROOT/tools/` 配下の 4 種を使った(`riscv-none-embed-gcc 8.2.0` /
`riscv-none-elf-gcc 14.3.0` / `riscv32-wch-elf-gcc 15.2.0` / `riscv-wch-elf-gcc 12.2.0`)。

> **注意**: fork の `ch32v003fun` submodule が未チェックアウトだったため upstream HEAD で代替した。
> **絶対値は upstream の CI と一致しない可能性がある。差分(`delta_vs_baseline`)は同一条件なので頑健**。

## 他 repo が持つデータ(ここには置かない)

- **flash 消去後の読み出し値**(系統 A = `0xFFFFFFFF` / B = `0xe339e339`)は
  `ch32-device-data` の `evidence/flash_geometry.csv` が一次ソース
  (`erased_read_word/half/byte_even/byte_odd` = RM 原文、**`blank_check_word` = word 幅に
  正規化した比較用の値**)。当初ここに暫定 CSV を置いていたが、依頼 `R-31`
  ([request-ch32-device-data.ja.md](request-ch32-device-data.ja.md))が反映されたので**削除した**。
  RM のページ番号は依頼書 §5 に残してある。

## 既知の穴

- `reg_ops.csv` は検証セット(V003 の 64 B fast program、5 実装)だけ。**全 series 展開は `reg_ops_sdk.csv` が別テーブルで持つ**(手作業版と自動版の二本立て)。
- `flash_ops.csv` の `granularity_bytes` は program 側が空の project がある
  (`FLASH_BufLoad` ループ回数から導出していないため)。
- wlink 系 loader の `a0` bit2/bit3 と作業 RAM の意味は未確定(U10)。逆アセンブルだけでは出ない。
- **GB18030 のヘッダがある**(BLE の `ota.h` 等)。`grep` が binary 扱いして黙って取り落とすので、
  抽出スクリプトを広げるときは `iconv -f GB18030` を通すこと(F34)。
