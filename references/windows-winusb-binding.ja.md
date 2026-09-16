# Windows が WinUSB を当てない — 調査記録

状態: **解決**(2026-09-13。**原因は MS OS 2.0 descriptor set の subset 構造**だった。[E081](../experiments/e081_p4_winusb_bind/README.ja.md) で対照実験済み)

> **結論**: **単一 interface の device では compatible ID を set header の直下(flat)に置く。** configuration / function subset は composite device の function に紐付けるための入れ子で、**interface が 1 本だと compatible ID が結び付く先を失い、Windows は driver を当てない**。
>
> [E081](../experiments/e081_p4_winusb_bind/README.ja.md): 同じ board・同じ firmware で layout flag だけ変えた 2 本 — **flat(162 byte)は `Status=OK` / `Service=WinUSB` / `USB\MS_COMP_WINUSB` あり**、**subsets(178 byte)は `CM_PROB_FAILED_INSTALL`(Code 28)**。**この台の汚れた `ConfigFlags` は無関係**で、**新しい serial を使えば普通に当たる**。
>
> ライブラリ側([CR-1](espusbdevice-change-requests.ja.md))は **interface 数で自動判定**するようになった(1 本なら flat、2 本以上なら subsets)。
>
> 副産物: **usbip を外した native の帯域は 21.2 MB/s** で、usbip 経由と差がない。**これまでの測定の「usbip 込みなので下限」という但し書きは外せる。**
>
> **以下は解決前の調査記録として残す。**

## 仕様で裏を取った(2026-09-16)

[E081](../experiments/e081_p4_winusb_bind/README.ja.md) は実測だけで結論を出していた。**MS OS 2.0 の仕様書本文**([Microsoft OS 2.0 Descriptors Specification](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification) からたどる `MS_OS_2_0_desc.docx`)に同じことが明文で書いてある。実測と仕様は一致した。

| 仕様の記述 | 引用 | E081 / M1〜M4 との対応 |
|---|---|---|
| configuration subset は composite 専用 | "Configuration subsets are only used for composite devices that load USB Generic Parent Driver (Usbccgp.sys) as the parent driver for the entire device." | subsets が届かなかった理由 |
| function subset も composite 専用 | "Function subsets are only used for composite devices, or single-function devices that use Usbccgp.sys as their client driver." | **単一 interface に subsets を出すと結び付く先がない** |
| compatible ID の scope | "The compatible ID can be applied to the entire device or a specific function within a composite device." | flat = device scope なので当たる |
| function 番号の決まり方 | "Function numbers are determined by the first (lowest) bInterfaceNumber of the interface(s) assigned to the function." | M4 の `bFirstInterface` 昇順 2 本 |

usbccgp が載る条件も一次資料にある([Enumeration of USB composite devices](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/enumeration-of-the-composite-parent-device))。**3 つすべてを満たしたときだけ** `USB\COMPOSITE` が付き、`Usb.inf` 経由で usbccgp が載る。

- `bDeviceClass` が 0、または `bDeviceClass`/`bDeviceSubClass`/`bDeviceProtocol` が 0xEF/0x02/0x01
- interface が複数
- **configuration が 1 つ**

ライブラリの自動判定(`bNumInterfaces > 1` なら subsets)は、`bDeviceClass` が上の条件を満たしている限り正しい。満たさない組み合わせ(例: `bDeviceClass = 0xFF` で interface 2 本)では usbccgp が載らないので、subsets を出すと**両方の interface が Code 28** になる。

### 仕様にあって、ライブラリが出していないもの

`EspUsbDevice.cpp` の `buildWebUsbDescriptors()` が出すのは set header / configuration subset / function subset / compatible ID / registry property の 5 種類だけである。仕様にはあと 2 つある。

| descriptor | 値 | 仕様の要求 | 現状 |
|---|---|---|---|
| `MS_OS_20_FEATURE_VENDOR_REVISION` | 0x08 | "If this value changes between enumerations the registry property descriptors will be updated in registry during that enumeration. **You must always change this value if you are adding/modifying any registry property or other MSOS descriptors.**" | 2.4.0 は**出していない**。→ [CR-14](espusbdevice-change-requests.ja.md) で先方が実装し、**Windows 実機で確認済み（2026-09-16）**: identity 固定のまま `DeviceInterfaceGUIDs` を変えても、**revision を据え置いた対照では registry が更新されなかった**。revision を上げた変種はすべて更新された |
| `MS_OS_20_FEATURE_CCGP_DEVICE` | 0x07 | "the device should be treated as a composite device by Windows regardless of the number of interfaces, configuration, or class, subclass, and protocol codes" | 出していない。単一 function でも usbccgp を強制できる逃げ道 → [CR-16](espusbdevice-change-requests.ja.md) |

`DeviceInterfaceGUIDs` の値 `{975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}` は**ライブラリに直書きで、設定できない**(`EspUsbDevice.cpp` の registry property 生成)。EspUsbDevice で作った device はすべて同じ interface GUID を名乗る → [CR-15](espusbdevice-change-requests.ja.md)。

### MS OS 1.0 の cache とは別物

`HKLM\SYSTEM\CurrentControlSet\Control\usbflags\<vvvvpppprrrr>` の `osvc` は **MS OS 1.0**(string index 0xEE)の応答を憶える cache で、key は **VID + PID + bcdDevice** である([USB Device Registry Entries](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-device-specific-registry-settings))。いまの binding は BOS 経由の MS OS 2.0 だけなので `osvc` には依存しない。**`bcdDevice` を上げても 2.0 の側は何も変わらない。**


### driver binding と registry property は別の規則で動く

**2026-09-16 に決着した。** EspUsbDevice 側 session が S3 直結の板（`303a:4080`、serial `guid-test-1`、先方の working tree 版＝未リリース）で測った結果。**Windows の挙動そのものはライブラリ版に依らないが、出典は先方の環境である。**

決め手になった 1 本。親に usbccgp が当たっている composite（`MI_00`=HidUsb / `MI_01`=WINUSB）を、identity 据え置き・**vendor revision 固定**のまま単一 vendor interface へ戻した。

- `SERVICE` は `WINUSB` に張り替わった（**同じ instance で usbccgp から再バインドされた**）
- `DeviceInterfaceGUIDs` は device が新しい値を送っているのに**古いまま**だった

つまり **driver binding は毎回の列挙で descriptor に追随し、MS OS 2.0 の registry property だけが vendor revision でゲートされている。**

| 変更（特記なき限り revision 固定） | instance | 結果 |
|---|---|---|
| composite → 単一 vendor interface | 同じ | **usbccgp から WINUSB へ再バインド**。GUID は古いまま |
| vendor のみ → vendor + HID | 親は同じ、`&MI_00` / `&MI_01` が新設 | 親が usbccgp に、`MI_01` は WINUSB で GUID を新規読み込み |
| `MI_01` の子で GUID だけ変更 | 同じ子 | GUID 据え置き。**cache は子 devnode にも効く** |
| revision descriptor が初登場（2.4.0 → 修正版） | 同じ | 新しい GUID が入る。**初登場も「変化」として扱われる** |
| PID 変更 / serial 変更 | **新規** | 全部読み直し |
| serial なし | `…\8&2EBC545B&0&4` | serial ではなく**ポート由来のパス**で keying |

**これで「serial を使い回すと古い判定が残る」という一般則は否定された。** 成功している instance は毎回 descriptor に追随する。

**失敗した instance についても、当初の断定は取り下げる（2026-09-16）。** 先方が単一 interface に subsets を強制して意図的に Code 28（compatible ID から `MS_COMP_WINUSB` が消える）にした instance は、**次の接続で有効な set を送るとそのまま回復した**（`Device Updated: false` で 400/410、WinUSB 再バインド、GUID 再記録。同じ instance、同じ PID / serial）。

こちらの E069 の 2 観測（`bcdDevice` 変更も composite 化も再評価を起こさなかった）は、**「失敗が貼り付いて再評価されない」ことの証明にはなっていない。** どの試行でも descriptor は単一 interface 向けの subsets のままで、**Windows が bind できる compatible ID を一度も受け取っていない**。「貼り付き」を持ち出さなくても説明がつく。

現時点で書ける範囲はここまでである。**失敗した instance は回復しないことがある（条件は未特定。hardware ID を変えた後の 1 例を観測）。** 断定はしない。

**interface 番号の機能入替も測った（2026-09-16、先方）。** ライブラリが HID function を先頭に固定するため、`MI_00` の中身を HID から MSC に差し替える形で行った。interface の数は 2 のまま、子の instance ID も両方とも不変、revision も固定。

| 子（instance ID は不変） | 入替前 | 入替後 |
|---|---|---|
| `…&MI_00\9&37D27646&0&0000` | HidUsb | **WINUSB** |
| `…&MI_01\9&37D27646&0&0001` | WINUSB | **USBSTOR** |

**両方とも `CM_PROB_NONE` で、同じ子 devnode のまま正しく張り替わった。** 子であっても driver binding は毎回 descriptor に追随する。**これで binding 側の未検証はなくなった。**

### registry property の規則は 3 つに分かれる（残骸が残る）

同じ入替で、`DeviceInterfaceGUIDs` の側に別の問題が出た。

- **無いところには書く。** revision 据え置きでも書かれる（2.4.0 → revision 対応版で新しい GUID が入るのと同じ理屈）。上の `MI_00` が該当
- **既にあるものは revision が動いたときだけ更新する**
- **消しはしない。** 上の `MI_01` は USBSTOR になったのに、vendor だった頃の GUID を保持したままだった。device はもうその番号向けに GUID を送っていない

3 つめは一見落とし穴に見えるが、**実害はない。** 経緯を残す。

**2026-09-16、一度こう書いて撤回した**: 「その GUID を列挙した host アプリが、応答できない interface を見つけてしまう。だから function を増やすときは PID を変える」。**レジストリに値があることと、device interface として列挙されることを混同していた。** 先方が実際に `SetupDiGetClassDevs`（`DIGCF_PRESENT | DIGCF_DEVICEINTERFACE`）で列挙すると、両方の値が同時にレジストリに載っている状態で、

```
{A1A1…}（親に残った残骸）  → (none present)
{B2B2…}（子の生きた方）    → PRESENT \\?\usb#vid_303a&pid_4084&mi_00#9&649df21&0&0000#{b2b2…}
```

**残骸は何も返さない。** device interface を作るのはレジストリの値ではなく、**そのノードに bind された driver** である。usbccgp の親も USBSTOR の子も WinUSB の interface は作らない。`DeviceClasses\{GUID}` のサブキーを見る方法では生死を判別できない（`Linked` は生きている interface でも立っていない）。**列挙そのものをやるまで答えは出なかった。**

device scope の残骸は実在する（未使用 PID `0x4084`、GUID の値を変えて識別。単一 vendor interface → 同 PID・同 serial で vendor + HID composite）。

```
親  USB\VID_303A&PID_4084\GUID-TEST-1        usbccgp   {A1A1…}   ← 残骸。ただし不活性
子  USB\VID_303A&PID_4084&MI_01\9&…&0001     WINUSB    {B2B2…}   ← 有効
```

**残るのは値だけで、列挙には出てこない。** 子側の残骸（function を替えた `MI_nn` に前の GUID が残る）も同じ機構なので不活性である（こちらは列挙まで確認していないが、interface を作るのは driver だという機構は同じ）。

### 子の path と設定は interface 番号に紐づく（2026-09-16、先方実測）

composite child の instance と、それにぶら下がる設定（CDC なら COM 番号、vendor なら device interface の path）は **interface 番号**に紐づく。

- **既存 function の番号を動かさず末尾に足す** → host から見た path も registry property も**そのまま**
- **既存 function を後ろへずらす** → path が変わる。**GUID で列挙する host アプリは追随し、path を保存している host アプリは壊れる**

こちらの host tool は libusb の VID/PID 直指定なので後者は踏まないが、**製品の profile 規則としては「vendor を interface 0 に固定し、DFU は末尾に足す」**が正しい。[Gate 3](probe-feasibility-gates.ja.md) の「番号と機能の対応を変えない」はこの意味でも効く。

### 本当の危険は生きている側の interface

**`DeviceInterfaceGUIDs` を変えたのに vendor revision が動かない場合**、列挙される側が古い GUID に応答し続け、新しい GUID を探す host アプリは何も見つけない。これが実害のある唯一の経路である。[CR-14](espusbdevice-change-requests.ja.md) の revision 自動導出が防いでいて、**revision を手で固定したときだけ落ちる**。

### CCGP descriptor（実測済み）

単一 vendor interface ＋ `config.msOs20CcgpDevice`（既定オフ、先方が実装）で、親に usbccgp が載り、子 `&MI_00` に WINUSB が当たり、**GUID は子だけに付いて親に付かない**。

用途は「幽霊デバイスを防ぐ」ではなく（防ぐべき幽霊はいなかった）、**トポロジを最初から固定して、後から function を足しても GUID の登録先が動かないようにする**ことである。既に device scope の値が入った PID に後から CCGP を足しても、親の残骸は消えない（消せないが実害もない）。採用するなら **usbccgp を挟んだ bulk 帯域の再測**が要る（現在の 366 Mbps は非 composite での値）。優先度は「トポロジを安定させたいか」で決める。

### 観測のしかた（2026-09-16、先方が対照つきで確認）

**`setupapi.dev.log` に install の節が無いことは症状ではない。** この Windows 11（25H2 build 26200.9457）では、**MS OS 2.0 の compatible ID 経由の WinUSB バインドは成功でも失敗でも節を作らない。**

先方が今日の全試験デバイス（`PID_408x`、`PID_4090`、`VID_1209&PID_0001`）について、ローテート済みログと現行ログの両方を grep した結果。

| 試行 | `setupapi.dev.log` の節 |
|---|---|
| WinUSB がバインドした（数十回、新規 PID 5 つ、VID 変更 1 回を含む） | **なし** |
| 意図的に FAILED_INSTALL にした（subset layout 強制） | **なし** |
| その instance が回復した（WinUSB 再バインド） | **なし** |
| usbser（Ports クラス）の install | **あり**（3 回。Kernel-PnP の id=430「追加インストール要」に対応、Exit status SUCCESS） |

**節が出るのはクラスドライバ（usbser / HidUsb / USBSTOR など）の install が走ったときだけ**である。WinUSB の成否と経過は次で読む。

- **Kernel-PnP / Configuration** の 400 / 410 / 411 / 430 と `Device Updated`
- `DEVPKEY_Device_InstallState` と `ProblemCode`（`pnputil /enum-devices … /ids`、`Get-PnpDevice`）

これで [E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) の「install の節が出ない」という観測は**意味を失った**。descriptor が無効だったこととも無関係で、WinUSB 経路の通常の見え方だった。

## どこまで自由に変えてよいか

| 変更するもの | Windows の認識への影響 | 手当て |
|---|---|---|
| endpoint 数・size、転送方式、bulk に流す中身(E108〜E119 の最適化すべて) | **なし**。binding は compatible ID だけで決まる | 不要 |
| capture profile descriptor(E114 以降の「動的 descriptor」) | **なし**。USB descriptor ではなく vendor bulk の payload | 不要 |
| `iProduct` / `iManufacturer` | 表示名だけ。USBDevice class は `iProduct` を Device Manager の表示に使う | 不要 |
| `iSerialNumber` | **新しい device instance になる**。binding をやり直す | 失敗からの復旧手段として意図的に使う |
| `bcdDevice` | instance ID は変わらない。hardware ID の `REV_` と MS OS **1.0** の cache key だけ | MS OS 2.0 には効かない |
| `DeviceInterfaceGUIDs` / registry property | 2.4.0 では vendor revision がないので**更新されない**（先方が対照実験で確認。cache は子 devnode にも効く）。revision を上げれば identity 固定のまま更新される | CR-14 実装済み。2.4.0 のままなら serial か PID を変える |
| interface を 1 本から 2 本へ(DFU 追加など) | **flat → subsets に切り替わる**。usbccgp の 3 条件を満たせば **driver は同じ instance で張り替わる**（先方実測） | 条件を満たすか確認する。CR-16 |
| interface 番号の機能入替 | **driver は同じ子 devnode のまま張り替わる**（先方実測）。前の function の GUID が値として残るが不活性 | 管理上は [Gate 3](probe-feasibility-gates.ja.md) の規則どおり番号と機能の対応を固定する |
| VID / PID | 新しい instance。usbipd は管理者権限で再 bind | 承知の上で |

**いまの firmware は serial を `e104-p4-windows-v1` に固定している**(E104〜E119 共通、sketch のコメントに「Windows の devnode / WinUSB binding を保つため」と書いてある)。つまり E104 以降の全実験は**同じ Windows instance を使い回していて、「生きている instance で descriptor を変える」条件は一度も通していない**。ここが未検証なのは意図的な設計であって、測って安全と分かったからではない。

## 解決前の調査記録

[E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) で ESP32-P4 の vendor bulk endpoint を Windows 11 から driverless に使おうとして詰まった件の記録。**device 側は正しいと確定している**ので、Windows 側の話としてここに分ける。

## 症状

VID:PID `1209:0008` の vendor-specific interface 1 本の device を挿すと、

```
status  = Error
problem = 28 (CM_PROB_FAILED_INSTALL)
service = (なし)
compatible IDs に USB\MS_COMP_WINUSB が付かない
```

## 確定していること

### device 側は正しい

| 確認 | 結果 |
|---|---|
| `bcdUSB` | **0x0210**(BOS を読む条件を満たす) |
| BOS descriptor | **57 byte / 2 capability**。UsbTreeView の dump で確認 |
| MS OS 2.0 platform capability | UUID `D8DD60DF-4589-4CC7-9CD2-659D9E648A9F`、`dwWindowsVersion=0x06030000`、`wTotalLength=0x00B2`、`bMS_VendorCode=0x02`、`bAltEnumCode=0x00` — **すべて正しい** |
| **MS OS 2.0 descriptor set 本体** | Linux から `bmRequestType=0xC0, bRequest=0x02, wValue=0, wIndex=7` を投げると **178 byte 返る。`WINUSB` の compatible ID を含む** |
| 同上(stack を変えて) | Arduino-ESP32 core 内蔵 stack と `EspUsbDevice` 2.2.0 で**同じ 178 byte** |

### Windows 側に一般的な阻害要因は無い

| 確認 | 結果 |
|---|---|
| `HKLM\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions` | **キー自体が存在しない**(制限ポリシー無し) |
| `DriverSearching\SearchOrderConfig` | `1`(Windows Update も検索する) |
| driver store の WinUSB | **ある**。`winusb_generic_device.inf`(libwdi、`USB\MS_COMP_WINUSB` 用)、他に SEGGER / Raspberry Pi の WinUSB INF も |

**`MS_COMP_WINUSB` さえ付けば当たる driver は揃っている。** 付かないことが唯一の原因である。

### 失敗は device instance に焼き付く

```
DEVPKEY_Device_InstallState = 2   (FailedInstall)
DEVPKEY_Device_ConfigFlags  = 64  = CONFIGFLAG_FAILEDINSTALL
```

これが立つと **以後その instance では driver 判定をやり直さない**。descriptor を直しても、`bcdDevice` を変えても効かない理由がこれ。

**instance ID を変える条件**(実測):

| 変えたもの | device instance |
|---|---|
| `bcdDevice`(0x0100 → 0x0200 → 0x0201) | **変わらない**(`USB\VID_1209&PID_0008\0` のまま) |
| **serial string** | **新しい instance になる**(`...\E069-A`) |

→ [E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md) の仮説「`bcdDevice` は identity に効かない / serial は効く」が片側ずつ裏付いた。

### 効かなかった対処

| 試したこと | 結果 |
|---|---|
| `bDeviceClass` を `0xEF/0x02/0x01` → `0x00` | 変わらず |
| `bcdDevice` を変える | 変わらず(instance が同じなので当然) |
| **serial を変えて新 instance にする** | **変わらず**(新 instance でも即 Code 28) |
| **`pnputil /remove-device` で devnode を消して再列挙** | **変わらず**(新しい `FirstInstallDate` が付いた上で再び Code 28) |
| **composite 化**(vendor + CDC、vendor が interface 0) | **変わらず**。`usbccgp` すら載らない |
| USB stack を `EspUsbDevice` に替える | 変わらず |

**`setupapi.dev.log` には、これらの再列挙に対する install の節が 1 つも書かれない。** 削除(`Delete Device`)は記録されるので、ログ自体は生きている。つまり **Windows は driver 検索を走らせずに Code 28 を付けている**。 **（2026-09-16 訂正: この推論は誤り。MS OS 2.0 の compatible ID 経由の WinUSB バインドは、成功でも失敗でも `setupapi.dev.log` に節を作らない。下の「観測のしかた」を参照）**

### composite での実測(EspUsbDevice 側 session、2026-09-15、参考観測)

E081 で対象外にしていた「composite device での MS OS 2.0」を、EspUsbDevice のライブラリ改修に伴って向こうの session が同じ P4(esp32-p4-80f1b2d0b261、VID:PID `303a:4090`、serial `os20-m1`〜`m4`、Windows native、Zadig なし)で測った。同じ composite・同じ interface 構成で、変えたのは MS OS 2.0 の中身だけ。

| 構成 | MS OS 2.0 | 親(usbccgp) | `MI_00`(DFU) | `MI_01`(vendor) |
|---|---|---|---|---|
| M1: DFU 1 本のみ、set なし(旧ライブラリ) | なし | — | **Error**、compatible ID は class 由来のみ | — |
| M2: DFU + vendor、function subset 1 つ(vendor だけ指す) | 178 byte subsets | OK / `usbccgp` | **Error / problem=28** | OK / `WINUSB` |
| M3: DFU 1 本のみ、flat | 30 byte flat、BOS 33 byte、`bcdUSB=0x0201` | — | OK / `WINUSB` | — |
| M4: DFU + vendor、function subset 2 つ(interface 0 → 1 の昇順) | 206 byte subsets | OK / `usbccgp` | **OK / `WINUSB`** | OK / `WINUSB` |

分かったこと。
- composite で `usbccgp` が載る構成では、**function subset が指した interface にだけ WinUSB が当たり、指されなかった interface は Code 28** になる。上の「composite 化しても変わらず、usbccgp すら載らない」は単一 interface 向け subsets のまま試したときの別現象。
- function subset は複数出せて、それぞれ独立に効く。
- **`DeviceInterfaceGUIDs` は binding に不要**。DFU 側は compatible ID(function subset 28 byte)だけで当たった。GUID は SetupDi 列挙用の付加情報で、function ごとに変えるかは binding とは別の問題。
- `bcdUSB=0x0201` のままで BOS は読まれる(0x0210 は不要)。Microsoft capability 単独の 33 byte BOS でも読まれる。

## 残っている仮説

### H1: MS OS 2.0 descriptor set の入れ子構造(本命)

両 stack が返す 178 byte は次の形で、TinyUSB の WebUSB サンプル由来と思われる。

```
Set header (0x0A)
  Configuration subset header (0x08)
    Function subset header (0x08, bFirstInterface = 0)
      Compatible ID feature (0x14, "WINUSB")
      Registry property feature (0x84, DeviceInterfaceGUIDs)
```

**Configuration / Function subset は composite device の function に compatible ID を結び付けるための入れ子**である。単一 interface の非 composite device では **Set header の直下に compatible ID を置く**のが素直で、現在の構造では Windows が device に結び付けられていない可能性がある。

**composite 化しても直らなかった**ことは H1 に不利に見えるが、その試験では `usbccgp` 自体が載らなかったので、**composite として扱われる前段で落ちている**可能性が残る(= H1 の検証になっていない)。

### H2: Windows が vendor request を投げていない

BOS を読んでも `bRequest=0x02, wIndex=7` を投げていないなら、descriptor の中身は無関係になる。

### H1 と H2 を分ける手段

1. **device 側で全 control request を観測する。** `EspUsbDevice` に観測用 hook を足してもらう([改修依頼 CR-2](espusbdevice-change-requests.ja.md))。**これが一番確実で安全**
2. **USBPcap で bus を取る。** この PC に導入済み。ただし**管理者権限が要る**うえ、HS のフルレートでは取りこぼす(列挙は低レートなので実用にはなる)
3. **MS OS 2.0 descriptor set を subset 無しで出す。** [改修依頼 CR-1](espusbdevice-change-requests.ja.md)

## 当面どうするか

| 手 | 権限 | 状態 |
|---|---|---|
| **usbip で WSL へ引き込み、libusb で叩く** | bind に管理者 1 回 | **採用中**。[E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) / [E070](../experiments/e070_p4_hs_vendor_stack_compare/README.ja.md) はこれで測れている。ただし usbip の overhead が乗る(値は下限になる) |
| Zadig で WinUSB を手動割当 | 管理者 | 未実施。**確実だが「driverless」ではなくなる**ので、製品として配る形の検証にはならない |
| CDC に載せる | 不要 | driverless だが**帯域が出ない**([E066](../experiments/e066_p4_usb_hs_tx_context/README.ja.md) 8.08 対 [E069](../experiments/e069_p4_hs_vendor_bulk_rate/README.ja.md) 9.73 MB/s)し、**転送途中の packet 欠落**がある([E068](../experiments/e068_p4_hs_cdc_tail_loss/README.ja.md)、30 転送中 4 件) |

## usbipd を使ううえでの注意(実測)

- `usbipd bind` は **VID:PID と device instance に紐づく**。**PID や serial を変えるたびに管理者権限の bind が要る**
- したがって **usbip で測る実験は USB identity を固定する**
- `attach` は bind 済みなら管理者不要

## 影響する doc

- [probe-feasibility-gates](probe-feasibility-gates.ja.md) Gate 2 — 「driverless で vendor bulk」が Windows で成立するかは**まだ言えない**
- [harness-channels](harness-channels.ja.md) §6c — 「帯域が要るときは Vendor 側へ逃がす」は**帯域としては正しい**が、**Windows の driver 当ての問題が別に残る**
- [E062](../experiments/e062_usb_same_identity_layout_change/README.ja.md) — instance ID の決まり方について実測が付いた
