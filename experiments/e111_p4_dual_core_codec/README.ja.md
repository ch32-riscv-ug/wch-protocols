# E111 codecを2 coreへ分ける — chunk連番で順序を保つ並列codec

状態: **完了 — 2 worker化で結合上限は8-bit 76→108 Msps（112も1回PASS）、wide 56→72 Msps。8-bitは次にUSB予算（350 Mbps≒112 Msps）、wideはcore 1のcodecが律速。8-bit 100 / wide 64の30 s soakも欠損0**（2026-09-15） — **参考値（独自patch版library）**（EspUsbDevice 2.3.0＋E097/E101/E102/E110の一時patch。正規libraryに取り込まれるまで製品の目安には使わず、修正依頼の根拠にのみ使う）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E110](../e110_p4_usb_in_ceiling/README.ja.md)、[E108](../e108_p4_zero_copy_stream/README.ja.md)、[E106](../e106_p4_mixed_rate_capture_stream/README.ja.md)

## 問い

E110でUSB帰路は理論の93%（389 Mbps、90%予算350 Mbps）になり、core 0はcapture中も約90%空いている。残る律速はcore 1のcodec（8-bit 76 Msps・wide 56 Mspsで97〜99%）だけである。**PARLIOのDMA chunkを2つのcodec worker（core 0とcore 1）へ振り分け、chunk連番から出力位置を決めて順序を保つと、結合上限はどこまで上がり、次に何が律速になるか。**

## 仮説

- codec容量は約1.8倍になり、8-bit 3 full＋5 D64は約110 Msps（wire 344 Mbps＝予算の98%）でUSB予算に、16-bit wideは約100 Msps（331 Mbps）でUSB予算かPARLIO 16-bit DMA（200 MB/s）に当たる。
- chunk長をblock長の倍数（3,840 byte＝60 / 30 / 15 block）にすると、chunk境界のblock持ち越しがなくなり、workerはchunkごとに独立して処理できる。PARLIOのnode分割はring 65,280 byteで17等分の3,840になる見込み（E106は65,408で平均3,847.5だった）。
- 出力順序はchunk連番`n`から`wire offset = n × chunkWire`で決まるので、workerが同時に別stageへ書いても順序は崩れない。stageの完了はbyte数の合計で判定し、USB側は連番順に送る。
- core 0のcodec workerは PARLIO ISR・usb task・usbd task と同居するが、それらは合計10%程度なので、core 1側とほぼ同じ速度で回る。

## 反証条件

- PARLIO chunkが3,840 byteにならない → chunk境界のblock持ち越しをworker間で受け渡す設計が要る（この実験では短chunkを数えて中止）。
- 2 workerでも上限が単coreの1.2倍未満 → 律速はcodec演算ではなくDMA ringのcache miss / memory帯域。
- 8-bit 100 Msps付近でqueue overflowでなくUSB退避が増える → USB予算が先に当たる（想定どおり）。

## 方法

- firmwareはE110/stream（E108 data path＋TX FIFO 2 packet）を基に、harvest taskを`codecWorker`×2に置き換える。ring 65,280 / delimiter 65,280、chunkに連番を付け、workerは`xQueueReceive`で次のchunkを取る。stage bufferは`stage index k`→`buffer k mod 4`、`round k / 4`で、bufferの解放回数（USB完了で加算）を待って書く。stageのbyte合計が満たされたworkerがcache writebackしてReady queueへ出し、usb taskは連番順に送る。
- flag `0x10`で単worker（core 1のみ）にでき、同じfirmwareでA/Bを取る。device側Gray checkはchunk内の連続だけ（chunk境界はhostの全stream検査に任せる）。
- 掃引（usbipd/WSL、E109と同じ判定）: 8-bit 76 / 84 / 92 / 100 / 108 / 116 Msps、wide 56 / 64 / 72 / 80 / 88 / 96 Msps。境界は3回。Gray check省略（`0x01`）でも測る。単worker対照は8-bit 76、wide 56。
- 計測: 各workerの実行率、chunk数、stage待ち回数、短chunk数、ring未読最大、退避byte、USB probe。

## 対象外

任意descriptor、PIE SIMD化、hub経路、外部GPIO。

## 必要な環境 / ベンチ種別

第三P4、PC直結、usbipd/WSL。ライブラリは`/tmp/EspUsbDevice-e110`（E108 patch＋TX FIFO 2 packet）。一時。

## 記録する数値 / 完了条件

profile別のPASS境界、その時のcore別busyとUSB予算比、単worker対照との比。次の律速がUSB予算・PARLIO DMA・codecのどれかを数値で言えれば完了。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.1（通常値の目安）/ §1.2 / §3 Phase D、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)。

## 再現

```sh
arduino-cli compile --profile esp32p4_device && arduino-cli upload --profile esp32p4_device --port /run/board-identify/by-id/esp32-p4-80f1b2d0b261
usbipd.exe attach --wsl --busid 1-7
uv run --with libusb1 python host_capture.py --width 8 --rate-mhz 100 --periods 320 --depth 8
uv run --with libusb1 python host_capture.py --width 16 --wide-profile --rate-mhz 80 --periods 160 --depth 8
uv run --with libusb1 python host_capture.py --width 8 --rate-mhz 76 --periods 320 --depth 8 --single-core
```

## 結果

生ログ: `_runs/E111_20260915T094822JST_p4_direct/sweep.log`。usbipd/WSL直結、host URB 1 MiB×8、判定はE106〜E109と同じ（host全stream照合＋device側counter）。firmwareはE110/stream（zero-copy、TX FIFO 2 packet、codec `-O2`＋先読み）の codec taskを2 workerに置き換えたもの。

### 1. 前提の修正: chunk長は固定でない

PARLIO driverはringを最大4,032 byteのDMA nodeに分け、末尾2 nodeを等分する。ring 65,280 byteでも観測されたchunk長は**2,368〜4,032 byte**（`chunk_min` / `chunk_max`）で、計画の「17等分の3,840」にはならなかった。最初の版はseq×3,840で出力位置を決めていたため、stageのbyte合計が揃わずworker両方がstage待ちで回り続け、IDLE0のtask watchdogで再起動した。

修正後はchunkに**raw stream offset**を持たせ、blockの位置は`offset / block長`で決める。chunk先頭にかかる跨ぎblockは、その chunkを受けたworkerが直前chunkの末尾（ringに残っている。ring先頭のchunkならring末尾）と自分の先頭を組み合わせて符号化し、自分の末尾の端数は次のchunkに任せる。stage待ちには500 msのtimeoutを付け、超えたらrunを中止してstatusを返す（chunkが1つでも落ちるとそのstageは永久に揃わないため、この経路が「overflow → 中止」を担う）。`short_chunks`は全runで0。

### 2. 2 worker の結合上限

| profile | rate | wire | 判定 | codec core 0 / core 1 | core 0 / core 1 busy | ring未読最大 |
|---|---:|---:|---|---:|---:|---:|
| 8-bit 3 full＋5 D64 | 76 | 237.5 Mbps | PASS | 64.9% / 66.1% | 74.0% / 67.1% | 10,432 |
| | 84 | 262.5 | PASS | 70.9 / 72.8 | 80.8 / 73.8 | 12,864 |
| | 92 | 287.5 | PASS 3回（初回1回は282 KBでhost timeout、再現せず） | 76.1 / 80.6 | 86.8 / 81.7 | 12,096 |
| | 96 | 300 | PASS | 82.7 / 84.5 | 93.6 / 85.6 | 14,464 |
| | 100 | 312.5 | PASS＋**30 s soak 1.17 GB欠損0** | 84.2 / 87.1 | 95.7 / 88.4 | 16,128 |
| | **108** | 337.5 | **PASS 3回** | 86.8 / 92.2 | 99.5 / 93.4 | 20,928 |
| | 112 | 350 | PASS 1回 | 86.0 / 96.4 | 99.6 / 97.5 | 20,928 |
| | 116 | 362.5 | FAIL（check有無とも。queue overflow 15,099、中止） | 79 / 99 | | 498,048 |
| 16-bit wide | 56 | 185.5 | PASS | 81.1 / 80.0 | 89.4 / 81.0 | 14,528 |
| | 64 | 212 | PASS＋**30 s soak 795 MB欠損0** | 88.7 / 89.6 | 98.1 / 90.9 | 18,496 |
| | **72** | 238.5 | **PASS** | 89.3 / 99.1 | 99.9 / 99.9 | 24,192 |
| | 76 | 251.75 | FAIL 3回（queue overflow 19,797、codec core 1 99%、中止） | 96 / 99 | | 516,608 |
| | 80 | 265 | FAIL（check有無とも） | | | |

単core（E110/stream）の上限は8-bit 76 / wide 56だったので、**8-bitは1.42倍、wideは1.29倍**。core 0のworkerはPARLIO ISR・usb task・usbd taskと同居するためchunkの取り分は約41%（core 1が約59%）で、両coreの合計codec率は8-bit 108で179%、wide 72で188%。

律速の切り分け:

- **8-bit**: 112 Msps（350 Mbps）はUSB-only probe 389 Mbpsの90%予算ちょうど。116では退避経路が動いてcore 0のusb taskがPSRAM copyを始め、core 0 workerが遅れてringを追い越した（codec core 0 79%、core 1 99%）。次の律速は**USB予算**。
- **wide**: 76でもwireは252 Mbps（予算の72%）だが、core 1のworkerが99%で追い越す。次の律速は**codec（core 1）**。core 0側に余裕（89%）があってもqueueからの取り分は自然に決まるので、core 1が先に飽和する。
- Gray check省略（製品相当）でも8-bit 116 / wide 80は通らず、上限は変わらない（USB予算・core 1飽和のため）。

### 3. 副作用と観測

- 単workerモード（flag `0x10`）は8-bit 76でFAIL（codec 99.7%、queue overflow 9,899）。E110/streamの単core版は同点で97%でPASSしていたので、chunk単位の構造（跨ぎblockのmemcpy、chunkごとのGray check初期化、stage計算）は約3 point重い。2 workerの利得がそれを上回る。
- stage待ち（`stage_waits`）はPASSした全runで0。USBが常に先行しているため、bufferの解放待ちは起きていない。
- 中止経路が働いたrunでは、deviceのstatus行がdata streamの途中に短packetとして届く。hostはそれを`captured`末尾から拾って理由を表示するようにした。
- USB-only probeはこのfirmwareで再測していない（ライブラリと送出経路はE110/streamと同一で389 Mbps）。

## 事実 / 候補 / 未決

**事実**

1. PARLIO chunk長は2,368〜4,032 byteで可変。並列codecはraw offset基準の配置と跨ぎblockの組み立てが必須で、固定chunk長の仮定は反証された。
2. 2 workerで結合上限は8-bit 108 Msps（3回、112も1回）、wide 72 Msps。8-bit 100 / wide 64は30 s soakで欠損0。
3. 8-bitの次の律速はUSB予算（350 Mbps≒112 Msps）、wideはcore 1のcodec。両coreの合計codec率は約180〜190%まで使えている。
4. 単worker換算では新構造は約3 point重い。

**候補**

- 通常値の目安を引き上げる余地がある: 8-bit 100 Msps（codec 84 / 87%、USB 89%）、wide 64 Msps（codec 89 / 90%、USB 61%）が現時点の「余裕を持つ点」。60 / 40 Mspsは大きな余裕になる。製品値の変更は持ち主判断。
- workerへの振り分けをcore 1優先にせず、core 0の空きを使い切るなら、chunkをworker別queueへ交互に配る（順序は既にoffset基準で保てる）。
- 中止経路は実験用。製品ではchunk落ちを検出したらstageを欠損印付きで送る、または再同期する設計が要る（Phase Bの「正しさ」項目）。

**未決**

- wideをさらに上げるならcodec演算そのもの（PIE SIMD）か、3 worker（core 0にもう1つ）か。
- 8-bit 112以上はUSB予算で決まるので、E110の残り7〜12%（複数endpoint、transfer長の16-bit上限撤廃）が次の候補。
- 92 Mspsで1回だけ出た早期timeout（282 KB）の原因。以後3回は再現せず。
