# マイコンレス直接書込のための bootloader 設計空間(手を入れるとどこまで何ができるか)

状態: **検討メモ**(設計案。実装・実測は未)。前提: **Arduino Core・bootloader・host tool を全部自分で握れる**(エコシステム全体を自由に触れる)。

probe 経由([generic-probe-design.ja.md](generic-probe-design.ja.md))は「適当なマイコンで自作」で解けるが、日常の便利さは **probe 無し・USB ケーブルだけ**で書ける経路にある。UIAPduino(V003、software USB)はそれを実現しているが、**「ボタンを押しながら USB 接続」か「アプリ側に reset→boot mode の処理を入れる」**が要る。ここではその制約を含め、bootloader に手を入れると何がどこまで可能かを整理する。

## 0. 結論(先に)

1. **最大の論点は「BL にどう入るか(entry)」**であり、書込 protocol ではない。Arduino(AVR/SAMD/RP2040)が解いた答えは **「BL は毎 reset 必ず先に走り、短い窓を持つ」+「Core が全 sketch に『BL へ戻る hook』を埋め込む」**。Core を握っているのでこれがそのまま使える。→ **ボタン不要・アプリ作者は何も書かなくてよい**状態にできる。
2. **抜け道の無い universal fallback は「USB 抜き差し(電源再投入)」**。BL に host 検出付きの起動窓があれば、抜き差し = entry になる。ボタンは「保険」に格下げできる。
3. **V003 は BL を system 領域(1,920 B)に置ける = user flash を 1 byte も食わない**。factory ISP を置換する形。hardware USB 持ち(V20x/X035/L103…)は本物の USB(CDC/HID/DFU/MSC)が使え、Serial monitor と upload を 1 本の USB で兼ねる Leonardo 型が組める。
4. **BL 自体は小さく保ち、能力は「RAM stub 実行」で拡張**する(ch32fun bootloader の sketchpad 方式)。read-back・部分書込・自己診断・option 操作を host 側の stub で足せる。
5. 初回だけ probe(または factory ISP)で BL を焼く bootstrap は避けられない。以降は永久にマイコンレス。board を配る側なら pre-flash で解決。

## 1. Entry(BL に入る)の設計空間 — ここが本題

「誰が」「どのタイミングで」「何を根拠に」BL に留まるかの組合せ。複数を **OR で重ねる**のが定石(どれか 1 つ効けば入れる)。

| 方式 | 仕組み | app の協力 | 部品 | 起動遅延 | 信頼性 | 実装事例 |
|---|---|---|---|---|---|---|
| **A. 常時 BL 先行 + 窓(timeout)** | reset で必ず BL が走り、N ms 待って何も来なければ app へ jump | 不要 | 不要 | あり(N ms) | ◎ 最強の土台 | rv003usb(~5 s)、tinyboot |
| **A'. host 検出で窓を短縮** | USB host の存在(VBUS / D± の状態 / SOF・bus reset 到来)を見て、居なければ即 app、居れば窓を開く | 不要 | 不要 | **ほぼ無し**(host 無し時) | ◎ | rv003usb「host detection」 |
| **B. button / BOOT pin** | 押しながら reset | 不要 | GPIO + button | 無し | ◎ | UIAPduino、WCH IAP、rv003usb |
| **C. double-tap reset** | 1 回目の reset で RAM magic を置き、短時間内に 2 回目が来たら BL に留まる | 不要 | reset button | 無し | ○(RAM 保持前提) | wch-uf2、SAMD Arduino |
| **D. app 発の reboot-to-BL** | app が RAM magic(or flash flag)を書いて software reset → BL が magic を見て留まる | **要**(hook) | 不要 | 無し | ◎(hook があれば) | ch32fun `funRebootToBootloader`、WCH IAP `CheckNum@CalAddr` |
| D-1. 1200 baud touch | host が CDC を 1200 bps で開く → app の CDC callback が D を呼ぶ | 要(CDC stack) | HW USB or software USB | 無し | ◎ | Arduino Leonardo/SAMD |
| D-2. UART magic command | app の Serial に特定 byte 列 → D | 要(数十 byte) | UART | 無し | ○ | tinyboot 系 |
| D-3. **USB bus-reset(SE0)検知** | app は USB 線を入力監視するだけ(stack 不要)。host の bus reset(D+/D− 同時 LOW ≥10 ms)を見たら D | 要(**極小**) | software USB の pin | 無し | △ 要検証(§9) | (未確認・本書の提案) |
| **E. reset 原因レジスタ** | BL が `RCC_RSTSCKR`(PINRSTF/SFTRSTF/IWDGRSTF…)を読み、software reset + RAM magic なら意図的 entry、IWDG reset なら「app 暴走」と判定して留まる | 不要 | 不要 | 無し | ○ | (STM32 互換フラグ。実装は容易) |
| **F. app 無効検出** | app 先頭が `0xFFFFFFFF` / magic 不一致 / CRC 不一致なら BL に留まる | 不要 | 不要 | 無し | ◎(初回・破損時) | WCH IAP、Swindle(CRC)、tinyboot |
| G. watchdog 救済 | app が IWDG を有効化 → hang → reset → E で検知 → BL に留まる(or trial boot 失敗扱い) | 要(IWDG 有効化。Core で自動化可) | 不要 | 無し | ○ | tinyboot(trial boot / confirm) |

### 組合せの推奨(Core を握っている前提)

```
reset →
  BL 先行(A)
    ├ RAM magic あり(D 経由)                → BL に留まる
    ├ app 無効(F)                             → BL に留まる
    ├ reset 原因が IWDG(E/G)                  → BL に留まる(暴走 app から救済)
    ├ button 押下(B、任意)                    → BL に留まる
    ├ USB host 検出(A')→ 短い窓(~200–500 ms)→ host tool が来れば留まる
    └ それ以外                                 → 即 app へ jump(起動遅延ほぼ無し)
```

- **Core が保証する hook(D)**: 全 sketch に自動で入る。HW USB chip なら Core の CDC stack が 1200-baud touch を受ける(D-1)。software USB の V003 で app が USB stack を持たない構成でも、**D-3(SE0 検知、数十 byte)を Core に埋める**か、UART magic(D-2)を Serial に埋める。
- **結果の UX**: 「host tool を実行 → app が動いていれば hook で reboot → BL 窓 → 書込 → app 起動」。app が死んでいても「**USB を抜き差し**(A')」か「reset 2 回(C)」で入れる。**ボタンは無くてもよい**。

### RAM magic の注意

- system reset で SRAM は保持される前提(wch-uf2 の double reset、Swindle の RAM marker、ch32fun がこれに依拠)。**BL は自分の `.bss/.data` 初期化より前に magic を読む**か、初期化対象外の固定番地(RAM 末尾など)に置く。読んだら必ずクリア(次回に持ち越さない)。
- flash flag(WCH IAP の `CheckNum@CalAddr`)は電源断を跨げるが、書込回数と erase が要る。RAM magic を主、flash flag を「次回起動は必ず BL」用の補助にする。

## 2. BL ができること(能力の範囲)

| 能力 | 内容 | 備考 |
|---|---|---|
| erase / program / verify | 基本。page 単位、family 別 FLASH controller 手順は BL 内に持つ(BL は chip 固定なので問題ない) | [pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §6 の手順を target 側で実行する形 |
| **非破壊の部分書込** | 書き換える page だけ erase | ch32fun BL。factory ISP は全消去 |
| **read-back** | 任意番地の読出し(factory ISP は不可) | ch32fun BL。verify・dump・UID 取得に |
| option bytes / user config | 読み書き | 保護 bit を誤って立てない防護が要る |
| **RAM stub 実行** | host が code を RAM に置いて走らせ結果を回収 | **BL を小さく保ったまま能力を無限に拡張**。CRC 計算、自己診断、周辺初期化テスト、将来の chip 差吸収 |
| metadata | app 領域先頭に magic / version / size / CRC / build id | F(無効検出)・版管理・rollback 判定の基礎 |
| 失敗耐性 | trial boot(次回起動で `confirm()` が無ければ BL へ戻す)、watchdog、(容量があれば)A/B slot | tinyboot 型。V003(16 KB)は A/B 不可、V20x 以上で可 |
| **Serial monitor 兼用** | BL と app が **同じ USB device 実装**(同 VID/PID)を共有し、app 側は CDC(or HID report)で print、host は 1200-touch で BL へ | Leonardo 型。HW USB chip では自然。V003 は software USB を app にも入れる(2 kB・timing 制約)必要 |
| 自己保護 | BL 領域は書込禁止(write-protect option or BL が自領域への書込を拒否) | BL を壊さない。BL 自身の更新は probe 経由に限定するのが安全 |
| 自己更新 | 2 段構え(BL が「BL updater」stub を RAM に載せて自分を書く) | 可能だが失敗時に brick。rv003usb は非対応。要件次第 |

## 3. chip 別の制約と選択

| chip 群 | USB | BL の置き場 | 容量感 | 現実的な transport | 難所 |
|---|---|---|---|---|---|
| **CH32V003 / CH641**(V2A) | **無し → software USB**([software-usb.ja.md](../protocols/software-usb.ja.md)) | **system 領域 1,920 B**(factory ISP 置換、user flash 消費ゼロ) | flash 16 KB / RAM 2 KB | **low-speed HID**(driver レス)。CDC は low-speed で不安定 | BL が 1,920 B に収まること(rv003usb は収まる)。app 側にも USB を持たせるなら timing/GPIO 0–4 制約 |
| CH32V00x(V2C) | 無し → software USB | system 領域(サイズは series 差) | 小 | HID | V003 と別実装(`bootloader_v006`) |
| **CH32V20x / L103 / X035 / CH643**(V4) | **USBFS(hardware)** | user flash 先頭(4–16 KB 予約、APP を offset)or 書換可 system 領域 | 32–256 KB | **CDC**(Serial 兼用)+ **DFU** or HID、**UF2 MSC** も可 | vector table の relocation、APP offset のリンク、USB pull-up の引き継ぎ |
| CH32V30x(V4F) | USBHS / USBFS | 同上 | 128–480 KB | 同上・高速 | 同上 |
| CH32V103(V3A) | USBFS | 2 分割 boot partition が特殊 | 64 KB | CDC/HID | ch32fun BL が「たぶん動かない」と注記。要調査 |

- **system 領域を BL にできるか**は series ごとに確認が要る(V003 は実績あり。V20x 以上は user flash 先頭に置くのが実績豊富: WCH IAP `0x08005000`、wch-uf2 `0x08001000`、Swindle `0x4000`)。
- **APP を offset に置く**と Core 側で linker script(FLASH origin)と割込みベクタ(BL から app へ jump 時に `mtvec` / vector 再設定)を面倒見る必要。Core を握っているので可能。

## 4. transport / protocol の選択(マイコンレス前提)

| 方式 | driver | ブラウザ | host tool | 向く chip | 特徴 |
|---|---|---|---|---|---|
| **HID(vendor report)** | **不要(全 OS)** | WebHID(Chromium) | 自作(hidapi / Python / JS) | **V003(software USB)**・HW USB 全部 | 最小・確実。8–64 B report。rv003usb / ch32fun BL がこれ |
| **CDC(仮想 COM)** | 標準(Win10+ 不要) | WebSerial(Chromium) | 自作 or 既存 serial protocol | HW USB | **Serial monitor と 1 本で兼用**、1200-touch。low-speed(V003)では不向き |
| **DFU** | 不要(WinUSB 自動 bind)| **WebUSB DFU(dfu-util の JS 実装あり)** | `dfu-util`(既存) | HW USB | 標準規格。Arduino 統合は alt/offset 管理が要る |
| **UF2 MSC** | 不要 | 不要(**ファイル copy**) | OS の file copy | HW USB(RAM に余裕) | UX 最良、Chromebook でも copy で書ける。MSC 実装が重い、V003 は不可 |
| UART(物理) | USB-UART 変換 | WebSerial | serial | 全部 | USB 無し board でも。BOOT 配線が board 依存 |

- **driver レス + ブラウザ**を最優先するなら **HID(WebHID)** が最小、**UF2** が最も一般人に優しい。両方持つのも可(HW USB chip なら composite: MSC+CDC+HID)。
- protocol は [serial-and-print.ja.md](../protocols/serial-and-print.ja.md) §1 の WCH IAP(`0x80..0x84`)や ch32fun BL の stub protocol を土台にできるが、**自分で握るなら「RAM stub 実行 + read/write 任意番地」の 2 primitive + metadata に絞る**方が BL は小さく、能力は host 側で伸ばせる。

## 5. エコシステム三者契約(BL ↔ Core ↔ host)— 全部握れるからできること

```
BL(chip 固定・小・不変)
  ├ 提供: entry 規約(RAM magic 番地・値、窓時間、button pin)、USB VID/PID・report 形式、
  │        protocol(read/write/stub/metadata/reset)、APP 配置(FLASH_Base、metadata 位置)
  └ 保証: 毎 reset で先行、app 無効なら留まる、自領域を守る

Core(全 sketch に自動で入る)
  ├ hook: rebootToBootloader() = RAM magic 書込 + software reset(D)
  ├ trigger: CDC 1200-touch(HW USB)/ SE0 検知 or UART magic(software USB・UART)
  ├ 配置: linker origin = FLASH_Base、vector/mtvec 設定、metadata 埋め込み(version/CRC)
  ├ 任意: IWDG 有効化 + confirm()(trial boot)、Serial monitor を BL と同じ USB device で
  └ BL と同じ USB 実装を共有(V003 なら rv003usb を Core に内蔵、HW USB なら Core の CDC)

host tool(ch32rv / Python / ブラウザ)
  ├ 発見: VID/PID(app mode と BL mode を別 PID か report で識別)
  ├ app mode なら trigger 送信 → 再列挙待ち(数百 ms) → BL mode
  ├ 書込: metadata 検証 → erase/program(stub)→ verify(CRC stub)→ jump/reset
  └ Arduino IDE の Upload ボタン = この一連。monitor は同じ USB の CDC/HID
```

**この契約を文書として固定する**のが本命の成果物。BL が小さく不変なら、Core と host は自由に進化できる。

## 6. 「どこまで可能か」の限界

- **app が協力せず、button も無く、BL に窓も無い**構成だけが詰む。→ A(窓)を必ず持てば詰まない。窓を A'(host 検出)で隠せば起動遅延も消える。
- **host が「reset」を発生させる手段が無い**とき(app 死亡・hook 無し)、残るのは物理: 抜き差し / reset button / BOOT pin。抜き差しは常に可能なので実質 universal。
- **BL 自身の破損**は probe 経由でしか直せない。BL 領域の write-protect と「BL は probe でしか更新しない」運用で回避。
- **V003 の同時 USB**(BL も app も software USB)は timing 制約が app の自由度を削る。「app は USB 不要、entry は SE0/UART/抜き差し」と割り切る選択肢が現実的。
- **セキュリティ**(署名・読出保護)は別軸。開発用 BL では metadata CRC まで、製品では署名 + 読出保護 + BL 更新禁止。

## 6b. 第 3 の道: 内蔵ライタ MCU(UIAPduino V006 の選択)

「target 自身が software USB で BL を持つ」(V003 UIAPduino)と「外付け probe」の間に、**board 上に専用の小型 MCU(V003)を書込器として載せる**構成がある。**UIAPduino の V006 版はこれを採った**(V003 版の software USB 方式の限界を把握した上での判断)。Arduino UNO の ATmega16U2、Pico の debug 用 RP2040 と同じ発想。

```
PC ──USB── [内蔵 V003 = probe firmware(software USB HID + SWIO/RVSWD bit-bang)] ──debug 線── [target V006 / V20x …]
```

| 観点 | target 自身の software USB BL | **内蔵ライタ MCU** | 外付け probe |
|---|---|---|---|
| ユーザーの手順 | board の USB を挿す | **board の USB を挿す(同じ)** | probe を別途つなぐ |
| **entry 問題** | あり(§1 の全機構が必要) | **無い**(debug 線なので app の状態と無関係。app が死んでも hang でも書ける) | 無い |
| target の app の自由度 | software USB の timing/GPIO 制約を受ける(or USB 諦め) | **完全に自由**(USB pin も timing も target 側は無関係) | 完全に自由 |
| target の flash/RAM 消費 | BL 分(V003 は system 領域でゼロ、HW USB chip は 4–16 KB) | **ゼロ** | ゼロ |
| デバッグ(halt/step/GDB) | 不可(BL は書込のみ) | **可**(DMI が通る。probe と同等) | 可 |
| 復旧 | app 破損時は entry 機構に依存 | **常に可**(power 制御を載せれば unbrick も) | 常に可 |
| Serial monitor | app の USB or 別 UART | **内蔵 MCU が UART bridge / SDI 相当を兼ねられる** | probe の CDC |
| BOM / 基板 | 追加無し | **+1 MCU(V003 ≈ $0.1)+ 数部品**、配線・面積 | 無し(別売) |
| firmware の保守 | target 種別ごとに BL | **probe firmware 1 本**(chip 知識は host、[generic-probe-design.ja.md](generic-probe-design.ja.md) §3) | 同左 |
| 内蔵 MCU の初回書込 | — | 製造時に 1 回(以後は自己更新も可能だが brick リスク) | — |

**評価**: 「マイコンレス」の体験(挿すだけ)を保ちつつ、**entry 問題・app の制約・復旧不能を全部消す**。代償は BOM +$0.1 程度と基板面積。**board を設計・配布する側なら、これが最も堅い**。software USB を target に載せる方式は「BOM を 1 円も増やせない」「既存 board を使う」場合の解、と位置づけが明確になる。

- 内蔵 MCU 側の firmware は [generic-probe-design.ja.md](generic-probe-design.ja.md) の「dumb DMI ブリッジ」そのもの(rvswdio_programmer が実例。1/2 線自動判別あり)。**同じ probe firmware/protocol が「外付け probe」と「内蔵ライタ」で共通化できる**のが大きい。
- 内蔵 MCU の USB は software USB(low-speed HID)だが、**firmware が固定・自分の管理下**なので timing 制約は問題にならない(user app が触らない)。より速くしたいなら内蔵 MCU を HW USB 持ち(CH32X035 等、+数十円)にすれば full-speed CDC/HID になる。
- 内蔵 MCU が余った UART を target の Serial に繋げば **upload + monitor が 1 本の USB**で完結する(Leonardo 型を app に何も要求せず実現)。

→ 3 構成の使い分け: **配布 board = 内蔵ライタ MCU** / **既存 board・BOM ゼロ = target 自身の BL(§1–§7)** / **開発者の手元 = 汎用外付け probe**。3 つとも host protocol と chip 知識を host 側で共通化できる。

### 実例: UIAPduino Pro Micro CH32V006 v1.1(一次情報)

出典: [uiap.jp / pro-micro / ch32v006 / v1.1](https://www.uiap.jp/uiapduino/pro-micro/ch32v006/v1dot1)。beta と明記。

| 項目 | 内容 |
|---|---|
| 内蔵ライタ | **CH32V003** を on-board debugger として搭載。firmware は cnlohr **rvswdio_programmer**(rv003usb、commit 80b1893)に独自 patch。MIT |
| 線 | V003 → V006 の **PD1(SWIO 1 線)** |
| USB | **V003 側の software USB**(low-speed)。patch で D+=PD3 / D−=PD2、pull-up は 3.3 V 固定(GPIO 制御廃止)、MaxPower 500 mA |
| VID/PID | **`0x1209:0xB806`(pid.codes)**、manufacturer "UIAP"。WCH VID を詐称しない選択 |
| entry | **無し(不要)**。USB を挿して `minichlink -c 0x1209b806 -C funprog -w blink.bin flash -b` |
| target 電源 | ライタから供給可(`-3`)。TARGET_POWER pin を PD2→**PC0** へ変更 |
| host tool | **minichlink のみ**。「Arduino IDE 未対応、PlatformIO 未対応」と明記 |
| monitor | ページ上に UART bridge / CDC の記載無し |

**実地で判明した限界**(この構成を採るときの設計要件):

- **Windows での device 認識が不安定**、**USB ケーブル長に敏感**(≤1 m 安定、≥4 m 不安定)→ V003 の **software USB(low-speed)が probe 側でもボトルネック**。内蔵 MCU を HW USB 持ち(CH32X035 等)にすれば解消見込み(§6b 本文)。
- **GPIO 衝突**: V006 が **PC0** を操作すると内蔵 V003 が reset(V003 の NRST/TARGET_POWER 配線と衝突)。対策として V003 NRST を GPIO mode に。→ 内蔵ライタの制御 pin は target の使う pin と**基板設計段階で分離**する要件。
- **host tool が minichlink に固定**され Arduino IDE から使えない → まさに [generic-probe-design.ja.md](generic-probe-design.ja.md) §8 の「共通 probe protocol + ch32rv backend」が要る理由。protocol が共通なら Arduino IDE の Upload はそのまま繋がる。
- 初回 lot ごとに firmware MD5 が異なる(評価用/先行 lot)= **内蔵ライタ firmware の版管理**が運用課題になる。

→ **「内蔵ライタ MCU」は entry 問題を消す最も堅い構成だが、実装は (a) 内蔵 MCU を HW USB 持ちにする、(b) 制御 pin を target と分離、(c) 共通 protocol で host を選べるようにする、の 3 点で完成度が決まる。**

## 7. 推奨構成(案)

| | V003 / V00x(software USB) | V20x / L103 / X035 / V30x(HW USB) |
|---|---|---|
| BL 置き場 | **system 領域 1,920 B**(rv003usb BL ベース) | user flash 先頭 4–8 KB(or 書換可なら system) |
| transport | low-speed **HID** | **CDC + HID**(composite)、余裕あれば UF2 MSC |
| entry | A' + C + D(SE0 or UART magic を Core に)+ F + E | A' + D-1(1200-touch)+ C + F + E |
| 能力 | read/write/stub、metadata | 同 + 非破壊部分書込 + trial boot |
| host | Python/ch32rv `boot hid` + **WebHID** ページ | ch32rv `boot` + WebSerial/WebUSB ページ + `dfu-util` 互換 |
| Serial monitor | UART(物理)or app 側 software USB HID report | Core の CDC(BL と同 device) |

## 8. 既存資産との関係

- **rv003usb bootloader**(V003、1,920 B、HID、timeout/button/host 検出)= V003 側の土台。entry に D(RAM magic)と E/F を足す。
- **ch32fun `examples_usb/bootloader`**(X035/CH5xx、HW USB、stub 実行、`funRebootToBootloader`、非破壊)= HW USB 側の土台と protocol 思想。
- **WCH IAP**(`CheckNum@CalAddr`、`0x08005000`)= flash flag 型 entry と配置の参考。**tinyboot** = trial boot / confirm / CRC16。**wch-uf2** = double reset + MSC。**Swindle DFU** = RAM marker + CRC32 header。
- UIAPduino の「ボタン or app 側処理」は、上記 A'+D を Core に入れることで解消できる。

## 9. 検証項目(設計を確定するために測るもの)

1. **SRAM が system reset で保持されるか**(V003 / V203 / X035 で RAM magic が生き残るか、`.bss` 初期化前に読めるか)。
2. **A' の host 検出の速さ**: USB 接続時に BL が host を判定するまでの時間(VBUS / D± / 最初の bus reset まで)。窓を何 ms にできるか。
3. **D-3(SE0 検知)の実現性**: software USB 未搭載の app で、GPIO 監視だけで host の bus reset を拾えるか。host 側から意図的に bus reset を出せるか(Linux `USBDEVFS_RESET` / hub port reset)。**未検証の提案**なので最初に潰す。
4. **`RCC_RSTSCKR` の reset 原因判定**が V003/V20x で期待どおりか(SFTRSTF / IWDGRSTF / PINRSTF)。
5. **1,920 B に収まるか**: rv003usb BL に RAM magic + metadata 検査 + reset 原因を足したサイズ。
6. **HW USB chip で BL と Core が同じ USB device を共有**したときの再列挙時間と Windows での COM 番号安定性(1200-touch UX)。
7. WebHID / WebSerial から最小の read(UID)→ write(1 page)→ verify が通るか(ブラウザ経路の feasibility)。

## 参照

- bootloader 全般の実装事例・transport・共通仕様: [../protocols/custom-bootloader.ja.md](../protocols/custom-bootloader.ja.md)
- V003 の software USB(BL の物理土台): [../protocols/software-usb.ja.md](../protocols/software-usb.ja.md)
- WCH IAP の frame と entry(`CheckNum`): [../protocols/serial-and-print.ja.md](../protocols/serial-and-print.ja.md) §1
- factory ISP の入口の弱さ(なぜ置換したいか): [../protocols/pc-to-device-isp.ja.md](../protocols/pc-to-device-isp.ja.md)
- probe 側の汎用化(こちらは別解): [generic-probe-design.ja.md](generic-probe-design.ja.md)
- **前提の整理**(hardware に手を入れられるかで方針が変わる → T0〜T3 階層、共通/差替の境界、BL・app・probe の **VID/PID/serial 方針**): [ecosystem-any-hardware.ja.md](ecosystem-any-hardware.ja.md)
