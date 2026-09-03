# CMSIS-DAP(ARM mode)

状態: **todo**。層は L3(CMSIS-DAP)。経路 ④ — WCH-Link を DAP モードに切り替え、業界標準の CMSIS-DAP デバッガとして ARM チップ(CH32F/CH57x 等の Cortex-M や他社 ARM)を SWD/JTAG で触る。RISC-V の DMI とは別系統。

## わかっていること

| 項目 | 内容 | 状態 |
|---|---|---|
| USB 識別 | ARM/DAP mode = `1a86:8012`(CMSIS-DAP + CDC) | attested |
| mode 切替 | RISC-V mode ↔ ARM mode の切替コマンドの記述あり(`81 ff 01 41` / `81 ff 01 52`)。要 capture | single-source |
| プロトコル | **CMSIS-DAP は公開標準**(ARM 提供)。WCH 固有ではないので、標準仕様がそのまま使える見込み | attested |
| version 照会の口 | DAP mode では version 照会(`81 0d 01 01`)が EP `0x02`/`0x83` で通るという実測あり | attested |

## 未解読(要調査)

- mode 切替コマンドの実バイトと前後手順(capture)。
- WCH-Link の DAP 実装が CMSIS-DAP v1(HID)か v2(bulk)か、対応コマンドの範囲。
- CDC との同居構成。

## 調査の入口

- CMSIS-DAP 公式仕様(ARM)= プロトコル本体はここ。
- pyOCD / OpenOCD の cmsis-dap ドライバで WCH-Link(DAP mode)を実測。
- 本 repo では「WCH 固有部分(mode 切替・enumerate)」を中心に記録し、DAP 本体は標準へ委譲する。
