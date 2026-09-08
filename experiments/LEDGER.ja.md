# 実験台帳

内部の記録。日本語のみ。規則は [README.ja.md](README.ja.md)。

観測した**事実**と、その場だけの**候補**、残った**未決**を数値で残す。**実験が通ったことを仕様の承認とはみなさない** — 仕様([../protocols/](../protocols/))の status を動かすのは [README.ja.md §6](README.ja.md) の証拠水準を満たしたときだけ。

## 読み方

- **事実** — 観測から直接言えること。解釈を混ぜない
- **候補** — その実験の中だけで試した方式。採用ではない
- **未決** — 残った疑問。次の実験 ID か [../coverage.ja.md](../coverage.ja.md) の穴番号に結び付ける
- **未測定** は `—` と書く。空欄にしない(合格と区別する)

## 1. 採番済み

機材が用意できることを確認したものだけがここに来る([README.ja.md §3.1](README.ja.md))。ID は再利用も再採番もしない。

| ID | 問い | ベンチ | 影響する doc | 状態 |
|---|---|---|---|---|
| **E001** | 実機なしで sketch を build・実行し、出力を assert できるか(profile 解決 / `socket://localhost` / 銘板) | **常設 v0**(実機なし。host Arduino core `lang-ship:host:host`) | (規則そのもの) | **完了**([e001_smoke_host/](e001_smoke_host/README.ja.md)) |
| **E002** | 実機 1 枚で upload → monitor → assert が通り、実時間が取れるか | **常設 v1**(peer 対の HOST 側 `esp32-s3-d0cf1359101c`) | (規則そのもの) | **完了 — 反証**([e002_smoke_board/](e002_smoke_board/README.ja.md)) |
| **E003** | peer は使えるか(**環境確認のみ**)— 2 台同時 upload / 2 台の間で実際に繋がっている線はどれか | **常設 v2**(peer 対 2 枚、配線変更なし) | [README.ja.md §4.4/§4.5](README.ja.md) | **完了**([e003_smoke_peer/](e003_smoke_peer/README.ja.md)) |
| **E012** | 銘板の版情報を conftest から build 時に自動で埋められるか。再ビルドのコストは | **常設 v0**(実機なし) | [README.ja.md §5](README.ja.md) | **完了**([e012_banner_autofill/](e012_banner_autofill/README.ja.md)) |
| **E013** | 同一VID:PID・異なる`bcdDevice`でHID-onlyとHID + Vendor + CDC × 1をWindowsが分離でき、Linuxでも各経路が通信できるか | **一時・専用機材**(別途用意するESP32-S3 native USB、Windows 11、Linux) | [probe-feasibility-gates](../references/probe-feasibility-gates.ja.md) Gate 2 / Gate 4 | **計画**([e013_usb_descriptor_profiles/](e013_usb_descriptor_profiles/README.ja.md)) |
| **E014** | ESP32-P4で8本のGPIOをLEDC出力に使ったまま、同じGPIOをPARLIO RXへ接続して配線なしで同時captureできるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **中断 — 選んだ初期化方法は反証**([e014_p4_parlio_internal_capture/](e014_p4_parlio_internal_capture/README.ja.md)) |
| **E015** | `io_loop_back`を使わず入力だけを追加するか、PARLIO→LEDCの順にすれば、同じ8 GPIOでLEDC PWMとPARLIO RXを共存できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了**([e015_p4_parlio_routing_order/](e015_p4_parlio_routing_order/README.ja.md)) |
| **E016** | Arduino環境からESP-IDF PARLIO driverを直接呼び、有限長PARLIO RXのDMA先をPSRAMにして8-bit captureできるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 直接DMA成立、cache警告あり**([e016_p4_parlio_psram_direct/](e016_p4_parlio_psram_direct/README.ja.md)) |
| **E017** | stock PARLIO driverでPSRAM有限長captureのdescriptor cache警告を避けられるburst size / capture size条件はあるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — burst sizeでは回避不能**([e017_p4_parlio_psram_cache_sync/](e017_p4_parlio_psram_cache_sync/README.ja.md)) |
| **E018** | `cache` tagのruntime logを抑制し、stock PARLIO driverで1 MiB PSRAM direct captureを正しく完了できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — soft delimiter上限で前提反証**([e018_p4_parlio_psram_log_suppression/](e018_p4_parlio_psram_log_suppression/README.ja.md)) |
| **E019** | `partial_rx_en`で1 MiB PSRAMをdirect ringにし、一周を検出して公開APIだけで停止後、正しい連続captureを得られるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — cache errorでInterrupt WDT**([e019_p4_parlio_psram_partial_ring/](e019_p4_parlio_psram_partial_ring/README.ja.md)) |
| **E020** | internal RAM↔PSRAM copyは4 / 16 / 64 KiB chunkで8-bit logic captureを退避できる実効帯域を持つか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — flush込み138.6〜182.7 MB/s**([e020_p4_psram_copy_bandwidth/](e020_p4_psram_copy_bandwidth/README.ja.md)) |
| **E021** | 8 MHz / 8-bit PARLIO RXを64 KiB internal ringへ連続取得し、taskから1 MiB PSRAMへdropなしで退避できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — receiver再生成で3/3成功**([e021_p4_parlio_psram_spool/](e021_p4_parlio_psram_spool/README.ja.md)) |
| **E022** | E021のinternal ring→PSRAM経路は80 MHz / 8-bit / 1 MiBでもdropなしで3回captureできるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 79.6 MB/s、3/3成功**([e022_p4_parlio_spool_80mhz/](e022_p4_parlio_spool_80mhz/README.ja.md)) |
| **E023** | 80 MHz captureと同時にSUMP基本trigger相当のpattern/mask・edge条件を全sampleでsoftware検索してもdropしないか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 15.1〜24.6 MB/s、overflow**([e023_p4_sump_basic_trigger_80mhz/](e023_p4_sump_basic_trigger_80mhz/README.ja.md)) |
| **E024** | 32-bit word内の4 sampleを並列検索すれば、80 MHz captureと同時にpattern/mask・edgeを全sample評価してdropを避けられるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 25.6〜31.0 MB/s、overflow**([e024_p4_sump_swar_trigger_80mhz/](e024_p4_sump_swar_trigger_80mhz/README.ja.md)) |
| **E025** | 32-bit software基本trigger付き8-bit captureがdropなしで成立するsample rate境界はどこか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 全条件24 MHz、28 MHzで分岐**([e025_p4_sump_trigger_rate_boundary/](e025_p4_sump_trigger_rate_boundary/README.ja.md)) |
| **E026** | 20 MHz captureをtrigger後の指定sample数で停止し、25/75・50/50・75/25のpre/post windowを構成できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 3比率、停止誤差<1 chunk**([e026_p4_sump_prepost_stop/](e026_p4_sump_prepost_stop/README.ja.md)) |
| **E027** | 1 MiB PSRAM circular ringを複数回wrapした後も20 MHzで50/50 pre/post windowを再構成できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **計画**([e027_p4_sump_circular_pretrigger/](e027_p4_sump_circular_pretrigger/README.ja.md)) |
| **E011** | `test_` を付けない規約は、実験が 10 本を超えた実プロジェクトでも誤爆から守れているか | **常設 v0**(実機なし) | [README.ja.md §1.3](README.ja.md) | **完了**([e011_collection_guard/](e011_collection_guard/README.ja.md)) |
| **E010** | 1 つの実験ファイルに複数のテスト関数を置けるか。置けないならその制約は何によるか | **常設 v0 + v1** | [README.ja.md §1.3](README.ja.md) | **完了**([e010_dut_scope/](e010_dut_scope/README.ja.md)) |
| **E009** | 実験の生ログを `_runs/` へ自動退避できるか。失敗した run でも残るか | **常設 v0**(実機なし) | [README.ja.md §3.4](README.ja.md) | **完了**([e009_runs_archive/](e009_runs_archive/README.ja.md)) |
| **E008** | SWIO write フレーム(start+addr7+rw+data32=41 bit)をパルス幅で出し、幅から復号できるか | **常設 v2** | [link-to-target](../protocols/link-to-target.ja.md) §3/§5(**status は動かさない**) | **完了**([e008_wire_swio_frame/](e008_wire_swio_frame/README.ja.md)) |
| **E007** | RVSWD host 位相の 42 bit(addr7+data32+op2+parity)を線上に出したとき、[link-to-target](../protocols/link-to-target.ja.md) §3 の仕様どおりか | **常設 v2** | [link-to-target](../protocols/link-to-target.ja.md) §3(**status は動かさない**) | **完了**([e007_wire_rvswd_frame/](e007_wire_rvswd_frame/README.ja.md)) |
| **E006** | RMT で SWIO 相当のパルス幅(290 / 890 ns)を生成・測定できるか。分解能は(**道具の性能測定**) | **常設 v2** | (道具)→ 候補 `swio-threshold` の機材要件 | **完了**([e006_tool_pulse_capture/](e006_tool_pulse_capture/README.ja.md)) |
| **E005** | 2 線でクロック同期の bit 列を取り込めるか。取り込める最短のクロック周期は(**道具の性能測定**) | **常設 v2** | (道具)→ 候補 `wire-bitstream` の機材要件 | **完了**([e005_tool_clocked_capture/](e005_tool_clocked_capture/README.ja.md)) |
| **E004** | host からの trigger で応答させれば、起動時出力の取りこぼし(E002)を回避して銘板と実時間を取れるか | **常設 v1** | (規則そのもの)+ [dmi-bridge](../protocols/dmi-bridge.ja.md) §4.1 の裏付け | **完了**([e004_smoke_board_trigger/](e004_smoke_board_trigger/README.ja.md)) |

順序に意味がある([README.ja.md §4.5](README.ja.md)):

- **E001 が最初**なのは、これが通らないうちに測った数値は測定対象ではなく環境を測っているから([§3.1.2](README.ja.md))。**実機を一切使わない**ので、board の有無に関係なく今日始められる。
- **E002** で初めて実機が出る。E001 で切り分け済みなので、ここで落ちたら原因は「実機まわり」に限定される。
- **E003** は peer を**やるかどうかを決めるための確認**であって、peer テストの実装ではない。既設の 2 枚は **GPIO19↔19 / 20↔20** が直結されており、**RVSWD が 2 線なので配線を触らずに 2 線ペアの形が既にある**。使えると分かれば選択肢として残り、使えないと分かれば早く諦められる。

## 2. 候補(未採番)

番号は付けない。参照は slug で行う。**機材の可否を確認してから** §1 へ移して採番する。

「用意」列: **有** = 手元にある / **要調達** = 買う・借りる必要がある / **不明** = 確認していない。

| slug | 問い | ベンチ種別 | 必要な機材 | 用意 | 影響する doc |
|---|---|---|---|---|---|
| `loopback-inject` | loopback phy で DMI status の fail/busy・無応答・CRC 誤りを注入したとき、host は仕様どおり回復するか | **常設 v0**(実機なし) | host Arduino core のみ | 有 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §2–§4/§6 |
| `device-lock` | device lock は 2 プロセス間で実際に効くか(片方が待つか) | **常設 v1** | 実機 1 枚 | 有 | [README.ja.md §7-9](README.ja.md) |
| `wire-bitstream` | SWIO / RVSWD の **bit 列**(start・addr7・data32・op2・parity)は [link-to-target](../protocols/link-to-target.ja.md) §3 の仕様どおりか。**タイミングは見ない** | **常設 v0**(実機なし)または **常設 v2**(E005 の道具で実線上を確認。半周期 5 us 以上) | host Arduino core / peer 対 | 有 | [link-to-target](../protocols/link-to-target.ja.md) §3 |
| `tool-fast-capture` | 受信を SPI slave / レジスタ直読み / 割り込みにすれば、実 RVSWD 速度で bit を拾えるか(E005 は 100 kbps が上限) | **常設 v2** | peer 対 2 枚 | 有 | (道具) |
| `p4-parlio-rate` | PARLIO TX等の既知patternを信号源にして、internal RAMへの8-bit有限長PARLIO RX captureが欠落・化けなしで成立するsample rate上限はどこか | **一時・配線なし** | ESP32-P4 1枚 | 有 | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 |
| `p4-parlio-spool-rate` | internal DMA ringからPSRAMへ退避するとき、dropなしで継続できる8-bit sample rate、ring/chunk size、capture時間の境界はどこか | **一時・配線なし** | PSRAM搭載ESP32-P4 1枚 | 有(E016で32 MiB確認) | 同上。E021の8 MHz成立後 |
| `p4-trigger-staged` | mask/value条件、edge、発生回数、段階遷移を組み合わせた4-stage相当triggerをdropなしで評価できるrate上限はどこか | **一時・配線なし** | ESP32-P4 1枚、内部生成pattern | 有 | 同上。SUMP multi-stage trigger相当 |
| `p4-rmt-capture` | 同じPWM/RMT信号をRMT RXのpulse-duration列で取得すると、PARLIO raw sampleより少ないdata量で何channel・何edge/sまで保持できるか | **一時・配線なし** | ESP32-P4 1枚 | 有 | 同上 |
| `p4-adc-continuous` | ESP32-P4のADC continuous DMAでanalog pinを連続captureでき、実効sample rate・欠落・noiseはどの程度か | **一時** | ESP32-P4 1枚、PWMまたはSDM出力、ADC pinへのjumper、必要ならRC | 有 | 同上 |
| `linke-error-frame` | WCH-Link の異常系 error 応答 frame の形式(target 無し等) | **常設**(capture) | LinkE + usbmon | 有 | [pc-to-link](../protocols/pc-to-link.ja.md) §3、P1-1 |
| `isp-usb-verify` | factory ISP の USB 実 frame を capture し、minichlink 転記の byte(XOR key = ΣUID・Erase sector 数・Program 56 B chunk・config 12 B の補数位置)と一致するか | **一時**(capture) | WCHISPTool(Windows)+ USBPcap、または minichlink `-c` ISP + usbmon | 不明 | [pc-to-device-isp](../protocols/pc-to-device-isp.ja.md) §3–§4、P2-3(**算法は転記で埋まった。確認待ち**) |
| `wch-iap-capture` | WCHMcuIAP_WinAPP.exe の UART(460800)/ USB(`1A86:55E0`)実 frame は [wch-iap](../protocols/wch-iap.ja.md) §3–§4 の転記(sync・checksum・VERIFY の addr・END 無応答・順序)と一致するか | **一時**(capture) | Windows + WCHMcuIAP + IAP を焼いた V003 または X035 + USBPcap / UART capture | 不明 | [wch-iap](../protocols/wch-iap.ja.md) §7、P2-4 |
| `boot-area-selfwrite` | V00X / X035 で、user code から `BOOT_MODEKEYR` 解錠後に BOOT 領域(`0x1FFF0000`、3,328 B)を erase / program できるか(V003 は `ch32_user_bootloader_flasher` で実証済み) | **一時** | X035 or V006 + probe(復旧用) | 不明 | [custom-bootloader](../protocols/custom-bootloader.ja.md) §2a、[bootloader-design-space](../references/bootloader-design-space.ja.md) §2 |
| `hid-bl-capture` | rv003usb / ch32fun bootloader と minichlink の HID feature report(ID・scratchpad 構造・`0x1234ABCD`・完了印 `0xFF`)は [custom-bootloader](../protocols/custom-bootloader.ja.md) §2b の転記と一致するか | **一時**(capture) | BL を焼いた V003 or X035 + minichlink + usbmon | 不明 | [custom-bootloader](../protocols/custom-bootloader.ja.md) §2b |
| `bl-size-baseline` | **現行の UIAPduino BL(fork `B803`)は実際に何 byte で、1,920 B のうち残りは幾らか。**関数別の内訳(`0x00`–`0x4F` の startup は静的計算で**空き 0** と出たので確認のみ) | **実機なし**(ビルドのみ) | RISC-V toolchain のみ(`.map` と `--print-memory-usage` は既に有効) | 有 | [v003-bootloader-replacement](../references/v003-bootloader-replacement.ja.md) §8.2 |
| `bl-size-entry-budget` | timeout / button / host 検出を **同時に**載せるには何 byte 足りないか(構成別ビルドの差分)。**UIAPduino 自身が `#warning` で「入らない」と記録している**(commit `dfe879d`)ので、**不足量が分かれば還元できる** | **実機なし**(ビルドのみ) | 同上 | 有 | 同 §4 / §8.2 / **§8.6.1** |
| `bl-softreboot-decouple` | **`SOFT_REBOOT_TO_BOOTLOADER` は button ブロックの中だけで参照され、`bootloader.c` L265 の POR 判定が無条件なので、button 無しでは効かない。** L265 を独立に条件化(`&& RSTSCKR != 0x10000000`、**≈8 B**)すれば **button の 20–40 B を払わずに app→BL entry が入るか** | **実機なし**(サイズ)+ **一時**(実機で app→BL を往復) | toolchain / UIAPduino | 有 / 不明 | 同 **§8.6.3**(**最小の upstream 還元候補**) |
| `bl-size-linker` | BL 専用 ld から未使用セクション(`.init_array` / `.ctors` 等)と `ALIGN(4)` の padding を削ると何 byte 戻るか | **実機なし**(ビルドのみ) | 同上 | 有 | 同 §8.3-C |
| `bl-size-flags` | `-Oz` / `-msave-restore` / 新 GCC / inline 掃引で何 byte 縮むか。`-msave-restore` は USB の C 部分の **40 サイクル制約**に触れないか | **実機なし**(縮み量)+ **一時**(動作確認) | toolchain 複数版 / UIAPduino | 有 / 不明 | 同 §8.3-B |
| `bl-size-descriptor` | **`descriptor_list`(7 × 12 B = 84 B)から空文字列 2 エントリの削除(≈28 B)と並列配列化(15–25 B)で実際に何 byte 戻るか。** HID report descriptor(17 B)の `HID_USAGE(0xff)`(upstream が `// Needed?` と書いている)は削れるか。**3 host 実装**(minichlink / rv003usb-webflasher / WebLink_USB)は通るか | **実機なし**(サイズ)+ **一時**(host 3 実装) | UIAPduino + minichlink + Chromium | 不明 | 同 §8.3-D / **§8.5.2(実数の内訳あり)** |
| `bl-no-ep1` | BL の protocol は control transfer だけなので **EP1 IN を省けるか**。**Windows / macOS / Linux が HID device として列挙するか** | **一時** | UIAPduino + 3 OS | 不明 | 同 §8.3-D2(当たれば最大の削減) |
| `sw-usb-app-footprint` | **app 側** rv003usb の flash / RAM 実消費は幾らか(`demo_terminal` の `.bss` / `.data`)。V003 の 2 KB RAM のうち何 byte 残るか | **実機なし**(ビルドのみ) | toolchain のみ | 有 | 同 §5.1 / §7-1 |
| `iap-order` | WCH IAP の host↔device 往復順序 | **一時**(capture) | WCHMcuIAP + 対応 chip | 不明 | [serial-and-print](../protocols/serial-and-print.ja.md) §6、P2-4 |
| `dap-mode` | DAP mode の切替 byte 手順と CMSIS-DAP v1/v2 判定 | **常設**(capture) | LinkE + usbmon | 有 | [dap](../protocols/dap.ja.md)、P2-5 |
| `uart-dtr-reset` | port open の DTR auto-reset で probe は reset するか。host は `hello` をいつから撃てるか | **一時** | Uno / ESP32 DevKit / Pico | 不明 | [ecosystem-any-hardware](../references/ecosystem-any-hardware.ja.md) §4.5 |
| `set-baud` | `set_baud` の切替はどの手順なら取りこぼさないか。失敗時に 115200 へ戻れるか | **一時** | CH340 / CP2102 / 内蔵 CDC の 3 種 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §9-3 |
| `dmi-latency` | DMI 1 往復の時間は transport ごとに幾らか(UART 115200/1M、USB CDC、TCP、WebSocket) | **一時** | probe(ESP32-S3 / Pico)+ target 1 個 | 不明 | [generic-probe-design](../references/generic-probe-design.ja.md) §6/§9-2 |
| `wire-autodetect` | 1 線/2 線の自動判別はどの手順で確実に効くか。誤判別する条件は | **一時** | probe + V003 と V307 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §9-1 |
| `ardulink-compat` | ardulink 互換モードの 1 byte 目自動判別は、minichlink を無改造で通すか | **一時** | probe + V003 + minichlink | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §7 |
| `flash-time` | 64 KB 書込時間は per-op / batch / batch+`poll`+`write_rep` で幾ら違うか。基準装置(LinkE + ch32rv)と比べて | **一時** | probe + V307 + **LinkE** | 不明 | [generic-probe-design](../references/generic-probe-design.ja.md) §6、[dmi-bridge](../protocols/dmi-bridge.ja.md) §4.3 |
| `lane-independence` | lane を 2 本同時に attach したとき、互いのタイミングは劣化するか。`max_inflight=1` で足りるか | **一時** | ESP32-S3 + V003 と V307 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §6.1 |
| `swio-threshold` | SWIO の LOW パルス幅は 0/1 をどこで分けるか。**動かなくなる境界は両側どこか** | **一時**(LA は任意) | probe + **実 V003**。幅の生成・測定は E006 の道具(12.5 ns 分解能)で足りる。**立ち上がり波形まで見るなら** LA 100 MS/s 以上 + marker 線 | V003 次第 | [link-to-target](../protocols/link-to-target.ja.md) §3/§5、P3-6、[dmi-bridge](../protocols/dmi-bridge.ja.md) §9-2 |
| `rvswd-frame` | RVSWD の bit フレーム(7+32+2+1 ×2)は実波形と一致するか。STOP 波形とクロック周波数は | **使い捨て** | probe + V203/V307 + **LA 3ch**(SWCLK/SWDIO/marker) | **要調達?** | [link-to-target](../protocols/link-to-target.ja.md) §3、P3-7 |
| `5v-swio` | 5 V board(Uno)から open-drain で SWIO を叩けるか。直列抵抗だけで安全か | **使い捨て** | Uno + V003 + **LA** + 抵抗 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §8.2 |

### セッションの束ね方(使い捨てベンチ)

LA を組むベンチは設営が重いので、**組んだら一度に消化する**([README.ja.md §3.1.1](README.ja.md))。現時点で見えている束は 2 つ:

- **SWIO セッション**: `swio-threshold` + `5v-swio`(どちらも SWIO 1 本 + marker。V003 のまま抵抗と board を差し替えるだけ)
- **RVSWD セッション**: `rvswd-frame`(3ch。SWIO セッションとは配線が違うので分ける)

- **BL サイズのセッション**: `bl-size-baseline` / `bl-size-entry-budget` / `bl-size-linker` / `bl-size-flags` / `sw-usb-app-footprint` は **実機も target も要らず、ビルドして `.map` を読むだけ**。設営ゼロなのでまとめて 1 回で消化できる。実機が要るのは `bl-no-ep1` と `bl-size-descriptor` の host 確認だけ。**`bl-size-baseline` の結果次第で残りが不要になる**([v003-bootloader-replacement](../references/v003-bootloader-replacement.ja.md) §8.4)ので、必ず先に 1 本だけ回す。

セッションを組む直前に、そのとき残っている候補をもう一度見て「ついでに取れるもの」を足す。

**先に `wire-bitstream` を v0 で通しておく**と、LA セッションで見るべきものが「タイミングだけ」に絞れる。符号の誤りと波形の誤りを同時に相手にしないで済むので、設営の重いベンチほどこの分離が効く。

## 3. 記録

### E001 スモーク: 実機なし(host Arduino core)— 完了 2026-09-04

計画・結果の全文: [e001_smoke_host/README.ja.md](e001_smoke_host/README.ja.md)。run: `_runs/E001_20260904T050917Z_host/`。

**事実**

1. **実機なしで build → 実行 → assert が成立する**(3/3 pass、7.3–7.5 s)。arduino-cli 1.3.1 / lang-ship:host 1.7.1 / pytest-embedded-arduino-cli 1.4.1。→ **常設 v0 は使える**。
2. **DUT はテスト関数ごとに生成される。** 同一ファイルの 2 関数目で `Connection refused`。**1 実験 1 テスト関数**にまとめれば通る。
3. **ビルド生成物は `<実験>/build/<profile>/`**(`output/` ではない)。
4. 生ログは `/tmp/pytest-embedded/<UTC>/<test 名>/dut.log`。銘板を含め期待どおりの 5 行のみ。
5. **実行の明示指定はファイルパスでなければならない**(ディレクトリでは収集 0 件)。

**候補**: 1 実験 1 テスト関数(採用)/ 銘板の `build=` に `__DATE__ __TIME__`(`banner-autofill` で置換予定)。

**未決**: `dut-scope`(DUT の scope 変更が可能か)/ `runs-archive`(退避は今回手動)/ `collection-guard`(bare `pytest` 0 件は観測したのみ)。

**反映**: 規則 §1.3(ファイルパス指定・1 実験 1 テスト関数)、§3.4(`output/` → `build/`)、§8(言語ルール)を更新。仕様の status は動かない。

### E002 スモーク: 実機 1 枚 — 完了 2026-09-04(**仮説は反証された**)

全文: [e002_smoke_board/README.ja.md](e002_smoke_board/README.ja.md)。run: `_runs/E002_*_s3_peer_host/`。

**事実**

1. **upload は通る。** `.env` の**別名パス(`/run/board-identify/by-id/...`)はそのまま port として解決された**。
2. **`setup()` の出力は 1 行も届かない**(確定的)。銘板 / `SMOKE *` / `CLOCK` の出現回数はいずれも 0。最初に届くのは `HEARTBEAT 1` で、**`HEARTBEAT 0` すら失われている**ことから遅れは **1 秒以上**。
3. **heartbeat を仕込んでいたおかげで「ボードが死んでいる」と「起動時出力の取りこぼし」を区別できた。**
4. 実クロック(反証条件 5)は **未測定 `—`**。

**候補**: (a) 銘板の周期再送 / (b) host からの DTR reset(プラグインは ESP 固有 reset を意図的に持たない)/ **(c) host の trigger に応答させる ← 本命**。

**未決**: どれを採るか、実クロックが取れるか → **E004**。

### E004 スモーク: 実機 + host trigger — 完了 2026-09-04

全文: [e004_smoke_board_trigger/README.ja.md](e004_smoke_board_trigger/README.ja.md)。run: `_runs/E004_*_s3_peer_host/`。

**事実**

1. **host が撃って probe が答える形にすれば、監視の接続タイミングに依存しない。** 3/3 で取得、**再送不要(1 発)**。
2. **実クロックが取れる。** `delayMicroseconds(1000)` に対し `micros()` 差分は **1003 us**(3 回とも同値)。仮想時計と明確に区別できる → **常設 v1 が成立**。
3. `setup()` で何も出さない設計にすると、`dut.log` に応答だけが残り読みやすい。

**候補**: **実機実験の共通の型 =「host が撃つ → probe が答える」**(採用)。E002 の候補 (a)(b) は試さずに済んだ。

**未決**: trigger を frame 化(magic+len+CRC)しても 1 発で通るか / reset 後 1 秒未満に撃った場合の挙動(候補 `uart-dtr-reset`)。

**反映**: 規則 §4.1(共有機材)・§7(実機実験の型)を更新。[ecosystem-any-hardware §4.5](../references/ecosystem-any-hardware.ja.md) と [dmi-bridge §4.1](../protocols/dmi-bridge.ja.md) に実測の裏付けを追記。

### E026 ESP32-P4: SUMP pre/post trigger停止 — 完了 2026-09-09

全文: [e026_p4_sump_prepost_stop/README.ja.md](e026_p4_sump_prepost_stop/README.ja.md)。採用run: `_runs/E026_20260908T155117Z_default/`。

**事実**

1. 20 MHzで25/75・50/50・75/25の512 Ki sample windowをすべて構成できた。
2. 3 runともqueue最大1、overflow 0、実効19.862〜19.865 MB/s、data正常。
3. descriptor単位の物理停止overshootは2,834〜2,961 sampleで、最大chunk 4,032 sample未満だった。

**候補**: descriptor単位で停止し、論理windowをsample単位でtrimする方式。

**未決**: PSRAM circular ringのwrap / multi-stage trigger / host転送時のwindow表現。

### E025 ESP32-P4: SUMP基本trigger rate境界 — 完了 2026-09-09

全文: [e025_p4_sump_trigger_rate_boundary/README.ja.md](e025_p4_sump_trigger_rate_boundary/README.ja.md)。採用run: `_runs/E025_20260908T154523Z_default/`。

**事実**

1. 16 / 20 / 24 MHzはno-match / rising / patternの全条件でqueue最大0〜1、overflow 0、data正常だった。
2. 28 MHzはpatternだけが27.871 MB/s、queue 1で追従した。no-match / risingは25.013 / 24.107 MB/s、queue 32 / 43まで滞留した。
3. 32 MHzはno-match / risingが14 / 28 overflow、patternも27.955 MB/s、queue 39で追従しなかった。

**候補**: 共通basic-trigger tier 24 MHz、保守的default 20 MHz、pattern-only tier 28 MHz。

**未決**: 24〜28 MHz間の厳密な境界 / pre/post制御の追加負荷 / multi-stage triggerのrate tier。

### E024 ESP32-P4: SUMP基本trigger 32-bit検索 80 MHz — 完了 2026-09-09

全文: [e024_p4_sump_swar_trigger_80mhz/README.ja.md](e024_p4_sump_swar_trigger_80mhz/README.ja.md)。採用run: `_runs/E024_20260908T153928Z_default/`。

**事実**

1. 32-bit SWAR検索はno-match 26.132、rising 25.605、pattern 30.926 MB/sだった。
2. 80 MHz入力に対しqueueは64まで飽和し、overflowは667 / 683 / 537件。capture dataは不成立。
3. 存在しないpatternはmatchせず、risingとpatternはmatchした。

**候補**: trigger付きcaptureのrate tier、ESP32-P4固有SIMD、edgeだけのhardware支援。

**未決**: software triggerの成立rate上限 / pre/post ring / multi-stage / hardware-assisted external trigger。

### E023 ESP32-P4: SUMP基本trigger検索 80 MHz — 完了 2026-09-09

全文: [e023_p4_sump_basic_trigger_80mhz/README.ja.md](e023_p4_sump_basic_trigger_80mhz/README.ja.md)。採用run: `_runs/E023_20260908T152754Z_default/`。

**事実**

1. 1-byteずつの全sample検索は、no-match 24.555、rising 17.324、pattern 15.078 MB/sだった。
2. 80 MHz入力に対しqueueは64まで飽和し、overflowは722 / 1,096 / 1,286件。capture dataは不成立。
3. 存在しないpatternはmatchせず、risingとpatternはmatchしたが、検出後も全sample走査する負荷には追従できなかった。

**候補**: 32-bit SWAR検索、trigger専用core、trigger有効時のrate制限。

**未決**: optimized basic trigger / pre/post ring / multi-stage / hardware-assisted external trigger。

### E022 ESP32-P4: PARLIO PSRAM退避 80 MHz — 完了 2026-09-09

全文: [e022_p4_parlio_spool_80mhz/README.ja.md](e022_p4_parlio_spool_80mhz/README.ja.md)。採用run: `_runs/E022_20260908T152201Z_default/`。

**事実**

1. 80 MHz / 8-bit / 1 MiBを3/3回、全API成功、queue overflow 0でPSRAMへ退避した。
2. captureは13,171〜13,174 us、実効79.594〜79.612 MB/s。callback / dequeueは273 / 273、queue最大1。
3. 最大duty誤差121 ppm、edge 2,620〜2,622で全8 laneが正常。

**候補**: 80 MHzでSUMP基本trigger相当のpattern / maskとedge検索負荷を測る。

**未決**: trigger検索時のdrop / pre/post trigger / 120〜160 MHz / 外部pad / 32 MiB級capture / host download。

### E021 ESP32-P4: PARLIO internal ringからPSRAM退避 — 完了 2026-09-09

全文: [e021_p4_parlio_psram_spool/README.ja.md](e021_p4_parlio_psram_spool/README.ja.md)。採用run: `_runs/E021_20260908T151406Z_default/`。

**事実**

1. receiverをcaptureごとに再生成すると、8 MHz / 8-bit / 1 MiBを3/3回、queue overflow 0でinternal 64 KiB ringからPSRAMへ退避できた。
2. captureは131,361〜131,362 us、実効7.982 MB/s。callback / dequeueは273 / 273、停止時超過は1,984 byteで全run一致。
3. 最大duty誤差12 ppm、edge 26,214〜26,216で全8 laneが正常。PSRAM syncは568〜571 us。
4. receiver再利用構成はrun 0が正常、run 1が最大duty誤差53,494 ppmとなった。APIとqueue overflowだけでは検出できない再arm時のdata不良がある。

**候補**: stock Arduino環境では64 KiB internal ring→task copy→PSRAMを使い、各armでreceiverを再生成する。

**未決**: sample rate上限 / receiver再利用不良 / basic trigger検索負荷 / pre/post trigger。

### E020 ESP32-P4: PSRAM copy帯域 — 完了 2026-09-09

全文: [e020_p4_psram_copy_bandwidth/README.ja.md](e020_p4_psram_copy_bandwidth/README.ja.md)。採用run: `_runs/E020_20260908T150231Z_default/`。

**事実**

1. 8 MiBのcopyを全9条件で行い、cache syncは全て`ESP_OK`、write/read mismatchは0件だった。
2. flush込みwriteは4 KiB chunkで181.085〜181.128 MB/s、16 KiBで182.654〜182.746 MB/s、64 KiBで138.590〜138.622 MB/s。
3. PSRAM→internal readは170.917〜183.815 MB/s。8 MiB flushは560〜575 us。

**候補**: 16 KiB前後のinternal DMA ringをtaskからPSRAMへ退避する。

**未決**: PARLIO同時動作時のdropなしrate / core分離 / trigger検索との競合 / GDMA memory copy。

### E019 ESP32-P4: PARLIO PSRAM partial ring — 完了 2026-09-08

全文: [e019_p4_parlio_psram_partial_ring/README.ja.md](e019_p4_parlio_psram_partial_ring/README.ja.md)。採用run: `_runs/E019_20260908T145625Z_default/`。

**事実**

1. 1 MiB PSRAM direct partial transactionはAPIに受理され、開始した。
2. `esp_log_level_set("cache", ESP_LOG_NONE)`後も、4,032-byte descriptorの不整列cache sync errorが出力された。
3. 27 error後にCore 1がInterrupt WDTでpanicした。2 runで再現した。
4. stack上のaddressは`esp_cache_msync` → `parlio_rx_default_desc_done_callback` → `gdma_default_rx_isr`へ復号でき、ISR内のdescriptor sync errorと一致した。

**候補**: external-memory alignmentを使うdriver修正、またはinternal DMA ringからPSRAMへのtask退避。

**未決**: PSRAM copy帯域 / spool方式のdropなしrate / driver修正のArduino組込み / 修正版ringの停止精度。

### E018 ESP32-P4: PARLIO PSRAM cache log抑制 — 完了 2026-09-08

全文: [e018_p4_parlio_psram_log_suppression/README.ja.md](e018_p4_parlio_psram_log_suppression/README.ja.md)。採用run: `_runs/E018_20260908T144655Z_default/`。

**事実**

1. 1 MiB PSRAM payloadを確保でき、PARLIO RX unitも`max_recv_size = 1 MiB`で作成できた。
2. soft delimiterの`eof_data_len = 1 MiB`は、最大65,535 byteを超えるため`ESP_ERR_INVALID_ARG`となった。8,192 byteのcontrol delimiterは成功した。
3. 単一有限長soft-delimited transactionでは1 MiB captureを開始できず、予定したlog抑制とdata検証には到達しなかった。

**候補**: PSRAM direct ringを使う`partial_rx_en`で一周を検出して停止する。

**未決**: partial direct mountの成立 / 公開APIだけでの正確な停止 / cache log抑制 / 一周後のcoherency。

### E017 ESP32-P4: PARLIO PSRAM cache sync境界 — 完了 2026-09-08

全文: [e017_p4_parlio_psram_cache_sync/README.ja.md](e017_p4_parlio_psram_cache_sync/README.ja.md)。採用run: `_runs/E017_20260908T143447Z_default/`。

**事実**

1. burst size 0 / 64 / 128 byteで、driver内部のcache警告数に差はなかった。
2. 3,968 / 4,096 / 7,936 byteは各3回とも無警告。8,064 / 8,192 byteは各runで2件警告した。
3. 全45 captureで全APIと完了後のpayload全体M2C syncが`ESP_OK`。全8 laneを復元し、最大duty誤差は6,048 ppmだった。
4. 8,064 byteでは`0xFC0` × 2、8,192 byteでは`0xFC0` + `0x800`のdescriptor callback syncが128-byte境界違反となった。

**候補**: cache logを抑制し、有限長受信完了後にpayload全体を明示M2C syncして、大容量PSRAM direct captureを測る。

**未決**: log抑制の可否 / 大容量時のdata一貫性・安定性・所要時間 / sample rate上限 / driver内部の分割条件。

### E016 ESP32-P4: PARLIO RXからPSRAMへの直接DMA — 完了 2026-09-08

全文: [e016_p4_parlio_psram_direct/README.ja.md](e016_p4_parlio_psram_direct/README.ja.md)。採用run: `_runs/E016_20260908T125457Z_default/`。

**事実**

1. **Arduino-ESP32 3.3.11からESP-IDF PARLIO APIを直接使い、有限長RXをPSRAMへ直接DMAできる。** PSRAM payloadはexternal-DMA-capableで、8 MHz設定・8-bit・8,192 sample × 3回の全APIと全laneが成功。
2. 搭載PSRAMは32 MiB。必要alignmentはinternal RAM 64 byte、external RAM 128 byte。
3. internal RAMの最大duty誤差は1,832 ppm、PSRAMは2,197 ppm。edge範囲はそれぞれ204〜206、205〜207。
4. PSRAMではdriver内部のdescriptor単位cache syncが128-byte境界違反となり、transactionごとに2件、計6件警告した。実験側の完了後payload全体M2C syncは`ESP_OK`で、dataも正しかった。

**候補**: 有限長batchはPSRAMへ直接DMAし、wait後にpayload全体を明示M2C syncする。internal RAM→PSRAM copyはfallback。

**未決**: cache警告の回避またはdriver修正(`p4-parlio-psram-cache-sync`) / direct DMAのrate・大容量安定性 / partial・continuous callback時のcoherency。

### E015 ESP32-P4: LEDC出力とPARLIO RX入力の同一GPIO共存 — 完了 2026-09-08

全文: [e015_p4_parlio_routing_order/README.ja.md](e015_p4_parlio_routing_order/README.ja.md)。採用run: `_runs/E015_20260908T095518Z_default/`。

**事実**

1. **`io_loop_back=false`なら、GPIO 2〜9でLEDC出力とPARLIO RX入力を公開APIだけで共存させられる。** LEDC→PARLIOとPARLIO→LEDCの両方で成立。
2. 最終GPIO mappingは両variantともLEDC SigOut 126〜133、PARLIO SigIn 188〜195、`InputEn=1`、peripheral output enabled。
3. 8 MHz設定・8-bit・8,192 sample × 3回で、全laneの100 kHz PWM dutyを取得。最大duty誤差は2,198 ppm、edgeは204〜207。
4. E014の失敗要因は初期化順ではなく`io_loop_back=true`による出力経路の上書きだった。

**候補**: 既存peripheralを内部観測するとき、PARLIOは`io_loop_back=false`で入力経路だけを追加する。

**未決**: sample rate上限(`p4-parlio-rate`) / PSRAMへの直接DMA([E016](e016_p4_parlio_psram_direct/README.ja.md)) / PSRAM帯域と退避限界(`p4-psram-bandwidth` / `p4-parlio-psram-spool`) / 外部pad上の電気的確認。

### E012 銘板: 版情報の自動埋め込み — 完了 2026-09-04

全文: [e012_banner_autofill/README.ja.md](e012_banner_autofill/README.ja.md)。

**事実**

1. **conftest の `pytest_configure` で設定した環境変数が compile に届く。** `build_config.toml` の `[defines]` が `-DNAME="値"` として展開される。
2. **git の short hash と dirty 判定が銘板に入る** → `dut.log` 1 つで firmware を特定できる。**未コミットで走らせたことも残る**(実験中は常に dirty なので、正直な表示として有用)。
3. **毎回変わる値を入れても host core では実行時間の差が測れない**(8.8〜9.1 s で重なる)。増分ビルドが効いている。

**候補**: **標準の銘板には git のみ。実行時刻は入れない** — コストではなく**冗長**だから(`_runs/` 名と pytest-embedded のログパスに既にある)。core 版も `sketch.yaml` の pin から git 経由で辿れる。

**未決**: 実機(ESP32)の再ビルドコストは未測定。毎回変わる値を入れないと決めたので当面は影響しない。既存 E001〜E011 の銘板は手書きのまま(記録済みのものを後から変えない)。

**反映**: `conftest.py` に `pytest_configure` を追加。規則 §5 に銘板の標準形と注入の仕組みを明記。

### E011 ハーネス: 誤爆防止の実地確認 — 完了 2026-09-04

全文: [e011_collection_guard/README.ja.md](e011_collection_guard/README.ja.md)。

**事実**

1. **命名規約だけで誤爆から守れている。** 実験 11 本の状態で、引数なし / ディレクトリ指定 / カレント全体のいずれも**収集ゼロ**。ファイル指定は 1 件収集。
2. **回帰テスト(`test_*.py`)を置けば引数なしでそれだけが拾われる** — 規則 §0 の「experiment はまとめて走らない / test はまとめて走る」が追加の仕掛けなしに成立。
3. marker やオプションによる選別は**要らなかった**。

### E010 ハーネス: 1 ファイルに複数のテスト関数 — 完了 2026-09-04

全文: [e010_dut_scope/README.ja.md](e010_dut_scope/README.ja.md)。

**事実**

1. **「1 実験 1 テスト関数」は一般的な制約ではなかった。** 実機では 2 関数とも動く(2 回とも 2 passed)。
2. **host core では 2 つ目の DUT 生成が `Connection refused` で失敗する。** 前の実行の後片付けと次の起動の競合と見られる。
3. **skip されるテスト関数でも DUT は作られる**(`pytest.skip()` は fixture setup の後)。
4. **実機では関数ごとに再 upload される**ので、関数を増やすと遅くなる。
5. `dut` fixture は function scope 固定で、scope を変える公開オプションは無い。

**反映**: 規則 §1.3 を「無条件」→「**host core 限定 + 実機では速度上の推奨**」に緩めた。**[E001](e001_smoke_host/README.ja.md) 事実 2 を一般化しすぎていたのを本実験が訂正**。

### E009 退避: `_runs/` の自動保存 — 完了 2026-09-04

全文: [e009_runs_archive/README.ja.md](e009_runs_archive/README.ja.md)。

**事実**

1. **`test_case_tempdir` に依存する autouse fixture の teardown で、成否に関わらず退避できる。** 失敗 run こそログが要るという用途に合う。
2. **peer を使う実験では `peer-device.log` も一緒に入る**(ディレクトリごとコピーするので、conftest は DUT 数を知らなくてよい)。
3. **ID は実験ディレクトリ名から導出できる**(`e001_smoke_host` → `E001`)。ID を path に入れると決めたことがここで実利になった。
4. 連続実行しても上書きしない(名前に UTC 秒を含む)。

**反映**: `experiments/conftest.py` を追加。規則 §3.4 を「手動」→「**自動**」に更新。

### E008 線: SWIO write フレームの検算 — 完了 2026-09-04

全文: [e008_wire_swio_frame/README.ja.md](e008_wire_swio_frame/README.ja.md)。run: `_runs/E008_*_s3_peer/`。

**事実**

1. **SWIO の write フレーム(start + addr7 + rw + data32 = 41 bit)をパルス幅で出し、幅から元の bit 列に復号できる**(5 ベクタ × 3 回すべて一致)。
2. **615 パルスを測って min = med = max**。ジッタが観測されない。
3. `1`(287.5 ns)と `0`(887.5 ns)は**重なりなく分離**。E006 の結果がフレーム全体でも保たれる。

**候補**: **ESP32 の SWIO phy は RMT TX で作る** — bit ごとに幅の違う symbol を並べるだけで、ソフトのタイミングループが要らない。

**未決**: read 位相は未実装(受信側が target を演じる必要がある)/ **実 CH32 の閾値は不明のまま**(`swio-threshold`)/ 立ち上がり時間は RMT では見えない。

**主張できる範囲**: 「自分の符号器が参照実装の読みどおりに出している」まで。`attested` 止まり。**[coverage](../coverage.ja.md) P3-6 は埋まっていない。**

### E007 線: RVSWD host 位相フレームの検算 — 完了 2026-09-04

全文: [e007_wire_rvswd_frame/README.ja.md](e007_wire_rvswd_frame/README.ja.md)。run: `_runs/E007_*_s3_peer/`。

**事実**

1. **RVSWD host 位相の 42 bit を線上に出せる**(5 ベクタ × 3 回すべて一致、半周期 5 us)。
2. **送信器・受信器・手計算の 3 者が一致**(全 1 → ones=41 → parity=0 → `FFFFFFFFFF80`、全 0 → parity=1 → `000000000040`)。送受が同じ誤りを共有している可能性は下がる。
3. [link-to-target §3](../protocols/link-to-target.ja.md) の bit レイアウトが**実行可能な形**になった。CH32RVProbe の RVSWD phy の出発点にできる。

**未決**: **parity の規約が確定していない**(仕様は「odd parity」としか書いておらず、この実験は「parity bit を含めて 1 の個数を奇数にする」と仮定した)。**実 CH32 が応答するかでしか決まらない**。target 応答位相・STOP 条件・初期化 100 clocks は未実装。

**主張できる範囲**: 「**自分の符号器が仕様の読みどおりに bit を並べている**」まで。送受とも自作なので `attested` 止まり。**[coverage](../coverage.ja.md) P3-7 は埋まっていない** — §3 を `verified` にするのは実 CH32 が応答したときだけ。

### E006 道具: パルス幅の生成と記録(RMT)— 完了 2026-09-04

全文: [e006_tool_pulse_capture/README.ja.md](e006_tool_pulse_capture/README.ja.md)。run: `_runs/E006_*_s3_peer/`。

**事実**

1. **分解能 12.5 ns**(`rmtInit` が 80 MHz を受け付ける)。SWIO の短パルス 290 ns = 23 tick。
2. **実測値は量子化された公称値と完全一致し、ばらつきが観測されない**(24 サンプルすべてで min=med=max)。
3. **250 ns でも取りこぼしゼロ**(RX フィルタを 0 にする)。
4. **290 ns と 890 ns は明確に分離できる** → SWIO の 0/1 を幅で見分ける道具として成立。
5. **安価な LA(24 MS/s、290 ns = 約 7 サンプル)より細かい。**

**候補**: SWIO の**送信**も RMT TX で作る(幅を tick 単位で正確に指定でき、ばらつきが無い)。将来の ESP32 phy にそのまま使える。

**限界(誤読注意)**: 示したのは**道具の精度**であって **CH32 が何を受け付けるか**ではない。`swio-threshold` は実 target が必須のまま。立ち上がり波形(~120 ns)は RMT では見えないので、形が要るなら LA が要る。

**反映**: `swio-threshold` の機材欄を「使い捨て + LA 必須」→「**一時、LA は任意**」に更新。

### E005 道具: クロック同期のビット取り込み — 完了 2026-09-04

全文: [e005_tool_clocked_capture/README.ja.md](e005_tool_clocked_capture/README.ja.md)。run: `_runs/E005_*_s3_peer/`。

**事実**

1. **peer 対の 2 線でクロック同期の bit 列を取り込める。** [link-to-target §3](../protocols/link-to-target.ja.md) の規則(data は clock LOW で変化、clock HIGH でサンプル、MSB first)がそのまま動く。
2. **道具の仕様値は半周期 5 us 以上 = 100 kbps まで**(3/3)。2 us で 2/3、1 us で 1/3、遅延なしでは **2 bit しか拾えない**。
3. **失敗の仕方は「取りこぼし」で「位相ずれ」ではない**(拾えた分の値は正しい)。サンプル位相の設計は正しく、純粋に受信側の速度が律速。

**候補**: 検証時は送信側を 100 kbps 以下に落とす(符号の検証には十分)。速くするなら **SPI slave で受ける**のが本命 → 候補 `tool-fast-capture`。

**未決**: 実速度での観測手段 / 送信側の上限(未測定)。

**反映**: 候補 `wire-bitstream` の機材要件を更新 — **LA なしで常設 v2 だけで RVSWD の bit 列を検証できる**(速度を落とす条件付き)。

### E003 スモーク: peer 2 台(環境確認)— 完了 2026-09-04

全文: [e003_smoke_peer/README.ja.md](e003_smoke_peer/README.ja.md)。run: `_runs/E003_*_s3_peer/`。

**事実**

1. **2 台の同時 build / upload / monitor が成立する。** `peers["device"]` で操作でき、両 board とも trigger に 1 発で応答。
2. **繋がっているのは GPIO19↔19 と GPIO20↔20 の 2 本**(双方向)。17・18・21 はどこにも繋がっていない。**ESP32-S3 の native USB ピン(D−=19 / D+=20)**。
3. 入力を pull-down にしたことで未接続が常に 0 になり、**`READ=1` が接続の実証**になった。仮定を検算する形ではなく**走査して topology を発見する形**にしたのが正解。

**候補**: peer 対は **2 線の直結**として使える(RVSWD の SWCLK + SWDIO と形が一致)。ただし 19/20 は native USB ピンなので、線として使う間は native USB を使えない(現在は CH340 経由の UART なので支障なし)。

**未決**: `device-lock`(2 プロセス間の排他)/ この 2 線で RVSWD の速度・波形が成立するか(別実験)。

**手順違反**: 計画を書く前に実装・実行した。最初の実装は誤った仮定を検算するだけで、失敗しても何が繋がっているかは分からなかった。**計画段階で「仮定を置かずに走査する」と決めていれば一度で済んだ** — 規則 §3.2 の実利がそのまま出た例。
