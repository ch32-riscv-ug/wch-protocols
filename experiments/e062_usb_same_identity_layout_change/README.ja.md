# E062 同一USB identityでのinterface構成変更

状態: **計画**

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E013](../e013_usb_descriptor_profiles/README.ja.md)(中断) / 根拠: [USB descriptor変更に対するhostの挙動](../../references/usb-host-descriptor-persistence.ja.md)

## 問い

**同一VID:PID・同一serialのESP32-S3が接続の間にinterface構成を変えたとき、Windows 11は次のどの切替で既存devnodeとdriverを再利用して誤ったbindingを残すか。(1)HID単機能↔composite、(2)compositeへの末尾function追加、(3)既存interface番号の機能入替。また、`bcdDevice`だけを変えた場合とserialだけを変えた場合に結果は変わるか。Linuxでは全条件で再認識されるか。**

## 仮説

Windows 11は(1)と(3)で既存devnodeを再利用してdriverを選び直さず、(2)では既存childを保ったまま新childを追加して動く。`bcdDevice`の値は結果に影響しない。serialを変えると新instanceになり、すべて再認識される。Linuxは全条件でdescriptorを読み直し、期待どおりのdriverとdevice nodeになる。

根拠は[調査結果](../../references/usb-host-descriptor-persistence.ja.md)§1.1〜1.5。Windowsのdevice instance IDに`bcdDevice`が含まれないことは一次資料で確認済み。既存devnodeの再利用はコミュニティ観測(OSR 2011、2017)で、Microsoft文書には記述がない。Windows 10では再現しなかったという報告が一件あるため、Windows 11の実挙動はこの実験で決める。

## 反証条件

次のいずれかが起きたら、現在の仮説をそのまま採用しない。

1. (1)でWindows 11が自動的にusbccgpへ切り替え、全interfaceが列挙・通信できる。devnode再利用による破綻は起きないことになり、同一PID内の自由度は仮説より大きい
2. (2)で新しいchildが列挙されない、または既存childのdriverが壊れる。「末尾追加は安全」が誤り
3. `bcdDevice`だけを変えた条件と変えない条件で結果が異なる。`bcdDevice`がidentityに効いており、E013の仮説を再検討する
4. serialを変えても旧状態が残る。instance identityの理解が誤り
5. Linuxで構成変更後にdriver bindやdevice nodeが期待と異なる
6. Linuxのraw descriptorが意図したprofile定義と異なる。firmware側の問題であり、Windowsの結果を判定に使わない

## 方法

### Profile

すべて`1209:0001`(pid.codesのprivate test用)、product string同一、`bDeviceClass`は単機能では`0x00`、compositeでは`0xEF/0x02/0x01`とする。serialは既定でESP32-S3のMAC由来。

| Profile | interface構成(番号: function) | 用途 |
|---|---|---|
| **A** | 0: vendor-defined HID | 単機能。E013のProfile Aと同じ |
| **B** | 0: vendor-specific(WinUSB) / 1: HID / 2–3: CDC ACM | composite。E013のProfile Bと同じ構成 |
| **B-rev** | Bと同一、`bcdDevice`のみ`0002` | `bcdDevice`の効果 |
| **B-ser** | Bと同一、serialに接尾辞`-P2` | serialの効果 |
| **C** | B + 4–5: 2本目のCDC ACM | 末尾追加 |
| **D** | 0: HID / 1: vendor-specific / 2–3: CDC ACM | interface 0と1の機能入替 |

Cの2本目がArduino-ESP32 3.3.11で構成できない場合は、2本目のvendor-specificまたはHIDで代替し、代替したことを記録する。末尾追加という条件は変えない。

### Sequence

各sequenceはclean stateから始める。clean stateは、試験deviceに関するdevnode(非表示を含む)をDevice Managerでuninstallし、`usbflags\12090001*`キーが存在しないことを確認した状態、またはVM snapshotの復元とする。どちらを使ったかを記録する。

| Seq | 手順 | 見るもの |
|---|---|---|
| S1 | A → B | 単機能→composite。親のdriver、child列挙、echo |
| S2 | A → B-rev | S1と同じ観測で、`bcdDevice`の差が結果を変えるか |
| S3 | A → B-ser | serialの差で新instanceになるか |
| S4 | B → C | 末尾追加。既存childの継続、新childの列挙とCOM番号 |
| S5 | B → D | interface 0/1の機能入替。各childのdriver |
| S6 | C → B | function削除。ghost devnodeとCOM番号の残り方 |
| S7 | S1とS4を、Windows再起動後と別USB portで再確認 | 永続性 |

各段階で、present/非表示を含む全devnodeのinstance ID、hardware ID、compatible ID、driver、service、COM port、`usbflags`キー、USBViewのdescriptor、HID/Vendor/CDCのecho結果を保存する。

### Linux

Profile A、B、C、Dごとに`lsusb -v`とhost toolのJSONを保存し、HID/Vendor/CDCのechoを行う。S1、S4、S5と同じ順で切り替え、`/dev/hidraw*`、`/dev/ttyACM*`、`/dev/serial/by-id/`の変化とkernel driverを記録する。

### Firmware / host tool

E013の`OepUsbDescriptorTest` library、`usb_profile_test.py`、`collect_windows.ps1`を拡張する。計画時点で分かっている必要な変更:

- profile C、Dと、`bcdDevice`・serial接尾辞のbuild-time指定
- **WebUSBを無効にしたまま`USB.usbVersion(0x0210)`でBOSを出し、MS OS 2.0 descriptor setを`USBVendor::onRequest`から返す。** Arduino-ESP32 3.3.11はCDCとWebUSBが同時有効だとdevice classを`0x02/0x02/0x00`へ強制するため、E013のProfile Bのままでは`0xEF/0x02/0x01`を保てない
- `collect_windows.ps1`に非表示devnode、`Enum\USB`キーの`Driver`/`Service`/`MatchingDeviceId`、`usbflags`キーの収集を追加
- `usb_profile_test.py`の`--expect-profile`をprofile C/Dへ拡張し、`bcdDevice`の一致は判定条件から外す(記録はする)

## 対象外

- macOS。PID申請前の正式検証で行う
- MS OS 1.0 descriptorと`usbflags\...\osvc`の挙動
- 複数台の同時接続。serialの効果はS3で単体確認する
- 正式なVID:PID、OEP protocol本体、throughput、長時間stress
- Windows 10、複数Linux distribution

## 必要な環境

E013と同じ。専用のESP32-S3 board 1枚、native USB用data cable、upload用UART/USB-Serial経路、Linux host、Windows 11実機またはUSB pass-throughを固定できる試験VM、USBView、Python 3.10以上と`uv`。

clean stateへ戻す手順が繰り返し必要になるため、**Windows 11はVM snapshotを復元できる環境が望ましい**。実機の場合はDevice Managerのuninstall手順を毎回記録する。

## ベンチ種別

**一時・専用機材**。E013と同じboard、cable、serial numberを維持する。S3だけserialを変える。

## 記録する値

| 項目 | 記録 |
|---|---|
| OS | edition、version、build、architecture |
| firmware | git revision、Arduino-ESP32 version、profile、`bcdDevice`、serial |
| clean state | 方法(uninstall / snapshot)、開始時のdevnode一覧が空であることの確認 |
| USB device | VID、PID、`bcdDevice`、device class、product、serial |
| interface | number、class/subclass/protocol、IAD、endpoint address/type/size |
| Windows | sequence、段階、present/非表示のdevnode一覧、instance ID、hardware ID、compatible ID、driver、service、`MatchingDeviceId`、COM port、`usbflags`キー |
| Linux | `lsusb -v`、sysfs path、kernel driver、device node、`by-id`名 |
| 通信 | HID/Vendor/CDCごとのopen、送信payload、受信payload、結果 |

## 完了条件

S1〜S7のraw記録が残り、(1)(2)(3)のそれぞれについてWindows 11が「再認識する / 既存devnodeを再利用して破綻する」のどちらかを言え、`bcdDevice`とserialの効果についてyes/noが言えること。Linuxで全profileのraw descriptorとechoが記録されていること。

いずれかのsequenceが機材の都合で実行できなくても、実行できた範囲で上記の表が埋まれば完了とし、残りは未決に書く。

## 影響

- [Probe protocol実現性ゲート](../../references/probe-feasibility-gates.ja.md) Gate 2の「分離手段の候補」から一つを選ぶ根拠。Gate 4の企画終了条件
- [基本コンセプト](../../references/probe-product-concept.ja.md)の「USB descriptor profile」節
- [PID取得ロードマップ](../../references/pid-acquisition-roadmap.ja.md) Step 3
- [harness-choices](../../references/harness-choices.ja.md) §2「CDCの個数は後から変えられるか」の表を実測で置き換える
- [USB descriptor変更に対するhostの挙動](../../references/usb-host-descriptor-persistence.ja.md) §1.5の「要実測」

## 実行手順

E013の[RUNBOOK.ja.md](../e013_usb_descriptor_profiles/RUNBOOK.ja.md)を基にする。機材、identity、software準備、build、書込み、Linux基準記録はそのまま使う。sequenceとclean state手順、拡張したprofileの分は、firmware / host tool実装時にこのdirectoryの`RUNBOOK.ja.md`へ書く(未作成)。

---

## 結果

未実行。

## 参考観測（2026-09-15、実験としては未実行）

EspUsbDevice側のDFU end-to-end試験で、第三P4（`303a:4021`、serial `e104-p4-windows-v1`）を同一identityのまま「vendor interface 1本（E112）→ DFU interface 1本・endpoint 0本 → vendor（E112）」と2往復layoutを入れ替えた。usbipd-winのbindは維持され、Windows側で「Not shared」に落ちることも、古い構成のcacheも観測されず、WSL側の`lsusb` / descriptorは毎回新しい構成を返した。ただし毎回attachをやり直し、観測はWSL側（usbip stub driver経由）なので、Windowsネイティブのdevnode / driver bindingの挙動を示すものではない。**usbip経由・同一identity・layout変更2往復では異常なし**という限定的な1データ点として残す。E062本体（Windowsネイティブでの列挙・driver binding）は未実行のまま。

## 追記（2026-09-16）: 機序が分かったので問いを絞れる

E062 の計画（上）は 2026-09-10 のもので、当時は「Windows がなぜ driver を当てないのか」自体が分かっていなかった。その後 [E081](../e081_p4_winusb_bind/README.ja.md) と EspUsbDevice 側 session の M1〜M4 で binding の機序が確定し、2026-09-16 に MS OS 2.0 の仕様書本文でも裏が取れた（[Windows が WinUSB を当てない](../../references/windows-winusb-binding.ja.md) §仕様で裏を取った）。**計画本体は書き換えない**が、いま分かっていることと、それでも残る問いを追記する。

### もう分かったこと（この実験で測らなくてよい）

- **単一 interface に function subset を出すと当たらない。** 仕様が "Function subsets are only used for composite devices, or single-function devices that use Usbccgp.sys as their client driver." と書いている。E013 / E062 が立った時点の Code 28 はこれが原因だった。
- **composite では function subset が指した interface にだけ当たる。** 指されなかった interface は Code 28（M2）。複数の function subset はそれぞれ独立に効く（M4）。
- **`bcdDevice` は device instance ID に入らない。** hardware ID の `REV_` と、MS OS **1.0** の `usbflags\<VID><PID><bcdDevice>` の `osvc` cache にだけ効く。MS OS 2.0（BOS 経由）は `osvc` を使わないので、**profile B-rev（`bcdDevice` のみ変更）は当初の意図では意味を失った**。残すなら「MS OS 1.0 の cache と混同していないことの確認」という位置づけになる。
- **usbccgp が載る条件は 3 つ**（`bDeviceClass` が 0 か 0xEF/0x02/0x01、interface 複数、configuration 1 つ）。profile B / C / D はいずれも満たす。

### まだ測っていないこと（これが E062 の残り）

1. **(1) 単機能 ↔ composite の切替で、既存 devnode が再利用されて古い binding が残るか。** 機序が分かっても、PnP が同じ instance ID に対して driver を選び直すかどうかは別問題で、Microsoft 文書に記述がない。**これが本題として残る。**
2. **(2) 末尾 function 追加で既存 child が壊れないか。**
3. **(3) interface 番号の機能入替。** いまは function subset が `bFirstInterface` で function を指すので、番号を入れ替えると subset の指す先も変わる。descriptor 側は正しくても、Windows が古い child を残すかどうかが問い。
4. **registry property の更新。** ライブラリは `MS_OS_20_FEATURE_VENDOR_REVISION` を出していない（[CR-14](../../references/espusbdevice-change-requests.ja.md)）。仕様は "You must always change this value if you are adding/modifying any registry property or other MSOS descriptors." と要求している。**同一 identity のまま `DeviceInterfaceGUIDs` を変えて、Windows 側の `Device Parameters` が更新されるか**を条件に足す価値がある。

### 機材の見直し

計画では ESP32-S3（`1209:0001`）を使うことになっている。いまは **P4 + EspUsbDevice 2.4.0（pin）** の方が実体に近い。ただし第三 P4（`esp32-p4-80f1b2d0b261`）は EspUsbDevice 側 session と共有していて、**serial を変える実験は usbipd の bind（管理者権限）をやり直す必要がある**ので、走らせる前に持ち主へ依頼と、相手 session への通知が要る。

**いまの firmware は serial を `e104-p4-windows-v1` に固定している**（E104〜E119 共通、「Windows の devnode / WinUSB binding を保つため」と sketch のコメントにある）。つまり製品 firmware の系列は、この実験が問うている条件を一度も通していない。

## 追記（2026-09-16 その2）: 残りはほぼ埋まった。未検証は interface 番号の機能入替だけ

EspUsbDevice 側 session が S3 直結の板（`303a:4080`、serial `guid-test-1`、先方 working tree 版＝未リリース）で、上の「まだ測っていないこと」の大半を実測した。**Windows の挙動自体はライブラリ版に依らないが、当方の板・当方の経路で取った数値ではない**ので、出典を明示して引く。

| E062 の問い | 結果 |
|---|---|
| (1) 単機能 ↔ composite の切替で古い binding が残るか | **残らない。** composite（親 usbccgp、`MI_01`=WINUSB）→ 単一 vendor interface を identity 据え置きで行い、**同じ instance で `SERVICE` が WINUSB に張り替わった**。逆向き（vendor のみ → vendor + HID）では親が usbccgp になり `&MI_00` / `&MI_01` が新設された |
| (2) 末尾 function 追加 | 上の逆向きが該当。既存 child は壊れず、新しい child が WINUSB で当たった |
| (3) interface 番号の機能入替 | **未検証のまま。** 親子とも instance ID が同じで compatible ID だけ変わる唯一のケース |
| registry property の更新 | **vendor revision でゲートされている。** revision 固定の対照では、device が新しい GUID を送っても Windows は古い値を保持した。cache は子 devnode にも効く。revision descriptor の初登場も「変化」として扱われる |
| `bcdDevice` のみ変更 | MS OS 2.0 には効かない（[CR-14](../../references/espusbdevice-change-requests.ja.md) の revision が担当）。MS OS 1.0 の `usbflags\<VID><PID><bcdDevice>` の `osvc` cache とは別物 |
| serial 変更 / PID 変更 | どちらも**新しい instance**になり全部読み直し |
| serial なし | **ポート由来のパス**で keying（`…\8&2EBC545B&0&4`）。当方の構成では常に serial を出すので踏まない |

**これで「serial を使い回すと前の build の判定が残る」という一般則は否定された。** 成功している instance は毎回 descriptor に追随する。当方が [E069](../e069_p4_hs_vendor_bulk_rate/README.ja.md) で観測した「`bcdDevice` を変えても composite 化しても再評価されない」は、**失敗が貼り付いた instance に限った話**として有効で、先方の結果と矛盾しない。成功と失敗で挙動が違う、が全体像。

**この実験に残っているのは (3) だけになった。** それも「同じ MI 番号のまま中身の function を差し替える」という限定条件で、製品としては「一度公開した profile の interface 番号と機能の対応は変更しない」（[Gate 3](../../references/probe-feasibility-gates.ja.md)）を守れば踏まない。走らせる優先度は下がった。

## 追記（2026-09-16 その3）: 全問に答えが出た。当方での実行は不要

残っていた (3) interface 番号の機能入替も EspUsbDevice 側 session が測った（S3、`303a:4080`、先方 working tree 版）。ライブラリが HID function を先頭に固定するため、`MI_00` の中身を HID から MSC に差し替える形。**interface の数は 2 のまま、子の instance ID も両方とも不変、vendor revision も固定。**

| 子（instance ID は不変） | 入替前 | 入替後 |
|---|---|---|
| `…&MI_00\9&37D27646&0&0000` | HidUsb | **WINUSB** |
| `…&MI_01\9&37D27646&0&0001` | WINUSB | **USBSTOR** |

**両方とも `CM_PROB_NONE`。同じ子 devnode のまま正しく張り替わった。** E062 の仮説「Windows 11 は (1) と (3) で既存 devnode を再利用して driver を選び直さない」は**反証された**。反証条件 1 に当たる（「devnode 再利用による破綻は起きない＝同一 PID 内の自由度は仮説より大きい」）。

ただし driver ではなく **registry property の側に残骸が残る**ことが分かった。規則は「無ければ書く／revision が動けば更新する／**消さない**」の 3 つで、USBSTOR になった `MI_01` に vendor 時代の `DeviceInterfaceGUIDs` が残った。**[Gate 3](../../references/probe-feasibility-gates.ja.md) の「一度公開した profile の interface 番号と機能の対応は変更しない」は、driver のためではなく GUID の残骸のために必要**、という形で根拠が付いた。変えるなら PID も変える。

**この実験の問いはすべて埋まった。当方の板での実行は不要**（走らせるなら P4 での再現確認のみで、優先度は低い）。ただし**すべて先方の板・先方の working tree 版での参考観測**である。Windows の挙動自体はライブラリ版に依らないはずだが、当方で取った数値ではない。

## 追記（2026-09-16 その4・完）: device scope の残骸も確定。製品の移行経路が決まった

先方が未使用 PID（`0x4084`）で、単一 vendor interface（GUID A、device scope）→ 同じ PID・同じ serial のまま vendor + HID composite（GUID B、function scope）を測った。GUID の値を変えてあるので、親に残るのが「古い方」だと区別できる。

```
親  USB\VID_303A&PID_4084\GUID-TEST-1        usbccgp   {A1A1…}   ← 残骸、device scope
子  USB\VID_303A&PID_4084&MI_01\9&…&0001     WINUSB    {B2B2…}   ← 有効、function scope
子  USB\VID_303A&PID_4084&MI_00\9&…&0000     HidUsb    （なし）
```

**「消さない」は function scope にも device scope にも当てはまる。** 古い GUID は usbccgp の親 node を指していて WinUSB の要求に応えられないので、その GUID を列挙した host アプリは開けないデバイスを 1 台見つける。

**製品への影響。** いまの probe は vendor 単独の非 composite で、将来 DFU を足すと composite になる。この経路では**親に残骸が出るので PID を変える**。[Gate 3](../../references/probe-feasibility-gates.ja.md) に規則として追加した。

回避策がもう 1 つある。**最初から composite にしておけば親に device scope の GUID が登録されない。** 単一 function のまま composite にするには `MS_OS_20_FEATURE_CCGP_DEVICE`（[CR-16](../../references/espusbdevice-change-requests.ja.md)）が要る。採用するなら **usbccgp を挟んだ bulk 帯域の再測（当方の仕事）** が前提になる。現在の 366 Mbps は非 composite での値である。

## 追記（2026-09-16 その5・訂正）: 「PID を変える」は撤回。残骸は不活性だった

**その4 の結論は誤りだった。撤回する。** 「親に残った GUID を host アプリが列挙して、開けないデバイスを見つける」と書いたが、**後半を検証していなかった。** レジストリに値があることと、device interface として列挙されることは別である。

先方がアプリと同じ方法（`SetupDiGetClassDevs` に `DIGCF_PRESENT | DIGCF_DEVICEINTERFACE`）で列挙した結果、両方の値が同時にレジストリに載っている状態で、

```
{A1A1…}（親に残った残骸）  → (none present)
{B2B2…}（子の生きた方）    → PRESENT \\?\usb#vid_303a&pid_4084&mi_00#9&649df21&0&0000#{b2b2…}
```

**device interface を作るのはレジストリの値ではなく、そのノードに bind された driver である。** usbccgp の親も USBSTOR の子も WinUSB の interface は作らないので、残骸は不活性でアプリの邪魔をしない。`DeviceClasses\{GUID}` のサブキーを見る方法では判別できない（`Linked` は生きている interface でも立っていない）。

**したがって「function を増やすときは PID を変える」という規則は根拠を失った。[Gate 3](../../references/probe-feasibility-gates.ja.md) から外した。** 子の残骸を根拠に「番号と機能の対応を変えない」へ足した機械的な理由も同じく撤回し、規則自体は当初の管理上の理由で残している。

持ち主の指摘（先方経由）: **「PID を変えろという結論は絶対にだめ。PID を変えればほぼ全部解決するのは当たり前で、変えられないから苦労している。」** 検証していない害を根拠に、一番コストの高い回避策を勧めていた。

**実害のある経路は1つだけ残る。** `DeviceInterfaceGUIDs` を変えたのに vendor revision が動かないと、**生きている側**が古い GUID に応答し続け、新しい GUID を探すアプリが何も見つけない。[CR-14](../../references/espusbdevice-change-requests.ja.md) の revision 自動導出が防いでいて、revision を手で固定したときだけ落ちる。

**CCGP は先方が実装し、3 点とも確認された**（単一 vendor interface ＋ `config.msOs20CcgpDevice`、既定オフ）: 親に usbccgp、子 `&MI_00` に WINUSB、**GUID は子だけで親に付かない**。用途は「幽霊を防ぐ」ではなく**トポロジを最初から固定する**こと。採用の判断材料は usbccgp 経由の帯域だけで、優先度は下げてよい。
