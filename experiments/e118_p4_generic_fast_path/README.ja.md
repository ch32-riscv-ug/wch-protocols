# E118 generic codecのfast部とper-block費用を削って、16 ch hold構成のstreamingを60 Mspsへ — 正規リリース2.4.0（pin）

状態: **完了 — 製品モードで16 ch hold構成が60 Msps byte一致＋25 s soak欠損0（上限65）。codecでなくworkerの周辺費用（chunkごとのqueue受信、runごとのcache書き戻し）が原因で、DMA chunkの結合と書き戻しの撤去で単coreの上限30→40、2 workerで55→65 Mspsに上がった。正式な数値（2.4.0 pin）**（2026-09-16）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E115](../e115_p4_grouped_plane_transpose/README.ja.md)（群転置、bench 47.9 Msps/core）、[E117](../e117_p4_stream_release_api/README.ja.md)（2.4.0 pinで16 ch hold 50 Msps byte一致、55で落ちる）

## 問い

generic codec（任意descriptor、群転置）の16 ch hold構成（3 raw＋hold/8＋12 hold/64）は、bench 47.9 Msps/core、2 worker streamingで50 Mspsが上限で、固定wide profileの72 Mspsに対して0.7倍。E115で縮約の取り出しはほぼ0になったので、残りは**fast部（F bit gather＋emit、約5 cycle/sample＝約640 cycle/block）**と**streaming固有の費用**（Gray check、DMA ringの読み、stage書き出しとmsync、chunk / stageの簿記）。これらの内訳を測り、効く順に削ると、**製品モード（device側Gray checkなし）で16 ch hold構成が60 Msps byte一致**に届くか。

## 仮説

- fast部はF=3 16-bitで8 sampleあたり約40 cycleで、うちbyte単位のemit（`*o++`）とループの簿記が半分。32-bit accumulatorでword単位に書き出し、8 sample群を4つまとめて処理すると8 sampleあたり25 cycle以下になる（≈3 cycle/sample）。
- streaming固有の費用のうちdevice側Gray checkは7〜8 point（E108）で、製品モードでは載らない。E117の上限50はcheck込みなので、`--no-check`なら55が通る可能性がある。
- 単coreのstreamingはbenchの0.8〜0.9倍（E114）。2 workerで1.6倍。60 Mspsには単core bench 60 / 1.6 / 0.85 ≈ 44以上、つまり現状の47.9で足りるはずなのに50で止まるのは、2 worker時の共有資源（DMA ring読み、stage書き、`StageMux`）の競合。core idleの内訳（codec task時間と全体）で切り分ける。

## 反証条件

- fast部の見直しでbenchが上がらない → gatherの演算数でなくload / storeの帯域（DMA ringからの読み、stageへの書き）が律速。cache line単位の先読みと書き出しに切り替える。
- bench 60以上でもstreaming 60が通らない → 2 workerの競合。worker数1でのstreaming上限と比べ、`StageMux` / `commitStage`の頻度を下げる（stageの粒度）か、chunkの分配を変える。
- byte不一致 → emitの並び替えの取り違え。`reduce_model_check.py`は縮約部だけなので、fast部の照合をhost `Reference`（sample-major）で取る。

## 方法

- firmware: E117をfork（library pinは同じ）。手順: (1) **内訳**: `Q` benchを「3 raw＋13 hold/128」（縦約費用≈0、fast部だけ）でF=1〜8（16-bit）、8-bitは「F raw＋(8−F) hold/128」で取る。E117 firmwareのまま取れる（同一code）。(2) **streaming内訳**: W16を`--no-check`で50 / 55 / 60、check込みで50 / 55、`--internal --single-core --no-check`で35 / 40 / 45、`--internal`（2 worker）で50 / 55 / 60。(3) fast部の書き換え（word emit、群のまとめ処理）→ bench再測 → streaming再測。(4) 2 worker競合の切り分けが要れば`codec0_task_us` / `codec1_task_us`と`stage_waits`。
- host: E117の`host_descriptor.py`（検証はcapture後）。
- 記録: bench（version別）、streaming上限（check有無、worker数）、core idle、byte一致。

## 対象外

any / edgeの縮約pass融合（別実験）、固定profileの変更、USB経路（E116で確定）。

## 必要な環境 / ベンチ種別

第三P4（esp32-p4-80f1b2d0b261）、PC直結usbipd/WSL、`EspUsbDevice (2.4.0)`。一時。着手前にEspUsbDevice側sessionへ板の使用を宣言する。

## 記録する数値 / 完了条件

fast部のbench（F別、版別）、W16のstreaming上限（check有無、worker数別）、byte一致。**製品モード（`--no-check`）でW16 60 Msps byte一致（60 periods）＋25 s soak**で完了。届かない場合は内訳表とその理由を記録して完了。

## 影響

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.1 / §1.3 / Phase D、[sample rateの選び方](../../references/p4-sample-rate-selection.ja.md)、[E115](../e115_p4_grouped_plane_transpose/README.ja.md) §4。

## 結果（2026-09-16、ログ `_runs/_runs/E118_20260916T072939JST_p4_direct_pin240/sweep.log`、library `EspUsbDevice (2.4.0)` pin、`--clean`）

### 1. 内訳（fast部だけのbench、3 raw＋13 hold/128で縮約≈0）

| F | 16-bit Msps/core | 8-bit Msps/core |
|---|---:|---:|
| 1 | 73.1 | 98.3 |
| 2 | 83.8 | 105.0 |
| 3 | **66.6**（690 cycle/block） | 84.1 |
| 4 | 65.4 | 75.2 |
| 5 / 6 | 60.9 / 62.4 | — / 62.1 |
| 7 / 8 | — / 53.7 | 55.8 / — |

W16（3 raw＋hold/8＋12 hold/64）47.5＝970 cycle/block の内訳: fast部 690、hold/8 1 channel（E114経路、16値）**約200**、hold/64 12 channel群（転置）約80。L1-cold入力（ringを歩くbench）は−5〜8%（47.0→44.8）で、入力のcache missは主因でない。

### 2. streaming側の内訳（cycle counter、単core、W16 30 Msps、予算1,536 cycle/block）

| 項目 | cycle/block | 備考 |
|---|---:|---|
| `encode()` | **980** | benchと同じ。core 0側は割り込みを含み1,400 |
| `writebackLines`（`esp_cache_msync` C2M）＋`commitStage` | **155** | runあたり約1,300 cycle。runはchunkとstage境界で切れ、平均8.4 block |
| `xQueueReceive`（chunkごと） | **158** | chunk（2,368〜4,032 byte＝約15 block）あたり約6 µs。blockingを含む |
| その他（跨ぎcopy、atomics、loop、先読み） | 約215 | 先読み（volatile load）はencodeを−43、その他を＋58で差し引き0 |

単coreは30 Mspsで98% busy＝1,508 cycle/block。**codecは980しか使っておらず、周辺が520**。これが「benchの0.58倍しかstreamingで出ない」正体。

### 3. 変更と効果（いずれも製品モード`--no-check`、内部sink）

| 変更 | 単core上限 | 2 worker上限（内部） | 備考 |
|---|---|---|---|
| E117のまま | 30 ○ / 40 × | 55 ○ / 60 × | |
| (a) runごとの`esp_cache_msync`を撤去（`E118_NO_WRITEBACK=1`） | — | — | W16 40 / 8-bit F3 60 / F4 50をhost全照合（491k block×3）で**bad 0**。idle 10 / 17% → 22 / 26%。TinyUSBがarm時に行う`dcd_dcache_clean`で足りている。**根拠: ESP32-P4のL1データキャッシュは2 coreで1つ（共有）**。SDKの`hal/esp32p4/include/hal/cache_ll.h`で、命令キャッシュは`CACHE_L1_ICACHE0_AUTOLOAD_CTRL_REG` / `ICACHE1`とcoreごとに分岐する（72〜76行）が、データキャッシュは`CACHE_L1_DCACHE_AUTOLOAD_CTRL_REG`1つで分岐がなく（94行）、`cache_ll_l1_writeback_dcache_addr()`は`Cache_WriteBack_Addr(CACHE_MAP_L1_DCACHE, vaddr, size)`を呼ぶだけでcore選択がない（644〜647行）。したがってusbd task（core 0）の`dcd_dcache_clean`はcore 1のworkerが書いた行も書き戻す。write-throughではない（`SOC_CACHE_WRITEBACK_SUPPORTED 1`）。EspUsbDevice側sessionが突き止め、2.4.0の`writeDirect()`契約から「別coreで書いたらC2M」を外した（先方commit `4e9330a`、docsのみ） |
| (b) RX callbackで隣接DMA nodeを結合（≤16 KiB、ring wrapと目標到達で押し出し） | **30で77% busy、40 ○** | **55 / 60 / 65 ○** | chunk数32,768→9,638（3.4分の1） |

### 4. 到達点（USB、byte一致、2.4.0 pin）

| 構成 | 結果 |
|---|---|
| **W16 60 Msps、製品モード** | **byte一致（214 Mbps）、25 s soak 434 MB欠損0**（退避1回27 KB、間隙1.3 ms以下）、idle 3% / 11% |
| W16 60、device Gray check込み | byte一致（idle 0.4% / 0.3%、上限いっぱい） |
| W16 65 / 70 | 65は欠損0（開始位相flake）、70はring溢れ → **上限65** |
| F4（4 raw＋12 hold/32）60 | **byte一致**（262.5 Mbps、idle 5% / 9%） |
| F5 60、8-bit F3 100 | 欠損0（開始位相flake） |
| 固定five 60 / eight 100 / eight 108 | core 0 busy 99.6→**83.5%** / 97→**84.5%** / 91%、欠損0。固定profileにも同じ効果 |

`codec_limit`の係数は1.0×0.9→**1.35×0.9（＝bench×1.215）**に改めた: W16 57、F4 60、F5 57、8-bit F3 90、any6 33、edge12 17、配線順不同56、mix 11 Msps。各上限でのstreamは全部欠損0・idle 5〜40%（W16 57は4.5% / 19%）。

### 5. loopback源の開始位相flakeの規則

byte一致検証の`find_start`は、rateが60 / 50 / 40 / 80 / 100では通り、57 / 56 / 45 / 33 / 17 / 65 / 90 / 150では「開始位相が見つからない」。Gray源はPARLIO TXをrate/4で回すので、rate/4の分周が正確に作れないrateでは源とサンプリングの位相関係が固定されず、1,024位相のどれにも合わない。deviceの欠損counterはこれらのrunでも0なので、**data pathの問題ではなく検証fixtureの限界**。上限の確認にはこれらのrunの「欠損0＋idle」を使い、byte一致は分周が正確なrateで取る。

### 6. 判定

- 仮説「fast部が主因」は**半分外れ**: fast部は690 cycle/blockで見積どおりだったが、60 Mspsに届かなかった本当の原因はworker周辺の520 cycle/block（queue受信とcache書き戻し）で、codecには触らずに完了条件を満たした。fast部の見直し（word emit、hold/8単独channelの200 cycle）は次の余地として残す（上限65→70以上へ）。
- 仮説「製品モードなら55が通る」は成立（55 / 60とも通った）。
- **完了条件（製品モードでW16 60 Msps byte一致＋25 s soak）を満たした。** 固定profileの余裕も増えた（five 60が「上限いっぱい」から84%へ）。

## 追記（2026-09-16）: 板の貸し出しは取り下げになった（書き込みなし）

第三 P4（`esp32-p4-80f1b2d0b261` / `/dev/ttyUSB2`、HS は Windows busid `1-7`）を、EspUsbHost の `end()` 回帰修正のリリース前テスト（全スイートを `--clean`）のために貸し出した（持ち主の指示、先方 session 経由）。**この実験の firmware は上書きされる。** 貸出時の状態は `303a:4021` / serial `e104-p4-windows-v1` で E118 firmware、usbipd は detach 済み（`1-7` は `Shared`）。

**再測するときは E118 を焼き直す。** `build_opt.h` を含むので `arduino-cli compile --clean` が要る。usbipd の再 attach は `usbipd attach --wsl --busid 1-7`（管理者権限不要）。先方の firmware が別の VID:PID で列挙して bind が外れていた場合、再 bind は管理者権限が要るので持ち主へ依頼する。

**取り下げ（同日）**: 先方が `pytest --collect-only` で確認したところ、`--clean` の全テストが収集するのは 42 件（`peer/` 23、`unit/` 12、`harness/` 6、`loopback/` 1）で、**`probe/` と `manual/` は収集対象外**だった。`TEST_SERIAL_PORT_ESP32P4` を使うテストは 1 件も走らない。**この板には一度も書き込まれていない。E118 firmware はそのまま載っている。** usbipd も再 attach して貸出前の状態（`1-7` が WSL へ Attached）に戻した。**焼き直しは不要。**

副産物として、`probe/p4_hs_host` / `p4_hs_device` / `p4_hs_fs_hub` は P4 の HS ポートを相手板へ繋ぐ前提なので、**HS が PC 直結のこの板では配線変更なしに走らない**ことが先方と共有できた（先方から持ち主へ連絡する）。
