# E142 ESP32-P4からCH32X035のRVSWD固定ペアを確認する

状態: **完了**（2026-09-20）

## 問い

追加されたESP32-P4（MAC `30:ed:a0:e3:11:08`）のGPIO2=SWDIO、GPIO54=SWCLKという候補で、CH32X035のRVSWDへ接続できるか。

## 方法

- 最初はユーザー指定の1組だけを調べる。成立しなければ、ブートストラップを除く接続候補20本の全有向ペアへ同じ判定を展開する。
- 100個のHIGH clockとSTOPでRVSWDを初期化する。
- `DMCONTROL.dmactive=1`だけを書き、`DMCONTROL(0x10)`と`DMSTATUS(0x11)`を読む。
- `DMCONTROL == 1`、DMSTATUS version nibbleが既知値（3または12）、両readのparity一致を同時に満たしたときだけ発見とする。
- 終了時は両線をHi-Zへ戻す。フラッシュ、option bytes、CPU haltは操作しない。

## 配線・環境

- probe: ESP32-P4 revision 1.3、`/run/board-identify/by-id/esp32-series-30eda0e31108`
- target: CH32X035（電源投入済み）
- 候補: GPIO2=SWDIO、GPIO54=SWCLK
- target USBはWindows側で未共有のままとし、この実験ではbindしない。

## 反証条件

- 5回とも有効なDMI応答を得られない。
- 応答dataらしき変化があってもparity、DMCONTROL readback、DMSTATUS versionのいずれかが不正。

## 次段

固定ペアが反証された場合は、同じread-only判定器を候補20本の `data != clock` 全380組へ展開する。各試行前後で全候補をHi-Zへ戻し、GPIO2/54を優先する。

## 固定ペア結果

GPIO2=SWDIO、GPIO54=SWCLKは5回とも不成立。idleは両方LOWで、RVSWDのidle HIGHとも一致しなかった。一部に`DMCONTROL=0x00001fff`等が現れたが、readback、DMSTATUS、parityの条件が同時成立せず、誤配線上のsampleと判定した。

ユーザー指示により次は配線済み20本を推測せず、P4の安全GPIO全体を走査する。公式資料に基づきGPIO24–27（USB PHY）、GPIO34–38（strapping、37/38はUART0兼用）を除外し、`0–23, 28–33, 39–54`の46本、全2070有向ペアを対象にする。誤ペアでの出力衝突を抑えるためopen-drain + pull-upで駆動する。

## 最終結果

最初の実装は52-bit frameのbit 47–51をtarget応答として読んだため失敗した。X035で動作済みの独立実装2系統と照合し、header後をhost固定`10101`、data parity後をhost固定`10111`、SWDIO=open-drain、SWCLK=push-pullへ直した。

修正版ではGPIO2=SWDIO、GPIO54=SWCLKが、half period 0/1/2/3/5 us × 各3回の**15/15で成功**した。全回で次が一致した。

- `DMCONTROL(0x10) = 0x00000001`
- `DMSTATUS(0x11) = 0x00000c82`
- 両readのdata parity一致

逆向き（GPIO54=SWDIO、GPIO2=SWCLK）は15/15で`0xffffffff`かつparity不一致だった。よって配線は**P4 GPIO2 → X035 PC18/SWDIO、P4 GPIO54 → X035 PC19/SWCLK**と確定した。

途中のopen-drain全線走査は2070組を完走したが、SWCLKまで内部pull-upだけで駆動したため0件だった。これは配線不在の証拠に使わない。走査ではSWDIOのみopen-drain、SWCLKはpush-pullに分ける必要がある。

## 事実・候補・未決

- **事実**: P4 GPIO2/54から実X035のDMI read/writeが成立する。
- **事実**: short frame末尾5 bitをtarget statusとして読む実装は失敗し、host固定`10111`は同じ配線・速度で15/15成功した。
- **候補**: 次のpin map探索はこのRVSWD接続からX035 GPIOを1本ずつ刺激し、P4の安全GPIOを同時観測する。
- **未決**: 配線された残り18本の対応、X035 reset線、USB/UARTを含む用途別の割当。
