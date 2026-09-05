# software USB(V003 系: hardware USB を持たない chip の bit-bang USB)

状態: **reference / attested**(一次 OSS = rv003usb / ch32fun から転記。自前 capture 未)。層は **L1 物理 + L2 転送を firmware で合成**する特殊な層。

CH32V003(QingKe V2A)や CH32V00x・CH641 は **USB peripheral を持たない**。それでも USB 機器として振る舞うために、**2 本の GPIO を D+/D− に見立て、USB 1.1 low-speed を丸ごとソフトで叩く**のが software USB。代表実装は [rv003usb](https://github.com/cnlohr/rv003usb)(cnlohr)。これは:

- **custom bootloader** の transport(→ [custom-bootloader.ja.md](custom-bootloader.ja.md))、
- **V003 を probe 化する**ときの host link(→ [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md) の rvswdio_programmer)、
- 一般の V003 USB 機器ライブラリ、

の土台になっている。**hardware USB を持つ chip(下記)では不要**。

## 1. なぜ必要か / どの chip か

| USB の持ち方 | 該当(例) | USB の出し方 |
|---|---|---|
| **無し** | CH32V003 / V00x / CH641(QingKe V2A/V2C) | **software USB(bit-bang)** が唯一の道 |
| USBFS(full-speed) | CH32V103 / V20x / X035 / L103 ほか | hardware peripheral(ch32fun `USBFS/*`) |
| USBHS(high-speed) | CH32V307 / V30x ほか | hardware peripheral(ch32fun `USBHS/*`) |
| USBD / USBPD | 一部 V/X 系 | hardware peripheral |

hardware USB 側は WCH peripheral driver / ch32fun の USBFS/USBHS 例が担うので本書の対象外。本書は **peripheral 無し chip の software USB** に絞る。

## 2. 物理・電気(rv003usb 実測)

- **D+/D− は GPIO 0〜4 に限定**(`c.andi` 命令の 5bit 即値制約による)。**PC5–PC7 / PD5–PD7 は不可**。
- **pull-up**: D− に **1.5kΩ**(low-speed の規定)。GPIO で on/off する構成も可(`USB_PIN_DPU`)。
- 任意で **33–47Ω** の直列保護抵抗を D+/D−。
- speed: **USB low-speed(1.5 Mbps)**。
- CPU は bit を追うため高クロックで回す(V003 を 48 MHz 付近で駆動する構成が一般的。正確値は実装で確認)。

## 3. どう動くか(タイミングが全て)

- コアは **pin-change 割込みで走る assembly** + 約 250 行の C。
- **pin-change 割込みは最優先・非 preempt**(横取りされると USB が壊れる)。critical section は **~40 cycle まで**。長い処理は preemption を有効化して別扱い。
- **NRZI 復号・bit-stuffing・CRC を受信しながら in-line で処理**(low-speed でも余裕が無い)。
- beta。タイミング最適化が継続中(CRC 誤差 ~6 cycle の記述)。

→ この timing 制約が software USB の本質的な難所。移植時は clock・割込み優先度・critical section 長が最重要。

## 4. device class と host

- **driver 不要の HID**(low-speed の max packet = **8 byte**)。
- デモ: HID(gamepad / mouse+keyboard / custom message)、概念実証で MIDI・CDC serial・Ethernet(RNDIS)。
- host は HID(直接叩くなら custom HIDAPI)。専用 driver は要らない。
- code size: 基本 HID ~2 kB、bootloader **1,920 byte**。

## 5. software USB bootloader(rv003usb bootloader)

peripheral 無しの V003 を **USB 端子だけで更新**できる custom bootloader。factory ISP([pc-to-device-isp.ja.md](pc-to-device-isp.ja.md))の弱い entry を補う。

- 配置: **1,920 byte** の system 領域(`0x1FFFF000`。実体は 1,916 B + 末尾 4 B の secret)。BOOT 領域の詳細と切替レジスタは [custom-bootloader.ja.md](custom-bootloader.ja.md) §2a。
- host: **minichlink**(driver 不要 HID、`1209:B003`。protocol は [custom-bootloader.ja.md](custom-bootloader.ja.md) §2b)。
- **entry**: ~5 秒 timeout / button / host 検出。firmware から戻るには `funRebootToBootloader` 相当。
- bootloader 自身は自己更新しない(recovery は別 programmer)。
- V006 系は別実装(`rv003usb/bootloader_v006`)。

### stub 実行型という設計(ch32fun 版で明確化)

ch32fun の [`examples_usb/bootloader`](https://github.com/cnlohr/ch32fun/tree/master/examples_usb/bootloader) は rv003usb bootloader の移植・発展で、**hardware USB を持つ chip(X035/CH5xx で確認、他は WIP)**向け。設計思想が明快:

- **sketchpad(下書き)buffer に host から binary stub を送って RAM 上で走らせる**。これで「read/write/erase/…」の機能を **probe/host 側の変更だけ**で増やせる(bootloader firmware は小さいまま)。
- **非破壊書き込み**: 上書きする page だけ消す(ISP のような全消去をしない)。
- minichlink の全操作(gdb 以外)に対応、任意アドレス read-back。
- I2C/UART/iSLER 等へ transport 追加も可能。CH32V103 は 2 分割 boot partition のため未対応見込み。

## 6. 未解読 / 要調査

- ~~bootloader の stub protocol(minichlink 側)~~ → **[custom-bootloader.ja.md](custom-bootloader.ja.md) §2b に byte 単位で転記**(feature report ID `0xAA` 128 B、scratchpad `0x20000100`、末尾 `0x1234ABCD` で実行、完了印 `0xFF`、`runwordpad` の 3 状態、stub 一覧)。残るは USB capture での verified 化。
- rv003usb の USB frame(setup / HID report)の実バイトを capture で確定。
- V003 の駆動クロック実値(BL は `SYSTEM_CORE_CLOCK 48000000`)と割込みタイミングの余裕(移植の要点)。
- **BOOT 領域の起動選択**(option byte、`configurebootloader`)と `FLASH_STATR` bit14 切替 → [custom-bootloader.ja.md](custom-bootloader.ja.md) §2a。

## 7. 参照

- 一次実装: [rv003usb](https://github.com/cnlohr/rv003usb)(software USB + bootloader、V003)/ ch32fun [`examples_usb/bootloader`](https://github.com/cnlohr/ch32fun/tree/master/examples_usb/bootloader)(hardware USB 移植・stub 設計)。
- これを使う probe: [rvswdio_programmer](https://github.com/cnlohr/rv003usb/tree/master/rvswdio_programmer)(→ [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md))。
- custom bootloader 全体(DFU/UF2/UART/HID): [custom-bootloader.ja.md](custom-bootloader.ja.md)。factory ISP: [pc-to-device-isp.ja.md](pc-to-device-isp.ja.md)。
- software USB を **target 自身の BL** に使うか、**board 内蔵ライタ MCU** に使うか(UIAPduino V003 → V006 の判断。内蔵側でも low-speed の限界が出る): [../references/bootloader-design-space.ja.md](../references/bootloader-design-space.ja.md) §6b。
