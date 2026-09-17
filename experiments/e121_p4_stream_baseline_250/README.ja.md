# E121 製品向けに公開している構成を、EspUsbDevice 2.5.0（pin＋ELF証明）で取り直す

状態: **完了 — 4構成すべてbyte一致、推奨構成の約4.8分soakも欠損0**（2026-09-17）

規則: [実測の規則](../README.ja.md) / 台帳: [LEDGER](../LEDGER.ja.md) / 先行: [E118](../e118_p4_generic_fast_path/README.ja.md)（同じfirmware、2.4.0 pinだが出所を証明できない）、[E120](../e120_p4_usb_baseline_250/README.ja.md)（USB天井を同じ方法で取り直した）

## 問い

**[ロードマップ §1.1](../../references/p4-probe-roadmap.ja.md)で製品の推奨値として公開している構成は、EspUsbDevice 2.5.0（Library Managerからpin、`dir:`なし、実リンク版をELFで証明）でもbyte一致で通るか。**

## 背景

持ち主の指示（2026-09-17）: **`dir:`で取った数値は再現性がないので使えない。正式値はリリース版をpinしたもの。** [E120](../e120_p4_usb_baseline_250/README.ja.md)でUSB天井は取り直したが、**構成別の数値（§1.1の表）はE117 / E118に依存したままだった**。この実験でそこを埋める。

## 方法

- firmware: [E118](../e118_p4_generic_fast_path/README.ja.md)をそのまま（marker名のみ変更）。**sketch.yamlは`EspUsbDevice (2.5.0)` pin 1本だけ**（`dir:` profileを置かない）。`build_opt.h`はE118と同じ6行。`arduino-cli compile --clean`。
- **出所**: `strings <elf>` → **`EspUsbDevice_2.5.0_55353dc9af97a625`**（E120と同一のstaging dir hash）。`_runs/E121_*/linked_library.txt`。
- host: `host_descriptor.py`（E118と同形。検証はcapture後）。製品モード（`--no-check`）。
- 構成: §1.1で公開している4つ。

## 結果

生ログ: `_runs/E121_20260917T*_p4_stream_pin250/`。

| 構成 | 公開値 | 実測（2.5.0 pin） | 判定 |
|---|---|---|---|
| **16 ch: 4 raw＋12 hold/32（F4）60 Msps** ＝推奨構成 | 262.5 Mbps | wire 262.5 Mbps、host 256.4 / 255.6 Mbps、`checked_blocks=1310635`、`bad_blocks=0`、`short=0` | **byte一致**（2回） |
| 16 ch: 3 raw＋hold/8＋12 hold/64（W16）60 Msps | 214 Mbps（上限） | wire 198.75 Mbps、host 193.6 Mbps、`checked_blocks=1310655`、`bad_blocks=0` | **byte一致**（`--no-codec-limit`が要る。`codec_limit=57`） |
| 8 ch: 3 raw＋5 hold/64 80 Msps ＝推奨 | 249 Mbps | wire 250 Mbps、host 243.9 Mbps、`bad_blocks=0` | **byte一致** |
| 2 ch 素通し 160 Msps | 319 Mbps（上限） | wire 320 Mbps、host 312.7 Mbps、`checked_blocks=1310720`、`bad_blocks=0` | **byte一致** |

deviceのcounterは全runで`raw_sequence_bad=0` / `spill_bytes=0` / `arm_failures=0` / `queue_overflow=0` / `fifo_overflow=0` / `short_chunks=0` / `stage_waits=0`、`direct_supported=1` / `last_direct_error=None`。

### F4 60の1回目は開始位相が見つからなかった

1回目だけ `first block does not match any start phase of the loopback source` で落ちた。ただし **device側は`sent=91750400`、host側は`received=91750400`で完全に一致**し、deviceのcounterも全部0だった。**stream自体は無傷で、loopback治具の開始位相探索だけが失敗した**（[E114](../e114_p4_dynamic_descriptor/README.ja.md) / [E115](../e115_p4_grouped_plane_transpose/README.ja.md)で記録済みのflake）。再走2回はどちらもbyte一致。

### host側の経路が重かった

全runで`host_urb_gaps_ms`が **28〜45 ms**あった（E118では1.3 ms以下）。同じマシンで別sessionがUSBのテストを回していた時間帯にあたる。**それでもbyte一致は崩れなかった**——deviceは埋め切っていて、hostの取りこぼしもない。**host側のMB/sはこの影響を受ける**ので、wire値（deviceが出した速度）と分けて読む。

### soak（推奨構成、約4.8分）

`_runs/E121_*/soak_f4_60_full.log`。**16 ch F4 60 Msps（262.5 Mbps）を16,000 period連続。**

| 項目 | 値 |
|---|---|
| 時間 / 量 | 287.7 s / **9.18 GB**（host受信9,175,040,000 byte＝deviceの`sent`と完全一致） |
| host検証 | `ok=1`、`checked_blocks=3,834,544`、**`bad_blocks=0`**、`short=0` |
| device counter | `spill_bytes=0` / `spill_overflow=0` / `arm_failures=0` / `queue_overflow=0` / `fifo_overflow=0` / `raw_sequence_bad=0` / `duplicate_bad=0` / `short_chunks=0` / `stage_waits=0` |
| device側の最大間隙 | `completion_gap_us_max=751 µs` |
| core空き | core 0 **5.2%** / core 1 **9.3%**（idle 14.6 s / 25.9 s ÷ 279.6 s） |
| host側URB間隙 | **最大102.7 ms**（87.4 / 87.1 / 74.6 ms が続く） |

**host側のURB間隙が100 msを超えても欠損0だった。** deviceは転送を埋め切り、hostも取りこぼしていない。[E117](../e117_p4_stream_release_api/README.ja.md)の同構成のsoak（279.6 s / 9.2 GB / 欠損0）と同じ結果である。

## 事実 / 候補 / 未決

- **事実**: 公開している4構成は、2.5.0 pin（ELF証明）でbyte一致する。E118（2.4.0）と同じ結果
- **事実**: W16 60は`codec_limit`（57）を超えるので、deviceは既定でREJECTする。60で通すには`--no-codec-limit`が要る。**製品としては57が推奨、60は「取れる場合もある」**という §1.1 の書き方と整合する
- **事実**: host側が重い（URB間隙28〜45 ms）状態でもbyte一致は保たれる
- **事実**: 推奨構成の約4.8分soakは欠損0（9.18 GB、`bad_blocks=0`、device counter全部0、core空き5.2 / 9.3%）。**host側URB間隙が最大102.7 msでも崩れない**
- **事実（コード確認、2026-09-17）**: EspUsbHost側sessionが自分のUVC転送プールで見つけた`read-then-do`（flagをlockなしで読んでからsubmitし、その間にteardownがfreeする）と同じ形が当方のfirmwareにないかを確認した。**無い。**
  - stage free listは`portENTER_CRITICAL(&StageMux)`の中で「空きbitを見る→落とす→slot tableに記録」を一括で行う**claim-then-use**。`releaseStage()`も同じlockでbitを戻す
  - `dispatchStage()`は`spillUsed() == 0 && armQueued() < kDirectDepth`をlockなしで読むが、**spillはusbTaskしか触らない**ので判定は正しく、**`armQueued()`はTX完了callback（usbd task）が減らす方向にしか動かさない**。つまり古い値を読んでも「直接armできるのにspillへ回す」方向にしか外れず、順序は崩れない（**spillが空のときだけ直接arm**するので、先行blockを追い越さない）
  - **判断基準**: lockが無いこと自体は危険を意味しない。**stale readで値がどちらへ動くか**を見る。`videoStopping`のような`false → true`は「相手がもう駄目と決めた後に実行する」ので危険、`armQueued()`のような減る一方の値は速い経路を1回逃すだけで安全
  - **この確認は一度やって終わりではない。** 先方は2.9.4の監査で「他に同じ形はない」と結論した直後、新しく書いたUVCのpoolで自分で再導入している。**firmwareに手を入れたらこの3点（spillの専有、`armQueued()`の単調性、spillが空のときだけ直接arm）を見直す。**1つでも崩れたら結論が変わる
- **未決**: fixed profile（five 60 / eight 100）の2.5.0での取り直し。ただしどちらも予算超えで製品説明に載せていないので優先度は低い

## 反映

[P4ロードマップ](../../references/p4-probe-roadmap.ja.md) §1.1の裏付け列をE121へ。E118は「出所を証明できない」注記つきで残す。
