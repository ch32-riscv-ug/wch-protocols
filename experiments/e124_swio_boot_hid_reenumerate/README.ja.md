# E124 SWIO から V003 boot HID を起動して再列挙を確認する

状態: **完了 — 基準状態が不適切で判定不能**

## 問い

現在 `1209:b803` として列挙中の UIAPduino CH32V003 に GPIO16 の SWIO からアクセスし、BOOT mode を選んで software reset すると、同じ HID bootloader が再列挙するか。

## 仮説

SWIO/DMI で CPU を halt し、`FLASH_BOOT_MODEKEYR` を解錠して `FLASH_STATR.BOOT_MODE=1`、reset flag clear、PFIC system reset の順に実行すれば BOOT 領域から再起動する。ただし UIAP fork の reset 原因判定によって即 user code へ抜ける可能性がある。

## 反証条件

SWIO attach または peripheral register write が失敗する、USB が一度も消えない、もしくは reset 後90秒以内に `1209:b803` が再出現しない。

## 方法

1. host から `1209:b803` が存在することを基準確認する。
2. E123 の SWIO PHY で attach し、CPU に halt request を出す。
3. DMI program buffer 経由で次を target peripheral register に書く。
   - `0x40022028 = 0x45670123`, `0xCDEF89AB`
   - `0x4002200C = 0x00004000`
   - `0x40021024 = 0x01000000` (reset flag clear)
   - `0xE000E048 = 0xBEEF0080` (system reset)
4. host は USB の消失を30秒、再出現を90秒待つ。再出現時の bus/address、`bcdDevice`、interface class を記録する。

フラッシュ erase/program は行わない。変更するのは volatile peripheral/debug register のみ。

## 対象外

HID feature report 通信、bootloader による flash 更新、app→boot の firmware hook、電源断リセット。

## 必要な環境

E123 と同じ ESP32 GPIO16↔V003 PD1、一時ベンチ。V003 software USB は host に接続済み。開始時に `1209:b803` が見えること。

## ベンチ種別

**一時**。配線変更なし。

## 記録する数値

開始時USB address、SWIO attach/halt/write結果、USB消失時間、再列挙時間、再列挙descriptor。

## 完了条件

再列挙成功、または安全な手順が失敗、または90秒 timeout を記録したら完了。

## 影響

SEDIO の reset/boot 操作候補と、V003 software USB bootloader を外部probeから回復・起動できる範囲を明らかにする。

## 結果

run: `_runs/E124_20260918T011332Z_default/`。

- 開始時: `1209:b803`, bus 5, address 29
- SWIO attach: `DMCFGR=0x5aa50401`
- halt: `DMSTATUS=0x004c0382`
- BOOT key、BOOT_MODE=1、reset flag clear の4 write: 全て status 0
- system reset command: 送信
- 30秒の消失待ち + 90秒の再出現待ちの間、`1209:b803` は bus 5/address 29 のまま連続して見えた

## 事実

SWIO から target の halt と peripheral register write までは成立した。一方、開始時点ですでに boot HID 上だったため、HID が見え続けた観測だけでは boot 経路を再実行したか判定できない。

## 候補

先に `BOOT_MODE=0` で user app へ移し HID の消失を確認してから、改めて `BOOT_MODE=1` にする二段階試験を行う。

## 未決

user app 実行中から boot HID を起動できるか。E125で確認する。

## 反映

仕様 status は変更しない。E124 の「HID消失を必須とする」判定は、開始状態を考慮していなかったため採用しない。
