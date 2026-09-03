# 全体ガイド(初心者向け)

「PC から WCH の CH32 チップに書き込む/デバッグする」ときに、内部で何が起きているかを層(レイヤー)で順に説明する。ここを読めば、[protocols/](../protocols/) の各仕様が「どの層の話か」わかるようになる。

## 0. そもそも何をしたいのか

マイコン開発でやりたいことは大きく 2 つ:

- **書き込み(flashing)**: 自分のプログラム(firmware)をチップの flash メモリに焼く。
- **デバッグ(debugging)**: チップを止めて(halt)、レジスタやメモリを覗く・書き換える、1 命令ずつ進める(step)、ブレークポイントで止める。

どちらも「PC からチップの中身を触る」ことなので、その通り道(経路)と、通り道の中の約束事(protocol)が要る。

## 1. 経路は 4 つある

チップに触る道は 1 本ではない。**経路が違うと、使える操作も約束事も根本的に違う**。

| 経路 | ざっくり | 何が要る |
|---|---|---|
| ① **debug probe** | 専用の変換器(WCH-Link)を間に挟む | WCH-Link 1 個 + 2〜3 本の線 |
| ② **factory ISP** | チップに元から入っている書き込み用 bootloader を USB で叩く | チップの USB を PC に直結 + BOOT ピン操作 |
| ③ **custom bootloader** | アプリに仕込んだ独自 bootloader(DFU/UF2/HID 等) | その bootloader 次第 |
| ④ **DAP** | WCH-Link を「普通の ARM 用デバッガ(CMSIS-DAP)」として使う | ARM チップ向け |

この repo の主役は **① debug probe**(いちばん高機能でデバッグもできる)。②③④ も同じ repo で扱う。

## 2. ① debug probe の中を層で見る

PC から WCH-Link を通してチップを触るとき、信号は次の 4 層を通る。**下(物理)から上(やりたいこと)へ積み上がっている**——ネットワークの階層と同じ考え方。

```
┌─────────────────────────────────────────────┐
│ L4 アプリ層   flash 書込 / メモリ読み書き / halt/step / print │  ← やりたいこと
├─────────────────────────────────────────────┤
│ L3 プロトコル層  WCH-Link コマンド  +  DMI(RISC-V Debug)      │  ← 約束事
├─────────────────────────────────────────────┤
│ L2 転送層     USB bulk 転送  |  線上の DMI 転送                │  ← 箱に詰めて運ぶ
├─────────────────────────────────────────────┤
│ L1 物理層     USB の電気信号  |  SWIO(1線)/ RVSWD(2線)      │  ← 電圧の上げ下げ
└─────────────────────────────────────────────┘
        PC ↔ WCH-Link 区間          WCH-Link ↔ target 区間
```

区間が 2 つある(PC↔Link と Link↔target)ので、各層も 2 区間ぶんある。

### L1 物理層 — 電気信号そのもの

- **PC ↔ WCH-Link**: USB ケーブル(D+/D− の 2 本で差動信号)。ここは普通の USB。
- **WCH-Link ↔ target**: チップの debug ピンに 1〜2 本つなぐ。
  - **1 線式(SWIO)**: データ 1 本で双方向(例: CH32V003 の PD1)。
  - **2 線式(RVSWD)**: クロック(SWCLK)+ データ(SWDIO/DAT)の 2 本(例: PA14 + PA13)。
  - ほかに NRST(リセット)と電源(3.3V/GND)。

### L2 転送層 — バイト列を箱に詰めて運ぶ

- **PC ↔ WCH-Link**: USB の **bulk 転送**。WCH-Link には 2 系統の口(endpoint)がある。
  - コマンド用: `0x01`(PC→Link) / `0x81`(Link→PC)
  - flash データ用: `0x02` / `0x82`
  - 1 コマンドは `0x81 | cmd | 長さ | 中身…` という枠(frame)で送る。詳細は [pc-to-link.ja.md](../protocols/pc-to-link.ja.md)。
- **WCH-Link ↔ target**: WCH 独自の線上エンコードで、後述の「DMI という読み書き要求」を運ぶ。**この線上の詳細はまだ解読できていない**(Link のファームが中でやっている)。

#### さらに細かい層: PC 側のドライバ

L2 の PC 側には、もう 1 枚「OS のどの**ドライバ**が USB device を握り、ツールがどの API で届くか」という層がある。ここを通せないと 1 byte も送れない。**Linux/macOS は汎用の usbfs で普通に開ける**が、**Windows は WCH-Link へのアクセスが 2 系統**ある:

- **WinUSB 系統**: Zadig で置換 or 自動 bind。`nusb`/`libusb` がそのまま開ける。ただし WCH-LinkUtility が使えなくなる。
- **WCH 純正ドライバ系統**(`WCHLinkW64.SYS`、CH375 系): WCH のツールを入れると当たる。WinUSB では開けず、**純正ドライバの IOCTL を直接叩く**(Zadig 不要)。

両者は排他で、「列挙できるのに開けない」の大半はここ。詳細は [pc-usb-driver.ja.md](../protocols/pc-usb-driver.ja.md)。

### L3 プロトコル層 — 約束事(何をどう頼むか)

ここが 2 段構えなのがポイント:

- **WCH-Link コマンド(WCH 固有の殻)**: 「firmware 版を教えて」「速度を設定」「チップに attach」「電源 on」「flash 領域を指定して書け」等。WCH-Link にしか通じない。
- **DMI(RISC-V 標準の中身)**: 「デバッグ用レジスタの 0x11 番地を読め/書け」という、**RISC-V の公式仕様(Debug Spec)で決まった汎用の読み書き要求**。ベンダに依らない。
  - これは WCH-Link コマンドの `DmiOp`(cmd `0x08`)の中に入れて送る。→ **WCH 固有の殻の中に、RISC-V 標準の中身が乗る**構造。

### L4 アプリ層 — やりたいこと

DMI で debug 用レジスタを叩けるようになると、その上に **RISC-V Debug Module** という「チップを止める/レジスタを読む/メモリを読む」仕組みが立つ(これも RISC-V 標準)。さらにその上に:

- **flash 書き込み**: RAM に小さな loader(stub)を送り込んで走らせ、flash controller を叩いて焼く。
- **メモリ/レジスタ読み書き**、**halt/resume/step**、**ブレークポイント**。
- **実行時に target が文字を出す**手段は複数(物理 UART printf / 物理線不要の SDI printf / RTT / semihosting)。firmware 側の出し方(EVT が提示、全シリーズ対応表つき)は [serial-and-print.ja.md](../protocols/serial-and-print.ja.md)、PC 側の読み方は [pc-to-link.ja.md](../protocols/pc-to-link.ja.md) §monitor。

## 3. 「flash 1 回」で何が起きるか(具体例)

`ch32rv flash blink.bin` の裏側を層でたどると:

1. **L2/L3**: USB で WCH-Link に「firmware 版は?(GetProbeInfo)」→「速度設定(SetSpeed)」→「チップに attach(AttachChip)」。attach の応答でチップの family と ID がわかる。
2. **L3(DMI)**: `DmiOp` で target の Debug Module を叩き、**halt**(止める)。
3. **L4**: flash 用の小さな loader(stub)を target の **RAM に書き込み**、実行させる。
4. **L2(data EP)**: firmware 本体を `0x02` の口から stub へ流し込む。stub が flash controller を使って焼く。
5. **L4**: 焼けたら **読み戻して照合**(verify)、**reset して実行**。

この 5 段が [protocols/](../protocols/) の各仕様に対応している。

## 4. ②③④ の経路は何が違うか

- **② factory ISP**: WCH-Link を使わず、チップに元から入っている **書き込み専用 bootloader** を PC から直接叩く。DMI もデバッグも無く、flash 焼きと設定だけ。BOOT ピンを操作して bootloader に入れる。同じ ISP protocol を **USB(チップの USB を直結)でも UART(シリアル)でも**使える — **UART 経由の手順は WCH の EVT / AN で提示されている**。→ [pc-to-device-isp.ja.md](../protocols/pc-to-device-isp.ja.md)。
- **③ custom bootloader**: アプリに仕込んだ独自 bootloader。WCH の EVT は **IAP**(app 内 bootloader、USB/UART 両対応、`0xAA 0x55`+`0x80..0x84`。factory ISP とは別 protocol)のサンプルを提示(→ [serial-and-print.ja.md](../protocols/serial-and-print.ja.md) §1)。ほかに DFU/UF2/HID/RS-485/OTA 等、実装事例は [custom-bootloader.ja.md](../protocols/custom-bootloader.ja.md)。**V003 のように USB peripheral を持たない chip**は、GPIO 2 本で USB を software で叩く(→ [software-usb.ja.md](../protocols/software-usb.ja.md))。
- **④ DAP**: WCH-Link を **CMSIS-DAP**(業界標準の ARM 用デバッガ規格)モードに切り替え、ARM チップを SWD/JTAG で触る。RISC-V の DMI とは別世界。

なお ① の WCH-Link は純正品以外に、**汎用 MCU(CH32V003・ESP32-S2/S3・RP2040 等)を probe 化した自作 firmware** が多数ある(採用事例・言語・リンクは [references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md))。これらは線上(SWIO/RVSWD)を解読した一次資料でもある(→ [link-to-target.ja.md](../protocols/link-to-target.ja.md))。

## 次に読む

- 各層をもっと深く、reverse engineering のやり方まで: [advanced.ja.md](advanced.ja.md)
- いま解読できている実プロトコル: [protocols/pc-to-link.ja.md](../protocols/pc-to-link.ja.md) / [riscv-debug-module.ja.md](../protocols/riscv-debug-module.ja.md)
