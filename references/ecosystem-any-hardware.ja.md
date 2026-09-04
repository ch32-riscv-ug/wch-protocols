# 「どんなものでもつながる」エコシステムの前提 — hardware 制御度の階層と USB ID 方針

状態: **検討メモ**(方針。実装は未)。[generic-probe-design.ja.md](generic-probe-design.ja.md)(probe 側)と [bootloader-design-space.ja.md](bootloader-design-space.ja.md)(BL 側)の**上位**に置く前提整理。

UIAPduino は **自分の hardware に合わせてエコシステムを組む**(内蔵ライタ MCU、pin 配置、firmware を board に固定)。ここで目指すのは逆で、**hardware に手を入れられるかどうかに依らず、任意の board・bare chip がつながる**こと。すると方針は「どれだけ hardware を触れるか」で**分岐**し、その全分岐で**共通に保つもの**を先に決める必要がある。加えて、firmware が自前の USB device を名乗る以上 **VID/PID をどうするか**が避けられない。

## 0. 結論(先に)

1. **不変にするもの**: host tool・chip 知識・書込/デバッグ protocol・識別方式。**変わるもの**: 「target にどう届くか」の backend(外付け probe / 内蔵ライタ / target 自身の BL / factory ISP)だけ。host は接続されたものの **capability を見て backend を選ぶ**。
2. hardware 制御度は **4 階層**(§1)。どの階層でも **初回 1 回だけは probe 経由の bootstrap が要る**(custom BL を入れるため)。エコシステムの仕事はその bootstrap を「何でも probe になる」ことで軽くすること — **既に書けた board が次の board の probe になる**(連鎖 bootstrap)。
3. **USB VID/PID**: 自分で VID を買う必要はない。無償経路は 3 系統(§4.2b)— **(A) vendor の community PID**(Espressif `0x303A` / Raspberry Pi `0x2E8A`。無償・公認だが **silicon 縛り**、WCH には無い)、**(B) pid.codes `0x1209`**(OSS 条件、**cross-MCU 可**。CH32 上の自前 firmware はこれ一択。UIAPduino V006 が `0x1209:0xB806`)、**(C) 検証用共有 ID**(`0x1209:0x0001–0x0010` 等。開発のみ・配布不可)。hardware USB を持つ chip でも **WCH の VID を第三者 firmware が名乗る権利は生じない**。ESP32-S3 は **内蔵 USB-Serial-JTAG(`303A:1001`、Espressif 自身の device)に protocol を載せれば ID 問題が消える**。個体識別は chip UID を USB serial string に。host の正体判定は ID でなく **protocol handshake** で行う。
4. **BL mode / app mode / probe firmware は PID を分ける**(または少なくとも enumeration だけで判別できる形にする)。同じ VID:PID で interface 構成を変えると Windows の driver cache が壊れる。
5. **USB(自前 descriptor)は「PID ごとに IF を固めて OSS 申請」という固定費**。**既存 USB-UART bridge 上の UART と、IP(Wi-Fi/WebSocket)はその固定費を払わない**(§4.5)。→ 主 transport は **UART(全 Arduino board に ID 判断ゼロで載る)と IP(ESP32 系・全ブラウザ)**、自前 USB は高速化・HID が要るときの最適化オプションに格下げ。境界: 自前 MCU の native CDC は自前 descriptor なので ID が要る。ESP32-S3 内蔵 USB-Serial-JTAG は bridge 側なので不要。

## 1. hardware 制御度の 4 階層と、そこで使える手段

| 階層 | 状況 | 使える「届き方」 | entry(BL に入る) | 制約 |
|---|---|---|---|---|
| **T0 bare / 既製 board、hardware 一切触れない** | 市販 board、他人の基板、breadboard の bare chip | 外付け probe(LinkE / 汎用 Pico probe)、**target 自身の BL**(初回 probe で導入後)、factory ISP(HW USB 持ち + BOOT 操作可なら) | **Core の hook + host 検出窓 + USB 抜き差し**のみ。button 無し前提 | 最も厳しい。ここで動く設計が全階層で動く |
| **T1 pin header / button を足せる** | 自作基板の軽い改造、ジャンパ | T0 + BOOT/NRST button、UART 配線 | + button / double-tap / UART magic | BOM ほぼゼロ |
| **T2 board を自分で設計** | 自前 board | T1 + **内蔵ライタ MCU**([bootloader-design-space.ja.md](bootloader-design-space.ja.md) §6b)、power 制御 FET | **entry 問題そのものが消える** | BOM +$0.1〜、pin 分離設計 |
| **T3 board + 出荷時 pre-flash** | 配布する製品 | T2 + BL/ライタ firmware を製造時に焼く | 同上。ユーザーは挿すだけ | 版管理・lot 管理 |

- **設計の順序は T0 から**: T0 で成立する「target 自身の BL + Core hook + 汎用 probe」を core にし、T1–T3 は**追加**で楽になるだけ、にする。UIAPduino の逆(T2/T3 前提で組む)をやると T0 が置き去りになる。
- **T0 の唯一の避けられない前提**: custom BL を入れる**初回は probe が要る**(factory ISP は入口が弱く XOR key 未確定、[pc-to-device-isp.ja.md](../protocols/pc-to-device-isp.ja.md))。→ §2。

## 2. bootstrap の連鎖 — 「1 台目さえあれば増える」

```
LinkE か 汎用 Pico probe(1 台)
   └→ board A に BL + Core firmware を焼く
        └→ board A(probe firmware も持てる)で board B を焼く
             └→ board B で C を …
```

- probe firmware を「dumb DMI ブリッジ」([generic-probe-design.ja.md](generic-probe-design.ja.md) §3)として **Core に同梱可能な小さなライブラリ**にしておけば、**エコシステムの任意の board が probe になれる**(rvswdio_programmer が V003 で実証。UIAPduino V006 の内蔵ライタもこれ)。
- これで T0 の「初回 probe」問題は「知り合いが 1 台持っていれば済む」に落ちる。教室なら先生の 1 台で全員分。
- 前提: probe firmware の **VID/PID と protocol が全 board で共通**(§3)。board ごとに違うと host が追えない。

## 3. 何を共通にし、何を差し替えるか

```
                 ┌──────────── 共通(不変)────────────┐
                 │ host tool(ch32rv / Python / ブラウザ)│
                 │ chip 知識(family・FLASH controller・stub)│
                 │ 書込/デバッグ protocol(DMI 層・BL 層)  │
                 │ 識別方式(VID:PID 体系・UID serial)     │
                 └───────────────┬───────────────────┘
        ┌──────────┬─────────────┼─────────────┬──────────────┐
   外付け probe   内蔵ライタ    target 自身 BL   factory ISP    (将来: Wi-Fi probe …)
   (DMI 経由)   (DMI 経由)    (USB/UART 直)   (USB/UART 直)
        └──────────┴─────────────┴─────────────┴──────────────┘
                          差し替え(backend)
```

host は backend ごとの **capability** を問い合わせて挙動を変える:

| capability | 外付け probe | 内蔵ライタ | target 自身 BL | factory ISP |
|---|---|---|---|---|
| flash / verify | ○ | ○ | ○ | ○ |
| read-back | ○ | ○ | ○(BL 次第) | × |
| 部分書込 | ○ | ○ | ○(BL 次第) | ×(全消去) |
| halt / step / GDB | ○ | ○ | × | × |
| app 非協力でも入れる | ○ | ○ | △(窓 + 抜き差し) | △(BOOT 操作) |
| target 電源制御 / unbrick | ○(LinkE / FET 付き) | ○(設計次第) | × | × |
| Serial monitor | probe CDC / dmdata | ライタ UART bridge | app の CDC(HW USB) | × |

→ ch32rv の route 概念(probe / isp / boot)はこの backend 抽象の萌芽。**backend の capability 宣言**を契約に入れると、Arduino IDE の Upload は「接続されているもので最善を選ぶ」だけになる。

## 4. USB VID / PID / serial の方針

### 4.1 前提: VID は「chip」ではなく「device を出荷する者」のもの

- USB-IF は VID を **企業**に割り当てる。**その企業が出荷する USB device**(= enumerate される firmware + hardware の組)がその VID を名乗る。
- **chip に hardware USB があることと、その chip 上で動く第三者 firmware が chip vendor の VID を名乗ることは無関係**。WCH の factory ISP(`4348:55E0` / `1A86:55E0`)や EVT の IAP demo(`1A86:55E0`)が WCH VID なのは、**WCH 自身の firmware**だから。私たちの BL / Core / probe firmware が `0x1A86` を名乗るのは、WCH の識別子を無断で使うことになる(WCH が第三者向けの VID 使用許諾を公開している事実は確認できていない)。
- 「他社の VID:PID を流用する慣行」(例: 某社 DFU ID の非公式再利用)は動く場合があっても**権利上グレーで、衝突を生む**。この repo でも既に **LinkE の IAP mode と factory ISP が同じ `4348:55E0` を名乗るため BTVER/chip 種別で判別している**([pc-to-device-isp.ja.md](../protocols/pc-to-device-isp.ja.md))— 同じ ID を複数の異なる device が使うと host は苦労する、という実例。
- **driver の観点では vendor VID は不要**: CDC / HID / MSC / DFU は class driver で VID を問わない。WinUSB 自動 bind(MS OS descriptor)も任意の VID で動く([pc-usb-driver.ja.md](../protocols/pc-usb-driver.ja.md))。VID が要るのは**識別**のためだけ。

### 4.2 答え: pid.codes + chip UID

| 項目 | 方針 | 根拠 / 事例 |
|---|---|---|
| **VID** | **`0x1209`(pid.codes)** | open-source hardware/firmware に無償で PID を配る仕組み。**UIAPduino V006 が `0x1209:0xB806`** で採用。代替: `0x1D50`(OpenMoko、同趣旨)。USB-IF 正規取得は数千ドル・企業向け |
| **PID** | エコシステムで **複数取る**: (a) BL mode、(b) Core app 既定(CDC 等)、(c) probe firmware。最低 3 つ | pid.codes は project 単位で PID を申請(公開 repo と OSS license が条件)。1 PID を用途で使い分けず、mode ごとに分ける(§4.3) |
| **serial string** | **chip UID(ESIG の unique ID。ChipInfo の UUID と同源)を hex で載せる** | 個体識別が **抜き差し・port 変更・複数台で安定**。ch32rv は既に USB serial で probe を識別(lock も serial 単位)。software USB(rv003usb)でも string descriptor は自由に生成できる |
| **product / manufacturer string** | エコシステム名 + board 名(board 側で override 可) | host の表示・GUI 選択用。識別には使わない |
| **bcdDevice** | BL/probe firmware の版 | host が「古い BL」を検出して更新を促せる |
| board 固有 ID | **descriptor でなく protocol 内**(`hello`/`caps` 応答に board id・pin 構成・power 制御有無) | T2/T3 の board 差は VID:PID に持ち込まず、能力宣言(§3)で吸収 |

- **software USB(V003)の場合**も同じ。descriptor は完全に自前なので pid.codes の PID と UID serial をそのまま入れられる(UIAPduino の patch が VID/PID/manufacturer を書き換えたのがこれ)。
- **既製 board(T0)で app mode の USB を持たせる**とき、board 製造者の VID:PID を勝手に名乗らない。エコシステム既定の(b)を使い、board 側で正規に持っている ID があれば override できる設計にする。

### 4.2b 自分で VID を買う必要はない — 無償で使える ID の全種類と、それぞれの限界

USB-IF から VID を正規取得するのは約 $6,000(一回、logo 無し)で個人・非商用には非現実的。だが**無償で正当に使える経路が 3 系統**あり、限界がそれぞれ違う。

| 経路 | ID | 条件 | **限界** | 向く用途 |
|---|---|---|---|---|
| **A. chip vendor の community PID** | **Espressif `0x303A`**([espressif/usb-pids](https://github.com/espressif/usb-pids))、**Raspberry Pi `0x2E8A`**([raspberrypi/usb-pid](https://github.com/raspberrypi/usb-pid)) | その vendor の silicon を使った製品なら**無償で PID を申請**できる(GitHub PR)。vendor 公認 = 権利上いちばん綺麗 | **silicon 縛り**: `0x303A` は Espressif chip 上でのみ、`0x2E8A` は RP2040/RP2350 上でのみ。**Pico と ESP32 と CH32 で共通の 1 ID にはできない**。WCH には同種の公開 program が(確認できる範囲で)**無い**→ CH32 上の自前 firmware には使えない | ESP32-S3 probe / Pico probe を**それぞれ**の vendor ID で出す |
| **B. 寄贈 VID の sub-allocation** | **pid.codes `0x1209`**、OpenMoko `0x1D50` | **open source**(hardware/firmware を OSI 系 license で公開)なら無償。project 単位で PID 申請(公開 repo 必須) | **cross-MCU で使える**のが最大の利点。ただし USB-IF 非公認の sub-allocation なので **USB-IF 認証/logo は不可**(hobby/OSS では無問題)。closed-source には使えない。1 project 1 PID が原則(複数は理由付きで) | **MCU を問わず同じ protocol を喋る probe firmware / BL に 1 ID**(本書の推奨) |
| **C. 検証用の共有 ID** | pid.codes **testing 範囲 `0x1209:0x0001`〜`0x0010`**、V-USB 系 shared ID(`0x16C0:0x05DC` vendor、`0x16C0:0x27DD` CDC 等) | 登録不要で即使える | **配布不可・開発/検証のみ**(誰でも使う=衝突前提)。V-USB 方式は「manufacturer/product **string で自分の device を識別**」が使用条件(VID:PID だけでは区別できない)。TinyUSB 例の `0xCAFE` や `0xF055`(FOSS)は**誰の割当でもない**=権利ゼロ・衝突リスク、手元実験限定 | 手元の試作・capture 取り・ブラウザ実験 |

**`0x6666`("Prototype product Vendor ID")について** — C 列の代表格で、CMSIS-DAP 界隈で長く使われてきた:

- usb.ids に `6666  Prototype product Vendor ID` として載る**慣習上の試作用 VID**。USB-IF が「試作用」に割り当てたものではなく、**所有者も申請 program も無い**。hobby の CMSIS-DAP 実装(例: ataradov の free-dap)や各種試作が既定値に使ってきた。
- **CMSIS-DAP で「動いてしまった」理由は string 識別**: CMSIS-DAP の仕様は host(pyOCD / OpenOCD)が **product string(v1・HID)/ interface string(v2・bulk)に含まれる `"CMSIS-DAP"` で device を見つける**と定めており、VID:PID は見ない。だから `0x6666` でも `0xCAFE` でも認識された。公式 DAPLink(mbed)は ARM の `0x0D28`(board ごとに ARM が PID を発行する program)で、これは A 列と同じ「vendor 公認」。
- **最近避けられる方向にある理由**: (1) 誰の割当でもないため、`0x6666` を名乗る無関係な device が世界中に大量にあり、Windows の driver cache(VID:PID 単位、§4.3)が衝突・混乱する。(2) 2015 年前後から **pid.codes(`0x1209`)という正当な無償経路**が定着し、「OSS なら 1209、vendor silicon なら vendor program」が規範になった。(3) usb.ids の "Prototype" 表記そのものが「配布物に使うな」の意味。→ 新規 project の既定値には使わず、既存の hobby CMSIS-DAP が歴史的に残しているだけ、と見るのが現状。
- **本エコシステムへの教訓**: CMSIS-DAP が VID に依存せず生き残れたのは **識別を protocol 側の string に置いた**からで、これは本書の「host は `hello`/`caps` handshake で正体判定、VID:PID はフィルタ」(§4.2b 末尾)と同じ設計。**識別を handshake に置けば開発中は C 列の ID でも困らないが、配布時は B(pid.codes)か A に載せ替える**、が結論。`0x6666` を repo 既定値にしないのは `0xCAFE` と同じ扱い。

**vendor 既定 ID のままでよいか**(「ESP32-S3 の標準 USB 機能を使う限り PID は ESP32-S3 のままでいい?」への答え)— **2 つの場合で答えが違う**:

1. **内蔵 USB-Serial-JTAG(hardware、firmware 非依存)を使う場合 → そのまま(`303A:1001`)でよい。** これは Espressif 自身の device(CP2102 のような bridge と同じ扱い)で、descriptor を私たちが書いていない。probe protocol をこの CDC に載せるだけなら **VID/PID 問題は発生しない**うえ driver レス。**ESP32-S3 probe の最短経路**。
2. **USB OTG + TinyUSB で自前 descriptor を出す場合**(HID にしたい、composite にしたい)→ Arduino-ESP32 の既定 `303A:1001` は**開発用の既定値**。手元・個人利用は事実上問題ないが、**配布する firmware は A の program で自分の PID を申請**するのが Espressif の求める運用(それでも VID は `0x303A` のまま=Espressif chip 上だから正当)。Pico も同型: Pico SDK 既定 `2E8A:000A` は開発用、配布は `raspberrypi/usb-pid` で申請。

同じ理屈で **CH32 上の自前 firmware(BL / Core app / V003 software USB probe)は A が無いので B(pid.codes)一択**。UIAPduino V006 の `0x1209:0xB806` はこの判断。

**エコシステムへの含意**:

- cross-MCU の probe firmware を 1 つの ID で揃えたいなら **B(pid.codes)を主**にする。vendor 既定/A を使う board があってもよいが、その場合 host は **VID:PID の表**を持つことになる。
- どちらにしても **host の識別は VID:PID だけに頼らず、protocol の `hello`/`caps` 応答で確定**させる(V-USB の string 識別と同じ思想)。ID は「候補を絞るフィルタ」、正体は handshake で判定。これで内蔵 USB-Serial-JTAG(ID は Espressif の汎用)経由でも、vendor 既定 ID でも、pid.codes でも同じ host コードで拾える。
- C(testing 範囲・`0xCAFE`)は **repo に commit する firmware の既定値にしない**(誰かがそのまま配ると衝突する)。開発時は build flag で切り替える。

### 4.3 PID を mode で分ける理由(Windows の落とし穴)

- Windows は **VID:PID(+ MI_xx)ごとに driver 割当を cache** する。同じ VID:PID で「BL mode = HID 1 interface」「app mode = CDC + HID composite」のように構成を変えると、cache と実物がずれて認識不良・COM 番号迷子が起きやすい。
- → **BL mode と app mode は別 PID**。probe firmware も別 PID。host は enumeration だけで「今どの mode か」が分かり、1200-touch 後の再列挙待ちも「PID が (b)→(a) に変わるまで待つ」と書ける。
- CDC を使う場合、Windows の COM 番号は VID:PID:serial で固定されるので、**serial に UID を入れておくと board ごとに COM が安定**する(逆に serial 無しだと port ごとに増殖する)。

### 4.4 この方針で起きること

- host tool(ch32rv 等)は **WCH VID をハードコードしない**。`0x1209` の自前 PID 群 + 既存 LinkE の `1A86:8010/8011/8012` + factory ISP の `4348/1A86:55E0` を**列挙対象の表**として持つ。
- WCH 純正ツール(WCH-LinkUtility / WCHISPTool)は私たちの ID を知らないので**認識しない**。これは正しい挙動(別物なので)。逆に私たちの host は WCH 純正 device も従来どおり扱える。
- **UIAPduino との相互運用**: 同じ `0x1209` 圏だが PID が違うので衝突しない。protocol が共通化されれば(generic-probe-design §8-1)、UIAPduino の内蔵ライタも host から「probe backend の 1 つ」として見える余地がある。

### 4.5 結論の更新: USB(自前 descriptor)は「固定費」、既存 bridge 上の UART と IP はそれを払わない

§4.2b〜4.3 を煮詰めると、**USB を自前 device として名乗る限り避けられない固定費**が見える:

1. **PID ごとに interface 構成(descriptor)を固めてから**でないと ID を確定できない(Windows cache、§4.3)。
2. その ID を **pid.codes(OSS 条件)か vendor program(silicon 縛り)で申請**する。
3. 以後 descriptor は**互換性契約**になり、気軽に変えられない(mode を増やす=PID を増やす)。

hobby / 非商用のエコシステムにはこれが重い。そして **この固定費を払わない transport が 2 つある**:

| transport | なぜ ID 問題が無いか | 識別 | 限界 |
|---|---|---|---|
| **UART(既存 USB-UART bridge 経由)** | USB device は **bridge vendor の製品**(CH340 / CP2102 / ESP32-S3 内蔵 USB-Serial-JTAG `303A:1001` / Uno の 16U2 …)。私たちは**byte stream に protocol を載せるだけ**で descriptor を書かない。driver は既に世界中の PC に入っている | **handshake(`hello`)で正体判定**。bridge に serial string が無い個体もあるので、`hello` 応答に **probe MCU 自身の UID** を入れて個体識別 | 帯域・latency(→ batch + stub 必須、[generic-probe-design.ja.md](generic-probe-design.ja.md) §6)。port 発見が弱い(COM は匿名 → 候補 port に `hello` を撃つ scan か user 選択)。**DTR 副作用**(下記) |
| **IP(Wi-Fi TCP / WebSocket)** | USB を使わない | mDNS(例 `_ch32probe._tcp`)+ handshake | Wi-Fi 機 MCU 限定(ESP32 系 / Pico W)、provisioning UX、**認証必須**(LAN の誰でも flash できてしまう)、学校/会社網で mDNS・multicast が塞がれがち、latency(batch/stub 必須) |

**境界線(ここを間違えると ID 問題が戻ってくる)**:

- 「UART」で ID 問題が消えるのは **USB 端点が他人の製品(bridge)であるとき**だけ。**自前 MCU の native USB CDC(Pico / ESP32-S3 OTG の TinyUSB)は自前 descriptor なので §4.2b の A/B が要る**(Pico は RPi の `0x2E8A` が無償・公認なので実害は小さい)。
- ESP32-S3 の **内蔵 USB-Serial-JTAG は「他人の製品」側**(Espressif 固定 firmware)なので free ride。同じ chip でも OTG+TinyUSB を選ぶと自前側に落ちる。
- 結果: **「あらゆる Arduino board」= Uno(16U2)/ Nano(CH340)/ ESP32 DevKit(CP2102)/ ESP32-S3(内蔵 CDC)/ Pico(`0x2E8A`)は、Arduino の `Serial` に protocol を載せるだけで一切の ID 判断なしに probe になれる。** 目標(§1 T0)と完全に整合する。

**UART の実務上の落とし穴(必ず設計に入れる)**:

- **DTR/RTS auto-reset**(**実測で裏付けあり**: [experiments E002/E004](../experiments/LEDGER.ja.md)。ESP32-S3 + CH340 で、監視が接続するのは reset の **1 秒以上あと**。起動時出力は確定的に失われ、**host が撃って probe が答える形にすれば 1 発で取れた**): 多くの Arduino board は port open 時の DTR で MCU が **reset** する(Uno/Nano の auto-reset 回路、ESP32 の esptool 式 DTR/RTS reset)。probe firmware が port open で reset されるので、**host は open 後に boot 完了と `hello` 応答を待つ**設計にする(逆に「open = probe を確実に初期状態にする」機能として使える)。LinkE の CDC で DTR が SDI forward を止めた実測([serial-and-print.ja.md](../protocols/serial-and-print.ja.md) §5)と同族の「bridge 副作用」。
- **baud**: 既定 115200 で `hello` → 能力交換で 921600〜(CH340 2 Mbps、CP2102 1 Mbps、USB-Serial-JTAG は baud 非依存)へ昇格。
- **port scan の安全性**: 無関係な serial device に `hello` を撃っても害が無いよう、sync byte 列を「他 protocol が偶然反応しない」形にし、応答が無ければ即閉じる。

**推奨の並べ替え**(§4.2 の修正):

1. **主: UART(既存 bridge)** — ID 判断ゼロで全 Arduino board に載る。WebSerial(Chromium)からも使える。
2. **主(ESP32 系): IP / WebSocket** — 全ブラウザ・遠隔・多人数。認証を最初から。
3. **選択: 自前 USB(CDC/HID/WebUSB)** — 高速化・driver レス HID が要る場合のみ。Pico は `0x2E8A`、ESP32-S3 OTG は `0x303A`、CH32 上は pid.codes。**descriptor を固めてから**。

→ USB 自前 descriptor を「既定」から「最適化オプション」に格下げする。これで **protocol は transport 非依存のまま、ID の固定費を払うのは最適化を選んだ人だけ**になる。

## 5. 決めごと(この repo で固定する候補)

1. backend の **capability 宣言**の項目一覧(§3 表)と、host の選択規則。
2. **PID 3 種の申請**(pid.codes): BL / Core app 既定 / probe。申請には公開 repo と license が要る → BL・probe firmware・Core の repo を先に公開状態にしておく。
3. **UID → serial string** の生成規則(series 別に UID 長が違う。hex 大文字・桁固定、ChipInfo UUID と一致させる)。
4. `hello`/`caps` の board id 体系(T2/T3 用。T0 は「unknown board」でも動くこと)。

## 参照

- probe 側の汎用化: [generic-probe-design.ja.md](generic-probe-design.ja.md)
- target 自身の BL と内蔵ライタ MCU: [bootloader-design-space.ja.md](bootloader-design-space.ja.md)(UIAPduino V006 の実例と限界は §6b)
- ID 衝突の実例(LinkE IAP = factory ISP): [../protocols/pc-to-device-isp.ja.md](../protocols/pc-to-device-isp.ja.md)
- driver は VID を問わない: [../protocols/pc-usb-driver.ja.md](../protocols/pc-usb-driver.ja.md)
- 自前 descriptor(software USB): [../protocols/software-usb.ja.md](../protocols/software-usb.ja.md)
