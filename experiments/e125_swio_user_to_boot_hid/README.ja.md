# E125 user app から SWIO で boot HID を起動する

状態: **完了 — PFIC reset未成立**

## 問い

V003 をいったん user mode へリセットして `1209:b803` の消失を確認した後、user app 実行中に SWIO から BOOT mode へ切り替えると `1209:b803` を起動できるか。

## 仮説

E124 の register write 経路は成功している。user mode を明示的な前状態にすれば、boot reset 後の B803 出現を bootloader 起動の結果として判定できる。

## 反証条件

- user reset 後60秒以内に B803 が消えない。
- user app 実行中に SWIOへ再attachできない。
- boot reset後90秒以内にB803が現れない。

## 方法

1. B803を基準確認する。
2. SWIOでhaltし、BOOT key解錠、`FLASH_STATR=0`、PFIC resetでuser appへ移る。
3. Windows側の `usbipd.exe list` からB803が連続1秒以上不在になるまで最大60秒待つ。
4. SWIOへ再attachし、E124と同じ `FLASH_STATR=0x4000` + resetを行う。
5. Windows側一覧へのB803の出現を最大90秒待つ。`Shared` / `Attached` はどちらも列挙成功とし、自動attach後にWSLからも見えればdescriptorを追加確認する。

flash erase/programは行わない。

## 対象外

user app側の機能確認、HID feature report、flash更新、power-on reset。

## 必要な環境

E124と同じ一時ベンチ。

## ベンチ種別

**一時**。配線変更なし。

## 記録する数値

user reset各write、Windows側B803消失時間、boot reset各write、Windows側B803出現時間・BUSID・usbip state、WSLへ自動attachされた場合のUSB address・bcdDevice。

## 完了条件

B803再出現、または各timeout/安全な操作失敗を記録したら完了。

## 影響

SEDIOからtargetのbootloaderを明示起動できるかを実機で確定する。

## 結果

run: `_runs/E125_20260918T011802Z_default/`。

- Windows baseline: BUSID `7-2`, `1209:b803`, Attached
- SWIO attach: `DMCFGR=0x5aa50401`
- halt: `DMSTATUS=0x004c0382`
- BOOT key 2語、`FLASH_STATR=0`: abstract command status 0
- `PFIC_CFGR=0xBEEF0080` write command送信後、Windows側B803は60秒間消えなかった

## 事実

user modeへの遷移は観測されなかった。peripheral writeのabstract commandがerror無しでも、PFIC resetが発火したことまでは示さない。

## 候補

BOOT_MODEをread-backしたうえで、DMI `DMCONTROL.ndmreset` のassert/releaseを使ってresetを切り分ける。

## 未決

BOOT_MODE writeの実値と、SWIOから有効なreset手段。E126で確認する。

## 反映

仕様statusは変更しない。
