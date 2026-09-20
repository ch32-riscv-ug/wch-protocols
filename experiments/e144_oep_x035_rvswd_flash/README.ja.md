# E144 OEP経由でX035をRVSWD書込みする

状態: **完了**（2026-09-20）

## 問い

E142/E143で確定したESP32-P4とCH32X035のRVSWD配線を、暫定OEP probeへ移植し、hostから
OEPの共通TargetControl、TargetMemory、TargetFlashだけを使ってflashを書換えられるか。

## fixture

- probe: ESP32-P4 revision 1.3、MAC `30:ed:a0:e3:11:08`
- target: CH32X035C8T6（62 KiB flash）
- SWDIO: P4 GPIO2 → X035 PC18
- SWCLK: P4 GPIO54 → X035 PC19
- PC15はP4 GPIO15とGPIO45の2本へ接続された開発board仕様だが、RVSWD書込みでは使用しない

probe実装は`oep-probe-arduino`の`X035RvswdTargetControl`および
`Esp32P4X035Prototype`（commit `28f832c`）に置いた。

## 方法

1. OEP endpoint confirmationとoffered function一覧を取得する。
2. TargetControlでattach/halt状態を取得する。
3. TargetMemoryで`0x08000000`から32 byteを読む。
4. 末尾物理page `0x0800f700..0x0800f7ff`の256 byteを全て退避する。
5. 論理page `0x0800f7c0`へTargetFlashの64-byte patternを書込む。
6. TargetMemoryで256 byteを再読し、patternと隣接領域を別々に比較する。
7. 同じTargetFlash操作で元の64 byteを戻し、256 byte全体を比較する。
8. TargetControlでsoftware resetし、再attachとflash先頭readを行う。

X035の物理erase pageは256 byteだが、暫定OEP操作は64 byte単位である。backend内部で物理pageを
read-modify-erase-programし、要求外の192 byteを保持する。

## 結果

- confirmation成功: revision 1、maximum message 96
- offered function: TargetControl `0x0101`、TargetMemory `0x0102`、TargetFlash `0x0103`
- status成功: `flags=3`、`boot_status=3`（いずれも暫定値）
- flash先頭32 byte取得成功
- 64-byte pattern書込みとbackend内verify成功
- 独立readでpattern 64 byte一致、隣接192 byte不変
- OEP経由の復元後、256 byte全体が退避値と一致
- software reset後の再attachと先頭word read成功

退避前と復元後の256 byte SHA-256はともに
`3d6876a0146de8576eb2395a858de1213d1b92c65b779df3a331cfd5a4584546`だった。

## 結論

現在の暫定OEPサービスを変更せず、V003/SWIOとは異なるX035/RVSWD backendへ差し替えて、識別、
halt、memory read、flash erase/program/verify、resetまで実行できた。物理erase単位の差はprobe側で
吸収できる。

これは一台、低速software bit-bang、末尾1論理pageの破壊前提試験である。全image、反復、電源断、
複数個体、速度および失敗途中からの復旧は未検証であり、正式Protocol仕様の確定根拠にはしない。
