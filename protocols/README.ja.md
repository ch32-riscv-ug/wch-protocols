# protocols — 実プロトコル仕様(索引)

領域ごとの byte レベル仕様。status 語彙は [../README.md](../README.md) 参照。層の位置づけは [../guides/overview.ja.md](../guides/overview.ja.md) / [../guides/advanced.ja.md](../guides/advanced.ja.md)。

| ファイル | 領域 | 層 | 状態 |
|---|---|---|---|
| [pc-usb-driver.ja.md](pc-usb-driver.ja.md) | PC 側 USB ドライバ層(host が device を開く。**Windows は 2 系統**) | L1/L2 の PC 側境界(全 USB 経路に共通) | Windows **verified** / 他 attested |
| [pc-to-link.ja.md](pc-to-link.ja.md) | PC ↔ WCH-Link(USB) | L2 転送 + L3 WCH cmd | **大半 verified** |
| [riscv-debug-module.ja.md](riscv-debug-module.ja.md) | RISC-V Debug Module(DMI 上) | L3 DMI + L4 DM | **大半 verified** |
| [link-to-target.ja.md](link-to-target.ja.md) | WCH-Link ↔ target(SWIO/RVSWD 線) | L1 物理 + L2 線上 DMI | **todo** |
| [pc-to-device-isp.ja.md](pc-to-device-isp.ja.md) | PC ↔ target(factory ISP、**USB / UART シリアル**) | L3 ISP | **attested** |
| [serial-and-print.ja.md](serial-and-print.ja.md) | target 側シリアル I/O(**WCH IAP** / USART printf / SDI printf、**全シリーズ対応表**) | L3/L4 target firmware | **attested**(WCH 公式 EVT ソース) |
| [custom-bootloader.ja.md](custom-bootloader.ja.md) | custom bootloader(DFU/UF2/UART/HID/OTA、実装事例) | L3 各 bootloader | **reference / attested** |
| [software-usb.ja.md](software-usb.ja.md) | software USB(V003 系の bit-bang USB。hardware USB 無し chip) | L1/L2 を firmware 合成 | **reference / attested** |
| [dap.ja.md](dap.ja.md) | CMSIS-DAP(ARM mode) | L3 DAP | **todo** |

線上・自作 probe・host ツールの landscape(採用事例・言語・license・リンク): [../references/probe-ecosystem.ja.md](../references/probe-ecosystem.ja.md)。

## 依存関係

```
flash / semihosting / gdb            ← L4 アプリ(pc-to-link §flash, riscv-debug-module)
   └ RISC-V Debug Module(halt/reg/mem) ← riscv-debug-module.ja.md
        └ DMI(DmiOp cmd 0x08)          ← pc-to-link.ja.md §DmiOp
             └ WCH-Link USB frame       ← pc-to-link.ja.md §frame/endpoint
                  └ [PC 側ドライバ層]     ← pc-usb-driver.ja.md(host が device を開けて初めて上が動く)
                  └ 線上 DMI(SWIO/RVSWD) ← link-to-target.ja.md(未解読)
```

上の層ほど verified が進んでいる(PC↔Link と DM は実機確認済み)。下の物理線は Link firmware がブラックボックス化しており未解読。**PC 側ドライバ層**([pc-usb-driver.ja.md](pc-usb-driver.ja.md))は USB frame の手前で全経路に共通に効く — Windows で「列挙できるのに開けない」の大半はここ。
