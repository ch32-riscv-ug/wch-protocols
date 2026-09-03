# PC 側 USB ドライバ層(host からデバイスを開く)

状態: **Windows は verified**(実機で両系統を確認)/ Linux・macOS は attested。層は L1/L2 の PC 側境界 — 「OS のドライバが USB device を握り、ユーザー空間のツールがどの API でそこへ届くか」。ここは**どのプロトコルを喋るかの手前**の問題で、[pc-to-link.ja.md](pc-to-link.ja.md)(probe)・[pc-to-device-isp.ja.md](pc-to-device-isp.ja.md)(ISP USB)・[dap.ja.md](dap.ja.md) の**全 USB 経路に共通**して効く。

まずここを通せないと 1 byte も送れない。特に **Windows は WCH-Link へのアクセス経路が 2 系統**あり、片方しか知らないと「列挙はできるのに開けない」で詰まる。

## 0. なぜドライバ層が問題になるか

USB device には「どの kernel ドライバがその interface を握るか」が OS ごとに決まる。ユーザー空間のツールは、握っているドライバが**汎用アクセスを許す種類**のときだけ read/write できる:

- **汎用 USB ドライバ**(WinUSB / libusb / usbfs)= ツールが直接 bulk 転送できる。`nusb`(Rust)・`libusb`(C)等はこれ前提。
- **ベンダ専用ドライバ**(WCH の CH375 系など)= 汎用 API では開けない。ベンダの DLL か、そのドライバの IOCTL を直接叩くしかない。

WCH-Link は composite device で、通常 2 interface を持つ:

| interface | 役割 | 握るドライバ |
|---|---|---|
| **MI_00**(vendor bulk) | WCH-Link コマンド(この repo の主対象) | Windows: **WinUSB か WCH 純正**(下記 2 系統)/ Linux/macOS: usbfs |
| MI_01(CDC-ACM) | 仮想 COM(UART bridge / print) | Windows: `usbser`=COM ポート / Linux: `/dev/ttyACM*` |

## 1. Linux

- MI_00 は特定ドライバに握られない。**usbfs(libusb/nusb)で直接開ける**。特別なドライバ導入は不要。
- 必要なのは**権限**だけ: 非 root で開くには udev ルール(VID `1a86` に `uaccess` / `plugdev` タグ、または `MODE="0666"`)。
- capture は **usbmon**(`/sys/kernel/debug/usb/usbmon/`)を Wireshark で。
- **WSL** は USB を直接見られないため **usbipd-win** で Windows から attach(`usbipd attach --wsl --busid <X>`)する。attach 中は Windows 側の CH375 経路は握れない(排他)。
- MI_01 は `cdc_acm` が `/dev/ttyACM*` を生やす。

## 2. macOS

- vendor interface は IOKit 経由で開ける。**nusb/libusb がそのまま動く**。専用ドライバ導入は不要。
- CDC は標準の CDC ドライバで `/dev/cu.usbmodem*`。

## 3. Windows — 2 系統(ここが要注意)

WCH-Link の MI_00 を握るドライバが 2 通りあり、**両者は排他**(片方にすると他方前提のツールが動かなくなる):

```
              MI_00 を握るドライバ
      ┌───────────────────────┴───────────────────────┐
  系統A WinUSB                              系統B WCH 純正(CH375 系)
  ├ Zadig で置換 / MS OS descriptor で自動bind   ├ WCH-LinkUtility 等の導入で当たる
  ├ nusb / libusb がそのまま開ける             ├ WinUSB/libusb は「incompatible driver」で開けない
  └ WCH-LinkUtility は使えなくなる              └ 純正ツールと共存できる
```

**どちらが当たっているか**は device マネージャや `pnputil /enum-interfaces` で確認できる。系統 B の実体:

- ドライバ: `WCHLinkW64.SYS`(class `WCH`、INF `wchlinkwdm.inf` / `oem*.inf`、表示名 `WCHLink_A64`)。CH375 汎用 USB ドライバ系。
- クリーンな Windows(WCH ドライバ未導入)では LinkE が **MS OS descriptor で WinUSB に自動 bind** されることがあり、その場合は系統 A で普通に動く。系統 B は「純正ドライバが先に入っている環境」向けの共存策。

### 3A. WinUSB 系統(Zadig / 自動 bind)

- Zadig で MI_00 を WinUSB(または libusbK)へ置換 → `nusb`/`libusb`/probe-rs がそのまま開ける。
- 欠点: **WCH-LinkUtility が使えなくなる**(純正ドライバを上書きするため)。ユーザーによっては受け入れ不可。

### 3B. WCH 純正ドライバ系統(Zadig 不要)

純正ドライバのまま喋る道は 2 つ。**ch32rv は B-2(IOCTL 直叩き)を採用**している(64bit のまま動くため)。

#### B-1. `WCHLinkDLL.dll` 経由(WCH 提供 API)

- `CH375*` stdcall API を持つ WCH の DLL。`CH375OpenDevice`/`CH375CloseDevice`/`CH375GetDeviceDescr`/`CH375ReadEndP(idx, ep, buf, len)`/`CH375WriteEndP(...)`/`CH375SetTimeoutEx` 等。EP 番号ベースなので EP 0x01/0x81/0x02/0x82 にほぼ 1:1。
- **制約: DLL は 32bit(x86 stdcall)のみ。64bit プロセスからロード不可**(実機確認: amd64 パッケージでも DLL は 32bit のまま、64bit 版は存在しない。64bit は kernel の `WCHLinkW64.SYS` だけ)。→ 使うには 32bit ビルドが要る。wlink(Rust)はこの経路を `cfg(target_arch="x86")` 限定で実装。

#### B-2. IOCTL 直叩き(`DeviceIoControl`、DLL 不要・64bit 可)★ch32rv 採用

DLL を介さず、純正ドライバの IOCTL を直接叩く。**arch 非依存で 64bit のまま動く**。汎用部品として [`ch32rv-usb-wch-win`](https://crates.io/crates/ch32rv-usb-wch-win) crate に切り出してある(WCH-Link 固有の知識を持たず、列挙・open・EP 指定 bulk 転送だけ)。

**device を開く**(SetupAPI または cfgmgr32):

- interface class GUID:
  - `{F8D5EDCA-B647-4E9C-9BD3-A5BD2328D55C}`(CH375 系がハードコード。**動作確認済みはこちら**)。
  - `{CDB3B5AD-293B-4663-AA36-1AAE46463776}`(`wchlinkwdm.inf` がレジストリ `DeviceInterfaceGUIDs` で登録する第 2 の GUID。補完用)。
- `SetupDiGetClassDevs(&GUID, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE)` → `SetupDiEnumDeviceInterfaces(index=0,1,...)`(複数 probe)→ `SetupDiGetDeviceInterfaceDetail`(DevicePath)→ `CreateFileW(DevicePath, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, ..., OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)` → `HANDLE`。
- cfgmgr32 の `CM_Get_Device_Interface_ListW` でも同等に列挙できる(コード量が少ない)。

**serial との対応付け**: interface path の instance ID には serial が含まれない(composite 親が持つ)。`CM_Locate_DevNodeW` → `CM_Get_Parent` → `CM_Get_Device_IDW` で親の `USB\VID_1A86&PID_8010\<SERIAL>` を引き、nusb 列挙(serial 基準)と突き合わせる。

**bulk 転送(`DeviceIoControl`)**:

- `IOCTL_CH375_COMMAND = 0x0022_3CDC`(= `CTL_CODE(FILE_DEVICE_UNKNOWN=0x22, 0x0f37, METHOD_BUFFERED, FILE_ANY_ACCESS)`)。
- 構造体(先頭 8B がヘッダ):
  ```c
  #define mCH375_PACKET_LENGTH 64
  typedef struct _WIN32_COMMAND {
      ULONG mFunction;                     // 方向 | pipe
      ULONG mLength;                       // データ長
      UCHAR mBuffer[mCH375_PACKET_LENGTH]; // 64
  } WIN32_COMMAND;
  ```
- `mFunction = (pipe_number - 1) | 方向`。方向 = **write `0x2_0000` / read `0x1_0000`**。方向 bit(EP の 0x80)は無視し、方向は write/read の呼び分けで決まる(`CH375WriteEndP`/`CH375ReadEndP` と同じ):

  | 論理 | EP | mFunction |
  |---|---|---|
  | cmd out | `0x01` | `0x2_0000` |
  | cmd in | `0x81` | `0x1_0000` |
  | data out | `0x02` | `0x2_0001` |
  | data in | `0x82` | `0x1_0001` |

- **write**: `mLength=len; memcpy(mBuffer,data,len)` → `DeviceIoControl(h, IOCTL, pCmd, len+8, pCmd, 64, &ret, NULL)`。`ret = 8 + 送信長`。
- **read**: `mLength=64` → `DeviceIoControl(h, IOCTL, pCmd, 8, pCmd, 64+8, &ret, NULL)`。`ret>8` なら応答 = `mBuffer[..mLength]`。
- **1 IOCTL は最大 64B**。WCH-Link の data EP は 1 論理転送が最大 4096B なので、**64B 単位に chunk 分割**する(crate 内で処理)。実機検証で V103=128B/V307=256B の data packet を 64B 分割して readback 一致を確認済み。

**実機検証**: WCH-Link(CH549 fw2.12)・WCH-LinkE(fw2.22)、Windows 11 x64、Zadig なし・DLL なし・管理者権限なし。GetProbeInfo 1 往復(`81 0d 01 01` → 例 `82 0d 04 02 16 12 00`)から、全 5 target の chip erase / flash / readback verify まで成立。

**特性**: 1 IOCTL=64B 駆動なので DMI 読みの往復回数がそのまま効き、**転送は遅い**(read ~2.3 KiB/s)。まず動くこと優先。

### 3C. 共存戦略(ランタイム選択)

系統 A/B のどちらが当たっているかは環境依存なので、**両対応**にしておくのが親切:

1. まず **nusb(WinUSB)で open を試す**。
2. 失敗し(「incompatible driver」等。文字列は安定 API でないので failure 全般で判定)、CH375 GUID で当該 serial の device が見つかれば **B-2(IOCTL)へフォールバック**。
3. どちらも駄目なら nusb の一次エラーを返す。

Linux/macOS は系統が 1 つなので常に usbfs(nusb)。

## 4. capture との関係

capture([../captures/README.ja.md](../captures/README.ja.md))はこのドライバ層の**上**で取る。ch32rv の `--capture` は backend(nusb / CH375)の外側で記録するので、どちらの系統でも同じ NDJSON が残る。純正ツールの観察は usbmon(Linux で usbipd 共有)や Windows の USB キャプチャで行う。

## 参照

- 系統 B 実装: [`ch32rv-usb-wch-win`](https://crates.io/crates/ch32rv-usb-wch-win)(IOCTL 直叩き、汎用)
- 系統 B-1 参照: wlink `src/usb_device.rs` の `ch375_driver`(`WCHLinkDLL.dll`、32bit)
- 系統 B-2 参照: minichlink fork(cw2/ch32v003fun `experimental/minichlink-wchlinkdll-driver`、C の IOCTL 直叩き)
- WinUSB 前提: probe-rs / wchisp(いずれも Windows では Zadig 必須と明記)
