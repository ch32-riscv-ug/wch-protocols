# EspUsbDevice と EspUsbHost、どちらから先に改修するか

状態: **決着**(2026-09-13。**device 側は CR-1〜CR-9 が全件対応済み**。host 側([EspUsbHost への依頼](espusbhost-change-requests.ja.md))は**まだ依頼していない**)

## 結果 — 順序の判断は当たり、理由の一つは外れた

**device 側から、で正しかった。** CR-1〜CR-9 はすべて device 側だけで完結し、**HS 同士の結線に戻す必要は最後まで出なかった**([結果](espusbdevice-change-requests.ja.md))。

**ただし §3 の見立ては半分外れた。** 「PC 側の URB を複数 in-flight にすれば天井が分かる」は正しく、実際に測られた(depth 1 = 18.64 / 2 = 22.68 / 4 = 22.69 / 8 = 22.87 MB/s)。**しかし天井を押し上げたのは in-flight 数ではなく 1 転送あたりの packet 数**だった。**depth は 2 で飽和し、約 23 MB/s は device 側の天井**である。

→ **[CR-7](espusbdevice-change-requests.ja.md)(device 側 in-flight 2 本)は不要**と結論された。**[E079](../experiments/e079_p4_host_urb_depth/README.ja.md) はこの測定に置き換わる。**

### 残っているもの

| | 状態 |
|---|---|
| **device 側の release** | **待ち**。新規バグの修正 → `--clean` フルテスト → 問題なければ release。**こちらはその版でピンし直してから追試する** |
| [E078](../experiments/e078_p4_continuous_stream/README.ja.md)(capture と同時に降ろす) | **release 待ち**。唯一こちらで追試する項目(先方の測定と重複しない) |
| [HR-3](espusbhost-change-requests.ja.md)(1,024 B periodic IN) | **未依頼**。HID を 1,024 B にする段で device 側と対で要る。512 B は device 側だけで通った |
| [HR-1](espusbhost-change-requests.ja.md) / [HR-2](espusbhost-change-requests.ja.md) | **未依頼**。測定の動機は消えた(PC 側で天井が出た)。**P4 を host として使う段になってから** |

---

## 以下は当初の提案(記録として残す)

**device 側(EspUsbDevice)から。** 順序は

**CR-4 → CR-3 → CR-2 → CR-1 → CR-8 → CR-9 → (PC 側 async 測定) → CR-7 → HR-1 → HR-3**

理由は3つある。

### 1. device 側は**今のベンチのまま**確認できる。host 側は配線を戻すところから始まる

device 側の改修は **board 1 枚 + PC** で確かめられる。いま繋がっている形そのままである。

host 側(EspUsbHost)の確認は **board 2 枚の OTG HS を直結**する必要があり、**その状態では PC からどちらの端も覗けない**。device が何を送ったか、host が何を受けたかを、**それぞれの console(USB-Serial-JTAG)越しの printf でしか見られない**。[E072](../experiments/e072_p4_hs_device_to_host_native/README.ja.md) / [E073](../experiments/e073_p4_hs_hid_throughput/README.ja.md) はこの形で測ったが、**同じ配線で往復するたびに 2 枚とも焼き直す**ことになり、回転が遅い。

**改修と検証を同時に回すなら、観測点が多い方から始めるのが速い。**

### 2. 近い目標の経路は**すべて device → PC** である

[近い目標](p4-logic-analyzer-investigation.ja.md)は logic analyzer / packet capture / RVSWD で、**どれも P4 が device、PC が host** である([E077](../experiments/e077_p4_pulseview_over_ip/README.ja.md) の PulseView も PC 側)。

**EspUsbHost が要るのは「P4 を host として使うとき」だけ**で、いまのところ**測定の道具としてしか使っていない**。

### 3. HR-1 の当初の動機は **device 側を触らずに解消できる**

HR-1(bulk IN の async queue)を立てた理由は「**device 側の天井が測れない**」だった。P4 host は 512 B × depth 1 でしか読めず 5.6 MB/s で止まる、というのが根拠である。

**だがこれは PC 側でも確かめられる。** いまの host 側 script は `libusb` の**同期 API** で、**URB を 1 本ずつしか投げていない**。`libusb` の async API で **2〜8 本 in-flight** にすれば、**device 側もライブラリも一切変えずに**「[E076](../experiments/e076_p4_capture_hs_download/README.ja.md) の 8.80 MB/s(6.6〜10.9)が device の天井なのか、host の投げ方なのか」が分かる。

- **伸びれば** → 天井は host の投げ方。**[CR-7](espusbdevice-change-requests.ja.md)(device 側の in-flight 2 本)の見立てが裏付けられる**うえ、**PC 側は script を直すだけで速くなる**
- **伸びなければ** → 天井は device 側。**CR-4(FIFO)+ CR-7 の優先度が上がる**

**どちらに転んでも、改修に着手する前に知っておきたい値である。** 所要は script 1 本(`python-libusb1` は導入済み)。

> **この測定は board 1 の console が復帰し次第すぐ走らせる**([E079](../experiments/README.ja.md) として採番予定)。

## 順序の根拠(項目ごと)

| 順 | 項目 | なぜここか |
|---:|---|---|
| 1 | **CR-4**(FIFO 深さ) | **規模が小さく、効きが実測済み**(8.80 → 10.59 MB/s)。`#ifndef` ガードを足すだけで、**以降のすべての測定の土台が上がる** |
| 2 | **CR-3**(per-speed endpointSize) | **1 行**。spec 違反を消しておくと、以降 descriptor を疑わなくて済む |
| 3 | **CR-2**(control request の観測 hook) | **CR-1 の切り分けに要る**。これ無しに CR-1 を触ると当て推量になる |
| 4 | **CR-1**(MS OS 2.0 の構造) | CR-2 の結果で**どちらを直すかが決まってから**。解ければ **Windows で driverless** になり、usbip 越しでない実測も初めて取れる |
| 5 | **CR-8**(HID 64 B 固定) | **小さく、8 倍効く**。512 B までなら **PC を host にして確認できる**ので host 側改修を待たない |
| 6 | **CR-9**(FIFO 空き待ち API) | streaming([E078](../experiments/e078_p4_continuous_stream/README.ja.md))で効く。CR-7 が入れば軽くなるが消えない |
| 7 | **PC 側 async 測定** | **CR-7 に着手する前の切り分け**(上記) |
| 8 | **CR-7**(in-flight 2 本) | **規模が大きい**(TinyUSB の class driver)。7 の結果を見てから |
| 9 | **HR-1**(bulk IN の async queue) | **P4 を host として使う段**になってから。配線を戻す必要がある |
| 10 | **HR-3**(1,024 B periodic IN) | **CR-8 と対でないと確かめられない**。CR-8 が先 |

## 例外 — host を先にしたくなる条件

次のどれかなら順序を入れ替える価値がある。

- **P4 を host として使う予定が近い**(USB device を P4 に挿して読む用途)。そのときは HR-1 が本番機能になる
- **PC 側 async 測定で帯域が伸びなかった**場合。device の天井が近いと分かるので、**HR-1 を入れて P4 同士で測る**方が、PC 経由(usbip 込み)より素直な数字が出る
- **CR-1 が解けない**場合。Windows で driverless にならないなら「PC を host にする」前提自体が弱くなる

## 一緒に入れると楽な組

- **CR-4 + CR-3**: どちらも小さく、同じ vendor 周りを触る
- **CR-2 + CR-1**: 観測と修正が対
- **CR-8 + HR-3**: HID の 1,024 B は device と host の両方が要る。**512 B までは CR-8 だけで完結する**ので、**先に 512 B を通してから 1,024 B を両側で**

## 参照

- [EspUsbDevice への改修依頼](espusbdevice-change-requests.ja.md)
- [EspUsbHost への改修依頼](espusbhost-change-requests.ja.md)
- [P4 USB HS まとめ](p4-usb-hs-summary.ja.md) — 実測値の出どころ
- [Windows で WinUSB が当たらない](windows-winusb-binding.ja.md) — CR-1 / CR-2 の背景
