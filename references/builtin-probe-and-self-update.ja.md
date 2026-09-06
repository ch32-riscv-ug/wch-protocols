# ビルドイン型 probe と「自己書換え」の capability 化 — PID を増やさずに役割を名乗る

状態: **検討メモ**(方針。実装は未)。[ecosystem-any-hardware.ja.md §4](ecosystem-any-hardware.ja.md) の VID/PID 方針と [bootloader-design-space.ja.md §6b](bootloader-design-space.ja.md) の内蔵ライタ MCU を、[dmi-bridge](../protocols/dmi-bridge.ja.md) の `caps` の上で繋ぐ。

## 0. 結論(先に)

1. **PID を分ける基準は「descriptor(interface 構成)が違うか」であって「役割が違うか」ではない。** [ecosystem §4.3](ecosystem-any-hardware.ja.md) の Windows driver cache の制約は **descriptor が変わるときだけ**効く。外付け probe / 内蔵ライタ / 自己書換えは **descriptor が同一**なので、**1 PID で全部覆える**。役割は `caps` が名乗る。
2. これは新しい原則ではなく、**既にある原則の適用範囲を 1 軸広げるだけ**。[dmi-bridge 設計原則 4](../protocols/dmi-bridge.ja.md):「**正体判定は handshake で行う。USB VID/PID は候補を絞るフィルタにすぎない**」。[ecosystem §5-1](ecosystem-any-hardware.ja.md) が未決にしていた「backend の capability 宣言の項目一覧」への回答でもある。
3. **PID 節約より重い理由がある — pid.codes は「1 project 1 PID が原則」**([ecosystem §4.2b](ecosystem-any-hardware.ja.md))。**board の種類ごとに PID を取る運用はそもそも通らない。** caps で役割を名乗るのは、pid.codes の下で複数の役割を成立させる**唯一の方法**。
4. 足すのは **2 項目だけ**:
   - **lane の `attach`** — その lane の向こう側に何があるか(`external` / `onboard` / `socket` / `self`)。
   - **probe の `self_update`** — 自分自身をどう書き換えられるか(`none` / `bootloader` / `in_place` / `peer`)+ **`recovery`**(失敗したときの逃げ道)。
5. **X035(hardware USB)も V003(software USB)も同じ形で載る。** dmibridge が transport 非依存だから([dmi-bridge §2](../protocols/dmi-bridge.ja.md))。V003 の low-speed HID は §2.4 の HID adapter、X035 は CDC か HID。**host から見える差は `caps.mtu` と速度だけ**で、host のコードは 1 本。
6. **本命は PID 節約ではなく「host が何を守るべきか分かる」こと。** `attach=self` は「消去 = 自分を brick する」を意味する。役割が申告されていれば host が事前に止められる。

## 1. ビルドイン型 probe とは

**board 上に専用の小型 MCU を書込器として載せる**構成([bootloader-design-space §6b](bootloader-design-space.ja.md)、[ecosystem §1](ecosystem-any-hardware.ja.md) の T2/T3)。

```
PC ──USB── [内蔵ライタ MCU(probe firmware)] ──SWIO/RVSWD── [同じ基板上の target MCU]
```

既存事例:

| 事例 | ライタ | target | host tool |
|---|---|---|---|
| **UIAPduino Pro Micro CH32V006 v1.1** | **CH32V003**(rvswdio_programmer + patch、software USB) | CH32V006(SWIO = PD1) | **minichlink のみ**(Arduino IDE 未対応) |
| Arduino UNO | ATmega16U2 | ATmega328P | avrdude |
| Raspberry Pi Pico(debugprobe) | RP2040 | 別の RP2040 | OpenOCD / picotool |

**entry 問題が消える**のが最大の利点(debug 線なので app の状態と無関係。app が死んでも hang でも書ける)。代償は BOM +$0.1〜と基板面積。

**UIAPduino の実地で判明した限界**([bootloader-design-space §6b](bootloader-design-space.ja.md))は 3 点で、本メモはそのうち (c) の続きにあたる:

| # | 限界 | 対応 |
|---|---|---|
| (a) | V003 の software USB が probe 側でもボトルネック(Windows で不安定、**ケーブル長に敏感**) | 内蔵 MCU を hardware USB 持ち(**X035/X033**)にする → §4 |
| (b) | **GPIO 衝突**(V006 が PC0 を操作すると内蔵 V003 が reset) | 制御 pin を基板設計段階で分離 → §5 の事故 3 |
| (c) | **host tool が minichlink に固定**され Arduino IDE から使えない | **共通 protocol で host を選べるようにする** → 本メモ |

## 2. なぜ役割の申告が PID の話になるか

### 2.1 現行方針

[ecosystem §4.2](ecosystem-any-hardware.ja.md) は「PID をエコシステムで複数取る: (a) BL mode、(b) Core app 既定、(c) probe firmware。**最低 3 つ**」としている。理由は §4.3 の Windows の落とし穴:

> Windows は **VID:PID(+ MI_xx)ごとに driver 割当を cache** する。同じ VID:PID で「BL mode = HID 1 interface」「app mode = CDC + HID composite」のように構成を変えると、cache と実物がずれて認識不良・COM 番号迷子が起きやすい。

**これは「interface 構成が変わるとき」の話**であって、「役割が変わるとき」の話ではない。ここを区別しないと、役割や board の種類ごとに PID を取りたくなる。

### 2.2 判定基準

| 変わるもの | PID を分けるか | 理由 |
|---|:--:|---|
| interface 構成(CDC のみ / CDC+HID / HID のみ / bulk) | **分ける** | Windows driver cache([§4.3](ecosystem-any-hardware.ja.md)) |
| device class | **分ける** | 同上 |
| mode(BL / app / probe firmware) | **分ける** | 通常 descriptor も一緒に変わる |
| **役割**(外付け probe / 内蔵ライタ / 自己書換え) | **分けない** | **descriptor は同一**。`caps.attach` で名乗る |
| **board の種類** | **分けない** | 同上。`caps.board` の文字列で([§4.2](ecosystem-any-hardware.ja.md) の「board 固有 ID は descriptor でなく protocol 内」) |
| lane の数・線種・能力 | **分けない** | `caps` |
| firmware の版 | **分けない** | `bcdDevice`([§4.2](ecosystem-any-hardware.ja.md)) |
| 個体 | **分けない** | serial string = chip UID([§4.2](ecosystem-any-hardware.ja.md)) |

**この表の下 5 行がすべて「caps / string に逃がす」で埋まっているのは偶然ではない。** [dmi-bridge 設計原則 4](../protocols/dmi-bridge.ja.md) の「正体は handshake で判定、ID はフィルタ」がそのまま効いている。CMSIS-DAP が `0x6666` のような無主の VID でも生き延びたのは、識別を protocol 側の string に置いたからだった([ecosystem §4.2b](ecosystem-any-hardware.ja.md))— 同じ構造。

### 2.3 pid.codes の制約が決定打

[ecosystem §4.2b](ecosystem-any-hardware.ja.md):

> pid.codes は project 単位で PID を申請(公開 repo と OSS license が条件)。**1 project 1 PID が原則**(複数は理由付きで)。

**board の種類ごとに PID を取る運用は、そもそも申請が通らない。** つまり「caps で役割を名乗る」は節約の工夫ではなく、**pid.codes という制約下で内蔵ライタと外付け probe を同居させる唯一の方法**。

## 3. 足す capability

### 3.1 lane の `attach` — 向こう側に何があるか

[dmi-bridge §5.2](../protocols/dmi-bridge.ja.md) の lane 入れ子 TLV(`0x01 id` / `0x02 wires` / `0x03 features` / `0x04 uart_max_baud` / `0x05 label` / `0x10 pin`)に追加:

| type | 項目 | 内容 |
|---|---|---|
| `0x06` | **`attach`** | u8(下表) |
| `0x07` | `target_hint` | str(`onboard`/`socket` のとき、基板が載せている chip 名。例 `CH32V006K8U6`) |
| `0x08` | `designator` | str(基板上の部品番号。例 `U1`)。**基板図と突き合わせられる** |

| 値 | 名前 | 意味 | host の挙動 |
|---:|---|---|---|
| `0` | **`external`** | 着脱可能。chip は不明 | chip 検出を必ず行う。全操作を許可 |
| `1` | **`onboard`** | **同じ基板に実装済み**。chip は既知 | 検出を省略してよい。`target_hint` と実測が食い違ったら**警告**(基板と firmware の不一致) |
| `2` | **`socket`** | ZIF / pogo。着脱可だが chip 種別は固定 | 検出は行い、`target_hint` と照合。量産治具で効く |
| `3` | **`self`** | **lane の先が probe 自身** | **破壊操作に確認を要求**。`recovery`(§3.2)を先に確認 |

**`attach` は「その lane の向こうに何があるか」であって「probe が何であるか」ではない。** だから 1 台が `lane0 = onboard` と `lane1 = external` を同時に持てる(内蔵 target を持ちつつ、外部 header にも出す board)。

### 3.2 probe の `self_update` と `recovery`

[dmi-bridge §5.2](../protocols/dmi-bridge.ja.md) の probe 全体 TLV(`0x01`–`0x0C`、`0x20 lane`)に追加:

| type | 項目 | 内容 |
|---|---|---|
| `0x0D` | **`self_update`** | u8(下表) |
| `0x0E` | **`recovery`** | u8 bitmask(下表) |

| 値 | 名前 | 意味 | 事例 |
|---:|---|---|---|
| `0` | `none` | 書き換えられない(出荷時固定) | mask ROM 的な運用 |
| `1` | **`bootloader`** | **別 identity に再列挙してから外から書かれる** | **WCH-LinkE の IAP がまさにこれ**([pc-to-link §10b](../protocols/pc-to-link.ja.md))。`81 0f 01 01` で落ちて `4348:55e0` として再列挙。**この PID 分割は §2.2 の基準に照らして正当**(interface 構成が bulk 4 本に変わる) |
| `2` | **`in_place`** | **走りながら自分の flash を書く** | [custom-bootloader §2a](../protocols/custom-bootloader.ja.md) の「user code からの BOOT 領域書換(**V003 実証**、app 側 updater で BL 自己更新可)」 |
| `3` | `peer` | **2 個目の MCU が書く**(相互) | 2 個載っている board。片方ずつ更新すれば常に復旧経路が残る |

**`self` の lane で自分を書く形("self_lane")は物理的に成立しないことが多い。** 自分の debug 線を自分に繋ぐと、書いている最中に自分が halt する。だから `attach=self` は **`in_place` / `peer` / `bootloader` のどれかとセットでしか意味を持たない**。この非対称を値域に反映してある。

| bit | `recovery` | 意味 |
|---:|---|---|
| 0 | `strap` | hardware の button / ジャンパで BL に落ちる |
| 1 | `double_reset` | 二重 reset |
| 2 | **`pad`** | **基板上に外部 probe 用の pad / header がある** |
| 3 | `factory_isp` | factory ISP に落ちられる(BOOT pin 操作が可能) |
| 4 | `peer` | もう 1 個の MCU から書ける |

**`recovery` が要る理由**: 自己更新は brick のリスクがある([bootloader-design-space §6b](bootloader-design-space.ja.md) の「以後は自己更新も可能だが **brick リスク**」)。復旧手段を申告させれば、host が更新前に **「この probe は失敗すると外部 probe が要る」と言える**。`recovery == 0` の probe に自己更新を撃たせないのは host の責務にできる。

### 3.3 `info` の見え方

内蔵ライタ board:

```
CH32RVProbe 0.1.0  (dmibridge/1)
profile : core+bulk+bench
board   : UIAPduino Pro Micro CH32V006 v1.1
probe   : CH32V003  (software USB, low-speed HID)   uid 8B1A...
self    : update=in_place  recovery=pad|strap          ← 自分を書ける。失敗しても pad から救える
lane0   swio PD1 → CH32V006K8U6  (onboard, U1)  pwr=PC0  uart=PD5/PD6   "target"
```

外付け probe:

```
CH32RVProbe 0.1.0  (dmibridge/1)
profile : core+bulk+bench+capture
board   : RP2040-Zero
probe   : RP2040  (USB CDC)   uid E66138...
self    : update=host_dfu(BOOTSEL)  recovery=strap
lane0   rvswd GP14/GP15 → (external)   nrst=GP11   uart=GP0/GP1   "breakout"
```

**同じ PID・同じ host コードで、この 2 行(`self` と `lane0` の `→` の右側)だけが違う。** これが「能力を見れば外部を書き換えるのかボード自体を書き換えるのかの区別がつく」の実体。

> RP2040 の BOOTSEL(USB MSC の UF2)は `self_update` の 5 番目の値になる。`host_dfu` として §3.2 の表に足すか、`recovery` 側の bit にするかは未決(§7-2)。

## 4. X035 と V003 — 同じ形で載る

内蔵ライタ MCU としての比較。**dmibridge が transport 非依存**([dmi-bridge §1–2](../protocols/dmi-bridge.ja.md))なので、両者は同じ protocol の別 adapter として繋がる。

| | **CH32V003**(software USB) | **CH32X035 / X033**(hardware USB) |
|---|---|---|
| USB | low-speed HID(rv003usb)。**実地で不安定・ケーブル長に敏感**(UIAPduino 実測: ≤1 m 安定 / ≥4 m 不安定) | **full-speed device**。安定 |
| dmibridge の transport | **§2.4 の HID adapter**(64 B report、byte0 に more + 長さ)。実質 mtu 63 | **§2.3 の CDC framing**(magic+len+CRC16)or HID。mtu 256–1024 |
| phy | CPU bit-bang(rvswdio_programmer の実績) | **PIOC**(2 ピン専用・48 MHz 単一サイクル・2K 命令。[harness-board-survey §2.1](harness-board-survey.ja.md)) |
| 線 | SWIO(1 線)が実績 | **PIOC は 1 線/2 線の両方が射程**(WCH が Single_Wire / IIC の ASM 例を同梱) |
| BOM | ≈$0.1 | ≈$0.3–0.4 |
| VDD | 3.3/5.0 V | **2–5.5 V** |
| 自己更新 | rv003usb BL(system 領域 1,920 B)= `bootloader`、または BOOT 領域自己書換 = `in_place`(**V003 で実証済み**) | BOOT 領域 + IAP。[custom-bootloader §2a](../protocols/custom-bootloader.ja.md) の BOOT 領域表に従う |
| `caps` の見え方 | `mtu=63, max_inflight=1, self_update=in_place, recovery=pad` | `mtu=512, max_inflight=1, self_update=bootloader, recovery=pad\|strap` |
| **host から見える差** | **`mtu` と速度だけ** | **同左** |

**「X035 でも V003 でも使える」の根拠は 2 段**:

1. **transport 層**: [dmi-bridge §2.2](../protocols/dmi-bridge.ja.md) の adapter 一覧に HID(境界あり・完全性あり)と CDC(境界なし・完全性なし)が両方ある。**L2 以上は 1 bit も変わらない**。
2. **能力層**: 速度差・mtu 差は `caps` の数値。[dmi-bridge §8.2](../protocols/dmi-bridge.ja.md) が「`mtu` 64 / `max_inflight` 1 が正当な値であることを仕様が保証する限り、低資源 MCU への移植は firmware 側の作業だけで済む」と既に約束している。**V003 はその約束の受益者**。

→ **PID も host コードも 1 本のまま、$0.1 の V003 board と $0.4 の X035 board が同じエコシステムに並ぶ。** [ecosystem §2](ecosystem-any-hardware.ja.md) の連鎖 bootstrap(「既に書けた board が次の board の probe になる」)は、**どの board も同じ PID で probe を名乗る**ことで初めて成立する。

## 5. `attach` が守るもの(PID 節約より重い)

役割が申告されていないと起きる事故。

| # | シナリオ | `attach` が無いと | あると |
|---|---|---|---|
| 1 | 内蔵ライタ board を挿して `flash app.bin` | **どちらの chip に書かれるか host に分からない**。ライタ firmware を上書きしうる | `lane0 = onboard` に書く、と明示できる。probe 自身は `attach=self` の別 lane |
| 2 | `erase --chip` を撃つ | `self` の lane なら **probe が消える**(以後 USB に出てこない) | 破壊操作の前に確認 + `recovery` の提示 |
| 3 | `power off` で target 電源を切る | **自分の電源も切れる board 設計だと自死**。UIAPduino の **PC0 衝突**(V006 が PC0 を操作すると内蔵 V003 が reset)が同類の実例 | `attach=onboard` + `features` の power bit と組み合わせて host が警告 |
| 4 | 基板と firmware の不一致(V006 board に V003 用 firmware) | 気づけない | `target_hint` と実検出の食い違いで**警告** |
| 5 | 量産治具で ZIF に別 chip を挿した | 気づかず書いて失敗 | `attach=socket` + `target_hint` で照合 |

**加えて、1 つの USB device に「probe 自身」と「target」の 2 つの書込先がある**という事実そのものが lane の並びで表現できる:

```
lane0  swio PD1 → CH32V006K8U6 (onboard, U1)    ← ch32rv flash --lane 0
lane1  (self)   → CH32V003     (self, U2)       ← ch32rv flash --lane 1  ← 確認を要求
```

**`ch32rv probe info` の出力を見れば、人も機械も「どっちに書くのか」が分かる。** [dmi-bridge §5.3](../protocols/dmi-bridge.ja.md) の「焼いた後に、どのピンをどの機能に割り当てたか分からなくなる問題は、設定ではなく**申告**で解く」を、書込先そのものに広げた形。

## 6. PID はいくつ要るか(§4.2 の更新案)

| # | identity | descriptor | PID | 役割の差は |
|---:|---|---|:--:|---|
| 1 | **probe firmware(通常動作)** | CDC or HID に固定 | **1** | **`caps.attach` / `caps.self_update` / `caps.board`** |
| 2 | probe firmware の更新 mode | 別構成(bulk 等) | 1 | — |
| 3 | Core app 既定 | CDC | 1 | — |
| 4 | BL mode | HID or DFU | 1 | — |

**合計 4。board が 100 種類でも、役割が何通りでも増えない。** [ecosystem §4.2](ecosystem-any-hardware.ja.md) の「最低 3 つ」は据え置きで、**(c) probe firmware が 1 つのまま外付け / 内蔵 / 自己書換えを全部覆う**ことが本メモの主張。

**逆に守るべき線**: 2 番(更新 mode)を 1 番と同じ PID にしてはいけない。**interface 構成が変わるから**(§2.2)。LinkE が通常 `1a86:8010` / IAP `4348:55e0` と分けているのは、この基準に照らして正当な分割([pc-to-link §1・§10b](../protocols/pc-to-link.ja.md))。

## 7. 未決

1. **`attach` に `socket` を入れるか**。量産治具では効くが、`external` + `target_hint` で足りるかもしれない。
2. **`self_update` に `host_dfu`(RP2040 BOOTSEL / UF2)を値として足すか、`recovery` の bit にするか**(§3.3 の注)。
3. **`attach=self` の lane を「番号付き lane」にするか、別の addressing にするか**。番号付きだと `lane_attach` などの既存コマンドがそのまま効くが、「self に `line_reset` を撃つ」の意味が曖昧。
4. **`target_hint` と実検出が食い違ったときの規則**(警告に留めるか、書込を拒否するか)。`onboard` は基板の事実なので、食い違い = firmware か基板のどちらかが間違っている。
5. **`self` への破壊操作にどこまで確認を強制するか**。protocol の話ではないが、host 側の規約として本 repo に書くかどうか。
6. **pid.codes への申請単位**(project = エコシステム全体で 1 か、firmware 種別ごとに 4 か)。§6 の 4 つは「1 project 1 PID が原則」に対して理由付きの複数申請になる。
7. **UIAPduino の `0x1209:0xB806` との相互運用**。同じ `0x1209` 圏で PID は違う([ecosystem §4.4](ecosystem-any-hardware.ja.md))。protocol が共通化されれば host が既存機も拾えるが、拾いにいくかは方針判断。
8. **`in_place` 自己更新の実証範囲**。V003 は実証済み([custom-bootloader §2a](../protocols/custom-bootloader.ja.md))だが、**X035 / X033 の BOOT 領域 self-write は未実証**(同 §「V00X/X035 での BOOT 領域 self-write」が gap のまま)。

## 8. 参照

- VID/PID 方針の一次資料(3 系統の無償 ID・Windows cache・UART/IP は固定費ゼロ): [ecosystem-any-hardware.ja.md §4](ecosystem-any-hardware.ja.md)
- 内蔵ライタ MCU の設計比較と UIAPduino の実地限界: [bootloader-design-space.ja.md §6b](bootloader-design-space.ja.md)
- `caps` TLV の既存項目と「ピンは申告する」原則: [../protocols/dmi-bridge.ja.md §5](../protocols/dmi-bridge.ja.md)
- **`bootloader` 型自己更新の実測**(entry・frame・stall・救出・脱出): [../protocols/pc-to-link.ja.md §10b](../protocols/pc-to-link.ja.md)
- **`in_place` 型自己更新の根拠**(BOOT 領域の user code からの書換、V003 実証): [../protocols/custom-bootloader.ja.md §2a](../protocols/custom-bootloader.ja.md)
- software USB(V003 側の transport): [../protocols/software-usb.ja.md](../protocols/software-usb.ja.md)
- X035/X033 の実力(PIOC・USB・5 V): [harness-board-survey.ja.md §3.5](harness-board-survey.ja.md)
- probe パターンの共存(能力は build 時、役割は実行時): [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md)
