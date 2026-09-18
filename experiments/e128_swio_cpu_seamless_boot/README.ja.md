# E128 CPU実行でUIAP Seamless Switchを再現する

状態: **完了 — boot HID起動成功**（2026-09-18）

## 問い

SWIOデバッガ自身による周辺register write/resetではなく、V003のRAMへ注入したコードをCPUに実行させてUIAP公式手順相当を行えば、オリジナルbootloaderの`1209:b803`を起動できるか。

## 仮説

E127でBOOT_MODEとPD4 LOWはread-backどおり設定できたが、DMI ndmresetでもGPIO23外部resetでもB803は出なかった。公式コードと残る差は、FLASH/PFIC操作をhalt中のdebug abstract commandで行わずCPU自身が実行する点である。

## 反証条件

RAM注入、DPC設定、resume、再attachのいずれかが失敗する、または最終reset後90秒以内にWindows側へB803が現れない。

## 方法

1. GPIO23 resetでuser app状態を作る。
2. SWIO attach/halt後、RAM `0x20000000`へ短いRV32Eコードを注入する。
3. DPCをRAMへ向けてresumeし、CPU自身に`NVIC_SystemReset()`相当を実行させる。
4. 再attach/halt後、BOOT_MODE設定、PD4 output LOW、100 ms待機をCPUコードとして実行する。
5. GPIO23から外部resetし、Windows `usbipd.exe list`でB803を最大90秒待つ。

bootloaderとuser flashは変更しない。

## ベンチ種別

**一時**。ESP32 GPIO16→V003 PD1/SWIO、GPIO23→V003 RST(PD7)。

## 完了条件

B803出現、または90秒timeout/操作失敗を記録したら完了。

## 結果

成功。2回連続でWindows側に`1209:b803`が操作開始から**1.551秒 / 1.071秒**で出現した（BUSID `7-2`、検出時`Shared`）。その後usbip自動attachされ、WSLでも`Generic 32V003`として確認した。

- NVIC reset payload: 6 wordsをRAM `0x20000000`へ書込み、DPC設定・resume成功
- reset後のSWIO再attach: `DMCFGR=0x5aa50401`、halt成功
- BOOT/PD4 payload: 36 wordsを同じRAMへ書込み、DPC設定・resume成功
- PD4 LOW待機: 100 ms
- GPIO23→RST(PD7): 20 ms LOW後Hi-Zへ解放
- pytest: 2 runともpass（21.44秒 / 15.93秒）
- run: `_runs/E128_20260918T025116Z_default/`、`_runs/E128_20260918T025307Z_default/`

V003のbootloaderとuser flashは変更していない。変更したのはresetで失われるRAMだけである。

## 結論

ESP32 GPIO16一本のSWIOとGPIO23のreset線から、UIAPduino Pro Micro CH32V003 V1.4のオリジナルbootloaderを起動できる。ただし、単なるdebug memory write + DMI/external resetでは足りず、少なくとも今回の実機ではFLASH/PFIC操作をV003 CPU自身に実行させる必要があった。

`payload.rv32.S.txt`は注入word列の原典であり、UIAP同梱toolchainの`riscv-none-embed-as -march=rv32imac -mabi=ilp32`で生成した。
