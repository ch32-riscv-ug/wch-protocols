# カバレッジと実装可否

各ドキュメントを「**これだけで実装できるか**」で判定し、穴と次に集める情報をまとめる。ここの判定語は実装できるかどうかの軸で、項目ごとの裏付けの status([README の status 語彙](README.md#status))とは別: **実装可**(byte 単位で足りる)/ **部分的**(コアは足りるが一部要追加)/ **解説**(理解と参照実装への索引。byte 仕様は含まない)/ **未着手**。

## 1. 層 × 経路のカバレッジ

| | L1 物理 | PC ドライバ | L2 転送 | L3 protocol | L4 アプリ |
|---|---|---|---|---|---|
| ① probe | SWIO/RVSWD([link-to-target] V003 SWIO = end-to-end `verified` / RVSWD = 主要 frame `verified`) | [pc-usb-driver] **実装可** | USB bulk([pc-to-link] **実装可**) | WCH-Link cmd + DMI([pc-to-link]/[riscv-debug-module] **実装可**) | flash/mem/reg/halt/print([pc-to-link]/[riscv-debug-module] **実装可**、print target 側=[serial-and-print]) |
| ② factory ISP | USB / UART | [pc-usb-driver] **実装可** | USB bulk / UART | ISP `0xAx`([pc-to-device-isp] USB **実装可**、UART **部分的**) | flash/option |
| ③ custom BL | USB / UART / software USB | [pc-usb-driver] / [software-usb] | 各 | WCH IAP([wch-iap] **実装可**)、DFU/UF2/HID([custom-bootloader] HID は **実装可**、他は **解説**) | flash/OTA |
| ④ DAP | SWD/JTAG | [pc-usb-driver] | USB bulk | CMSIS-DAP([dap] **未着手**、標準へ委譲) | ARM debug |

[link-to-target]: protocols/link-to-target.ja.md
[pc-usb-driver]: protocols/pc-usb-driver.ja.md
[pc-to-link]: protocols/pc-to-link.ja.md
[riscv-debug-module]: protocols/riscv-debug-module.ja.md
[serial-and-print]: protocols/serial-and-print.ja.md
[pc-to-device-isp]: protocols/pc-to-device-isp.ja.md
[custom-bootloader]: protocols/custom-bootloader.ja.md
[software-usb]: protocols/software-usb.ja.md
[dap]: protocols/dap.ja.md
[wch-iap]: protocols/wch-iap.ja.md

**結論**: **① probe 経路(flash/read/debug/print)と ② factory ISP の USB 経路は byte 単位で実装可能**(ch32rv が実機で往復)。② の UART 経路、③ の WCH IAP の往復順序、④ DAP は capture 待ち。

## 2. ファイル別 実装可否判定

| ファイル | 判定 | これで作れるもの | 不足(byte 単位で足りない点) |
|---|---|---|---|
| [pc-to-link](protocols/pc-to-link.ja.md) | **実装可** | attach/probe info/chip info/setspeed/DMI/**高速バルク read(§5b)**/flash(stub + 直接 FLASH controller)/**option byte 書込(§6b)**/erase/power/monitor/**RV↔ARM mode 切替(§4、両方向 verified)**、**probe firmware の更新・救出・脱出(§10b。ch32rv が実装し実機往復検証済み)** | error 応答 frame 形式(§3)、IAP の異常時応答形式(§10b.5)、§12 の未解読(`armversion`、ChipReset の frame、RstOut の効果、GetRomRam / DisableDebug の実機) |
| [riscv-debug-module](protocols/riscv-debug-module.ja.md) | **実装可** | halt/resume/step/read_reg/write_reg/**read_mem32/write_mem32/write_mem16**/breakpoint/semihosting。DMCOMMAND encode の読み方も明記。H417 / LinkE FW 2.22 の hartsel 0 / 1 選択・CSR 読み出し・コア 1 の halt / resume / step は限定 `verified` | abstract autoexec 詳細(軽微)、[H417 のコア制御](#h417-core-control) |
| [pc-usb-driver](protocols/pc-usb-driver.ja.md) | **実装可** | 3 OS で device を開く。Windows 純正(CH375 IOCTL)含む | HID/CDC-GDB probe 系の driver 差(軽微) |
| [wch-iap](protocols/wch-iap.ja.md) | **実装可** | **3 世代**(BOOT 常駐 / user 先頭 / 旧 V103)の配置・entry 極性・jump、**12 シリーズ表**(FLASH_Base / CalAddr / page / USART・pin・baud / USB ID)、UART・USB frame、コマンド意味(addr 不使用・VERIFY で flush)、派生(HOST/ETH/BLE) | WCHMcuIAP の実 capture、V103 の UART 末尾、V4 系 `SW_Handler` の実体(MRS テンプレート) |
| [serial-and-print](protocols/serial-and-print.ja.md) | **実装可(USART / SDI target 側)** | USART printf、SDI printf(target・**dmdata 2 方式**)、host dmdata 対応 | — |
| [pc-to-device-isp](protocols/pc-to-device-isp.ja.md) | **実装可(USB)/ 部分的(UART)** | Identify / ReadConfig(config 12 B・BTVER・UID)/ IspKey(**XOR key = ΣUID、seed 0**)/ Erase(**sector 数 = ceil(len/1K)、最小 8**)/ Program(**56 B chunk、addr は 0 起点、256 B pad**)/ WriteConfig / IspEnd。X035(BTVER 2.60)の USB で `verified`、入り方・終わり方・RDPR の振る舞いも実測。**BOOT 選択は RM から転記済み** | UART 枠(`57 AB` + checksum、`0xC5` SetBaud)、X035 以外の系列の USB の実 byte(VID が `4348` か `1a86` か) |
| [custom-bootloader](protocols/custom-bootloader.ja.md) | **§2a/§2b 実装可、他は解説** | **BOOT 領域の番地・サイズ(全 family、RM 転記)、`BOOT_MODEKEYR`/`STATR` bit14 切替、BootAsUser 手順**、**HID scratchpad BL の protocol**(report ID・scratchpad 構造・`0x1234ABCD`・完了印・stub 一覧・app→BL hook)、**user code からの BOOT 領域書換(V003 実証、app 側 updater で BL 自己更新可)**、**ブラウザ(WebHID)host 2 実装** | wch-uf2 / Swindle DFU / PlumBL / tinyboot の実 byte(ソース未入手)、HID BL の USB capture、V00X/X035 での BOOT 領域 self-write(V003 は実証済み) |
| [software-usb](protocols/software-usb.ja.md) | **解説** | rv003usb の仕組み理解、移植の要点(pin/clock/割込)。BL の stub protocol は custom-bootloader §2b へ | USB descriptor / HID report の実 byte(capture) |
| [link-to-target](protocols/link-to-target.ja.md) | **V003 SWIO = 実装可 / RVSWD = 概ね実装可** | SWIO で DMI・memory・CPU 制御・flash・boot 切替(E123〜E131)、RVSWD の short / long / burst の frame | RVSWD の形式の選択規則、**SWIO の pulse 幅・電気条件の許容限界**(既知の動作点は実証済み) |
| [dap](protocols/dap.ja.md) | **未着手** | **mode 切替(両方向 `verified`)** | CMSIS-DAP v1/v2 判定。DAP mode の capture |
| [captures](captures/README.ja.md) | (方法論 + 実例) | capture の取り方・replay 検証・**注釈付き実 fixture(target-info-v307)** | flash/erase/DMI/ISP/DAP の実 capture は今後追加 |
| [references/probe-ecosystem](references/probe-ecosystem.ja.md) | (索引) | probe/host ツール選定、参照実装・言語の索引 | — |
| guides([overview](guides/overview.ja.md)/[advanced](guides/advanced.ja.md)) | (ガイド) | 全体像・層モデル・RE 方法論 | — |

## 3. 穴と、次に集める情報(優先順)

各項目の実測は [experiments/LEDGER.ja.md](experiments/LEDGER.ja.md) に ID(`E01`… / `C11`…)として登録し、[experiments/README.ja.md](experiments/README.ja.md) の規則(計画を先に commit、証拠の水準、未測定と合格の区別)に従って実行する。

**P1 — 要 capture(実機・軽い)**

<a id="h417-core-control"></a>

- **H417 の選択コア制御**: [hartsel 0 / 1 と CSR 読み出し、コア 1 の halt / resume / step](protocols/riscv-debug-module.ja.md#h417-core-select) は CH32H417QEU6 / LinkE FW 2.22 で確認済み([依頼 0009 の証拠](captures/fixtures/ch32h417-core-select-2026-10-10/README.ja.md)、ch32rv の実機 capture)。残るのは選択コアの reset、DetachChip・再 attach 時の両コア状態・選択保持、高速 bulk read が hartsel を保つか、他 firmware / 他 H41x、必要なら物理 SWIO capture。

<a id="chip-id-vs-memory"></a>

- **AttachChip の chip ID とメモリの device ID が同じ値か**: 11 chip(V003 / V103 / V203 / V307 / L103 / X035 / V205 / X315 / V407 / H417 / M030)で一致を確認済み([pc-to-link](protocols/pc-to-link.ja.md#chip-id-vs-memory))。残るのは V006(AttachChip の値 `0x00600620` だけ測った)と、手元に無い V467 / X305 / M103。AttachChip と、その family の device ID の番地の `read` を同じ session で取る。

- **RstOut の電気的な効果**([pc-to-link](protocols/pc-to-link.ja.md#cmd-rst-out)): LinkE 2.22 は `81 0d 01 13` を受理する。RST を NRST につないだ target で、USB と NRST の線を同時に収録する。ChipReset の frame(`81 0b 02` / `81 0b 01 02`)も WCH OpenOCD の原文で確かめる。

1. **WCH-Link error 応答 frame 形式**([pc-to-link](protocols/pc-to-link.ja.md) §3): 異常系(target 無し `0x55` 等)の capture 収集。
2. **flash/erase/DMI・ISP・HID BL の実 capture** を [captures/fixtures/](captures/fixtures/) に注釈付きで追加(ch32rv の replay fixture にある)。

**P2 — 要 capture(純正ツール)**

3. **factory ISP の残り**([pc-to-device-isp](protocols/pc-to-device-isp.ja.md)): X035 の USB は `verified`。残るは他の系列の USB と、WCHISPTool の UART capture(UART 枠、`0xC5` SetBaud)。
4. **WCH IAP の往復順序照合**([wch-iap](protocols/wch-iap.ja.md) §7): frame・配置・entry は **12 シリーズ分 EVT から確定済み**。WCHMcuIAP の capture で host↔device 順序を照合し `verified` 化。
5. **DAP mode**([dap](protocols/dap.ja.md)): mode 切替 byte は `verified`([pc-to-link](protocols/pc-to-link.ja.md) §4)。残るは DAP mode に切替えた後の CMSIS-DAP のやり取りの capture と v1/v2 判定。

**P3 — 自作 probe/線を作る場合のみ**

6. **SWIO の物理的な許容限界**([link-to-target](protocols/link-to-target.ja.md) §3): 既知の動作点ではV003の全書込み・実行まで実証済み。残るのはpulse幅の受理範囲、pull-up/open-drain条件、温度・個体差のmargin測定。
7. **RVSWD の残り**([link-to-target](protocols/link-to-target.ja.md) §3 / §5): short / long / burst の frame、区切り方、接続時の問い合わせ、速度設定と clock は LinkE 2.22 の線で `verified`。残りは非標準 DMI 番地 0x7c〜0x7f の意味、long / short の選択規則、LinkE が long 形式に答える target に接続したときの続き、host → target の切り替わりの電気的な振る舞い。
8. custom BL 各実装(wch-uf2 / Swindle DFU / PlumBL)の header/CRC/entry を source から転記([custom-bootloader](protocols/custom-bootloader.ja.md))。

## 4. 現況

**① probe 経路は byte 単位で実装可能**(driver / USB / DMI / flash / debug / print が揃った)。② factory ISP は X035 の USB 経路まで実装可能。残る主な穴は capture 依存: P1-1(error frame)、P1 の RstOut の効果、P2-3(ISP の UART と他系列)、P2-5(DAP)、P3(自作 probe 用の線タイミング)。

## 5. protocol の外側 — ツール側の穴(別軸)

protocol が読めても「手に入る道具」が LinkE に偏っている。**汎用 probe(任意 MCU をライタに)と PC 連携(UART / USB / Wi-Fi / ブラウザ)**の穴と設計案は [references/generic-probe-design.ja.md](references/generic-probe-design.ja.md) にまとめた。要点: probe は DMI ブリッジだけでよい(chip 知識は host)、latency 対策に batch + RAM stub を protocol に最初から入れる、最大の空白は driver レスのブラウザ書込。

**マイコンレス直接書込**(probe 無し、USB ケーブルだけ)の設計空間は [references/bootloader-design-space.ja.md](references/bootloader-design-space.ja.md)。要点: 勝負は BL への **entry**(常時 BL 先行 + host 検出窓 + Core が全 sketch に reboot-to-BL hook を保証 = ボタン不要)、BL は小さく保ち RAM stub で能力拡張、**第 3 の道 = board 内蔵ライタ MCU**(UIAPduino V006 の実例。entry 問題を消すが、内蔵 MCU の software USB と minichlink 固定が実地の限界 → HW USB 化と共通 protocol が要件)。

上記 2 本の**前提**は [references/ecosystem-any-hardware.ja.md](references/ecosystem-any-hardware.ja.md)。要点: 目標は UIAPduino の逆(hardware に依らず**どんな board・bare chip もつながる**)。hardware 制御度を T0(一切触れない)〜T3(出荷時 pre-flash)の 4 階層で分け、**T0 で成立する設計を core** にする。共通に保つのは host・chip 知識・protocol・識別、差し替えるのは backend(probe / 内蔵ライタ / target BL / factory ISP)で、host は capability 宣言で選ぶ。初回 probe は不可避だが「書けた board が次の probe になる」連鎖 bootstrap で軽くする。**USB ID は pid.codes(`0x1209`)で BL / app / probe の PID を分け、chip UID を serial string に**(hardware USB があっても WCH VID を名乗る権利は生じない。`4348:55E0` 衝突が反例)。
