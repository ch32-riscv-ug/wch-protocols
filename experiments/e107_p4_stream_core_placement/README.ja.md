# E107 直結USBとの結合上限を決めるcore配置・優先度・copy経路

状態: **完了 — USB割り込みをcore 0へ移しcodec loopを直すと、PC直結（Windows native）で8-bit 60 Msps 5回 / 16-bit wide 40 Msps 3回PASS。残る律速はcodec（core 1）とUSB帰路そのもの**（2026-09-15）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E106](../e106_p4_mixed_rate_capture_stream/README.ja.md)、[E067](../e067_p4_usb_vs_capture_core/README.ja.md)、[E061](../e061_p4_drain_core_split/README.ja.md)、[E102](../e102_p4_vendor_in_zero_copy_precomputed/README.ja.md)

## 問い

PC直結USBとの結合上限（8-bit 44 / 16-bit wide 32 Msps）を決めているのは、どのcoreのどの処理か。USB割り込みとusbd taskのcore、task優先度、PSRAM FIFO経由のcopy、DWC2のDMA modeを変えると、内部sink上限（8-bit 60 / 16-bit wide 40 Msps）まで連続転送が戻るか。

## 仮説

E106は「速くなったUSB taskがcore 0のRX callback / spoolと競合した」と書いたが、これは推定で、core別の実行時間は測っていない。コードを読み直すと別の候補が3つある。

1. **USB割り込みはcodecと同じcore 1にある。** TinyUSBのDWC2 port（`dwc2_esp32.h`）は`esp_intr_alloc(..., ESP_INTR_FLAG_LOWMED, ...)`を呼んだcoreへ割り込みを固定する。呼び出し元は`Device.begin()`→`tusb_init()`で、Arduinoの`setup()`はcore 1（`CONFIG_ARDUINO_RUNNING_CORE=1`）で走る。しかもEspUsbDevice 2.3.0のDWC2はslave mode（`CFG_TUD_DWC2_DMA_ENABLE=0`）で、bulk INのpayloadはISRがCPU storeでTX FIFOへ押し込む。wire 17〜23 MB/s分のstoreとその割り込みが、core 1のcodecを直接中断する。
2. **usbd task（`espusb-device`、priority 24、core非固定）は割り込みが起きたcoreで起床する。** 8 KiBごとに`tx_ff`→endpoint bufferのmemcpyとre-armを行うので、これもcore 1に乗る。
3. **core 0ではspool（Stage→PSRAM memcpy）とusbTask（PSRAM→`tx_ff` memcpy）が同じpriority 5で時分割される。** spoolが遅れるとFreeStageQueueが空になり、core 1のcodecがblockしてPARLIO ringを追い越す。

予測: USB割り込みをcore 0へ移すだけで8-bitの結合上限は44 Mspsから大きく上がる。ただしcore 0側にはPSRAM往復2回、`tx_ff` copy、ISRのFIFO pushが集まるので、60 Msps（wire 23.4 MB/s）ではcore 0が飽和する可能性がある。その場合は内部RAM FIFOとDMA modeで段階的に削る。

## 反証条件

- USB割り込みをcore 0へ移してもPASS境界が8-bit 44 / wide 32のまま → 律速はUSB ISRではない。run-time statsのcore別idleで再判定する。
- core 1のidleが十分残るのにringを追い越す → 律速はcore 0の処理か、queue / stageの段数。
- 内部FIFOで`fifo_overflow`が出る → 経路jitterに対し256 KiBでは足りず、PSRAM FIFOは必要。

## 方法

E106のfirmwareを複製し、firmwareを書き換えずに条件を切り替えられるようにする。commandは16 byteにし、末尾に`flags`を足す。

| 切替 | 内容 | 基準値 |
|---|---|---|
| `usb_core` | `Device.begin()`をcore 0 / 1どちらに固定したtaskから呼ぶか。RTC memoryへ保存して`esp_restart()`する（command `EC`） | 1（Arduino `setup()`と同じ） |
| flag `0x01` | spool priority 6、usbTask 4 | 両方5 |
| flag `0x02` | 8 MiB PSRAM FIFOの代わりに256 KiB内部RAM FIFO。prebufferはFIFOの1/4 | PSRAM、prebuffer 1 MiB |
| flag `0x04` | 制御taskをcore 1へ置き、PARLIO RX割り込みをcore 1へ | core 0 |
| flag `0x08` | capture中だけusbd taskのpriorityを4へ下げる | 24 |
| build | `CFG_TUD_DWC2_DMA_ENABLE=1`（`build_opt.h`） | slave mode |

計測はFreeRTOSのrun-time stats（`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y`）を使う。capture窓の前後でIDLE0 / IDLE1 / `espusb-device`の累積実行時間を取り、各taskは終了時に自分の累積実行時間を記録して、statusに出す。ISRの時間は中断されたtaskに計上されるので、core別idleの減少がそのcoreの真の負荷になる。PARLIO callbackが走ったcoreも記録する。

掃引: 8-bit 3 full＋5 D64を44 / 48 / 52 / 56 / 60 Msps、16-bit wide（3 full＋1 D8＋12 D64）を32 / 36 / 40 Msps。試験量はE106と同じ8-bit 65.536 MB、wide 69.468 MB。PASS境界は3回繰り返す。判定はE106と同じで、raw連番、複製lane、PARLIO queue overflow、FIFO overflow、PC側sequenceがすべて0で全byte受信。

## 対象外

任意channel descriptor、host側gateway、16本独立GPIO、zero-copy TX APIの設計、hardware TX FIFOの増量（[E090](../e090_p4_dwc2_double_buffer/README.ja.md)で反証済み）。

## 必要な環境 / ベンチ種別

第三P4（MAC `80:f1:b2:d0:b2:61`、UART `/dev/ttyUSB0`、HSはPC直結でWindows bus `1-7`をusbipdでWSLへattach）。一時。E106と同じ内部loopback配線。

## 記録する数値 / 完了条件

variantごとのPASS境界rateと、その時のcore別busy率、usbd / usbTask / spool / codecの実行時間。8-bit 60 / 16-bit wide 40 MspsでPASS 3回が出れば、ロードマップの表向き値を直結の保証値へ動かせる。届かなければ、残る律速がどの処理かを数値で残す。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.2 / §1.3 / §3 Phase D、[E106](../e106_p4_mixed_rate_capture_stream/README.ja.md)の結合上限、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)の通常上限。

## 再現

```sh
arduino-cli compile --profile esp32p4_device
arduino-cli upload --profile esp32p4_device --port /dev/ttyUSB0
# 再列挙後（WSLから読む場合）: usbipd.exe attach --wsl --busid 1-7
# /dev/bus/usb がroot専用でsudoが使えない場合は detach して Windows 側から動かす:
#   usbipd.exe detach --busid 1-7 ; cd /mnt/c/Users/<user>/AppData/Local/Temp && cp host_capture.py . && uv.exe run --with libusb1 python host_capture.py ...
uv run --with libusb1 python host_capture.py --set-usb-core 0      # 再起動。以後は約9 s待ち、短いinternal runで温める
uv run --with libusb1 python host_capture.py --width 8 --rate-mhz 48 --periods 320 --depth 8
uv run --with libusb1 python host_capture.py --width 8 --rate-mhz 60 --periods 320 --depth 8 --spool-prio --internal-fifo
uv run --with libusb1 python host_capture.py --width 16 --wide-profile --rate-mhz 40 --periods 160 --depth 8
```

## 結果

生ログ: `_runs/E107_20260915T061646JST_p4_direct/sweep.log`。対象は第三P4 `80:f1:b2:d0:b2:61`（ESP32-P4 rev v1.3、`esp32p4_es-libs`、CPU 360 MHz、`-Os`）。HSはPC直結で、hostは**Windows nativeのWinUSB**（`uv.exe run --with libusb1 python host_capture.py`、E104と同じ経路）から動かした。WSL側の`/dev/bus/usb`ノードが再列挙後にroot専用（`crw-rw-r-- root root`）になり、この作業環境では`sudo`がpasswordを要求して使えなかったため、usbipd attachをやめてWindows側から直接読んだ。したがってE106の44 / 32 Msps（usbipd/WSL経由）と数値を直接並べる場合は経路差があることに注意する。同じE107 firmwareでの`usb_core` A/Bは同一経路で取った。

試験量はE106と同じで、8-bitは65,536,000 wire byte（2,621,440 block = 167,772,160 sample）、16-bit wideは69,468,160 wire byte（1,310,720 block）。PASSはraw連番違反 0、複製lane不一致 0、PARLIO queue overflow 0、FIFO overflow 0、PC側sequence不一致 0、全byte受信。

### 1. USB割り込みのcoreだけを変えたA/B

同じfirmware（最初の版、codecはE106相当の構造をlambda化したもの。後述のとおりE106より遅い）で`Device.begin()`のcoreだけを切り替えた。

| `usb_core` | 8-bit 44 Msps | core 0 busy | core 1 busy | core 1の内訳 |
|---:|---|---:|---:|---|
| 1（Arduino `setup()`と同じ、E106の配置） | **FAIL**（raw連番違反 24、ring未読最大120,256 B） | 28.3% | **96.8%** | codec 85.0%＋usbd task 12.6%＋DWC2 ISR |
| 0 | **PASS**（ring未読最大8,064 B） | 40.6% | 88.8% | codec 87.5%のみ |

`usbd_us`（`espusb-device` taskの実行時間）は`usb_core=1`ではcore 1側、`usb_core=0`ではcore 0側の busy に現れた。つまり**TinyUSBのDWC2割り込みと、それに起こされるusbd taskは、`Device.begin()`を呼んだcoreに乗る**。Arduinoでは`setup()`がcore 1なので、E106はUSBの割り込み・FIFO push・8 KiBごとのmemcpyをcodecと同じcoreで動かしていた。E106が「core 0でspoolとUSB taskが競合」と書いた推定は外れで、競合はcore 1側だった。

`usb_core=0`のまま同じfirmwareで上げると44 / 48 / 52 MspsはPASS、56 / 60はFAILした。このときcore 1はcodecだけで88.8%（44）→ 95.7%（48）→ 99.7%（52）→ 100%（56）で、内部sink試験（USBなし）でも44 Mspsで86.2%、52で98.3%、60で100%・破綻だった。**USBとの結合がcore 1に足す負荷は44 Mspsで約1.3 pointにすぎず、残る律速はcodec自体**だった。

### 2. codec loopの直し

最初の版はE106の3本の直書きloopをlambda `encodeOne` / `rotateStage` にまとめたもので、`-Os`ではこれがE106より遅く、内部sink試験で60 Mspsに届かなかった（E106は61 MspsまでPASS）。profile別templateに分け、chunk / stage / targetで区切った連続blockを内側loopで回し、bit gatherを短縮した。

- 8-bit: 1 wordの4 sampleから下位3 bitずつを`w &= 0x07070707; w = (w | w>>5) & 0x003f003f; (w | w>>10) & 0xfff`の3段で12 bitへ集める。
- 16-bit: 2 wordの4 sampleを`(w0 & 0x70007) | ((w1 & 0x70007) << 6)`で並べ、`(x | x>>13) & 0xfff`で12 bitへ集める。
- wide profileのD=8は各bucket先頭wordを再利用し、16回の追加loadをなくした。

内部sink試験（USBなし、`usb_core=0`）のcodec task実行率:

| base rate | 最初の版 | 直した版 |
|---:|---:|---:|
| 8-bit 44 Msps | 86.2% | 64.6% |
| 8-bit 52 Msps | 98.3% | 76.0% |
| 8-bit 60 Msps | 100%・破綻 | **85.9%** |
| 8-bit 64 Msps | — | 90.5% |
| wide 40 Msps | — | 92.8% |
| wide 44 Msps | — | 98.0% |
| wide 48 Msps | — | 99.8%（PASS） |

### 3. 直した版での結合上限（`usb_core=0`、PSRAM FIFO、flags 0）

| profile | rate | 判定 | core 0 busy | core 1 busy | codec | usbd | usbTask | spool | FIFO high water |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 8-bit 3 full＋5 D64 | 56 | PASS | 60.0% | 84.5% | 82.7% | 20.3% | 18.4% | 22.2% | 1.05 MB |
| | **60** | **PASS 5回** | 59.8〜69.6% | 90.6〜91.9% | 88.9〜90.0% | 17.6〜21.4% | 19.7〜26.8% | 22.6〜23.2% | 1.06〜2.29 MB |
| | 64 | PASS 2回（USBが追い付かずFIFO増加） | 68.5% | 95.7% | 93.8% | 19.8% | 26.8% | 24.0% | 4.7〜5.0 MB |
| 16-bit wide 3 full＋1 D8＋12 D64 | **40** | **PASS 3回**（＋最初の版で1回） | 52.6% | 95.1% | 93.3% | 16.2% | 18.5% | 18.3% | 1.06 MB |
| | 44 | PASS 1回 | 57.9% | 99.2% | 97.9% | 18.4% | 19.1% | 20.5% | 1.06 MB |
| | 48 | FAIL（queue overflow 92、raw違反778） | 55.0% | 100% | 99.8% | | | | |

FIFO high waterはprebuffer 1 MiBを含む。8-bit 60 Msps（wire 23.4375 MB/s）ではrunによって1.6〜2.3 MBまで伸び、64 Mspsでは4.7〜5.0 MBまで伸びた。`usb_us`もcapture時間より長い（64 Mspsで2.82 s対2.62 s）。**このPC直結・native経路では、8-bit 60 Mspsで既にUSB帰路の実効速度（約23 MB/s）と生成速度が並び、64 Mspsは65 MBの試験量だけ通る状態**である。

対照として直した版を`usb_core=1`へ戻すと、8-bit 52 / 56はPASSしたが60はFAIL（core 1 99.5%、codec 80.4%＋USB約19%）、wide 40もFAIL（core 1 99.4%）だった。codecを速くしてもUSBをcore 1に置いたままでは目標値に届かない。

### 4. USB-only probeと予算

同じ経路の`EP` probe（64,000,000 byte、depth 8×1 MiB）:

| 回 | host実測 | 90%予算 |
|---:|---:|---:|
| 1 | 24.087 MB/s = 192.693 Mbps | 173.424 Mbps |
| 2 | 24.102 MB/s = 192.812 Mbps | 173.531 Mbps |
| 3 | 24.231 MB/s = 193.850 Mbps | 174.465 Mbps |

probe中はcore 0が32.3%（usbd 18.3%、残りはDWC2 ISRのFIFO pushと`usbTask`相当の書込）で、core 1は1%だった。8-bit 3 full＋5 D64は3.125 bit/base sampleなので、90%予算173.4 Mbpsに収まるbase rateは**約55 Msps**（例: 52 Msps = 162.5 Mbps）。16-bit wide 40 Msps = 132.5 Mbpsは予算の76%で収まる。E106の直結probe（usbipd/WSL）212.666 Mbpsより約9%低いが、これはhost経路の差であり、probe→90%で吸収する対象である。

### 5. その他の切替

いずれも直した版、`usb_core=0`、8-bit 60 Msps、1回。

| flag | 結果 |
|---|---|
| `0x01` spool 6 / usbTask 4 | PASS。busy配分はflags 0と同等。core 0に余裕があるので効かない |
| `0x04` PARLIO RX割り込みをcore 1へ | PASS したがcore 1が99.7%（codec 95.3%に割り込み分が乗る）。余裕を削るだけで不利 |
| `0x08` usbd taskをpriority 4へ | PASS したが`usb_us` 3.006 s対capture 2.796 s、FIFO high water 5.3 MB。USB帰路が遅くなるので不利 |
| `0x02` 内部RAM FIFO（確保できたのは128 KiB） | 最初の版で56 MspsはPASS（spool 20.6→9.6%、core 0 57→40%）。60 / 64ではUSB排出が生成に追い付かずFIFO overflow。PSRAM FIFOの容量は必要 |

### 6. 見つけた不具合

1. **spoolがFIFO overflowで抜けるとcodecが永久待ちになる**（E106由来）。`fifoPush`失敗で`break`するとstageが返らず、`harvestTask`が`FreeStageQueue`で止まり、`RunPending`が下りずdeviceが以後の命令を受けなくなった。overflowは数えるだけにしてstageは返す。
2. **未flushの短いstatus行が残るとusbTaskが動けない**（E106由来）。`waitWritable(8192)`はFIFOに端数が残っている限り成立せず、`kMaxTimeouts`回（10 s）待って諦める。E107では`writeAvailable() < span`のときに`flush()`してから待つ。usbTaskの書込は常に512の倍数なので定常ではshort packetにならない。
3. **`EP` probeのstatus行にflushがなく**、次のrunの先頭に132 byteの残留として現れ、上記2と合わさって以後2〜3 runを連鎖で壊した。probe後にflushする。
4. host側: WinUSBで列挙直後に短いtimeoutのread（残留掃き出し）を行うとpipeが同期を失ったので、`--drain`はopt-inにした。status読みはtimeoutを許容して再試行する。Windows consoleのcode pageで`\ufffd`が例外になるので`errors="replace"`にした。

## 事実 / 候補 / 未決

**事実**

1. TinyUSBのDWC2割り込みとusbd taskは`Device.begin()`を呼んだcoreに乗る。Arduinoでは`setup()`のcore 1、すなわちE106のcodec側だった。`usb_core=0`にするだけで同一firmwareの8-bit結合上限は44未満から52 Mspsへ上がった。
2. `usb_core=0`では、USB結合がcore 1へ足す負荷は約1 pointで、残る律速はcodecの実行時間。lambda化したloopは`-Os`でE106より遅く、profile別templateとbit gather短縮で8-bit 60 Mspsのcodec率は100%→85.9%になった。
3. 直した版は8-bit 60 Msps 5回、16-bit wide 40 Msps 3回PASS。内部sinkだけなら8-bit 64、wide 48まで成立する。
4. このPC直結・Windows native経路のUSB-only probeは192.7〜193.9 Mbps、90%予算173.4〜174.5 Mbps。8-bit 60 Msps（187.5 Mbps）は予算超えで、FIFO high waterの伸びとして実際に見えた。
5. spool priority、PARLIO割り込みのcore、usbd priorityの変更はいずれも上限を上げない。

**候補**

- USBは**core 0で初期化する**（`Device.begin()`をcore 0固定taskから呼ぶ）。codecはcore 1、PARLIO RX割り込み・spool・usbTaskはcore 0。
- codecはprofile別に特化し、blockの連続runを内側loopで回す。generic codecを使う場合も`-Os`でのlambda / 間接呼び出しを避ける。
- PSRAM FIFOは残す。内部RAM FIFOはUSB排出が生成を常に上回る条件でしか成立しない。
- 通常値はprobe→90%で決める。この経路なら8-bitは52〜55 Msps、wideは40 Msps。usbipd/WSL経路のprobeが212 Mbpsなら8-bit 60が予算内に入る。

**未決**

- 8-bit 60 Mspsを直結の**保証値**にするには、USB帰路が生成23.4 MB/sを常に上回る必要がある。native probeは24.1 MB/sで余裕3%。DWC2 DMA mode（`CFG_TUD_DWC2_DMA_ENABLE=1`、TinyUSB 0.21のesp32 portにcache hookあり）でIN側の実効速度とcore 0負荷が変わるかは未測。E104の列挙単位の16.8 / 24.0 MB/s二状態が出ればさらに下がる。
- wide 44 Msps PASSは1回で、codec 97.9%。40 Mspsの余裕はcodec 93%で薄い。wideのcodecをさらに詰めるか、40を通常上限に据えたまま少数channel高速モードを分けるかは製品判断。
- 長時間soak（分単位）、hub経路、usbipd/WSL経路での同じ掃引は未実施。usbipd経路は今回`/dev/bus/usb`の権限で試せなかった（udev ruleで`303a:4021`を`plugdev`にすれば戻せる）。
- run-time statsのISR時間は中断されたtaskに計上されるため、codec 89.9%のうちPARLIO以外のISR分は分離していない（`usb_core=0`ではcore 1の割り込みはtickとIPIのみ）。

## 追記（2026-09-15、E108）

未決に挙げたDWC2 DMA modeは[E108](../e108_p4_zero_copy_stream/README.ja.md)で試し、帯域・負荷に差はなかった。代わりにcodec stageをそのままDWC2へ渡すzero-copy送信でUSB-onlyは同じusbipd/WSL直結で209→247 Mbps、core 0のtask負荷は8-bit 60 Mspsで57〜66%→7%になり、結合上限はcodecだけで決まるようになった（8-bit 72 / wide 52 MspsまでPASS）。本実験の`spin`なしrun-time statsはISR時間をidleに含めるため、core 0の負荷は過少に見えている点もE108のspin法で分かった。

## 訂正（2026-09-15、E109）

仮説1の「EspUsbDevice 2.3.0のDWC2はslave mode（`CFG_TUD_DWC2_DMA_ENABLE=0`）で、bulk INのpayloadはISRがCPU storeでTX FIFOへ押し込む」は誤りだった。`tusb_option.h`の既定値0だけを見て、ライブラリ側`src/internal/EspUsbTinyUsbConfig.h`がP4で`CFG_TUD_DWC2_DMA_ENABLE 1`を定義していることを見落とした。[E109](../e109_p4_stream_soak/README.ja.md)で`GAHBCFG.DMAEn=1`・`GINTMSK.RXFLVL=0`を読んで確認した。本文の測定と結論（USB割り込みとusbd taskがcore 1に乗っていたこと、core 0初期化で解消すること）は変わらないが、core 1を奪っていたのはDWC2完了割り込みの処理とusbd taskの8 KiBごとのmemcpy・再armであり、FIFO pushではない。§4の「残りはDWC2 ISRのFIFO push」も同様に読み替える。
