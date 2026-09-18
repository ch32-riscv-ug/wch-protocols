# E129 SWIOだけでUIAP bootloaderを起動する

状態: **完了 — SWIOだけで2/2成功**（2026-09-18）

## 問い

ESP32 GPIO23→RST(PD7)を使わず、PD1/SWIO一本からRAMコードを注入・実行するだけで、オリジナルbootloaderの`1209:b803`を起動できるか。

## 仮説

E128ではRAMコード上の`NVIC_SystemReset()`相当（`PFIC_CFGR=0xBEEF0080`）が成立して再attachできた。BOOT_MODE設定とPD4 LOWの後にも同じCPU実行resetを置けば、最後の外部resetは不要になる。

## 反証条件

SWIO attach、RAM注入、CPU resume、software reset後の再attachのいずれかが失敗する、またはWindows側にB803が90秒以内に現れない。

## 方法

1. SWIOから第1 payloadを実行し、CPU自身のsoftware resetで既存BOOT状態を正規化する。
2. software reset後にSWIO再attachする。
3. 第2 payloadでBOOT_MODE設定、PD4 output LOW、CPU loopによるdetach待機を行う。
4. 同じpayloadの末尾で`PFIC_CFGR=0xBEEF0080`を書き、software resetする。
5. Windows `usbipd.exe list`でB803を最大90秒待つ。

GPIO23は全過程でINPUTのままにし、bootloader/user flashは変更しない。

## ベンチ種別

**一時**。ESP32 GPIO16→V003 PD1/SWIO。GPIO23→RST(PD7)は接続されたままだが駆動しない。

## 完了条件

B803出現、または90秒timeout/操作失敗を記録したら完了。

## 結果

成功。GPIO23を一度も駆動せず、PD1/SWIOだけで`1209:b803`を2回連続して起動できた。

- 開始時のB803状態から第1 software reset payloadを実行し、B803消失を確認
- software reset後のSWIO再attach: `DMCFGR=0x5aa50401`、halt成功
- 第2 payload: 45 wordsをRAM `0x20000000`へ注入、DPC設定・resume成功
- 第2 payload内でBOOT_MODE設定、PD4 LOW、5,000,000 loop待機、`PFIC_CFGR=0xBEEF0080`
- Windows B803出現: **1.302秒 / 1.133秒**、いずれもBUSID `7-2`、検出時`Shared`
- pytest: 2 runともpass（20.76秒 / 14.22秒）
- run: `_runs/E129_20260918T030637Z_default/`、`_runs/E129_20260918T030702Z_default/`

V003のbootloader/user flashは変更していない。GPIO23はESP32起動時にINPUTへ設定した後、そのままHi-Zを維持した。

追試用firmwareでは、追加ジグからの復帰確認用にserial command `R` / `H`も公開した。
`R`はGPIO23→RST(PD7)を20 msだけLOWにし、その後INPUT (Hi-Z)へ戻すreset単体の診断。
`H`はE128の成立手順（CPU resetで正規化→SWIOからBOOT_MODE設定とPD4 LOW→GPIO23外部reset）を
一操作にした復帰コマンド。上記E129の2/2結果ではどちらも使用しておらず、SWIO-onlyという
結論には影響しない。

## 結論

外部resetは不要。必要なのは、SWIOからRAMコードとDPCを設定したあと、halt中のabstract commandではなくV003をresumeして通常実行状態で処理させることである。

`NVIC_SystemReset()`の実体はUIAP coreで次の1 writeであり、payloadも同じ値をCPUから書いた。

```c
NVIC->CFGR = NVIC_KEY3 | (1 << 7);
// address 0xE000E048, value 0xBEEF0080
```

したがってSEDIOのboard固有boot操作は、追加reset線を要求せず「RAM書込み・DPC設定・resume」のSWIO primitiveだけで構成できる。
