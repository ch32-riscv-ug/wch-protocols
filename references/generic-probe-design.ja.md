# 汎用 probe と PC 連携の検討(ツール側の穴と、作ると便利なもの)

状態: **検討メモ**(設計案。実装・実測は未)。protocol 層([../protocols/](../protocols/))はおおむね解読できたので、次は「実際に手に入る道具」の穴を埋める番。ここでは LinkE 依存を外し、**手元の任意 MCU(Pico / ESP32 / ESP32-S3 / AVR …)をライタ/probe にする**方向と、その **PC 連携(UART / USB / Wi-Fi / ブラウザ)**を検討する。

## 0. 結論(先に)

1. **probe firmware に必要なのは DMI の read/write だけ**。線層の解読で、SWIO も RVSWD も運ぶ中身は `(addr7, data32, op2)` の同じ DMI トランザクションと判明した([link-to-target.ja.md](../protocols/link-to-target.ja.md) §3)。flash アルゴリズム・halt・レジスタ・semihosting は全部その上に host 側で組める([riscv-debug-module.ja.md](../protocols/riscv-debug-module.ja.md)、[pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §6)。→ **probe は「dumb な DMI ブリッジ」でよく、chip 知識を firmware に入れなくてよい**。
2. その DMI ブリッジを **transport 非依存の小さな byte protocol** として仕様化し、**Arduino ライブラリ(bit-bang SWIO/RVSWD + transport 抽象)**として実装すれば、Pico/ESP32/ESP32-S3/AVR のどれでもライタになる。
3. PC 連携は transport ごとに性格が違う。**latency が支配的**なので、per-op 往復ではなく **batch(複数 DMI を 1 frame)+ RAM stub 実行**が必須。これを protocol に最初から入れる。
4. **いま一番欠けているのは「ブラウザから driver レスで書ける道」と「汎用 firmware 側の共通 protocol」**。個別 probe(PicoRVD / Swindle / funprog / WebLink …)はあるが、MCU × transport × host が 1 対 1 に固定されていて横展開できない。

## 1. 問題設定: なぜ LinkE がネックか、既存の自作 probe で足りないか

| 課題 | 内容 |
|---|---|
| LinkE 依存 | 唯一の「全部できる」装置だが、proprietary firmware、Windows は driver 2 系統の排他([pc-usb-driver.ja.md](../protocols/pc-usb-driver.ja.md))、入手性・個数がボトルネック。教育/量産/現場で人数分そろえにくい |
| 自作 probe の固定化 | 既存は **MCU × transport × host tool が 1 対 1**。PicoRVD=RP2040+CDC+GDB(V003 のみ)、Swindle=RP2040+CDC+GDB(V2xx/V3xx、GPL-3)、funprog=ESP32-S2+vendor HID+minichlink、rvswdio=V003+software USB HID、WebLink=ESP32+Wi-Fi WS(V003、未完成)。→ 「手元の board で、好きな host から」ができない([probe-ecosystem.ja.md](probe-ecosystem.ja.md) §1) |
| V003 probe の弱さ | USB 側が software USB(low-speed HID、timing 制約、[software-usb.ja.md](../protocols/software-usb.ja.md))。probe 自身の初回書込に別 programmer が要る。安価だが「最初の 1 台」にならない |
| ESP32-S2 の微妙さ | timing-sensitive な GPIO + critical section、vendor HID で minichlink 専用、非 V003 の検証範囲が不明。汎用ライブラリではない |
| 直接 UART ISP の非実用性 | factory bootloader は BOOT pin/`START_MODE` の入口が弱く(V003 は app が壊れると入れない)、XOR key 算法も未確定([pc-to-device-isp.ja.md](../protocols/pc-to-device-isp.ja.md))。USB 無し chip では **debug 線が唯一堅い経路** |
| ブラウザ書込の不在 | driver レスで Chromebook/学校 PC から書ける「Web ライタ」が事実上無い(WebLink は V003 限定・機能不足) |

## 2. ユースケース(誰が何を欲しいか)

| ユースケース | 必要なもの | 今の状況 |
|---|---|---|
| **手元に LinkE が無い個人**(Pico/ESP32 は持っている) | 任意 board を probe 化する firmware + host CLI | 個別 probe はあるが board と host が固定 |
| **教室・ワークショップ**(多人数、Chromebook、driver 入れられない) | 安価 probe + **ブラウザ書込**(WebSerial/WebUSB) | ほぼ無い |
| **Arduino IDE から Upload** | Upload ボタン → host command → probe | ch32rv 等の CLI 経路。probe 側が汎用化されれば自動的に恩恵 |
| **遠隔/現場更新**(bench 上の target を別室から) | **Wi-Fi/IP probe**(ESP32)+ 認証 | WebLink 的だが実用度低 |
| **量産・多台**(headless、複数 lane) | 複数 probe の安定識別、batch、power 制御 | LinkE + ch32rv で可。汎用 probe では未整備 |
| **復旧(unbrick)** | probe から target **電源制御**(power-off erase、[pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §7) | LinkE 専用機能。汎用 probe は GPIO/FET で power 制御を持つ必要 |
| **flash だけでいい**(大多数) | 高速書込 + シリアルモニタ | debug(GDB)不要。**flash-only の軽量経路**が最も需要が大きい |
| **本格 debug**(step/breakpoint/GDB) | DM 操作の完全性 + GDB server | Swindle/PicoRVD は probe 内 GDB。dumb probe なら host 側 GDB server(ch32rv gdb) |

**示唆**: 需要の山は「flash + monitor」。debug は host 側 GDB server に寄せれば probe firmware は小さく保てる。

## 3. 鍵の洞察: probe の最小責務は DMI ブリッジ

線層の解読結果を並べると、probe firmware が**本当に**やることは:

```
host  ──(transport)──►  probe  ──(SWIO 1線 / RVSWD 2線)──►  target
        dmi_read(addr7)               bit-bang / PIO で
        dmi_write(addr7, data32)      (addr7, data32, op2, parity) を送受
        + power / reset / delay
```

- **同じ抽象がすでに事実上の標準**: minichlink の programmer 抽象は `WriteReg32(reg_7bit, u32)` / `ReadReg32(reg_7bit, *u32)` / `FlushLLCommands` / `DelayUS` / `Control3v3` / `Exit`([link-to-target.ja.md](../protocols/link-to-target.ja.md) §3)。Ardulink は `'w' reg data[4]` / `'r' reg → data[4]` の 6 byte で UART に載せている。**これを整理して仕様化すればよい**(車輪の再発明ではなく、散在する抽象の標準化)。
- **chip 知識は host に置く**: family 別の FLASH controller 手順(PgStart / Buffered / V103 halfword)、SDI アドレス、stub は全部 host 側の知識([pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §6、[serial-and-print.ja.md](../protocols/serial-and-print.ja.md))。firmware は chip を知らなくてよい → 新 chip 対応は host の更新だけ。これは Swindle 型(probe 内 GDB + chip DB)の弱点を避ける。
- **速さは stub で**: 「RAM に code を送って実行」の primitive を 1 つ持てば、flash の bulk 部分は target 上の stub が担う(WCH-Link の stub 経路、ch32fun bootloader の sketchpad 発想、[software-usb.ja.md](../protocols/software-usb.ja.md) §5)。probe は依然 dumb のまま高速化できる。

## 4. 汎用 probe のアーキテクチャ案

```
┌──────────── host(PC / ブラウザ / スマホ)────────────┐
│ chip DB・flash algo・DM ops・GDB server・semihosting  │  ← 知識は全部ここ(ch32rv / Python / JS)
└───────────────┬───────────────────────────────────┘
                │  共通 byte protocol(transport 非依存)
   ┌────────────┼──────────────┬──────────────┬──────────────┐
  UART       USB CDC        USB HID      Wi-Fi TCP/WS      BLE   ← transport adapter(差し替え)
   └────────────┴──────────────┴──────────────┴──────────────┘
┌──────────── probe firmware(Arduino ライブラリ)────────────┐
│ frame parse → DMI bridge → SWIO/RVSWD bit-bang(or PIO)    │  ← chip 知識なし・小さい
│ + power/reset GPIO + batch 実行 + RAM stub run             │
└─────────────────────────────────────────────────────────────┘
```

### 共通 protocol に入れるべき最小コマンド(案)

| cmd | 内容 | 理由 |
|---|---|---|
| `hello` / `caps` | protocol 版・firmware 版・対応線(1/2 線)・power 制御有無・batch 上限・stub RAM 上限 | host が probe 能力を判定(LinkE の GetProbeInfo 相当) |
| `dmi_read(addr7)` → `(data32, status2)` | DMI 1 往復 | 全ての基礎 |
| `dmi_write(addr7, data32)` → `status2` | 同上 | |
| **`batch([...ops])`** → `[...results]` | 複数 DMI を 1 frame で | **latency 対策の核**(§6) |
| `line_reset` / `line_mode(1wire\|2wire\|auto)` | 線の初期化(RVSWD の 100 clocks + STOP 等)、1/2 線切替 | 自動判別は rvswdio の実績 |
| `target_reset(nrst)` / `power(3v3\|5v, on\|off)` / `delay_us` | 物理制御 | unbrick・power-off erase・timing |
| **`stub_load(addr, bytes)` / `stub_run(pc, args)` / `stub_poll`** | RAM に code を置いて走らせ、結果を取る | flash bulk の高速化。probe は中身を知らない |
| `uart_bridge`(任意) | target UART ⇄ host のパススルー | monitor 用途。probe に余った UART があれば |

> **→ この案は [../protocols/dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md)(`dmibridge/1`)として仕様化した。** 確定時の主な差分: (a) `lane`(複数 target レーン)を全 datagram のヘッダに追加、(b) transport 差は「境界」と「完全性」のみと整理し L1 adapter に閉じた、(c) **`stub_load`/`stub_run`/`stub_poll` は削除** — stub の load/起動/回収はすべて DMI read/write なので `batch` で表現でき、probe に DM 知識を入れずに済む、(d) BUSY 再試行は **probe 側**が吸収(下の未決事項への回答)、(e) BLE は対象外(Bluetooth SIG の手続きが重い。adapter を足せば後から可能)。

- **frame**: sync + len + cmd + payload + CRC(UART/BLE 用。USB/TCP は省略可でも統一しておく)。**版番号**を必ず持つ(将来の非互換を吸収)。
- **status**: RISC-V DTM 準拠(0 ok / 2 fail / 3 busy)をそのまま通す。busy の再試行は host か probe のどちらが担うか決める(probe 側で吸収すると host が単純)。

### firmware 側(Arduino ライブラリ)の層

1. **線層**: SWIO(pulse 幅、`digitalWrite` では遅すぎるので cycle count / 直接レジスタ / RP2040 は PIO)、RVSWD(clock 付き、比較的容易)。PicoRVD/Swindle(PIO)、cnlohr(bit-bang)が参照。
2. **DMI 層**: (addr7, data32, op2, parity) の組立・検証。
3. **protocol 層**: frame 解析、batch、stub。
4. **transport adapter**: `Stream`(UART/CDC)、HID、WiFiServer/WebSocket、BLE。Arduino の `Stream` 抽象に載せれば UART と USB CDC は同じコードで済む。

## 5. transport 比較(PC 連携の選択肢)

| transport | driver | ブラウザから | latency(概算/往復) | 帯域 | 対応 MCU | 向く用途 |
|---|---|---|---|---|---|---|
| **UART**(USB-UART 経由) | OS 標準 CDC(ほぼ不要) | **WebSerial**(Chrome 系) | ~1–3 ms(115200)/ 0.3–1 ms(921600+) | 115k–3 Mbps | **全部**(最も汎用) | 最初に実装すべき基準経路 |
| USB CDC(native) | 標準 | WebSerial | ~0.5–1 ms | 高 | RP2040 / ESP32-S3 / S2 | UART と同コードで高速 |
| USB HID | **不要**(全 OS) | WebHID(Chrome 系) | ~1 ms(1 ms poll、64B/報告) | 低〜中 | RP2040 / ESP32-S3 / S2 / (V003 software) | driver レス最優先の環境 |
| USB vendor + WebUSB | 不要(Windows は WinUSB 自動 bind 可) | **WebUSB**(Chrome 系) | ~0.3–1 ms | 高 | RP2040 / ESP32-S3 | ブラウザ + 高速の両立 |
| **Wi-Fi TCP / WebSocket** | 不要 | **WebSocket は全ブラウザ** | **~2–20 ms(LAN)** | 十分 | ESP32 / S3 / C3、Pico W | 遠隔・多人数・ブラウザ。**batch 必須** |
| BLE | 不要 | Web Bluetooth(Chrome 系) | ~10–30 ms(接続間隔) | 低 | ESP32 / C3 / S3 | 携帯機・スマホ。batch 必須、flash は遅い |

- **ブラウザ対応の現実**: WebSerial / WebHID / WebUSB は **Chromium 系のみ**(Firefox/Safari 非対応)。**WebSocket(Wi-Fi 経由)だけが全ブラウザ**で動く。→ 「どのブラウザでも」を狙うなら **Wi-Fi probe + WebSocket** が唯一、Chrome 前提でよければ WebSerial が最も簡単(UART probe がそのまま使える)。
- **ESP32(無印)**: native USB 無し → UART(内蔵 USB-UART 変換)か Wi-Fi。**ESP32-S3**: native USB OTG あり → CDC / HID / vendor(WebUSB)を選べる + Wi-Fi/BLE も。**Pico(RP2040)**: PIO で線層が決定論的、USB は CDC/HID/vendor。Pico W なら Wi-Fi も。
- Windows の driver 問題([pc-usb-driver.ja.md](../protocols/pc-usb-driver.ja.md))は **自作 probe には無い**(CDC/HID/WinUSB はいずれも標準 bind)。LinkE 固有の悩みは汎用 probe で自然に消える。
- **VID/PID の固定費で transport を選ぶ**([ecosystem-any-hardware.ja.md](ecosystem-any-hardware.ja.md) §4.5): 上表のうち **UART(既存 USB-UART bridge 経由)と Wi-Fi/WebSocket は自前 descriptor を持たないので ID 判断が不要**。USB CDC(native)/ HID / WebUSB は自前 descriptor = PID ごとに IF を固めて pid.codes か vendor program(Pico `0x2E8A`、ESP32-S3 OTG `0x303A`)。→ **主は UART(全 Arduino board に載る)と IP、自前 USB は最適化オプション**。ESP32-S3 は内蔵 USB-Serial-JTAG(`303A:1001`)を使えば UART 側に入る。UART の DTR auto-reset 副作用(port open で probe が reset)は host が `hello` 待ちで吸収する。

## 6. 性能の現実: latency が全て(概算)

DMI 1 往復あたりの時間 × 必要往復数で書込時間が決まる。

- **直接 FLASH controller 経路**(host が DMI で controller を叩く、[pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §6)は 4 byte word ごとに `write_mem32` ≈ 6 DMI + BUSY poll 数回 ≈ **~8–10 往復/word**。64 KB = 16K word → **~150K 往復**。
  - LinkE USB(~0.5 ms): ~75 s(実測でも遅い経路)。
  - UART 115200 per-op(~2 ms): **~5 分**。Wi-Fi per-op(~10 ms): **~25 分**。→ **per-op 往復は成立しない**。
- **対策 1: batch**。1 frame に数十〜数百 DMI を詰めて往復を 1 回に。UART では帯域律速へ(6 byte × 150K ≈ 900 KB → 115200 で ~80 s、921600 で ~10 s)。Wi-Fi なら帯域は余るので batch で実用域。
- **対策 2: RAM stub**。page program ループを target 上で回し、host→probe→target は**データだけ**を流す。64 KB のデータ転送は 115200 で ~6 s、921600 で <1 s、Wi-Fi で <1 s。DMI 往復は page 単位の数回に激減。**これが本命**(LinkE も stub 経路で速い)。
- **read/verify** も同様に stub(RAM 上で CRC/hash を計算して返す)で往復を削れる。Swindle の `ch32v3x_crc32.stub` がまさにこれ。

→ protocol に **batch と stub_load/run を最初から入れる**理由。

## 7. 直接 UART(ISP)経路が実用的でない理由(確認)

- factory bootloader の入口が弱い: BOOT pin に加え **app が `START_MODE` を設定して reset** する必要(V003)。app が壊れると入れない。
- XOR key 算法が chip 系列別で未確定、実 frame も capture 待ち。
- CH32M030 は工場 ISP 無し。USB 無し chip は UART ISP しか無いが上記の弱さ。
- → **debug 線(SWIO/RVSWD)からの書込が唯一「いつでも入れる」経路**。安価 probe で debug 線を叩くのが正解、という user の直感は protocol 面からも裏付けられる。

## 8. 足りていないもの・作ると便利なもの(優先順)

| 優先 | 作るもの | 何が変わるか | 依存 |
|---|---|---|---|
| **1** | **汎用 probe protocol 仕様** → **[dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md) として draft 化済み** | host も firmware も、この 1 仕様に合わせれば横展開できる。散在する minichlink/Ardulink 抽象の標準化 | 線層の解読(済) |
| **2** | **Arduino ライブラリ実装**(SWIO+RVSWD bit-bang、`Stream` transport)。最初は **RP2040(PIO)+ UART/CDC** | 「Pico を挿すだけでライタ」。ESP32/S3/AVR へ port | 1、PicoRVD/Swindle/cnlohr の線層 |
| **3** | **ch32rv に汎用 probe backend**(serial/TCP で §4 protocol を喋る) | 既存 CLI(flash/read/monitor/gdb)がそのまま自作 probe で動く | 1, 2 |
| **4** | **ブラウザ Web ライタ**(WebSerial → JS で DMI + flash algo。Wi-Fi なら WebSocket) | driver レス・Chromebook・ワークショップ。**現状最大の空白** | 1, 2。flash algo の JS 移植 |
| **5** | **Wi-Fi probe firmware**(ESP32/S3: WebSocket server + 認証)| 遠隔更新・多人数同時 | 1, 2, batch/stub |
| **6** | **power 制御付き参照 hardware**(Pico + FET で 3.3/5 V、NRST、level shift) | unbrick(power-off erase)や 5 V target を汎用 probe で | 2 |
| **7** | host 側 **GDB server の probe 非依存化**(ch32rv gdb が汎用 backend で動く) | debug も LinkE 不要に | 3 |
| 8 | UART bridge / SDI 相当 print の汎用 probe 対応 | monitor の LinkE 依存解消(dmdata は DMI で既に非依存) | 2 |

## 9. 検証計画(何を測れば設計が決まるか)

1. **線層の実測**: Pico(PIO)で RVSWD を生成し、[link-to-target.ja.md](../protocols/link-to-target.ja.md) §3 の bit フレームで V203/V307 の DMSTATUS が読めるか(attested → verified)。SWIO は V003 で pulse 幅を振って閾値を確定(§5-6 の gap 解消と同時)。
2. **latency 実測**: UART 115200/921600、USB CDC、Wi-Fi TCP、WebSocket で DMI 往復時間を測り、§6 の概算を実値に置換。
3. **batch/stub の効果**: 64 KB flash を per-op / batch / stub の 3 方式で時間比較。stub は既存 WCH-Link 用 stub(family 別)がそのまま使えるかを確認([pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §5)。
4. **ブラウザ可否**: WebSerial で UART probe に繋ぎ、JS から DMI read(DMSTATUS)まで通す最小実験。WebSocket 版は ESP32 で。
5. **driver レス確認**: Windows/macOS/Linux/ChromeOS で追加 driver 無しに開けるか(CDC/HID/WinUSB 自動 bind)。

## 10. 既存資産との関係

- **流用**: minichlink の programmer 抽象と Ardulink protocol(標準化の土台)、PicoRVD/Swindle の PIO(線層)、rvswdio の 1/2 線自動判別、ch32rv の DM/flash アルゴリズム(host 側の中身)、Swindle の CRC stub(verify 高速化)。
- **避ける**: probe firmware に chip DB / flash algo を入れる設計(Swindle 型の保守負荷、GPL 汚染)。WCH VID の詐称(host に独自 VID/PID を足す)。
- host アプリ側の理想像(probe-rs 統合 / core+service / wlink 完全化 / minichlink universal / Arduino broker)は別軸の検討として note/research にあり、本書の「probe 側の汎用化」はそのどれとも組める。

## 参照

- 自作 probe・host ツールの現況: [probe-ecosystem.ja.md](probe-ecosystem.ja.md)
- 線層(probe が出す信号): [../protocols/link-to-target.ja.md](../protocols/link-to-target.ja.md)
- host が持つ中身: [../protocols/riscv-debug-module.ja.md](../protocols/riscv-debug-module.ja.md) / [../protocols/pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §5–6
- transport の driver 事情: [../protocols/pc-usb-driver.ja.md](../protocols/pc-usb-driver.ja.md) / [../protocols/software-usb.ja.md](../protocols/software-usb.ja.md)
- **エコシステム全体の前提**(hardware 制御度の階層・共通/差替の境界・probe firmware の VID/PID は pid.codes): [ecosystem-any-hardware.ja.md](ecosystem-any-hardware.ja.md)
- **probe 無しの直接書込(target 自身の BL / board 内蔵ライタ MCU)**: [bootloader-design-space.ja.md](bootloader-design-space.ja.md)。内蔵ライタ(UIAPduino V006)が minichlink 固定で Arduino IDE から使えない実地報告は、本書 §8-1「共通 probe protocol」の必要性をそのまま示している
