# E127 UIAP公式Seamless SwitchをSWIOから再現する

状態: **完了 — debug経由のregister操作ではboot HIDを起動できず**（2026-09-18）

## 問い

UIAPduino Pro Micro CH32V003 V1.4の公式手順どおり、BOOT_MODE設定に加えてPD4（software USB D−）をOUTPUT LOWにしてdetach期間を作ってからSWIO/DMI resetすれば、オリジナルbootloaderの`1209:b803`を起動できるか。

## 仮説

E126はBOOT_MODEとreset自体は成立したがPD4操作を欠いた。公式の3行はPD4をLOW出力にしてUSB hostへdetachを認識させるため、これを加えれば短いbootloader待機時間内にWindowsが列挙できる。

## 反証条件

BOOT_MODE read-back、PD4 register設定、resetのいずれかが失敗する、またはWindows側に90秒以内にB803が現れない。

## 方法

1. user app状態（Windows側B803不在）を基準にする。
2. SWIO attach/haltし、`FLASH_STATR`を読む。
3. BOOT key解錠、BOOT_MODE=1、read-back。
4. GPIOD clockを有効化し、PD4をpush-pull output LOWにする。100 ms保持する。
5. ndmreset、続いてESP32 GPIO23→V003 RST(PD7)の外部resetをそれぞれ試し、Windows `usbipd.exe list`でB803を最大90秒待つ。

bootloader/user flashは変更しない。

## 対象外

bootloader改変、user app書換え、HID feature report、PD4 detach時間の掃引。

## 必要な環境

E126と同じ。開始時はuser app状態でB803不在。

## ベンチ種別

**一時**。配線変更なし。

## 記録する数値

STATR、RCC/GPIOD read-back、detach保持時間、reset後のWindows列挙時間・BUSID・usbip state。

## 完了条件

B803出現、または90秒timeout/操作失敗を記録したら完了。

## 影響

UIAP固有のboot操作をSEDIOから提供する際に、BOOT_MODEだけでなくUSB detach操作も必要かを確定する。

## 結果

反証。SWIO attach/haltと周辺register設定はすべて成功したが、どちらのresetでもB803は90秒以内に出現しなかった。

- `DMCFGR=0x5aa50401`、`DMSTATUS=0x004c0382`
- `FLASH_STATR`: 設定前後とも`0x00004000`
- PD4 read-back: `APB2PCENR=0x00000030`、`CFGLR=0x44434444`（PD4 nibble=`3`）、`OUTDR=0x00000000`
- PD4 LOW保持: 100 ms
- DMI `ndmreset`: B803は90秒以内に出現せず
- GPIO23から20 ms LOWの外部reset: B803は90秒以内に出現せず

初回の既存BOOT bit消費resetでは再attach直後の`DMCFGR=0xffffffff`となった。この経路を除去しても結果は変わらなかった。

## 解釈

PD4 detach不足だけがE126失敗の原因ではない。halt中のdebug abstract commandによるFLASH/GPIO writeはread-backできても、UIAPのユーザーコードがCPU上で行う処理と同じ結果にはならない。CPU実行との差をE128で検証する。

run: `_runs/E127_20260918T013546Z_default/`（ndmreset）、`_runs/E127_20260918T024606Z_default/`（GPIO23外部reset）。
