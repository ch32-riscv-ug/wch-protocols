# E126 DMI ndmresetでuser↔boot HIDを切り替える

状態: **完了 — user移行成功、boot HIDはsoftware resetでは起動せず**

## 問い

BOOT_MODEをSWIO経由でread-backして確認した後、DMI `DMCONTROL.ndmreset` をassert/releaseすれば、V003をuser modeへ移し、続いてboot HID `1209:b803`へ戻せるか。

## 仮説

E125の失敗はPFIC reset writeに限定される。debug module自身が提供するndmresetならtarget resetを直接制御できる。

## 反証条件

BOOT_MODE read-back不一致、user mode後60秒以内にWindows側B803が消えない、再attach不能、またはboot mode後90秒以内にWindows側B803が戻らない。

## 方法

E125と同じ二段階試験だが、各mode write後に`FLASH_STATR`をread-backする。resetは`DMCONTROL=3`（dmactive + ndmreset）を10 ms保持後、`DMCONTROL=1`でreleaseする。USB列挙の一次観測はWindows `usbipd.exe list`、WSL自動attachは追加観測とする。

## 対象外

flash erase/program、HID通信、power reset、reset pulse幅の掃引。

## 必要な環境

E125と同じ。

## ベンチ種別

**一時**。配線変更なし。

## 記録する数値

mode read-back、ndmreset assert/release、Windows側B803消失・出現時間と状態、WSL側descriptor。

## 完了条件

B803再出現、または各timeout/操作失敗を記録したら完了。

## 影響

SEDIOのboot操作に必要なreset primitiveを決める根拠になる。

## 結果

pytest run: `_runs/E126_20260918T012111Z_default/`。boot側の一時的abstract-command errorをclear後、同じuser状態からretry版firmwareでboot操作だけを再実行した。

### user mode

- `FLASH_STATR` read-back: `0x00000000`
- `DMCONTROL.ndmreset`: assert / release成立
- Windows側 `1209:b803`: reset後 **0.249秒**で消失

### boot mode

- user app実行中からSWIO再attach: 成功
- 最初のrunではBOOT_KEY1のprogram-buffer実行がcmderr=3 (`-33`)。error clear後のretryでは全word writeが1回目で成功
- `FLASH_STATR` read-back: `0x00004000`（BOOT_MODE=1）
- `DMCONTROL.ndmreset`: assert / release成立
- Windows側 `1209:b803`: **90秒待っても出現せず**

終了時点でWindows/WSLのどちらにもB803は存在しない。targetはuser app側に戻っている。

## 事実

1. DMI `ndmreset` はV003を実際にresetできる。PFIC memory writeを使ったE125と違い、user modeへの移行をUSB消失で確認した。
2. SWIO経由のBOOT key解錠とBOOT_MODE bitのset/clearはread-backまで成立した。
3. BOOT_MODE=1でndmresetしても、software USB boot HIDは90秒以内に列挙しなかった。

## 候補

観測はUIAP bootloader forkの既知コード「power-on reset以外なら即user codeへjump」と一致する。SWIOからのsoftware/debug resetだけでHIDに留めるには、bootloader側のsoftware-reset entry対応、またはpower-on reset相当の外部reset/power controlが必要と考えられる。

## 未決

- reset/power制御線がESP32の他GPIOへ接続されているか
- UIAP bootloaderを変更せず、debug状態からPOR判定を迂回して安全にentryできるか
- `SOFT_REBOOT_TO_BOOTLOADER`対応版bootloaderなら同じ手順で留まるか

## 反映

DMI ndmresetをSWIO reset primitive候補として記録する。一方、「BOOT_MODE=1 + software resetでUIAP B803を起動できる」は反証された。
