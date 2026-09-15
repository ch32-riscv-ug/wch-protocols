# E108 USB帰路のzero-copy化とDWC2 DMA — 結合上限をUSB予算まで押し上げる

状態: **完了 — stageからのzero-copy送信でUSB-only 247 Mbps（＋18%）、core 0のtask負荷60〜70%→7%。結合上限はcodecだけで決まり、PC直結で8-bit 72 Msps / 16-bit wide 52〜56 MspsまでPASS。DWC2 DMA flagは効果なし**（2026-09-15） — **参考値（独自patch版library）**（EspUsbDevice 2.3.0＋E097/E101/E102/E110の一時patch。正規libraryに取り込まれるまで製品の目安には使わず、修正依頼の根拠にのみ使う）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E107](../e107_p4_stream_core_placement/README.ja.md)、[E102](../e102_p4_vendor_in_zero_copy_precomputed/README.ja.md)、[E101](../e101_p4_vendor_in_callback_chain/README.ja.md)、[E097](../e097_p4_vendor_out_direct_rx/README.ja.md)

## 問い

E107の構成では、wire 1 byteがcore 0で4回運ばれる（Stage→PSRAM、PSRAM→TinyUSB ring、ring→endpoint buffer、slave modeのISRがendpoint buffer→DWC2 FIFO）。codec stageをそのままDWC2へ渡すzero-copy送信にし、PSRAMをUSBが遅れた時だけの退避にし、さらにDWC2をDMA modeにすると、**USB-only上限、core 0負荷、結合上限**はどこまで変わるか。8-bit 60 Msps（187.5 Mbps）はUSB予算の90%以内に入るか。

## 仮説

- E102はzero-copy＋callback chainでP4 host相手に36.159 MB/sを出した（buffered 25.575の1.41倍）。Windows / usbipd hostでも同じ比率なら、USB-onlyは現在の193〜209 Mbpsから260 Mbps級へ上がり、8-bit 60 Mspsは90%予算に収まる。
- core 0のusbd＋usbTask＋spoolの約60 point（8-bit 60 Msps時）は、zero-copyで大半が消える。DMA modeにするとslave modeのISRによるFIFO push分も消える。
- codec（core 1）は変わらないので、結合上限の残り律速はcodecになり、8-bitで64 Msps付近、wideで44 Msps付近が内部と一致する。

## 反証条件

- zero-copyでもUSB-onlyが200 Mbps前後のまま → 天井はhost側（Windows xHCI / usbipd）か線上で、device側copyではない。
- DMA modeで列挙・転送が壊れる → TinyUSB 0.21のesp32 DMA portはこの構成では使えない。slave mode zero-copyの値だけを採る。
- 結合でringを追い越す点がE107（8-bit 64 / wide 48で破綻）と同じ → USB経路はもう律速でなく、codecだけが残る。

## 方法

### data pathの設計（ゼロベース）

```
PARLIO RX DMA ring (64 KiB) --ISR(core 0)--> ChunkQueue --> codec task (core 1)
  codecは wire byte を Stage[4] (各27,136 B = lcm(53, 512)、内部RAM、64 B整列) へ直接書く
  fill: 8-bit / legacy 1,024 block = 25,600 B、wide 512 block = 27,136 B  → どのprofileでも512 Bの倍数
  Ready --> usb task (core 0)
     退避FIFO(PSRAM)が空 かつ arm ringに空きがある → Stageをそのまま arm ring へ (zero-copy)
     それ以外                                      → StageをPSRAM退避FIFOへcopyし、Stageを即返す
     退避FIFOに残りがある間は Bounce[2] (各27,136 B) へ512 B単位で詰め、順に arm ring へ
  arm ring (深さ4) --> TX完了callback (usbd task) が次のsegmentを即 arm (callback chain)
```

- 送信は常にstage / bounce 1個 = 1 transfer（最終回だけ端数）。codec block境界とUSB packet境界の分離は、transfer長が512の倍数であることで保つ。short packetは最終回とstatus行だけ。
- 順序はarm ringのFIFO性と「退避FIFOが空になるまで直接送信しない」規則で保つ。
- 完了callbackはInflight segmentを返す（Stage→FreeQueue、Bounce→empty、status→semaphore）だけで、copyはしない。
- TinyUSB vendor classはnon-buffered（`CFG_TUD_VENDOR_TXRX_BUFFERED=0`）。E097のdirect RX hook、E101のTX完了hook、E102のzero-copy hunkに、bufsize上限を外す変更を足した[patch](espusbdevice-e108.patch)を出荷版EspUsbDevice 2.3.0へ当て、`/tmp/EspUsbDevice-e108`を`sketch.yaml`の`libraries: dir:`で使う。
- 命令（16 byte）はdirect RX callbackからmailboxへ。USB初期化はcore 0固定（E107の結論）。
- build variant: A = slave mode、B = `CFG_TUD_DWC2_DMA_ENABLE=1`＋`CFG_TUD_MEM_DCACHE_ENABLE=1`＋`CFG_TUD_MEM_DCACHE_LINE_SIZE=64`。codecはstageを渡す前に`esp_cache_msync(C2M)`し、core 1で書いた行がDMA前に必ずmemoryへ落ちるようにする。
- flags: `0x01` = device側Gray checkを省く（検査のcodec費用を測る）、`0x02` = 直接送信を禁止し全stageを退避FIFO経由にする（copy経路との対照）。

### 計測

E107と同じrun-time stats（core別idle、usbd / usbTask / codecの実行時間）に、direct / bounce segment数、退避byte数、退避high water、完了数、arm失敗数を足す。判定はE106 / E107と同じ。掃引はhost経路をWSL usbipdとWindows nativeの両方で、`EP` probe 64 MB×3、8-bit 60 / 64 / 68 Msps、wide 40 / 44 / 48 Msps、各65.536 / 69.468 MB。PASS境界は3回。

## 対象外

任意descriptor、host gateway、外部GPIO、EspUsbDeviceの公開API設計（patchはこの実験の再現用で、製品化するならbuffer ownershipを明示したAPIとして改修依頼にする）。

## 必要な環境 / ベンチ種別

第三P4（`/run/board-identify/by-id/esp32-p4-80f1b2d0b261`、HSはWindows bus `1-7`直結）。WSL経路は`/etc/udev/rules.d/99-wch-protocols-p4.rules`（`303a:4021`→plugdev）で開通済み。一時。

## 記録する数値 / 完了条件

USB-only probe（両経路）、8-bit / wide のPASS境界とcore別busy、直接送信とcopy経路の差、DMA modeの可否と効果。8-bit 60 Mspsが両経路の90%予算に収まり、結合PASS境界が内部sink上限と一致すれば完了。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.2 / §1.3 / §3 Phase D、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)、[EspUsbDevice改修依頼](../../references/espusbdevice-change-requests.ja.md)（zero-copy TX APIの新規依頼候補）。

## 再現

`build_opt.h`は最終構成（variant D: slave mode、`-DE108_CODEC_O2=1`、`-DE108_CODEC_PREFETCH=1`）を置いてある。variant Bは`-DCFG_TUD_DWC2_DMA_ENABLE=1 -DCFG_TUD_DWC2_SLAVE_ENABLE=1 -DCFG_TUD_MEM_DCACHE_ENABLE=1 -DCFG_TUD_MEM_DCACHE_LINE_SIZE=64`を足す。

```sh
cp -r ~/.arduino15/internal/EspUsbDevice_2.3.0_*/EspUsbDevice /tmp/EspUsbDevice-e108 && (cd /tmp/EspUsbDevice-e108 && patch -p1 < espusbdevice-e108.patch)
arduino-cli compile --profile esp32p4_device
arduino-cli upload --profile esp32p4_device --port /run/board-identify/by-id/esp32-p4-80f1b2d0b261
uv run --with libusb1 python host_capture.py --probe-bytes 64000000 --depth 8
uv run --with libusb1 python host_capture.py --width 8 --rate-mhz 60 --periods 320 --depth 8
uv run --with libusb1 python host_capture.py --width 16 --wide-profile --rate-mhz 40 --periods 160 --depth 8
```

## 結果

生ログ: `_runs/E108_20260915T075902JST_p4_direct/sweep.log`。対象と経路はE107と同じ第三P4のPC直結で、hostはWSL usbipd経路（udev ruleで`303a:4021`をplugdevにし、WSLの`uv run --with libusb1`から実行）。同日同経路でE107 firmwareを測った値を対照にした。試験量、PASS判定はE106 / E107と同じ。build variantは A = slave mode（既定）、B = `CFG_TUD_DWC2_DMA_ENABLE=1`＋DCACHE hook、C = A＋codecを`#pragma GCC optimize("O2")`、D = C＋次blockのcache line先読み。

### 1. USB-only probe（`EP` 64,000,000 byte、depth 8×1 MiB）

| firmware | host実測 | 90%予算 | core 0（task計上） | core 0（spin法、ISR込み負荷） |
|---|---:|---:|---:|---:|
| E107（buffered、copy 3回） | 209.0 / 209.4 Mbps | 188.1 / 188.5 Mbps | 33.8% | 未測 |
| **E108 A（zero-copy）** | **247.7 / 247.3 / 246.7 Mbps** | **222.9 / 222.6 / 222.1 Mbps** | 5.5%（usbd 3.9%） | **約5%**（空き94.9〜95.0%） |
| E108 B（DMA flag） | 241.2 / 245.7 / 248.9 Mbps | 217.0 / 221.1 / 224.0 Mbps | 5.5% | 約5% |

zero-copyでUSB-onlyは**約18%**上がり、E102がP4 host相手に見た「copyを外すと上がる」と同じ方向だった。transferは27,136 byte（53 packet）を1回として2,359回で、arm失敗0、short 0。

### 2. 8-bit 3 full＋5 D64の結合（A、flags 0、`-Os`）

| rate | wire | 判定 | codec（core 1） | core 0 task busy | core 0 spin法負荷 | direct / bounce |
|---:|---:|---|---:|---:|---:|---|
| 56 | 175 Mbps | PASS | 77.4% | 7.0% | | 2560 / 0 |
| 60 | 187.5 Mbps | PASS | 82.6% | 7.3% | 23% | 2560 / 0 |
| 64 | 200 Mbps | PASS | 87.8% | 7.5% | | 2560 / 0 |
| 68 | 212.5 Mbps | PASS | 92.9% | 8.0% | 26% | 2560 / 0 |
| **72** | 225 Mbps | **PASS** | 97.1% | 12.9% | | 2223 / 332（退避8.6 MB、high water 63 KB） |
| 76 | 237.5 Mbps | FAIL（queue overflow 1,213） | 99.9% | 51% | | 139 / 2285 |

E107 firmwareの同日同経路は8-bit 60 Mspsでcore 0 task busy 57〜66%、codec 87.7〜88.4%だった。E108では**core 0が7.3%**、codecも82.6%へ下がった（stageが12,800→25,600 byteになりqueue往復とspoolのmemcpyによるbus競合が減った分と見る）。72 Mspsではwire 28.1 MB/sがUSB-only 31 MB/sの90%に達し、arm ringが埋まった瞬間に退避経路へ回る動きが見えたが欠損はない。

### 3. codecの変種（内部sink、core 1のcodec task実行率）

| rate | A `-Os` | C `-O2` | D `-O2`＋先読み |
|---:|---:|---:|---:|
| 8-bit 68 Msps | 93.0% | 92.0% | 90.8% |
| 8-bit 72 Msps | 97.0% | 96.5% | 95.6% |
| 8-bit 76 Msps | — | 96.8%（PASS） | 97.1%（PASS） |
| wide 48 Msps | 99.0% | — | 94.5% |
| wide 52 Msps | — | — | 95.8% |

結合では C / D で8-bit 72 PASS、76 / 80 FAIL（codec 100%）。wideは A で40 / 44 / 48 / 52 PASS（52は99.9%）、C で48 / 52 / 56 PASS（56は99.4%）、D で52 PASS 95.9%。`-O2`は8-bitで約1 point、wideで約4 point、先読みはさらに約1 point。**現在のcodecは8-bit約72〜74 Msps、wide約52〜56 Mspsが実測上限**で、cache miss隠しでは大きく動かないので残りは演算量（PIE SIMD化か命令数削減）になる。

### 4. 対照

| 条件 | 結果 |
|---|---|
| flag `0x02` 全stageをPSRAM退避経由（8-bit 60） | PASS。core 0 task busy 35.9%（usbTask 32.8%）。直接送信の7.3%との差**約29 point**がPSRAM往復2回の費用 |
| flag `0x01` Gray check省略（8-bit 68 / 72） | codec 92.9→85.3%、97.1→90.3%。**device側検査は約7〜8 point**。製品firmwareには載らない |
| flag `0x01`＋C、8-bit 80 | PASS（codec 99.7%）。wire 31.25 MB/sがUSB-onlyと並び、退避high water 5.0 MB |
| B DMA flag、8-bit 60 / 68、wide 44 / 48 | Aと全項目同値（codec、core 0、spin法とも） |

spin法（priority 1のspin taskをcore 0に置き、校正値50.1 M回/sとの比で空きを出す）では、8-bit 60 Mspsでcore 0の実負荷は約23%、task計上の7.3%との差**約16 point**がISR（PARLIO callback約15 k回/s、DWC2完了）である。run-time statsはISR時間を中断されたtaskに計上するため、idle中の割り込みはidleに見える。E107のcore 0値もこの分を過少に見ていた。

### 5. 観測

- `GAHBCFG.DMAEn`（`0x50000008` bit 5）の読み値はA / B両buildで1、`GHWCFG2.arch`は2（internal DMA）だった。**[E109](../e109_p4_stream_soak/README.ja.md)で決着**: EspUsbDevice 2.3.0の`src/internal/EspUsbTinyUsbConfig.h`がP4で`CFG_TUD_DWC2_DMA_ENABLE 1` / `CFG_TUD_DWC2_SLAVE_ENABLE 0`を定義しており、variant Aも最初からDMA modeだった。`GINTMSK.RXFLVL=0`もDMA初期化経路と一致する。したがって本文の「slave mode」「4回目のcopy（ISRのFIFO push）」は誤りで、copyは3回、B variantがAと同値なのは両方DMAだからである。Bで足した`CFG_TUD_DWC2_SLAVE_ENABLE=1`はnon-buffered vendorだけのE108では害が出なかったが、buffered classと併用すると同headerのコメントどおり壊れる。
- Windows console（cp932）向けに`errors="replace"`、status読みのtimeout許容、`--drain`はopt-in、はE107から引き継いだ。
- E108のfirmwareはE106 / E107のusbTask、spoolTask、PSRAM FIFO常時経由、TinyUSB bufferedモードを持たない。commandはdirect RX callbackのmailbox、statusは1 transferで、probe / capture / statusのすべてがarm ringを通る。

## 事実 / 候補 / 未決

**事実**

1. codec stageをそのままDWC2へ渡すzero-copy送信で、USB-onlyは209→247 Mbps（＋18%）、core 0のtask負荷は8-bit 60 Mspsで57〜66%→7.3%になった。PSRAM退避は8-bit 72 Msps以上でしか使われず、退避経路でも欠損はない。
2. USB帰路は律速でなくなり、結合上限はcodec（core 1）だけで決まる。8-bit 72 Msps（225 Mbps）と16-bit wide 52 Msps（172 Mbps）がPASS、8-bit 76・wide 56以上でcodec 100%になり8-bitは破綻する。
3. `-O2`とcache line先読みはcodecに合計1〜5 pointの効果で、cache missは主因ではない。device側Gray checkは7〜8 pointを占める。
4. `CFG_TUD_DWC2_DMA_ENABLE`は帯域・core 0負荷ともに変えない。EspUsbDeviceがP4では既定でDMA modeにしているためで、slave modeとの比較にはなっていない（E109）。
5. E107の直結値（8-bit 60 / wide 40 Msps）はE108でそれぞれcodec 82.6% / 約86%の余裕を持つ通常値になった。USB予算90%（222 Mbps）に対し8-bit 60は187.5 Mbps（84%）で収まる。

**候補**

- 製品data pathは「codec→stage（512の倍数）→zero-copy DMA arm→PSRAMは退避のみ」。stage 27,136 byte×4、bounce×2、arm ring深さ4、直接送信は2段まで。
- EspUsbDeviceへの改修依頼: non-buffered vendor classでのzero-copy TX（buffer ownership付き）、TX完了callback、direct RX callback。[patch](espusbdevice-e108.patch)がその仕様の下書きになる。
- codecは`-O2`で組む。次に上げるならPIE（`xesppie`）SIMDで4〜16 sampleのbit gatherを1命令にするか、Gray checkをhost側へ移す。

**未決**

- Windows native経路のUSB-only probe（E107は193 Mbps）はE108では未測。usbipd/WSL経路で＋18%だったので同程度に上がる見込みだが要実測。
- 長時間soak（分単位）、hub経路、繰り返し列挙、途中切断後の復帰。
- ~~`GAHBCFG.DMAEn`読み値の解釈~~ → E109で決着（既定でDMA mode）。
- 16本独立GPIO、任意descriptor、host gatewayはE107までと同じく未着手。
