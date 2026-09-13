# 実験台帳

内部の記録。日本語のみ。規則は [README.ja.md](README.ja.md)。

観測した**事実**と、その場だけの**候補**、残った**未決**を数値で残す。**実験が通ったことを仕様の承認とはみなさない** — 仕様([../protocols/](../protocols/))の status を動かすのは [README.ja.md §6](README.ja.md) の証拠水準を満たしたときだけ。

## 読み方

- **事実** — 観測から直接言えること。解釈を混ぜない
- **候補** — その実験の中だけで試した方式。採用ではない
- **未決** — 残った疑問。次の実験 ID か [../coverage.ja.md](../coverage.ja.md) の穴番号に結び付ける
- **未測定** は `—` と書く。空欄にしない(合格と区別する)

## 1. 採番済み

機材が用意できることを確認したものだけがここに来る([README.ja.md §3.1](README.ja.md))。ID は再利用も再採番もしない。

| ID | 問い | ベンチ | 影響する doc | 状態 |
|---|---|---|---|---|
| **E001** | 実機なしで sketch を build・実行し、出力を assert できるか(profile 解決 / `socket://localhost` / 銘板) | **常設 v0**(実機なし。host Arduino core `lang-ship:host:host`) | (規則そのもの) | **完了**([e001_smoke_host/](e001_smoke_host/README.ja.md)) |
| **E002** | 実機 1 枚で upload → monitor → assert が通り、実時間が取れるか | **常設 v1**(peer 対の HOST 側 `esp32-s3-d0cf1359101c`) | (規則そのもの) | **完了 — 反証**([e002_smoke_board/](e002_smoke_board/README.ja.md)) |
| **E003** | peer は使えるか(**環境確認のみ**)— 2 台同時 upload / 2 台の間で実際に繋がっている線はどれか | **常設 v2**(peer 対 2 枚、配線変更なし) | [README.ja.md §4.4/§4.5](README.ja.md) | **完了**([e003_smoke_peer/](e003_smoke_peer/README.ja.md)) |
| **E004** | host からの trigger で応答させれば、起動時出力の取りこぼし(E002)を回避して銘板と実時間を取れるか | **常設 v1** | (規則そのもの)+ [dmi-bridge](../protocols/dmi-bridge.ja.md) §4.1 の裏付け | **完了**([e004_smoke_board_trigger/](e004_smoke_board_trigger/README.ja.md)) |
| **E005** | 2 線でクロック同期の bit 列を取り込めるか。取り込める最短のクロック周期は(**道具の性能測定**) | **常設 v2** | (道具)→ 候補 `wire-bitstream` の機材要件 | **完了**([e005_tool_clocked_capture/](e005_tool_clocked_capture/README.ja.md)) |
| **E006** | RMT で SWIO 相当のパルス幅(290 / 890 ns)を生成・測定できるか。分解能は(**道具の性能測定**) | **常設 v2** | (道具)→ 候補 `swio-threshold` の機材要件 | **完了**([e006_tool_pulse_capture/](e006_tool_pulse_capture/README.ja.md)) |
| **E007** | RVSWD host 位相の 42 bit(addr7+data32+op2+parity)を線上に出したとき、[link-to-target](../protocols/link-to-target.ja.md) §3 の仕様どおりか | **常設 v2** | [link-to-target](../protocols/link-to-target.ja.md) §3(**status は動かさない**) | **完了**([e007_wire_rvswd_frame/](e007_wire_rvswd_frame/README.ja.md)) |
| **E008** | SWIO write フレーム(start+addr7+rw+data32=41 bit)をパルス幅で出し、幅から復号できるか | **常設 v2** | [link-to-target](../protocols/link-to-target.ja.md) §3/§5(**status は動かさない**) | **完了**([e008_wire_swio_frame/](e008_wire_swio_frame/README.ja.md)) |
| **E009** | 実験の生ログを `_runs/` へ自動退避できるか。失敗した run でも残るか | **常設 v0**(実機なし) | [README.ja.md §3.4](README.ja.md) | **完了**([e009_runs_archive/](e009_runs_archive/README.ja.md)) |
| **E010** | 1 つの実験ファイルに複数のテスト関数を置けるか。置けないならその制約は何によるか | **常設 v0 + v1** | [README.ja.md §1.3](README.ja.md) | **完了**([e010_dut_scope/](e010_dut_scope/README.ja.md)) |
| **E011** | `test_` を付けない規約は、実験が 10 本を超えた実プロジェクトでも誤爆から守れているか | **常設 v0**(実機なし) | [README.ja.md §1.3](README.ja.md) | **完了**([e011_collection_guard/](e011_collection_guard/README.ja.md)) |
| **E012** | 銘板の版情報を conftest から build 時に自動で埋められるか。再ビルドのコストは | **常設 v0**(実機なし) | [README.ja.md §5](README.ja.md) | **完了**([e012_banner_autofill/](e012_banner_autofill/README.ja.md)) |
| **E013** | 同一VID:PID・異なる`bcdDevice`でHID-onlyとHID + Vendor + CDC × 1をWindowsが分離でき、Linuxでも各経路が通信できるか | **一時・専用機材**(別途用意するESP32-S3 native USB、Windows 11、Linux) | [probe-feasibility-gates](../references/probe-feasibility-gates.ja.md) Gate 2 / Gate 4 | **中断 — 前提を机上調査で反証、E062へ**([e013_usb_descriptor_profiles/](e013_usb_descriptor_profiles/README.ja.md)) |
| **E014** | ESP32-P4で8本のGPIOをLEDC出力に使ったまま、同じGPIOをPARLIO RXへ接続して配線なしで同時captureできるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **中断 — 選んだ初期化方法は反証**([e014_p4_parlio_internal_capture/](e014_p4_parlio_internal_capture/README.ja.md)) |
| **E015** | `io_loop_back`を使わず入力だけを追加するか、PARLIO→LEDCの順にすれば、同じ8 GPIOでLEDC PWMとPARLIO RXを共存できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了**([e015_p4_parlio_routing_order/](e015_p4_parlio_routing_order/README.ja.md)) |
| **E016** | Arduino環境からESP-IDF PARLIO driverを直接呼び、有限長PARLIO RXのDMA先をPSRAMにして8-bit captureできるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 直接DMA成立、cache警告あり**([e016_p4_parlio_psram_direct/](e016_p4_parlio_psram_direct/README.ja.md)) |
| **E017** | stock PARLIO driverでPSRAM有限長captureのdescriptor cache警告を避けられるburst size / capture size条件はあるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — burst sizeでは回避不能**([e017_p4_parlio_psram_cache_sync/](e017_p4_parlio_psram_cache_sync/README.ja.md)) |
| **E018** | `cache` tagのruntime logを抑制し、stock PARLIO driverで1 MiB PSRAM direct captureを正しく完了できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — soft delimiter上限で前提反証**([e018_p4_parlio_psram_log_suppression/](e018_p4_parlio_psram_log_suppression/README.ja.md)) |
| **E019** | `partial_rx_en`で1 MiB PSRAMをdirect ringにし、一周を検出して公開APIだけで停止後、正しい連続captureを得られるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — cache errorでInterrupt WDT**([e019_p4_parlio_psram_partial_ring/](e019_p4_parlio_psram_partial_ring/README.ja.md)) |
| **E020** | internal RAM↔PSRAM copyは4 / 16 / 64 KiB chunkで8-bit logic captureを退避できる実効帯域を持つか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — flush込み138.6〜182.7 MB/s**([e020_p4_psram_copy_bandwidth/](e020_p4_psram_copy_bandwidth/README.ja.md)) |
| **E021** | 8 MHz / 8-bit PARLIO RXを64 KiB internal ringへ連続取得し、taskから1 MiB PSRAMへdropなしで退避できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — receiver再生成で3/3成功**([e021_p4_parlio_psram_spool/](e021_p4_parlio_psram_spool/README.ja.md)) |
| **E022** | E021のinternal ring→PSRAM経路は80 MHz / 8-bit / 1 MiBでもdropなしで3回captureできるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 79.6 MB/s、3/3成功**([e022_p4_parlio_spool_80mhz/](e022_p4_parlio_spool_80mhz/README.ja.md)) |
| **E023** | 80 MHz captureと同時にSUMP基本trigger相当のpattern/mask・edge条件を全sampleでsoftware検索してもdropしないか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 15.1〜24.6 MB/s、overflow**([e023_p4_sump_basic_trigger_80mhz/](e023_p4_sump_basic_trigger_80mhz/README.ja.md)) |
| **E024** | 32-bit word内の4 sampleを並列検索すれば、80 MHz captureと同時にpattern/mask・edgeを全sample評価してdropを避けられるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 25.6〜31.0 MB/s、overflow**([e024_p4_sump_swar_trigger_80mhz/](e024_p4_sump_swar_trigger_80mhz/README.ja.md)) |
| **E025** | 32-bit software基本trigger付き8-bit captureがdropなしで成立するsample rate境界はどこか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 全条件24 MHz、28 MHzで分岐**([e025_p4_sump_trigger_rate_boundary/](e025_p4_sump_trigger_rate_boundary/README.ja.md)) |
| **E026** | 20 MHz captureをtrigger後の指定sample数で停止し、25/75・50/50・75/25のpre/post windowを構成できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 3比率、停止誤差<1 chunk**([e026_p4_sump_prepost_stop/](e026_p4_sump_prepost_stop/README.ja.md)) |
| **E027** | 1 MiB PSRAM circular ringを複数回wrapした後も20 MHzで50/50 pre/post windowを再構成できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 1/2/4 wrap、3回連続成功**([e027_p4_sump_circular_pretrigger/](e027_p4_sump_circular_pretrigger/README.ja.md)) |
| **E028** | pattern・edge・occurrence countを組み合わせた4-stage software triggerからpost取得・停止まで成立するか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 16 MHz、固定4-stage 3/3成功**([e028_p4_sump_four_stage_trigger/](e028_p4_sump_four_stage_trigger/README.ja.md)) |
| **E029** | circular pre-trigger ringを1/2/4 wrap × 10組、独立reset 2回の計60 captureで再現性確認できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 | **完了 — 独立reset 2回、60/60成功**([e029_p4_circular_ring_soak/](e029_p4_circular_ring_soak/README.ja.md)) |
| **E030** | 20 MHz / 8-bitを64 KiB internal ringから16 MiB PSRAMへ連続退避し、dropやdata化けなしで保持できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) batch capture候補 | **完了 — 16 MiB、19.995 MB/s**([e030_p4_deep_batch_capture/](e030_p4_deep_batch_capture/README.ja.md)) |
| **E031** | PARLIO RXの1 / 2 / 4 / 8 / 16 data lineでpackingを復元し、各幅1,048,576 sampleを8 MHzでdropなく取得できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) channel幅 | **完了 — 全5幅成立**([e031_p4_parlio_channel_width/](e031_p4_parlio_channel_width/README.ja.md)) |
| **E032** | 1 / 2 / 4 / 8 / 16 channelのtriggerなしbatch rate境界は20 / 40 / 80 / 120 / 160 MHzのどの区間か | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — 1/2/4ch 160 MHz、8ch 80 MHz、16ch 40 MHz成立**([e032_p4_parlio_width_rate_coarse/](e032_p4_parlio_width_rate_coarse/README.ja.md)) |
| **E033** | 8ch 84〜120 MHz、16ch 44〜80 MHzのtriggerなしbatch境界は4 MHz刻みでどこか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — 8ch 100 MHz、16ch 48 MHz成立**([e033_p4_parlio_width_rate_fine/](e033_p4_parlio_width_rate_fine/README.ja.md)) |
| **E034** | ADC1の1 / 2 / 4 / 8chを最大83,333 conversion/sでcontinuous DMA取得し、channel IDを保ってPSRAMへ退避できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) analog | **完了 — 全12条件成立、最大rateでは非逐次channel順**([e034_p4_adc1_continuous_batch/](e034_p4_adc1_continuous_batch/README.ja.md)) |
| **E035** | ADC1最大rateのchannel ID列を特定し、ADC2単独・ADC1+ADC2 continuous modeを利用できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) analog topology | **計画**([e035_p4_adc_topology_order/](e035_p4_adc_topology_order/README.ja.md)) |
| **E036** | PARLIO TXの連番rampを源にしring未読量でdropを直接検出すると、8 channel triggerなしbatchのdropなし境界はどこか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — 律速はspool側、1 Mi burstは104 MHz成立**([e036_p4_parlio_rate_seq_verify/](e036_p4_parlio_rate_seq_verify/README.ja.md)) |
| **E037** | PARLIO RX pulse delimiterで4 data channel + 1 valid lineを構成し、hardware pulseでframe開始・`eof_data_len`停止・hardware timeoutが成立するか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) trigger | **完了 — hardware trigger成立、arm待ちtimeoutは非対応**([e037_p4_parlio_pulse_trigger/](e037_p4_parlio_pulse_trigger/README.ja.md)) |
| **E038** | pulse delimiterによるhardware trigger付き有限frameは、どのsample rateまで欠落なく成立するか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) trigger | **完了 — 160 MHzまで成立、上限は内部clock源**([e038_p4_parlio_pulse_trigger_rate/](e038_p4_parlio_pulse_trigger_rate/README.ja.md)) |
| **E039** | level delimiterでenable線がactiveな間だけ取得するhardware gatingと、`eof_data_len`=0による可変長frameが成立するか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) trigger | **完了 — gating成立、可変長は不成立**([e039_p4_parlio_level_gate/](e039_p4_parlio_level_gate/README.ja.md)) |
| **E040** | `eof_data_len`=0のlevel delimiterでDMAはpayloadへ書くのか。`partial_rx_en`との組で「hardware gate + software停止」modeになるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) trigger | **完了 — DMAは走る、gateはdutyどおり間引く**([e040_p4_parlio_level_open_frame/](e040_p4_parlio_level_open_frame/README.ja.md)) |
| **E041** | `valid_gpio_num`をdata線と同一GPIOにして、8 channel全部を残したままhardware edge triggerを使えるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) trigger | **完了 — 共有成立、triggerはchannelを消費しない**([e041_p4_parlio_shared_valid_line/](e041_p4_parlio_shared_valid_line/README.ja.md)) |
| **E042** | 16 channelのtriggerなしbatchをsample単位検証とring未読量で測ると、dropなし境界はどこか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — 48 MHz成立、複製lane不一致を検出**([e042_p4_parlio_16ch_seq_verify/](e042_p4_parlio_16ch_seq_verify/README.ja.md)) |
| **E043** | hardware gateで間引かれたstreamから、gate windowの境界と長さを復元できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 内部圧縮 | **完了 — window長は決定論的、境界は自己記述されない**([e043_p4_parlio_gate_window_boundary/](e043_p4_parlio_gate_window_boundary/README.ja.md)) |
| **E044** | gate線を同時にRMT RXへ入力して、window境界の長さと間隔をhardwareで記録できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 内部圧縮 | **中断 — GPIO共有は成立、RMTがsymbolを返さず**([e044_p4_gate_rmt_timestamp/](e044_p4_gate_rmt_timestamp/README.ja.md)) |
| **E045** | RMT RXがgate線のdurationを返すのは、どのperipheral生成順とどの回収時間か | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 内部圧縮 | **完了 — 原因は回収時間、durationは期待値と完全一致**([e045_p4_gate_rmt_order/](e045_p4_gate_rmt_order/README.ja.md)) |
| **E046** | 幅が可変なgateでRMTが各window長を返し、そこからPARLIO側の境界位置を予測できるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 内部圧縮 | **完了 — 可変幅でも境界を完全復元**([e046_p4_gate_variable_width/](e046_p4_gate_variable_width/README.ja.md)) |
| **E047** | 1本のGPIOをPARLIO data線・valid線・RMT RXの3者へ同時に渡し、8 channel + qualification + timestampが8 pinで成立するか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 内部圧縮 | **完了 — 3者共有成立、8 pinで成立**([e047_p4_gate_three_way_share/](e047_p4_gate_three_way_share/README.ja.md)) |
| **E048** | 3者共有qualification構成でgated captureがdropなしで成立する最大sample rateはどこか。約98 MB/sを超えるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — 160 MHzまで成立、gateはrate上限を上げる**([e048_p4_gated_rate_ceiling/](e048_p4_gated_rate_ceiling/README.ja.md)) |
| **E049** | gated captureの成立条件はmodel `window byte長 × (1 − spool ÷ rate) < ring容量`で決まるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — modelは反証、境界は平均byte rate**([e049_p4_gated_window_absorption/](e049_p4_gated_window_absorption/README.ja.md)) |
| **E050** | dutyを50%に固定してwindow長だけを振ると、window長はgated captureの成立に影響するか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — window長は無関係、条件は平均byte rateのみ**([e050_p4_gated_window_at_fixed_duty/](e050_p4_gated_window_at_fixed_duty/README.ja.md)) |
| **E051** | gate loop周期2.4 msでRMTが`on_recv_done`を発火する条件はuser buffer・`mem_block_symbols`・回収時間のどれか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 内部圧縮 | **完了 — buffer・blockは閾値でない、正体未特定**([e051_p4_rmt_partial_threshold/](e051_p4_rmt_partial_threshold/README.ja.md)) |
| **E052** | RMTの`on_recv_done`は何ms後に最初に発火しどの間隔で何symbolずつ届くか。PSRAM copy loopのCPU飽和は影響するか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 内部圧縮 | **完了 — 初回48 symbol・以降24ごと、CPU負荷は無関係**([e052_p4_rmt_callback_timing/](e052_p4_rmt_callback_timing/README.ja.md)) |
| **E053** | RMT RXをDMA modeにすると`mem_block_symbols`を48より小さくして初回遅延を縮められるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 内部圧縮 | **完了 — DMAでは縮まらず、規則が完成**([e053_p4_rmt_dma_block/](e053_p4_rmt_dma_block/README.ja.md)) |
| **E054** | RMT DMA modeが受理する`mem_block_symbols`の最小値はいくつで、初回遅延はnon-DMAより良いか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 内部圧縮 | **完了 — DMAでは縮まらず、下限は`48 × 周期`**([e054_p4_rmt_dma_block_min/](e054_p4_rmt_dma_block_min/README.ja.md)) |
| **E055** | gated captureの緩衝は64 KiB ringか64 entry queueか。driverはringを周回して使っているか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — 緩衝はqueue、ringは線形**([e055_p4_gated_buffer_source/](e055_p4_gated_buffer_source/README.ja.md)) |
| **E056** | ring容量をpattern周期の倍数から外すと、未読 > ring容量の条件で破損が現れるか(検証器のalias 疑い) | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — alias で盲だった、ringは実際に上書きされる**([e056_p4_ring_period_alias/](e056_p4_ring_period_alias/README.ja.md)) |
| **E057** | alias から外したringで、gated captureの条件2の境界はどこにあるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — 境界はgate 3,000〜4,000、容量は完全chunk数**([e057_p4_gated_ring_boundary/](e057_p4_gated_ring_boundary/README.ja.md)) |
| **E058** | window長を固定してsample rateを振ると、window中のdrain帯域は一定かrate依存か | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — drainはrate依存、未読に2 chunkの床**([e058_p4_window_drain_vs_rate/](e058_p4_window_drain_vs_rate/README.ja.md)) |
| **E059** | window中のdrain低下はmemcpy自体が遅いのか(memory競合)、memcpyに使える時間が減るのか(ISR overhead) | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — memcpy帯域は一定、原因は1 chunkあたり固定cost**([e059_p4_drain_breakdown/](e059_p4_drain_breakdown/README.ja.md)) |
| **E060** | 1 chunkあたりの固定costは`xQueueReceive`のまとめ取りと連続chunkのmemcpyまとめで下がるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — どちらも効かず、固定costはISRが支配**([e060_p4_drain_batch_coalesce/](e060_p4_drain_batch_coalesce/README.ja.md)) |
| **E061** | PARLIOのISRが走るcoreとmemcpyするcoreを分けると、window中のdrain帯域は上がるか | **一時・配線なし**(`esp32-p4-e8f60ae0aa24`) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) raw rate | **完了 — core分離でdrainが82.3→119.7 MB/s**([e061_p4_drain_core_split/](e061_p4_drain_core_split/README.ja.md)) |
| **E062** | 同一VID:PID・同一serialでinterface構成(HID単機能↔composite、末尾追加、interface番号の機能入替)を変えたとき、Windows 11はどの切替で既存devnodeとdriverを再利用するか。`bcdDevice`のみ・serialのみの変更は結果を変えるか。Linuxは全条件で再認識するか | **一時・専用機材**(E013と同じESP32-S3 native USB、Windows 11、Linux) | [probe-feasibility-gates](../references/probe-feasibility-gates.ja.md) Gate 2 / Gate 4、[usb-host-descriptor-persistence](../references/usb-host-descriptor-persistence.ja.md) | **計画**([e062_usb_same_identity_layout_change/](e062_usb_same_identity_layout_change/README.ja.md)) |
| **E063** | Arduino-ESP32 3.3.11でESP32-P4のUSB 2.0 OTG HS portをdeviceとして列挙でき、negotiateする速度はHSかFSか。USB-Serial-JTAG(FS)のconsole経路は生き続けるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはWindows 11へ接続) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) §後段、[harness-channels](../references/harness-channels.ja.md) §物理IF | **完了 — High-Speedで列挙、consoleも同時に生きる**([e063_p4_usb_hs_enumerate/](e063_p4_usb_hs_enumerate/README.ja.md)) |
| **E064** | PSRAM上のdataをUSB 2.0 HSのCDC bulk INでWindowsへ連続送出したとき、実効帯域は何MB/sか。chunk sizeと転送総量でどう変わるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはWindows 11へ接続) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) §後段、[harness-channels](../references/harness-channels.ja.md) §物理IF | **完了 — 約5.6〜5.7 MB/sで飽和、16 MiBが2.968秒**([e064_p4_usb_hs_cdc_rate/](e064_p4_usb_hs_cdc_rate/README.ja.md)) |
| **E065** | USB 2.0 HS上のCDCを2本同時に流したとき、合計帯域は1本の約5.6 MB/sより上がるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはWindows 11へ接続) | [harness-channels](../references/harness-channels.ja.md) §CDCの上限は endpoint 予算、[P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) §後段 | **完了 — 反証。2本でも合計は上がらない(比0.943)。ただし送出taskを分けた1本が7.94 MB/s**([e065_p4_usb_hs_dual_cdc_rate/](e065_p4_usb_hs_dual_cdc_rate/README.ja.md)) |
| **E066** | CDC 1本のdownload帯域は送出を実行するcontext(`loop()`か専用taskか、優先度、pin先core)で変わるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはWindows 11へ接続) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) §後段、[E065](e065_p4_usb_hs_dual_cdc_rate/README.ja.md)の事実5 | **完了 — 変わる。効くのは優先度ではなくpin先core。core 0で7.4〜8.1、core 1で5.2〜5.7 MB/s**([e066_p4_usb_hs_tx_context/](e066_p4_usb_hs_tx_context/README.ja.md)) |
| **E067** | PARLIO captureとUSB HS送出を同時に走らせると互いをどれだけ食うか。core配分で変わるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはWindows 11へ接続) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) §後段 | **完了 — captureは不変(8.00 MB/s、overflow 0)、USBのみ7〜16%低下。最良はharvest=core 1 / USB=core 0の7.42 MB/s。**副産物として転送末尾の間欠欠落を観測**([e067_p4_usb_vs_capture_core/](e067_p4_usb_vs_capture_core/README.ja.md)) |
| **E068** | USB HS CDCの転送でhostへ届かなかった分は、失われているのか滞留しているのか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはWindows 11へ接続) | [E067](e067_p4_usb_vs_capture_core/README.ja.md)「経路の異常」、[P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) §後段 | **完了 — 前提が誤り。欠落は転送の途中で、dataは失われる。30回中4回(13%)、512 Bの4〜5 packet**([e068_p4_hs_cdc_tail_loss/](e068_p4_hs_cdc_tail_loss/README.ja.md)) |
| **E069** | OTG HS上のvendor bulkの実効帯域は何MB/sか。CDCの約8 MB/sを超えるか。1 URBの大きさで変わるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ) | [harness-channels](../references/harness-channels.ja.md) §6c、[P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) §後段 | **完了 — usbip越しで9.73 MB/s。天井は`usbser`側だった**([e069_p4_hs_vendor_bulk_rate/](e069_p4_hs_vendor_bulk_rate/README.ja.md)) |
| **E070** | 同じvendor bulk構成をcore内蔵stackとEspUsbDevice 2.2.0で作ると、帯域・data完全性・descriptorの正しさはどう違うか | **一時・配線なし**(同上) | (P4でUSBを使う実験すべての土台) | **完了 — 帯域は互角(9.04 対 9.03 MB/s、clean buildで測り直し)、descriptor準拠とFIFOの自由度はEspUsbDevice**([e070_p4_hs_vendor_stack_compare/](e070_p4_hs_vendor_stack_compare/README.ja.md)) |
| **E071** | vendor bulkの送信FIFOを深くするとdevice側の帯域はどこまで伸びるか。天井はFIFOか別か | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ) | [EspUsbDeviceへの改修依頼](../references/espusbdevice-change-requests.ja.md) CR-4 / CR-7 | **完了 — 8 KiBで飽和(9.03 → 10.59 MB/s、+17%)。64 KiBはmountせず。host役の36.4 MB/sには遠い**([e071_p4_hs_vendor_fifo_depth/](e071_p4_hs_vendor_fifo_depth/README.ja.md)) |
| **E072** | P4を2枚HS port同士で直結し、PCを経路から外してdevice → hostのbulk INを測ると何MB/sか | **一時・要配線**(`...78` = device / `...f5` = host、OTG HS同士を直結) | [EspUsbHostへの改修依頼](../references/espusbhost-change-requests.ja.md) HR-1 | **完了 — 5.6 MB/s。直結の方が遅い。host側の継続INが512 B×depth 1のため**([e072_p4_hs_device_to_host_native/](e072_p4_hs_device_to_host_native/README.ja.md)) |
| **E073** | USB 2.0 HSのinterrupt endpoint(HID)でdevice → hostへ流せる実効帯域は何MB/sか。packet sizeでどう変わるか | **一時・要配線**(P4 2枚のOTG HS直結) | [harness-channels](../references/harness-channels.ja.md) §USBクラス8種の得失 | **完了 — 既定64 Bで0.52 MB/s、512 Bで4.14 MB/s。「HID = 64 kB/s」はFSの値**([e073_p4_hs_hid_throughput/](e073_p4_hs_hid_throughput/README.ja.md)) |
| **E074** | 2 channelのPARLIO captureを取り、hostで`.sr`に変換してsigrokが読み戻せるところまで通るか。どのrateまでsample単位の欠落なしか | **一時・配線なし**(`esp32-p4-30eda0e31478`、信号源は内部LEDC) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 限界matrix、[PulseView / sigrok 連携](../references/pulseview-integration.ja.md) | **完了 — 160 Mspsまでsample精度、16 Mi sampleの深さも通り、`.sr`をsigrokが読み戻す**([e074_p4_2ch_capture_to_sr/](e074_p4_2ch_capture_to_sr/README.ja.md)) |
| **E075** | PARLIOのchannel幅1 / 4 / 8で、どのsample rateまでsample単位の欠落なしに取れるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、信号源は内部LEDC) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 限界matrixの※ | **完了 — 1 / 2 / 4 chは160 Mspsでsample精度。8 chは96 MHzまで1 MiBで精度、160 MHzは`overflow=131`**([e075_p4_width_sample_accuracy/](e075_p4_width_sample_accuracy/README.ja.md)) |
| **E076** | captureしたdataをOTG HSのvendor bulkで降ろすと4 MiBのdownloadは何秒になり、sampleは落ちずに`.sr`まで通るか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ、信号源は内部LEDC) | [P4 USB HSまとめ](../references/p4-usb-hs-summary.ja.md) §5 / §7、[E074](e074_p4_2ch_capture_to_sr/README.ja.md)の残した律速 | **完了 — 4 MiBが平均0.48秒(8.80 MB/s)、console経路の12倍。7/7でsample精度。PSRAM読み出しは律速ではない**([e076_p4_capture_hs_download/](e076_p4_capture_hs_download/README.ja.md)) |
| **E077** | BeagleLogicのTCP protocolを演じるPython serverを置くと、stockのsigrok / PulseViewがP4のcaptureをIP経由で取れるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ、信号源は内部LEDC) | [PulseView / sigrok 連携](../references/pulseview-integration.ja.md) 経路B | **完了 — 取れる。4 M sample @ 80 MHzが0.45秒でsample精度。1回のcaptureを超える要求は継ぎ目が出る**([e077_p4_pulseview_over_ip/](e077_p4_pulseview_over_ip/README.ja.md)) |
| **E078** | PARLIO の capture を PSRAM に貯めずに OTG HS へ流したとき、欠落なく continuous に保てる sample rate の上限は何 Msps か | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ、信号源は内部LEDC) | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) 連続streamingの釣り合い点、[E077](e077_p4_pulseview_over_ip/README.ja.md)の継ぎ目 | **完了 — 86 Mspsまで継ぎ目なく降ろせる(線上21.5 MB/s)。88 Mspsからbacklogが時間に比例して積む。capture同居でもUSBは落ちない(`stalls`=0)**([e078_p4_continuous_stream/](e078_p4_continuous_stream/README.ja.md)) |
| **E080** | sigrok / PulseView が要求する sample 数を、1 回の capture として継ぎ目なく渡せるか。上限は E078 の 86 Msps と一致するか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ、信号源は内部LEDC) | [E077](e077_p4_pulseview_over_ip/README.ja.md)の未決「継ぎ目」、[PulseView / sigrok 連携](../references/pulseview-integration.ja.md) 経路B | **完了 — 継ぎ目は消えた。86 Msps・64 M sampleまで一本で通る。それ以上の律速はdeviceでもserverでもなく`srzip`の書き出し**([e080_p4_pulseview_gapless/](e080_p4_pulseview_gapless/README.ja.md)) |
| **E081** | MS OS 2.0 descriptor set を flat にすると Windows 11 は vendor bulk device に WinUSB を当てるか。subset のままなら当たらないままか | **一時・配線なし**(`esp32-p4-30eda0e31478`、**HS portはWindows側に置く**) | [Windows が WinUSB を当てない](../references/windows-winusb-binding.ja.md)、[EspUsbDeviceへの改修依頼](../references/espusbdevice-change-requests.ja.md) CR-1 | **完了 — flatは`Status=OK`/`Service=WinUSB`、subsetsは`CM_PROB_FAILED_INSTALL`。汚れた台でも新しいserialなら当たる。nativeは21.2 MB/sでusbip経由と差なし**([e081_p4_winusb_bind/](e081_p4_winusb_bind/README.ja.md)) |
| **E082** | capture中はpackedのまま一時ファイルへ落とし、終わってから`.sr`へ変換すると、どのrate・どの深さまで通るか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ、信号源は内部LEDC) | [E080](e080_p4_pulseview_gapless/README.ja.md)の未決「受け側が律速」 | **完了 — E080が落ちた条件(86 MHz × 256 M sample)が`fifo_overflow=0`・占有57 KBで通る。変換は無圧縮0.51秒 / deflate 9.5秒で94分の1**([e082_p4_spool_then_convert/](e082_p4_spool_then_convert/README.ja.md)) |
| **E083** | [E075](e075_p4_width_sample_accuracy/README.ja.md)が観測した「`overflow=0`のまま1 channelだけduty 0.00%」は`create_receiver()`と`configure_pwm()`の順序で説明できるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、信号源は内部LEDC) | [E075](e075_p4_width_sample_accuracy/README.ja.md)の未決 | **完了 — 順序が原因。receiver先はcold bootの29%(24回中7回)で1 channel死亡、LEDC先は30回で0件。再現はhard reset直後の1回だけ**([e083_p4_attach_order/](e083_p4_attach_order/README.ja.md)) |
| **E084** | capture と同時に降ろすとき、TX FIFO / 1転送長 / host の URB をどう選ぶと排出が最大になるか。釣り合い点はどこまで上がるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ、信号源は内部LEDC) | [E078](e078_p4_continuous_stream/README.ja.md)の釣り合い点、[E071](e071_p4_hs_vendor_fifo_depth/README.ja.md) | **完了 — FIFO 8192 / 転送 8192が最良。連続streamingは86 → 96 Msps。host側のURBは大きさもdepthも効かない。n=3での「32768が最良」と「飽和させて排出を測る」はどちらも訂正済み**([e084_p4_transfer_tuning/](e084_p4_transfer_tuning/README.ja.md)) |
| **E085** | 1転送の長さを変えたときの所要は`S / R + T`で表せるか。`R`(線上の漸近rate)と`T`(1転送あたりの死に時間)はいくらか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ) | [E084](e084_p4_transfer_tuning/README.ja.md)の2点外挿、[CR-7](../references/espusbdevice-change-requests.ja.md) / [HR-1](../references/espusbhost-change-requests.ja.md) | **完了 — `S/R + T`で表せる(残差1.0%)。`R`=24.64 MB/s、`T`=21.7 us。転送長を無限に伸ばしても24.4 MB/sで36.4には届かない**([e085_p4_transfer_size_model/](e085_p4_transfer_size_model/README.ja.md)) |
| **E086** | 8 channelで継ぎ目なく流せるsample rateの上限はいくらか。FX2(fx2lafw、8ch公称24 Msps)の置き換えになるか | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ、信号源は内部LEDC) | [E084](e084_p4_transfer_tuning/README.ja.md)は2chのみ、[sample rateの選び方](../references/p4-sample-rate-selection.ja.md) | **完了 — 8chは23 Mspsまで、20 Mspsなら余裕。24 Msps(FX2の公称)は積む。上限はchannel数ではなくbyte rate(23〜24 MB/s)で決まる。pinは飛び飛び・順不同で自由**([e086_p4_8ch_stream/](e086_p4_8ch_stream/README.ja.md)) |
| **E087** | WT9932P4-TINYでターゲットへ適当に挿してよいピンはどれか。pull-upとpull-downを同時に掛けると中間電圧になるか | **一時・配線なし**(`esp32-p4-30eda0e31478` = WT9932P4-TINY) | [ピンの当たりを付ける](../references/pin-discovery.ja.md) | **完了 — ヘッダ上34本がfree、推奨16本(IO16-23/IO26-33)は駆動も健全。両pullで1.46〜1.49 V。IO24/IO25(J3のUSB)に触るとconsoleが落ちる**([e087_p4_pin_survey/](e087_p4_pin_survey/README.ja.md)) |
| **E088** | capture を止めると vendor bulk の天井はどこまで上がるか。[E085](e085_p4_transfer_size_model/README.ja.md)の`R`は素の値か | **一時・配線なし**(`esp32-p4-30eda0e31478`、HS portはusbipdでWSLへ) | [E085](e085_p4_transfer_size_model/README.ja.md)の未決 | **完了 — 動かない。idle 23.88 対 capture同時 23.36 MB/s(8 KiB転送)で差はばらつきの内側。模型は8 KiBまでの近似で、idleでは16 KiBが8 KiBより遅い**([e088_p4_usb_ceiling_idle/](e088_p4_usb_ceiling_idle/README.ja.md)) |
| **E079** | host 側(PC)が bulk IN の URB を複数同時に投げると、device を変えずに帯域は伸びるか | **一時・配線なし**(同上) | [改修の着手順](../references/usb-library-change-plan.ja.md)、[EspUsbDeviceへの改修依頼](../references/espusbdevice-change-requests.ja.md) CR-7 | **中止 — 同じ測定がライブラリ側で先に行われた。depth 2 で飽和(1=18.64 / 2=22.68 / 8=22.87 MB/s)、約23 MB/sはdevice側の天井**([e079_p4_host_urb_depth/](e079_p4_host_urb_depth/README.ja.md)) |

**表は番号順に並べている。番号順は実行順ではない。** E002 が反証されて追試が要り、それが E004 になったので、実行順は E001 → E002 → E004 → E003 だった。§2 の「採番は着手直前に 1 件ずつ」はこの反省から来ている。

最初の 4 本は [README.ja.md §4.5](README.ja.md) の常設ベンチの梯子(v0 → v1 → v2)を登るために組んだもので、以後の全実験がこの上に乗る。

- **E001 が最初**なのは、これが通らないうちに測った数値は測定対象ではなく環境を測っているから([§3.1.2](README.ja.md))。**実機を一切使わない**ので、board の有無に関係なく今日始められる。
- **E002** で初めて実機が出る。E001 で切り分け済みなので、ここで落ちたら原因は「実機まわり」に限定される。
- **E004** は E002 の追試。起動時出力の取りこぼしを host からの trigger で避ける。
- **E003** は peer を**やるかどうかを決めるための確認**であって、peer テストの実装ではない。既設の 2 枚は **GPIO19↔19 / 20↔20** が直結されており、**RVSWD が 2 線なので配線を触らずに 2 線ペアの形が既にある**。使えると分かれば選択肢として残り、使えないと分かれば早く諦められる。

## 2. 候補(未採番)

番号は付けない。参照は slug で行う。**機材の可否を確認してから** §1 へ移して採番する。

「用意」列: **有** = 手元にある / **要調達** = 買う・借りる必要がある / **不明** = 確認していない。

| slug | 問い | ベンチ種別 | 必要な機材 | 用意 | 影響する doc |
|---|---|---|---|---|---|
| `loopback-inject` | loopback phy で DMI status の fail/busy・無応答・CRC 誤りを注入したとき、host は仕様どおり回復するか | **常設 v0**(実機なし) | host Arduino core のみ | 有 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §2–§4/§6 |
| `device-lock` | device lock は 2 プロセス間で実際に効くか(片方が待つか) | **常設 v1** | 実機 1 枚 | 有 | [README.ja.md §7-9](README.ja.md) |
| `wire-bitstream` | SWIO / RVSWD の **bit 列**(start・addr7・data32・op2・parity)は [link-to-target](../protocols/link-to-target.ja.md) §3 の仕様どおりか。**タイミングは見ない** | **常設 v0**(実機なし)または **常設 v2**(E005 の道具で実線上を確認。半周期 5 us 以上) | host Arduino core / peer 対 | 有 | [link-to-target](../protocols/link-to-target.ja.md) §3 |
| `tool-fast-capture` | 受信を SPI slave / レジスタ直読み / 割り込みにすれば、実 RVSWD 速度で bit を拾えるか(E005 は 100 kbps が上限) | **常設 v2** | peer 対 2 枚 | 有 | (道具) |
| `p4-parlio-rate` | PARLIO TX等の既知patternを信号源にして、internal RAMへの8-bit有限長PARLIO RX captureが欠落・化けなしで成立するsample rate上限はどこか | **一時・配線なし** | ESP32-P4 1枚 | 有 | [Arduino向けprobe protocol実現性](../references/arduino-probe-protocol-feasibility.ja.md) logic capture候補 |
| `p4-parlio-spool-rate` | internal DMA ringからPSRAMへ退避するとき、dropなしで継続できる8-bit sample rate、ring/chunk size、capture時間の境界はどこか | **一時・配線なし** | PSRAM搭載ESP32-P4 1枚 | 有(E016で32 MiB確認) | 同上。E021の8 MHz成立後 |
| `p4-parlio-width` | PARLIOの1 / 2 / 4 / 8 / 16 data lineでpackingを復元し、既知patternを欠落なくbatch取得できるか | **一時・配線なし** | PSRAM搭載ESP32-P4 1枚 | 有 | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) channel幅 |
| `p4-wide-gpio-snapshot` | CPUのGPIO input register snapshotで24 / 32 / 33〜55 channelを同時取得できるrate・jitter・core占有率の限界はどこか | **一時・配線なし** | PSRAM搭載ESP32-P4 1枚 | 有 | 同上。wide低速tier |
| `p4-trigger-matrix` | channel幅、pattern/edge/occurrence/multi-stage条件ごとのdropなしsample rate境界はどこか | **一時・配線なし** | PSRAM搭載ESP32-P4 1枚 | 有 | 同上。trigger |
| `p4-batch-compression` | RLE、transition timestamp、blockごとのraw/RLE選択はどの入力で有効で、最大sample rate・edge rate・最悪膨張率はいくつか | **一時・配線なし** | PSRAM搭載ESP32-P4 1枚 | 有 | 同上。内部圧縮 |
| `p4-external-clock` | PARLIO external clock入力でfinite/circular batch captureが成立する周波数・停止条件はどこか | **一時・配線なしで試せる見込み** | PSRAM搭載ESP32-P4 1枚 | **有**(下記) | 同上。clock |
| `p4-rmt-capture` | 同じPWM/RMT信号をRMT RXのpulse-duration列で取得すると、PARLIO raw sampleより少ないdata量で何channel・何edge/sまで保持できるか | **一時・配線なし** | ESP32-P4 1枚 | 有 | 同上 |
| `p4-adc-batch` | ADC continuous DMAの1〜14 channel pattern順、aggregate rate上限、pool overflow、PSRAM batch退避は無配線入力でも成立するか | **一時・配線なし** | ESP32-P4 1枚 | 有 | [P4 logic analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) analog |
| `p4-adc-signal-quality` | ADC continuousのattenuation別範囲、noise、ENOB、channel間skew、digital captureとの同期精度はどの程度か | **一時・要配線** | ESP32-P4 1枚、SDM/PWM、RC、jumper、基準電圧 | 現在不可 | 同上。analog実信号 |
| `linke-error-frame` | WCH-Link の異常系 error 応答 frame の形式(target 無し等) | **常設**(capture) | LinkE + usbmon | 有 | [pc-to-link](../protocols/pc-to-link.ja.md) §3、P1-1 |
| `isp-usb-verify` | factory ISP の USB 実 frame を capture し、minichlink 転記の byte(XOR key = ΣUID・Erase sector 数・Program 56 B chunk・config 12 B の補数位置)と一致するか | **一時**(capture) | WCHISPTool(Windows)+ USBPcap、または minichlink `-c` ISP + usbmon | 不明 | [pc-to-device-isp](../protocols/pc-to-device-isp.ja.md) §3–§4、P2-3(**算法は転記で埋まった。確認待ち**) |
| `wch-iap-capture` | WCHMcuIAP_WinAPP.exe の UART(460800)/ USB(`1A86:55E0`)実 frame は [wch-iap](../protocols/wch-iap.ja.md) §3–§4 の転記(sync・checksum・VERIFY の addr・END 無応答・順序)と一致するか | **一時**(capture) | Windows + WCHMcuIAP + IAP を焼いた V003 または X035 + USBPcap / UART capture | 不明 | [wch-iap](../protocols/wch-iap.ja.md) §7、P2-4 |
| `boot-area-selfwrite` | V00X / X035 で、user code から `BOOT_MODEKEYR` 解錠後に BOOT 領域(`0x1FFF0000`、3,328 B)を erase / program できるか(V003 は `ch32_user_bootloader_flasher` で実証済み) | **一時** | X035 or V006 + probe(復旧用) | 不明 | [custom-bootloader](../protocols/custom-bootloader.ja.md) §2a、[bootloader-design-space](../references/bootloader-design-space.ja.md) §2 |
| `hid-bl-capture` | rv003usb / ch32fun bootloader と minichlink の HID feature report(ID・scratchpad 構造・`0x1234ABCD`・完了印 `0xFF`)は [custom-bootloader](../protocols/custom-bootloader.ja.md) §2b の転記と一致するか | **一時**(capture) | BL を焼いた V003 or X035 + minichlink + usbmon | 不明 | [custom-bootloader](../protocols/custom-bootloader.ja.md) §2b |
| `bl-size-baseline` | **現行の UIAPduino BL(fork `B803`)は実際に何 byte で、1,920 B のうち残りは幾らか。**関数別の内訳(`0x00`–`0x4F` の startup は静的計算で**空き 0** と出たので確認のみ) | **実機なし**(ビルドのみ) | RISC-V toolchain のみ(`.map` と `--print-memory-usage` は既に有効) | 有 | [v003-bootloader-replacement](../references/v003-bootloader-replacement.ja.md) §8.2 |
| `bl-size-entry-budget` | timeout / button / host 検出を **同時に**載せるには何 byte 足りないか(構成別ビルドの差分)。**UIAPduino 自身が `#warning` で「入らない」と記録している**(commit `dfe879d`)ので、**不足量が分かれば還元できる** | **実機なし**(ビルドのみ) | 同上 | 有 | 同 §4 / §8.2 / **§8.6.1** |
| `bl-softreboot-decouple` | **`SOFT_REBOOT_TO_BOOTLOADER` は button ブロックの中だけで参照され、`bootloader.c` L265 の POR 判定が無条件なので、button 無しでは効かない。** L265 を独立に条件化(`&& RSTSCKR != 0x10000000`、**≈8 B**)すれば **button の 20–40 B を払わずに app→BL entry が入るか** | **実機なし**(サイズ)+ **一時**(実機で app→BL を往復) | toolchain / UIAPduino | 有 / 不明 | 同 **§8.6.3**(**最小の upstream 還元候補**) |
| `bl-size-linker` | BL 専用 ld から未使用セクション(`.init_array` / `.ctors` 等)と `ALIGN(4)` の padding を削ると何 byte 戻るか | **実機なし**(ビルドのみ) | 同上 | 有 | 同 §8.3-C |
| `bl-size-flags` | `-Oz` / `-msave-restore` / 新 GCC / inline 掃引で何 byte 縮むか。`-msave-restore` は USB の C 部分の **40 サイクル制約**に触れないか | **実機なし**(縮み量)+ **一時**(動作確認) | toolchain 複数版 / UIAPduino | 有 / 不明 | 同 §8.3-B |
| `bl-size-descriptor` | **`descriptor_list`(7 × 12 B = 84 B)から空文字列 2 エントリの削除(≈28 B)と並列配列化(15–25 B)で実際に何 byte 戻るか。** HID report descriptor(17 B)の `HID_USAGE(0xff)`(upstream が `// Needed?` と書いている)は削れるか。**3 host 実装**(minichlink / rv003usb-webflasher / WebLink_USB)は通るか | **実機なし**(サイズ)+ **一時**(host 3 実装) | UIAPduino + minichlink + Chromium | 不明 | 同 §8.3-D / **§8.5.2(実数の内訳あり)** |
| `bl-no-ep1` | BL の protocol は control transfer だけなので **EP1 IN を省けるか**。**Windows / macOS / Linux が HID device として列挙するか** | **一時** | UIAPduino + 3 OS | 不明 | 同 §8.3-D2(当たれば最大の削減) |
| `sw-usb-app-footprint` | **app 側** rv003usb の flash / RAM 実消費は幾らか(`demo_terminal` の `.bss` / `.data`)。V003 の 2 KB RAM のうち何 byte 残るか | **実機なし**(ビルドのみ) | toolchain のみ | 有 | 同 §5.1 / §7-1 |
| `iap-order` | WCH IAP の host↔device 往復順序 | **一時**(capture) | WCHMcuIAP + 対応 chip | 不明 | [serial-and-print](../protocols/serial-and-print.ja.md) §6、P2-4 |
| `dap-mode` | DAP mode の切替 byte 手順と CMSIS-DAP v1/v2 判定 | **常設**(capture) | LinkE + usbmon | 有 | [dap](../protocols/dap.ja.md)、P2-5 |
| `uart-dtr-reset` | port open の DTR auto-reset で probe は reset するか。host は `hello` をいつから撃てるか | **一時** | Uno / ESP32 DevKit / Pico | 不明 | [ecosystem-any-hardware](../references/ecosystem-any-hardware.ja.md) §4.5 |
| `set-baud` | `set_baud` の切替はどの手順なら取りこぼさないか。失敗時に 115200 へ戻れるか | **一時** | CH340 / CP2102 / 内蔵 CDC の 3 種 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §9-3 |
| `dmi-latency` | DMI 1 往復の時間は transport ごとに幾らか(UART 115200/1M、USB CDC、TCP、WebSocket) | **一時** | probe(ESP32-S3 / Pico)+ target 1 個 | 不明 | [generic-probe-design](../references/generic-probe-design.ja.md) §6/§9-2 |
| `wire-autodetect` | 1 線/2 線の自動判別はどの手順で確実に効くか。誤判別する条件は | **一時** | probe + V003 と V307 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §9-1 |
| `ardulink-compat` | ardulink 互換モードの 1 byte 目自動判別は、minichlink を無改造で通すか | **一時** | probe + V003 + minichlink | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §7 |
| `flash-time` | 64 KB 書込時間は per-op / batch / batch+`poll`+`write_rep` で幾ら違うか。基準装置(LinkE + ch32rv)と比べて | **一時** | probe + V307 + **LinkE** | 不明 | [generic-probe-design](../references/generic-probe-design.ja.md) §6、[dmi-bridge](../protocols/dmi-bridge.ja.md) §4.3 |
| `lane-independence` | lane を 2 本同時に attach したとき、互いのタイミングは劣化するか。`max_inflight=1` で足りるか | **一時** | ESP32-S3 + V003 と V307 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §6.1 |
| `swio-threshold` | SWIO の LOW パルス幅は 0/1 をどこで分けるか。**動かなくなる境界は両側どこか** | **一時**(LA は任意) | probe + **実 V003**。幅の生成・測定は E006 の道具(12.5 ns 分解能)で足りる。**立ち上がり波形まで見るなら** LA 100 MS/s 以上 + marker 線 | V003 次第 | [link-to-target](../protocols/link-to-target.ja.md) §3/§5、P3-6、[dmi-bridge](../protocols/dmi-bridge.ja.md) §9-2 |
| `rvswd-frame` | RVSWD の bit フレーム(7+32+2+1 ×2)は実波形と一致するか。STOP 波形とクロック周波数は | **使い捨て** | probe + V203/V307 + **LA 3ch**(SWCLK/SWDIO/marker) | **要調達?** | [link-to-target](../protocols/link-to-target.ja.md) §3、P3-7 |
| `5v-swio` | 5 V board(Uno)から open-drain で SWIO を叩けるか。直列抵抗だけで安全か | **使い捨て** | Uno + V003 + **LA** + 抵抗 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §8.2 |

### P4候補の現状(2026-09-09)

上表のP4候補のうち、いくつかは別の名前で採番されて解決している。番号は再利用しないので候補行はそのまま残し、行き先だけここに書く。

| slug | 状態 |
|---|---|
| `p4-parlio-rate` | **解決** — [E015](e015_p4_parlio_routing_order/README.ja.md)・[E016](e016_p4_parlio_psram_direct/README.ja.md)・[E031](e031_p4_parlio_channel_width/README.ja.md)〜[E033](e033_p4_parlio_width_rate_fine/README.ja.md)・[E036](e036_p4_parlio_rate_seq_verify/README.ja.md)・[E038](e038_p4_parlio_pulse_trigger_rate/README.ja.md)。sampling上限は内部clock源の160 MHz |
| `p4-parlio-spool-rate` | **解決** — [E021](e021_p4_parlio_psram_spool/README.ja.md)・[E022](e022_p4_parlio_spool_80mhz/README.ja.md)・[E036](e036_p4_parlio_rate_seq_verify/README.ja.md)。持続約98 MB/s |
| `p4-parlio-width` | **解決** — [E031](e031_p4_parlio_channel_width/README.ja.md)、sample単位再検証は[E036](e036_p4_parlio_rate_seq_verify/README.ja.md)と[E042](e042_p4_parlio_16ch_seq_verify/README.ja.md) |
| `p4-adc-batch` | **解決** — [E034](e034_p4_adc1_continuous_batch/README.ja.md)。続きは[E035](e035_p4_adc_topology_order/README.ja.md)(計画) |
| `p4-trigger-matrix` | **一部** — 8 channelのsoftware走査tierは[E025](e025_p4_sump_trigger_rate_boundary/README.ja.md)・[E028](e028_p4_sump_four_stage_trigger/README.ja.md)、hardware tierは[E037](e037_p4_parlio_pulse_trigger/README.ja.md)〜[E041](e041_p4_parlio_shared_valid_line/README.ja.md)で確定。**width × 条件種のmatrixは未着手** |
| `p4-batch-compression` | **一部** — RLE・transition timestamp・block adaptiveは未着手。CPUを使わない手段としてhardware capture qualificationが[E040](e040_p4_parlio_level_open_frame/README.ja.md)〜[E060](e060_p4_drain_batch_coalesce/README.ja.md)で実装候補まで固まった |
| `p4-rmt-capture` | **未着手**(元の問い)。RMT RX自体は[E044](e044_p4_gate_rmt_timestamp/README.ja.md)〜[E054](e054_p4_rmt_dma_block_min/README.ja.md)でgate windowのtimestamp用途として使い、発火条件まで確定した |
| `p4-wide-gpio-snapshot` | **未着手**。限界matrixの24 / 32 / 33〜55 channel行は空のまま |
| `p4-external-clock` | **未着手だが配線なしで試せる見込みへ変わった**。`PARLIO_CLK_SRC_EXTERNAL`の`clk_in_gpio_num`はGPIO matrix経由で、[E041](e041_p4_parlio_shared_valid_line/README.ja.md)と[E047](e047_p4_gate_three_way_share/README.ja.md)で1つの入力GPIOを複数のperipheral入力へfan-outできることが確認済みである。PARLIO TXの`clk_out_gpio_num`か`esp_clock_output`で同じGPIOへclockを出せば、API・経路・停止条件は無配線で測れる。周波数精度と実信号品質は配線が必要なまま |
| `p4-adc-signal-quality` | **未着手・要配線**のまま |

### セッションの束ね方(使い捨てベンチ)

LA を組むベンチは設営が重いので、**組んだら一度に消化する**([README.ja.md §3.1.1](README.ja.md))。現時点で見えている束は 2 つ:

- **SWIO セッション**: `swio-threshold` + `5v-swio`(どちらも SWIO 1 本 + marker。V003 のまま抵抗と board を差し替えるだけ)
- **RVSWD セッション**: `rvswd-frame`(3ch。SWIO セッションとは配線が違うので分ける)

- **BL サイズのセッション**: `bl-size-baseline` / `bl-size-entry-budget` / `bl-size-linker` / `bl-size-flags` / `sw-usb-app-footprint` は **実機も target も要らず、ビルドして `.map` を読むだけ**。設営ゼロなのでまとめて 1 回で消化できる。実機が要るのは `bl-no-ep1` と `bl-size-descriptor` の host 確認だけ。**`bl-size-baseline` の結果次第で残りが不要になる**([v003-bootloader-replacement](../references/v003-bootloader-replacement.ja.md) §8.4)ので、必ず先に 1 本だけ回す。

セッションを組む直前に、そのとき残っている候補をもう一度見て「ついでに取れるもの」を足す。

**先に `wire-bitstream` を v0 で通しておく**と、LA セッションで見るべきものが「タイミングだけ」に絞れる。符号の誤りと波形の誤りを同時に相手にしないで済むので、設営の重いベンチほどこの分離が効く。

## 3. 記録

### E001 スモーク: 実機なし(host Arduino core)— 完了 2026-09-04

計画・結果の全文: [e001_smoke_host/README.ja.md](e001_smoke_host/README.ja.md)。run: `_runs/E001_20260904T050917Z_host/`。

**事実**

1. **実機なしで build → 実行 → assert が成立する**(3/3 pass、7.3–7.5 s)。arduino-cli 1.3.1 / lang-ship:host 1.7.1 / pytest-embedded-arduino-cli 1.4.1。→ **常設 v0 は使える**。
2. **DUT はテスト関数ごとに生成される。** 同一ファイルの 2 関数目で `Connection refused`。**1 実験 1 テスト関数**にまとめれば通る。
3. **ビルド生成物は `<実験>/build/<profile>/`**(`output/` ではない)。
4. 生ログは `/tmp/pytest-embedded/<UTC>/<test 名>/dut.log`。銘板を含め期待どおりの 5 行のみ。
5. **実行の明示指定はファイルパスでなければならない**(ディレクトリでは収集 0 件)。

**候補**: 1 実験 1 テスト関数(採用)/ 銘板の `build=` に `__DATE__ __TIME__`(`banner-autofill` で置換予定)。

**未決**: `dut-scope`(DUT の scope 変更が可能か)/ `runs-archive`(退避は今回手動)/ `collection-guard`(bare `pytest` 0 件は観測したのみ)。

**反映**: 規則 §1.3(ファイルパス指定・1 実験 1 テスト関数)、§3.4(`output/` → `build/`)、§8(言語ルール)を更新。仕様の status は動かない。

### E002 スモーク: 実機 1 枚 — 完了 2026-09-04(**仮説は反証された**)

全文: [e002_smoke_board/README.ja.md](e002_smoke_board/README.ja.md)。run: `_runs/E002_*_s3_peer_host/`。

**事実**

1. **upload は通る。** `.env` の**別名パス(`/run/board-identify/by-id/...`)はそのまま port として解決された**。
2. **`setup()` の出力は 1 行も届かない**(確定的)。銘板 / `SMOKE *` / `CLOCK` の出現回数はいずれも 0。最初に届くのは `HEARTBEAT 1` で、**`HEARTBEAT 0` すら失われている**ことから遅れは **1 秒以上**。
3. **heartbeat を仕込んでいたおかげで「ボードが死んでいる」と「起動時出力の取りこぼし」を区別できた。**
4. 実クロック(反証条件 5)は **未測定 `—`**。

**候補**: (a) 銘板の周期再送 / (b) host からの DTR reset(プラグインは ESP 固有 reset を意図的に持たない)/ **(c) host の trigger に応答させる ← 本命**。

**未決**: どれを採るか、実クロックが取れるか → **E004**。

### E004 スモーク: 実機 + host trigger — 完了 2026-09-04

全文: [e004_smoke_board_trigger/README.ja.md](e004_smoke_board_trigger/README.ja.md)。run: `_runs/E004_*_s3_peer_host/`。

**事実**

1. **host が撃って probe が答える形にすれば、監視の接続タイミングに依存しない。** 3/3 で取得、**再送不要(1 発)**。
2. **実クロックが取れる。** `delayMicroseconds(1000)` に対し `micros()` 差分は **1003 us**(3 回とも同値)。仮想時計と明確に区別できる → **常設 v1 が成立**。
3. `setup()` で何も出さない設計にすると、`dut.log` に応答だけが残り読みやすい。

**候補**: **実機実験の共通の型 =「host が撃つ → probe が答える」**(採用)。E002 の候補 (a)(b) は試さずに済んだ。

**未決**: trigger を frame 化(magic+len+CRC)しても 1 発で通るか / reset 後 1 秒未満に撃った場合の挙動(候補 `uart-dtr-reset`)。

**反映**: 規則 §4.1(共有機材)・§7(実機実験の型)を更新。[ecosystem-any-hardware §4.5](../references/ecosystem-any-hardware.ja.md) と [dmi-bridge §4.1](../protocols/dmi-bridge.ja.md) に実測の裏付けを追記。

### E013 ESP32-S3: USB descriptor profile分離 — 中断 2026-09-10

全文: [e013_usb_descriptor_profiles/README.ja.md](e013_usb_descriptor_profiles/README.ja.md)「中断」節。実機は走らせていない。根拠: [usb-host-descriptor-persistence](../references/usb-host-descriptor-persistence.ja.md)。

**事実**(机上調査)

1. Windowsのdevice instance IDは`USB\VID&PID\<serial>`(composite childは`&MI_nn`)で、`bcdDevice`は含まれない。一次資料(Microsoft Learn)で確認。
2. 既存instanceが異なるinterface構成で再出現すると既存devnodeとdriverが再利用される観測が複数あり、`bcdDevice`変更では解消しなかった報告がある。Microsoft文書には記述がない。
3. LinuxとmacOSには永続cacheがなく、接続ごとにdescriptorを読み直す(kernel source、Apple文書で確認)。
4. Arduino-ESP32 3.3.11はCDC + WebUSBでdevice classを`02/02/00`へ強制し、E013 Profile Bはこの対象。

**候補**: 同一PIDでの分離手段はinterface番号の固定 + 末尾追加(常にcomposite)、serial規則、別PID。`bcdDevice`は候補から外す。

**未決** → [E062](e062_usb_same_identity_layout_change/README.ja.md)。

### E088 ESP32-P4: capture を止めた素の USB 上限 — 完了 2026-09-13

全文: [e088_p4_usb_ceiling_idle/README.ja.md](e088_p4_usb_ceiling_idle/README.ja.md)。[E085](e085_p4_transfer_size_model/README.ja.md)と同じ 4 点を、**PARLIO も harvest も PSRAM も無い firmware**(internal RAM の pattern を送るだけ)で測り直した。各 n=9。

**事実**

1. **capture の有無で天井は動かない。** 8 KiB 転送で **idle 23.88 / capture 同時 23.36 MB/s**、差 ±0.52 は run 間の幅(0.38〜0.84)の内側。**仮説(止めれば 25〜27 まで伸びる)は否定された。**
2. **[E085](e085_p4_transfer_size_model/README.ja.md) の `R` = 24.64 MB/s は素の上限でもあった。** **capture 負荷で 36.4 MB/s との差を説明することはできない。**
3. **8192 が最適で 16384 は遅い** — **capture の有無に関わらず**(idle 23.40 対 23.88)。[E084](e084_p4_transfer_tuning/README.ja.md) の結論は負荷の産物ではない。
4. **`S/R + T` は 8 KiB までの近似。** 3 点当てはめは 16384 を 1.2〜1.6 MB/s 過大に予測する。**`R` を漸近線として引用してはいけない。**
5. [E084](e084_p4_transfer_tuning/README.ja.md) の「capture 負荷で排出が下がる」は**残るが 3% 程度**で、天井を決めてはいない。

**候補**: **vendor bulk の実力は 8 KiB 転送で約 24 MB/s** / **転送長は 8192、それ以上は逆効果** / **36.4 との差は device 側の USB 経路そのもの** → [HR-1](../references/espusbhost-change-requests.ja.md)。

**未決**: 16 KiB で遅くなる理由 `—` / microframe あたり 6 transaction で止まる理由 `—` / CDC・HID でも同じ天井か `—`。

### E087 ESP32-P4: 使えるピンと pull 二重掛け — 完了 2026-09-13

全文: [e087_p4_pin_survey/README.ja.md](e087_p4_pin_survey/README.ja.md)。**駆動せず内部 pull だけで**空きを調べ、中間電圧は**同じパッドを P4 自身の ADC で**測った(ADC1 = GPIO 16〜23 なので配線不要)。

**事実**

1. **ヘッダ上の 34 本が free**(pull に追従)。握られていたのは **IO51 だけ**。
2. **チップは GPIO を 1 本も予約しない** — `MSPI_IOMUX_PIN_NUM_* = INVALID`(flash / PSRAM)、`USBPHY_*_NUM = -1`(USB)。**使用中は基板の都合だけ。**
3. **推奨 16 本(IO16〜23 / IO26〜33)は駆動しても健全**(high=1 / low=0、競合なし)。**IO16〜23 は ADC1 ch0〜7 でもある。**
4. **`pull-up + pull-down` は 1460〜1487 mV。** 3.3/2 = 1.65 V ではない(pull-down がやや強い)。ピン間のばらつきは 27 mV で、**0 / 1.47 / 3.28 V の 3 値は十分離れている**。
5. **floating の読みは 1354〜2424 mV と不定。** 「未接続」を floating で判定してはいけない。
6. **USB が取るのは IO24 / IO25 の 2 本だけ**(J3 = full-speed)。**J4(high-speed)は専用パッドで GPIO を取らない。**

**事故(観測)**: 最初の版は GPIO 0〜54 を無差別に舐める作りで、**IO24 / IO25(J3 の USB)を引いて console を落とし、物理的な挿し直しが必要になった**。範囲指定を必須にし、1 ピンごとに flush し、駆動も pull も触らない `L`(レベルのみ)を足した。**データシートを読む前に全ピンを舐めない。**

**候補**: **IO16〜23 + IO26〜33 の 16 本を既定の作業領域に** / **IO24 / IO25 は禁止** / **IO39〜48 は LDO_VO4(1.8 V ありうる)なので避ける**。

**未決**: 中間電圧が 1.65 V でない理由 `—` / CH32 を繋いだときの分圧 `—` / IO39〜48 を 3.3 V で使えるか `—` / strapping ピンを起動後に使えるか `—`。

### E086 ESP32-P4: 8 channel の連続 streaming — 完了 2026-09-13

全文: [e086_p4_8ch_stream/README.ja.md](e086_p4_8ch_stream/README.ja.md)。[E084](e084_p4_transfer_tuning/README.ja.md)の firmware を `LANE_COUNT` で幅を選べる形にし、8 / 4 channel を測った。TX FIFO / 1 転送は 8192。

**事実**

1. **8ch は 23 Msps まで継ぎ目なく流せる**(64 MiB で占有 36〜43 KB、全 lane の周期一致)。**20 Msps なら余裕**(占有 46〜189 KB)。**24 Msps は積む**(占有 1.67〜1.95 MB)。
2. **4ch は 46 Msps まで**(48 は際どい、50 は積む)。
3. **上限は byte rate で決まり channel 数に依らない** — 8ch 23 / 4ch 46 / 2ch 96 Msps はすべて **23〜24 MB/s**。見積もりは `rate × channel ÷ 8 ≤ 23 MB/s`。
4. **8ch では host 側の展開が要らない。** PARLIO の packed(1 sample = 1 byte、bit n = lane n)が **sigrok の `unitsize=1` そのもの**。**2ch より host が軽い。**
5. **pin は自由。** `9,2,7,4,12,6,20,8`(飛び飛び・順不同・元の block 外を含む)で 8 lane すべて正しく取れ、duty も lane 順どおり。**連番である必要も昇順である必要もない。**
6. **FX2 との比較**: 公称 24 Msps には 1 Msps 届かないが、**実用域(16〜20 Msps)では置き換えになる**。少ない channel なら遥かに上(2ch で 96 Msps)。

**方法の誤り(観測)**: 4ch の初回で周期が `0/501` と壊れたのは **host 側 `unpack()` が 8ch と 2ch しか扱えず、4ch を 2 bit/sample として展開していた**ため。`lanes` から導く形に直して 400/400 になった。**幅を変えたら host 側の展開も変わる。**

**候補**: **FX2 置き換えは 8ch 20 Msps 常用・23 上限** / **8ch を既定の形に**(host が素通し)/ **pin は空いているところへ自由に**。

**未決**: 24 Msps を通す方法 `—`([HR-1](../references/espusbhost-change-requests.ja.md) 待ち)/ 16 channel `—` / 外部信号 `—`。

### E085 ESP32-P4: 1 転送あたりの死に時間と線上の漸近 rate — 完了 2026-09-13

全文: [e085_p4_transfer_size_model/README.ja.md](e085_p4_transfer_size_model/README.ja.md)。転送長 2048 / 4096 / 8192 / 16384 を 128 MHz(飽和)で各 n=9。

**事実**

1. **`period(S) = S/R + T` で表せる。** 4 点が残差 **±3.44 us(平均 period の 1.0%)**で直線に乗る。
2. **`R` = 24.64 MB/s、`T` = 21.67 us。** [E084](e084_p4_transfer_tuning/README.ja.md) の 2 点外挿(26.34 / 30.8)は `R` を 7%、`T` を 42% 過大に見ていた。**結論は変わらないが数値は本実験のものを使う。**
3. **`R` は漸近線。** 32768 で 24.25、65536 で 24.44、∞ で 24.64 MB/s。**転送長では 36.4 に届かない。**
4. **`R` は microframe あたり 6.02 transaction**(HS は 13、host 役は 8.89)。
5. **16384 は 8192 より +1.9% だけ。** internal RAM 32 KB に見合わない。
6. **`R` は定数ではない** — capture 負荷で動く(96 MHz で 23.9、110 MHz で 23.1)。**素の USB 上限を測るには capture を止める必要がある**(未実施)。

**候補**: **1 転送は 8192** / **`T` を消しても上限は 24.6 MB/s**([CR-7](../references/espusbdevice-change-requests.ja.md))/ **36.4 との差は [HR-1](../references/espusbhost-change-requests.ja.md) でしか切り分かない**。

**未決**: capture を止めた `R` `—` / `T` の内訳 `—` / 6 transaction で止まる理由 `—`。

### E084 ESP32-P4: 転送の形を詰める — 完了 2026-09-13

全文: [e084_p4_transfer_tuning/README.ja.md](e084_p4_transfer_tuning/README.ja.md)。[E078](e078_p4_continuous_stream/README.ja.md)の streaming 経路で、device 側の TX FIFO / 1 転送長と host 側の URB を振った。96 MHz(釣り合い点より上)で飽和させて排出を測る。

**事実**

1. **TX FIFO と 1 転送長で +9.0%。** n=9 で 4096/4096(既定)の **21.99** に対し、**8192/8192 で 23.97 MB/s**(16384/8192 = 23.48、32768/8192 = 23.65)。**n=3 で「32768 が最良かつ安定」と書いたのは誤りで、同日訂正した**(8192 の低い 1 本は flash / attach 直後の系統差)。ライブラリ側の単体測定(21.12 → 23.34)と同じ方向・同程度で、**capture と同居していても効きは失われない**。
2. **8192/8192 がいちばん速く、いちばん狭い**(幅 0.28、stdev 0.093)。32768 は 23.65 / 幅 0.44 で**むしろ劣る**。**+8 KB で +9.0% が最良の取引**。
3. **host 側の URB は何をしても変わらない。** 64 KiB〜1 MiB、depth 2〜4 のどれでも 23.5〜23.8 MB/s。**律速は device 側。**
4. **釣り合い点は 86 → 90 Msps。** 90 MHz は届いた MB/s(22.40/22.53)が生成(22.50)に一致し占有も小さい。92 MHz から下回る。
5. **占有の大きさだけでは判定できない。** 釣り合い点より下では FIFO が枯れて ZLP が出るぶん効率が落ちるので、**占有は rate に対して単調にならない**(92/94 より 96 のほうが小さい)。**「届いた MB/s 対 生成 MB/s」を主の判定にする。**
6. **`write()` に渡す塊は `writeCapacity()` から取る。** 固定 4096 のままだと FIFO を広げても使われない。

**候補**: **`CFG_TUD_VENDOR_TX_BUFSIZE=32768` / `CFG_TUD_VENDOR_TX_EPSIZE=8192` を既定に** / **連続 streaming は 90 Msps を上限に**(余裕を見るなら 88)/ **host 側は好きな形でよい**。

**未決**: 8192 のばらつきの原因 `—` / internal RAM に余裕がない場合の折り合い `—` / 4・8 channel `—` / 96 Msps で短い URB が増える理由 `—`。

### E083 ESP32-P4: 間欠的に 1 channel が死ぬ現象 — 完了 2026-09-13

全文: [e083_p4_attach_order/README.ja.md](e083_p4_attach_order/README.ja.md)。[E075](e075_p4_width_sample_accuracy/README.ja.md)が observation として残した現象を、**準備順序を引数にして cold boot ごとに 1 標本**取る形で詰めた。

**事実**

1. **原因は準備順序だった。** `create_receiver()` → `configure_pwm()`(E075 と同じ)は **cold boot 24 回中 7 回(29%)**で 1 channel が死ぬ。**逆順は 30 回で 0 件。**
2. **`overflow` は常に 0**、`status` も 0、死んだ lane 以外の edge 数は正常。**capture 経路の drop ではない**という [E075](e075_p4_width_sample_accuracy/README.ja.md) の見立ては正しかった。
3. **再現するのは hard reset 直後の 1 回だけ。** 同一 boot 内 200 trial で 0 件、`esp_restart()` 10 回でも 0 件。**「掃引の初回にだけ出る」「再実行すると再現しない」がこれで説明できる。**
4. **死ぬ lane は条件ごとに一定**(ここでは常に D2、E075 では別の幅・rate で D3 / D5)。特定の GPIO の問題ではない。
5. **既存の測定は影響を受けていない。** 立ち上がり周期で判定しているので、死んだ channel があれば必ず落ちる。

**候補**: **信号源を先に attach してから receiver を作る** / **cold boot の 1 回目を疑う**(同一 boot 内の再実行で「直った」ように見えても直っていない)/ **lane ごとの edge 数を board 上で数えるのは安い**(65,536 byte で数 ms)。

**未決**: なぜ順序で決まるのか `—`(レジスタ未確認)/ なぜ cold boot 限定か `—` / 2・8 channel での頻度 `—` / 外部信号での挙動 `—`。

### E082 ESP32-P4: 一時ファイルへ受けてから `.sr` へ変換する — 完了 2026-09-13

全文: [e082_p4_spool_then_convert/README.ja.md](e082_p4_spool_then_convert/README.ja.md)。[E080](e080_p4_pulseview_gapless/README.ja.md)で受け側(`srzip` の圧縮)が律速になったので、**capture 中は packed のまま追記するだけ**にして、展開と zip を後へ回した。

**事実**

1. **律速は device 側へ戻った。** [E080](e080_p4_pulseview_gapless/README.ja.md)が落ちた条件(86 MHz × 256 M sample)が **`fifo_overflow=0` / 占有 57 KB / 21.48 MB/s** で通る。E080 は同条件で **8 MiB 満杯 + `fifo_overflow=31,506`**、周期 60〜1392 の欠落だった。
2. **capture 中の仕事は「packed のまま追記」だけでよい。** 展開(4 倍)も圧縮も外へ出せる。
3. **変換は安い。** 無圧縮 **0.51 秒**(245 MB)、deflate **9.5 秒**で **2.6 MB(94 分の 1)**。どちらも `sigrok-cli` が読み戻す。
4. **深さの上限は firmware の 1 回の上限(268,435,456 sample)に戻った。**
5. **live で見る用途と保存する用途は分ける。** [E080](e080_p4_pulseview_gapless/README.ja.md) は PulseView で見るため、E082 は残すため。

**候補**: **`.sr` に残すなら一時ファイル経由** / **保存時は deflate**(9.5 秒で 94 分の 1)/ **live は E080 の経路で深さ 64 M sample 程度まで**。

**未決**: 268 M sample を超える深さ `—` / spool 先が遅い媒体だったら `—` / 変換の並列化 `—`。

### E081 ESP32-P4: Windows が WinUSB を当てるか — 完了 2026-09-13

全文: [e081_p4_winusb_bind/README.ja.md](e081_p4_winusb_bind/README.ja.md)。**同じ board・同じ firmware で `msOs20Layout` と serial だけ変えた 2 本**を焼き、Windows の判定を並べた。

**事実**

1. **flat(162 byte)なら当たる** — `Status=OK` / `Service=WinUSB` / compatible ID に `USB\MS_COMP_WINUSB` / `DeviceDesc=WinUSB Generic Device`。
2. **subsets(178 byte)なら当たらない** — `CM_PROB_FAILED_INSTALL`(Code 28)、compatible ID なし。[E069](e069_p4_hs_vendor_bulk_rate/README.ja.md) の症状と同一。
3. **原因は構造だけ。** 分岐は layout flag 1 つで、[調査記録](../references/windows-winusb-binding.ja.md)の仮説 2(Windows が vendor request を投げていない)は不要になった。
4. **汚れた台でも直る。** 失敗判定が残る Windows でも、**新しい serial なら普通に当たる**。逆に **serial を使い回すと直った firmware でも Code 28 が返る**([E062](e062_usb_same_identity_layout_change/README.ja.md))。
5. **usbip を外した native の帯域は 21.21 / 20.97 / 21.20 MB/s**(device 自身の時計で 21.2〜21.5)。**usbip 経由([E078](e078_p4_continuous_stream/README.ja.md) の 21.4〜22.4)と差がない。** 全測定の「usbip 込みなので下限」という但し書きは外せる。

**方法の誤り(観測)**: host 側の計測が最初 **28.58 MB/s** を出した — 先頭 block の完了で時計を始めながらその byte を数に入れていた。直したら今度は **0.46 MB/s** — **長さ 0 の packet が読みを即完了させる**ので、trigger 前の 9 秒が分母に入っていた。空 block を開始点にしないよう直して device 自身の時計と一致。**もう 1 件**: cwd が WSL のプロジェクト内のまま Windows の `uv run` を呼び、**`.venv/pyvenv.cfg` を Windows の CPython に書き換えられて WSL 側の python が起動しなくなった**(`rm -rf .venv && uv sync` で復旧)。**Windows の `uv` は Windows 側の作業ディレクトリで実行する。**

**候補**: **単一 interface の vendor device は flat で出す** / **WinUSB の検証は毎回新しい serial で** / **Windows 直結で 21.2 MB/s、driver 追加不要**。

**未決**: 読み size を変えた native の掃引 `—` / WebUSB からの接続 `—` / 複合 device での layout `—` / リリース版での再確認 `—`。

### E080 ESP32-P4: PulseView へ継ぎ目なく流す — 完了 2026-09-13

全文: [e080_p4_pulseview_gapless/README.ja.md](e080_p4_pulseview_gapless/README.ja.md)。[E077](e077_p4_pulseview_over_ip/README.ja.md)の server の**送出元だけ**を [E078](e078_p4_continuous_stream/README.ja.md) の streaming firmware に差し替えた(protocol の実装は import して共有)。

**事実**

1. **継ぎ目は消えた。** [E077](e077_p4_pulseview_over_ip/README.ja.md)で batch 境界に 3 箇所の乱れが出ていた **16 M sample** が、32 / 48 / 64 / 80 / **86 MHz** の全 rate で**周期完全一致**。
2. **86 Msps・64 M sample(0.74 秒の連続 capture)まで一本で取れる。** 74,419 周期すべて 860、`fifo_overflow=0`。**[E078](e078_p4_continuous_stream/README.ja.md) の上限がそのまま PulseView 経路の上限になっている。**
3. **律速は device でも server でもない。** 256 M sample を 86 MHz で要求すると壊れるが、**client 側の出力先だけを変えると同じ run が通る** — `-O srzip` は `fifo_overflow=31,506` で欠落、`-O binary` は 21.72 MB/s で `fifo_overflow=0`、protocol だけ話して捨てる client は 22.60 MB/s(展開後 89.32 MB/s)。
4. **server の展開(numpy)は 89 MB/s 出ており余裕がある。** **仮説(展開が律速になる)は外れた。**
5. **`srzip` の遅さは rate ではなく出力の総量で効く。** 64 MB は 18.56 MB/s で通るのに、256 MB では **16 Msps でも溢れる**。chunk ごとに zip entry を足す形式なので entry 数が効いていると読んでいる(未確認)。
6. **弾性 FIFO は burst を買うだけ。** 短い capture なら sink が遅くても吸収するが、総量が増えれば必ず効く。
7. **`fifo_overflow` は sample 欠落と一致した。** 溢れた run は周期が 60〜1392 に乱れ、溢れなかった run は完全一致。
8. **1 回の capture を超える要求は、繋がずに止まる。** server は `--samples` ぶんを 1 回流すだけなので、**client の要求に合わせて起動する**。上限は firmware の 268,435,456 sample。

**候補**: **`.sr` へ落とすなら 64 M sample 程度まで**、それ以上は `-O binary` か生受け / **server の `--samples` は client の要求に合わせる** / **継ぎ目不要なら 86 Msps、継ぎ目可なら batch で 160 Msps**。

**未決**: `srzip` が総量に対してどう重くなるか `—`(libsigrok 側)/ PulseView GUI での挙動 `—` / 268 M sample を超える連続 capture `—` / client が要求 sample 数を伝えられないこと `—`。

### E078 ESP32-P4: capture しながら OTG HS へ流す連続 streaming — 完了 2026-09-13

全文: [e078_p4_continuous_stream/README.ja.md](e078_p4_continuous_stream/README.ja.md)。[EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) の working tree(commit `7a6d9dc`、release 前)に対して、TX FIFO 4096 / 1 転送 4096、送出は `waitWritable()`、host は URB 4 本 in-flight。

**事実**

1. **2 channel を継ぎ目なく連続で降ろせるのは 86 Msps まで**(線の上で 21.5 MB/s)。**88 Msps から弾性 FIFO の占有が duration に比例して積む**(16 MiB で 0.5〜0.7 MB → 64 MiB で 2.0〜2.2 MB)。8 MiB を使い切るまでの外挿は 88 MHz で約 11 秒、92 MHz で約 5 秒。
2. **釣り合い点の見積り(約 84 Msps)はほぼ当たった。** 降ろす側 21.5 MB/s ÷ 0.25 byte/sample。
3. **capture と同居しても USB は落ちない。** 飽和時 **21.4〜22.4 MB/s** で、ライブラリ側が単体で測った新既定 21.1 MB/s と同等以上。**[E067](e067_p4_usb_vs_capture_core/README.ja.md) の「同居で 7〜16% 落ちる」は再現しなかった。**
4. **理由は [CR-9](../references/espusbdevice-change-requests.ja.md) の `waitWritable()`。** `stalls` は全条件 0、`waits` は転送数と一致(16 MiB で 4,095、64 MiB で 16,383)。**1 転送につき 1 回だけ block する。** E067 の損は**競合ではなく spin だった**。
5. **64 Msps までは MB/s が rate ÷ 4 に一致**し、USB は遊んでいる。
6. **「帯域が出ている」は「追いつけている」ではない。** 96 MHz でも 16 MiB は完走し周期も一致する。**占有が duration に比例するかどうかだけが判定になる。**
7. **sample は落ちていない。** 全条件で `overflow` 0、周期は head / tail とも一致。92 MHz・16 MiB の全長 scan(67,108,864 sample / 72,944 周期)で期待値と違う周期は **sample 0 の 1 つだけ**(capture 開始時の PWM 位相)。

**方法の誤り(観測)**: 最初の掃引で 88 MHz 以上に出た `ring_overflow`(8〜271)は **teardown の数え間違い**だった。`run_stream()` が usb task の完了を先に待ち、その間 PARLIO を止めていなかったため、harvest が抜けたあとも ISR が誰も読まない queue へ積み続けていた。**backlog が大きい高 rate ほど drain が長い**ので「高 rate でだけ overflow する」という本物らしい形になる。harvest を先に待って PARLIO を止める順序に直すと全条件 0。**`overflow` を信用してよいのは数え方が正しいときだけ。**

**候補**: **連続 streaming は 86 Msps を上限に置く**(余裕を見るなら 84)/ **batch なら 160 Msps まで** — 継ぎ目の可否で上限が 2 倍違う / **送出は `waitWritable()`、host は URB 2 本以上**。

**追記(同日)**: 送出を **`waitWritable(4096)` → `write(4096 ちょうど)`** に変えると、**釣り合い点より上で短い URB が 0** になる(半端な長さの転送が消えるため)。88 MHz の積み方は 2.1 → 1.5 MB と約 3 割ゆるむが、**上限は 86 Msps のまま**。**二通りの送出実装で同じ境界**が出た。釣り合い点より下で短い URB が残るのは半端ではなく **ZLP**([CR-5](../references/espusbdevice-change-requests.ja.md))で、**「追いついていない側」の徴候として使える**。

**未決**: 8 MiB を実際に使い切るまで回していない `—`(firmware の上限が 64 MiB = 約 3 秒)/ 86 と 88 の間 `—` / PulseView から continuous で引く `—` / 4・8 channel `—`。

### E077 ESP32-P4: stockのsigrok / PulseViewからIP経由で取る — 完了 2026-09-12

全文: [e077_p4_pulseview_over_ip/README.ja.md](e077_p4_pulseview_over_ip/README.ja.md)。[連携メモ](../references/pulseview-integration.ja.md)の経路B。libsigrokの`beaglelogic` driverのTCP modeを演じるPython serverを書き、その背後に[E076](e076_p4_capture_hs_download/README.ja.md)の経路で実機を置いた。

**事実**

1. **stockの`sigrok-cli`がserver経由で実機のcaptureを取れる。** driverの追加もlibsigrokの入れ替えも要らない。**経路Bが成立した。**
2. **波形は[E074](e074_p4_2ch_capture_to_sr/README.ja.md)の周期判定を通る。** 3回とも D0 / D1 が 800 / 800.00 / 800(80 MHz ÷ 100 kHz)、duty 25.00% / 50.00%。
3. **4 M sample @ 80 MHzが0.45秒**(capture 50 ms + download 0.10秒 + TCP送出)。sample数はちょうど4,000,000。
4. **driverは「何sample欲しいか」をserverに伝えない。** `limit_samples`はhost側だけに留まり、**必要なbyte数を受け取ったら`close`を送って読むのをやめる**。serverは**clientが止めるまで送り続ける**実装になる。
5. **`close`を受けてserverがsocketを閉じると`sigrok-cli`がCPU 100%で終わらなくなる**(2分以上回してkillした)。driverは`close`送出後に25 msのdrainをしてから自分で閉じるので、**serverは待つ**。
6. **`numchannels`はdriverのscan optionだが`sigrok-cli` 0.7.2はconn文字列で受け付けない。** 1 byte/sampleにするには`--channels P8_45,P8_46`でindex 8以上を無効にする。
7. **1回のcaptureを超える要求は継ぎ目が出る。** 16 M sampleは4 M batch × 4回のcaptureになり、**周期が800でない箇所は4つだけ、すべてbatch境界**(3,999,837 / 7,999,487 / 11,999,549 / 12,000,000)。**欠落ではなく実際の空白。**

**候補**: **PulseViewから使うときは`--samples`をserverのbatch以下にする** / **rateは80 MHzを既定にする**(PARLIOは160 MHz ÷ 整数、driverのlistは100 MHzまで)/ **BeagleLogicを演じるserverは`close`でsocketを閉じない**。

**未決**: PulseView(GUI)での確認 `—` / 継ぎ目のない連続streaming `—` / trigger `—` / 160 MspsをPulseViewへ出す `—`(driverのrate listが100 MHzまで)。

**道具の注意**: `srzip`は`logic-1-1`…`logic-1-10`…と分割するので、**chunkを文字列順に並べると継ぎ目で偽のedgeが出る**。数値順に並べること。

### E076 ESP32-P4: captureをOTG HSのvendor bulkで降ろす — 完了 2026-09-12

全文: [e076_p4_capture_hs_download/README.ja.md](e076_p4_capture_hs_download/README.ja.md)。[E074](e074_p4_2ch_capture_to_sr/README.ja.md)のfirmwareの`run_dump()`だけをvendor bulkへ差し替えた。制御はconsole、dataはOTG HS。host側はlibusbで1 MiBずつread。

**事実**

1. **capture → download → `.sr`が通る。** 4 MiB × 7回すべてでsample精度(周期1600がmin = mean = max)、byte欠落0、`sigrok-cli`が160 MHz / 2 channel / 16,777,216 sampleとして読み戻す。
2. **4 MiBのdownloadは平均0.48秒(8.80 MB/s、0.416〜0.579秒)。** console経路の5.8秒([E074](e074_p4_2ch_capture_to_sr/README.ja.md))に対し**12倍**。[まとめ](../references/p4-usb-hs-summary.ja.md)の見積もり0.39秒よりは遅い(あれはTX FIFO 8 KiBの[E071](e071_p4_hs_vendor_fifo_depth/README.ja.md)の値、本実験は既定の512 B)。
3. **PSRAMからの読み出しは律速ではない。** 同じloopで送出元だけを差し替えた対照(交互に8回ずつ)で、internal RAM mean **8.38**、PSRAM mean **9.04 MB/s**。**分布は完全に重なり、PSRAMの方がわずかに速い。**
4. **同一条件で1.65倍ばらつく**(6.62〜10.91 MB/s)。**`stalls`がrateと逆相関する**(25,413回で10.91、70,318回で6.62 MB/s)。送出元でも captureの有無でも動かないので、**stackかusbip経路に由来する**と見ている。[E070](e070_p4_hs_vendor_stack_compare/README.ja.md)のEspUsbDevice側のばらつき(6.79〜10.02)と一致。
5. **device側実測とhost側実測の差は2%以内。** usbipとlibusbの取り分は小さい。

**候補**: **captureのdownloadはOTG HS vendor bulkで行う**(consoleは制御用に残す)/ **送出元をPSRAMに置くことに帯域上の不利はない** / **1回の測定で帯域を語らない**。

**未決**: ばらつきの出どころ `—`(stack側かusbip経路か)/ TX FIFOを深くした状態での再測 `—`([CR-4](../references/espusbdevice-change-requests.ja.md))/ 連続streaming `—` / native(usbipなし)での帯域 `—`。

### E075 ESP32-P4: channel幅ごとのsample単位精度 — 完了 2026-09-12

全文: [e075_p4_width_sample_accuracy/README.ja.md](e075_p4_width_sample_accuracy/README.ja.md)。[E074](e074_p4_2ch_capture_to_sr/README.ja.md)と同じ周期判定を`LANE_COUNT`で幅を振って行った。

**事実**

1. **1 channelは160 Mspsでsample精度**(周期1600がmin = mean = max)。**限界matrixの※を外せる。**
2. **4 channelも160 Mspsでsample精度**(3回とも)。同上。
3. **8 channelは持続spool帯域の内側なら深さを増やしても精度を保つ** — 80 MB/s・1 MiBで`overflow=0`、周期完全一致。**96 MHz(96 MB/s)でも1 MiBで精度。**
4. **8 channel × 160 MHz(160 MB/s)で1 MiB取ると落ちる** — `overflow=131`、周期が162〜2368にばらける。**burst深度モデルどおり。**
5. **短い捕捉(131 KiB)なら8 ch × 160 MHzでも通る。** 「rate上限を単一の値で言えない」([E036](e036_p4_parlio_rate_seq_verify/README.ja.md))が8 channelでも再現した。
6. **`overflow`は欠落を正しく捉える**(落ちた条件のみ非0)。

**経路の異常(観測)**: 掃引の初回に、**`overflow=0`のまま特定の1 channelだけがduty 0.00%になる**ことが3回あった。**再実行すると再現しない**(3回連続で精度)。死ぬchannelは毎回違うのでGPIO固有ではなく、**test firmwareの準備順序**(`create_receiver()`がPARLIOのGPIO matrixを張った後に`ledcAttach`が同じpinを踏む)を疑っている。**capture経路そのものの問題ではないと見ているが未特定**(`p4-ledc-parlio-attach-order`)。

**候補**: **1 / 2 / 4 channelは160 Mspsまで使ってよい** / **8 channelは96 MHzを実用上限に置く** / **`overflow`を信用してよい**。

**未決**: 間欠的に1 channelが定数0になる現象 `—` / 16 channel `—`(`PARLIO_PINS`が8本)/ 8 channelのburst窓の正確な境界 `—` / 外部信号 `—`。

### E074 ESP32-P4: 2 channel captureからsigrok `.sr`まで — 完了 2026-09-12

全文: [e074_p4_2ch_capture_to_sr/README.ja.md](e074_p4_2ch_capture_to_sr/README.ja.md)。PARLIO RX data_width 2、信号源は内部LEDC PWM 100 kHz(duty 25% / 50%)、downloadはUSB-Serial-JTAG console経由。

**事実**

1. **2 channelのcaptureは160 Mspsまでsample単位で正確。** 32 / 64 / 128 / 160 MHzすべてで**立ち上がりedge間隔のmin = mean = max = 期待値**(320 / 640 / 1280 / 1600 sample)、overflow 0、取りこぼし0。**dutyでは欠落を検出できない**([E036](e036_p4_parlio_rate_seq_verify/README.ja.md))ので周期で判定した。
2. **160 MHzはP4の内部clock源の上限。** [限界matrix](../references/p4-logic-analyzer-investigation.ja.md)の「1 / 2 / 4 channelは160 MHz成立(※sample単位未検証)」に**sample単位の裏付けが付いた**。
3. **深さは16,777,216 sample(4 MiB packed、104.9 ms @ 160 Msps)まで通る。** PSRAMは32 MiBあるのでfirmwareの上限を上げればさらに伸びる。
4. **`.sr`をsigrokが正しく読み戻す**(channel数・rate・sample数・波形すべて一致)。`.sr`は`version` / `metadata` / `logic-1-1`のzipで、**1 sample = 1 byte**。
5. **packedのまま運んでhostで展開する方式が成立**。2 channelでは`.sr`が4倍に膨らむので、**線の上でpackedのままにするだけで転送量が1/4**になる。
6. downloadはconsole(FS CDC)で**0.72〜0.80 MB/s**。4 MiBに5.8秒。**深さを使い切ると律速はここ**。

**近直の目標への到達**: **「2chで数十Msps」は160 Mspsで達成(約5倍)**、**「ローカルで`.sr`保存」も達成**、**「ch単位の詰め替え」はpackedのまま運んでhostで展開する形で達成**(転送量1/4)。

**候補**: capture側は2 channelについては**完了扱いでよい** / **downloadをOTG HSのvendor bulk(10.74 MB/s)へ差し替える** — 経路を変えるだけで**4 MiBが5.8秒 → 0.39秒**。

**未決**: OTG HS経由との通し `—`(いまHS portは2枚目へ配線)/ 連続streaming `—` / 4 MiB超の深さ `—` / **外部信号** `—`(信号源は内部PWM)/ 3 channel以上 `—` / triggerとの組み合わせ `—`。

### E073 ESP32-P4: HSでのHID限界throughput — 完了 2026-09-12

全文: [e073_p4_hs_hid_throughput/README.ja.md](e073_p4_hs_hid_throughput/README.ja.md)。[E072](e072_p4_hs_device_to_host_native/README.ja.md)と同じ2枚直結ベンチ。device = `EspUsbDeviceHidVendor`、host = `onHIDVendorInput()`。

**事実**

1. **既定(64 B endpoint)で0.517 MB/s。** [harness-channels](../references/harness-channels.ja.md)の「HID = interrupt 64 B/1 ms = 64 kB/s」は**full speedの値**で、**HSでは約8倍**。
2. **帯域はpacket sizeに比例する。** 64 / 128 / 512 Bで **0.517 / 1.034 / 4.136 MB/s**。`packet size × 8,000/s`がそのまま出る(HSのmicroframe周期125 us)。
3. **1 microframeあたり1 transactionのみ。** high-bandwidth(最大3回)は使われていない → **理論上あと3倍**。
4. **1,024 Bは動かない。** `begin()`は通るがstreamが流れない。`EspUsbHost`側のperiodic FIFO配分が疑わしい([HR-3](../references/espusbhost-change-requests.ja.md))。
5. **`EspUsbDevice`はHIDのpacket sizeを2か所でハードに64 Bへ縛っている**(`begin()`の`reportSize_ <= 63`、`configurationDescriptor()`の`mps > 64`)。`CFG_TUD_HID_EP_BUFSIZE`も`#ifndef`ガード無し → [CR-8](../references/espusbdevice-change-requests.ja.md)。
6. **HID(512 B)は vendor bulk(10.74 MB/s)の約40%** まで届き、しかも**driverレス**で**帯域が予約される**。

**候補**: **HIDを「帯域不足」として捨てない。** driverレスを優先するならHID(512 B)、生帯域を優先するならvendor bulk。2 channelのPARLIO captureなら**HID 512 Bでも約16.5 Msps相当**。

**未決**: 1,024 Bが動かない理由 `—` / high-bandwidth transactionを使えるか `—`(使えれば3倍)/ PCをhostにしたときの値 `—` / HID OUT方向 `—` / HID interfaceを複数並べたら合算されるか `—`。

**反映**: [harness-channels](../references/harness-channels.ja.md) §USBクラス8種の得失の**HID行はFS前提**で、HSでは書き直しが要る。

### E072 ESP32-P4: 2枚直結でのdevice → host bulk IN — 完了 2026-09-12(**仮説は反証された**)

全文: [e072_p4_hs_device_to_host_native/README.ja.md](e072_p4_hs_device_to_host_native/README.ja.md)。board 1 = device(EspUsbDevice 2.2.0、TX FIFO 8 KiB)、board 2 = host(EspUsbHost 2.8.0)、**OTG HS port同士を直結**。4 MiB × 5回。

**事実**

1. **直結の方が遅い。5.6 MB/s**で、usbip + PC経由の10.74 MB/sの約半分。**仮説「PCを外せば速くなる」は反証された。**
2. **原因はhost側の読み方。** `chunks=8192`、`max_chunk=512` — **512 Bずつ8,192回**受けている(4 MiB ÷ 512 B と一致)。
3. **`EspUsbHost`の継続IN(`READ_CONTINUOUS`)はendpointの`wMaxPacketSize`ぶんを1転送ずつ投げる。** `device->usbVendorInPacketSize = inEndpoint.maxPacketSize`で固定され、**転送サイズもqueue深さも指定できない**。**OUT側には`vendorWriteQueueBegin(depth, bufferBytes, ...)`があるのにIN側に無い。**
4. **device側の`stalls`は88,914**(PC host相手では28,844)。**deviceはhostを待っており、まだ余裕がある。**
5. HSで繋がり(`speed=2`)、5回とも4 MiB全部が届いて欠落0。

**天井の現在地**

| 経路 | hostの読み単位 | 実測 |
|---|---|---:|
| P4 device → **P4 host**(継続IN) | **512 B × depth 1** | **5.6 MB/s** |
| P4 device → PC(usbip + libusb) | 1 MiB URB × depth 1 | **10.74 MB/s** |
| P4 host → device(async queue) | 8 KB × **depth 2** | **36.4 MB/s**(ライブラリ側実測) |

**ここまでの測定はどれも「hostの読み方」か「deviceのFIFO」で頭打ちで、deviceの本当の天井にはまだ届いていない。**

**候補**: PCを外しても速くなるとは限らない — **経路の長さよりhostの読み単位が効く**。

**未決**: **`EspUsbHost`のIN側にasync queueが入ったらいくつ出るか** `—`([HR-1](../references/espusbhost-change-requests.ja.md)。**本命**)/ deviceの本当の天井 `—` / `vendorReadSync()`で転送サイズを指定できるか `—`。

**副産物**: **usbipは思ったほど悪くない。** 1 MiB URBのPC hostの方が512 BのP4 hostより速い。

### E071 ESP32-P4: device側 vendor bulkの天井 — 送信FIFOの深さ — 完了 2026-09-12

全文: [e071_p4_hs_vendor_fifo_depth/README.ja.md](e071_p4_hs_vendor_fifo_depth/README.ja.md)。EspUsbDevice 2.2.0のコピーで`CFG_TUD_VENDOR_TX_BUFSIZE`だけを振った。

**事実**

1. **512 B → 8 KiBで9.03 → 10.59 MB/s(+17%)。** ばらつきも6.79–10.02 → 10.27–10.76へ縮む。
2. **8 KiBで飽和。** 16 KiB 10.33、32 KiB 10.39で差が無い。
3. **64 KiBでは動かない。** `usb_ready=1`だが**`mounted=0`**でhostがconfigureせず、転送も全て失敗。
4. `stalls`は39,746 → 28,844で**下げ止まる**(1 packetあたり約3.5回のspinが残る)。
5. **同じP4がhost役では36.4 MB/s**([EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)の`vendor_bulk_throughput`、**async queue depth 2**、8 KB転送)。device役の10.74 MB/sは**その約30%**で、**FIFOの深さで説明できるのは17%だけ**。
6. host read sizeは深いFIFOでもまだ効く(64 KiBで8.31、4 MiBで10.74)→ usbipのoverheadは残っている。

**候補**: vendor bulkのTX FIFOは**8 KiBを既定に**(採用)/ 16 KiB以上は無意味、**64 KiBは壊れる** / 次に効くのは**FIFOではなく「同時に投げる転送の数」**([CR-7](../references/espusbdevice-change-requests.ja.md))。

**未決**: **転送を2つin-flightにしたらどこまで伸びるか** `—`(**本命**)/ usbipのoverheadの大きさ `—`(**2枚目のP4をEspUsbHostでhostにしてHS port同士を繋げばPCを外せる。要配線**)/ 64 KiBでmountしない理由 `—` / RX側・CDC・HIDでの同じ掃引 `—`。

### E070 ESP32-P4: vendor bulkのstack比較(core内蔵 対 EspUsbDevice) — 完了 2026-09-12

全文: [e070_p4_hs_vendor_stack_compare/README.ja.md](e070_p4_hs_vendor_stack_compare/README.ja.md)。read size 1 MiB / 4 MiB転送 / 各15回を**背中合わせ**で測った(usbip経由)。

**事実**

1. **帯域はcore内蔵stackが速く安定。** median **9.41 対 7.57 MB/s(+24%)**、ばらつきは7.90–9.70(±9%)対5.83–10.01(**±27%**)。
2. **EspUsbDeviceはFIFO待ちが約1.9倍**(stalls median 57,980対30,606)。同じ送出loop・同じ512 B FIFOなので、**差はstackがFIFOを掃き出す速さ**にある。
3. **descriptorの正しさはEspUsbDeviceが上。** **core内蔵はDEVICE_QUALIFIERにもOTHER_SPEED_CONFIGURATIONにも答えない(STALL)** — USB 2.0でHS deviceに必須。[E063](e063_p4_usb_hs_enumerate/README.ja.md)・[E069](e069_p4_hs_vendor_bulk_rate/README.ja.md)でUsbTreeViewが`ERROR_GEN_FAILURE`を出していた正体。
4. **ただしEspUsbDeviceのOTHER_SPEED_CONFIGURATIONは中身が誤り** — FS側のbulkに`wMaxPacketSize=512`(FSの上限は64)。`EspUsbDeviceVendor::configurationDescriptor()`がper-speedの`endpointSize`を`(void)`で捨て、constructor値を両速度に使うため。
5. **data完全性は両方とも0/15。** [E068](e068_p4_hs_cdc_tail_loss/README.ja.md)のCDCは30転送中4件(13%)だったので、**あの欠落はCDC class側の問題である疑いがさらに強まった**。
6. flashはEspUsbDeviceが**約18 KB小さい**(374,242 対 392,098 B)。
7. **MS OS 2.0はどちらも正しく答えるのにWindowsはどちらでもbindしない** → [E069](e069_p4_hs_vendor_bulk_rate/README.ja.md)のWindows側の問題は**stackと無関係**と確定。

**訂正(同日)**: 上の帯域比較は`--build-property 'build.extra_flags=...'`で潰れたビルドで測っていた。`build_opt.h` + `--clean`で測り直すと**median 9.04 対 9.03 MB/sでほぼ同じ**、`stalls`比も1.9倍ではなく約1.18倍。残る差は**ばらつき**(core 8.68–9.11、EspUsbDevice 6.79–10.02、各25回)。さらに[E071](e071_p4_hs_vendor_fifo_depth/README.ja.md)でTX FIFOを8 KiBにするとEspUsbDeviceは**10.59 MB/s**まで伸び、core内蔵はFIFOを動かせないため**EspUsbDeviceが上回る**。

**どちらを使うか(当初の判断)**: **いま帯域が要るならcore内蔵**(速く、ばらつきが小さい)。**HS deviceとして正しく振る舞わせたいならEspUsbDevice**。そして**512 B FIFOを深くできるのはEspUsbDeviceだけ**なので、[E069](e069_p4_hs_vendor_bulk_rate/README.ja.md)の「1 microframeあたり約2.4 transaction」という天井を超える道はそちらにある — **現状の−24%はFIFOを深くした効果で逆転しうる**。

**未決**: EspUsbDeviceのFIFO待ちが多い理由 `—` / **FIFOを深くしたときの効果** `—`(`p4-hs-vendor-fifo-depth`。**EspUsbDeviceでしか試せない。次の問い**)/ ばらつき±27%の理由 `—` / usbipなしの値 `—` / vendor OUTはBでは未確認 / HID throughput `—`。

**ベンチの知見**: usbipdの`bind`は**VID:PID + device instanceに紐づく**ので、PIDやserialを変えるたびに管理者権限のbindが要る。**usbipで測る実験はidentityを固定する**。またarduino-cliは**symlinkしたlibrary dirの`.cpp`を拾わない**(Library Manager経由なら問題なし)。

### E068 ESP32-P4: USB HS CDCの欠落は消失か滞留か — 完了 2026-09-12(**問いの前提が誤っていた**)

全文: [e068_p4_hs_cdc_tail_loss/README.ja.md](e068_p4_hs_cdc_tail_loss/README.ja.md)。run: `_runs/E068_20260912T05*`。4 MiB / chunk 4,096 B / 送出taskはcore 0 / captureなし、30回。

**事実**

1. **欠落は「末尾」ではなく「転送の途中」で起きている。** 受信streamは`前半 + (gap) + 後半`で、**後半は正しく届く**。[E067](e067_p4_usb_vs_capture_core/README.ja.md)がbyte数だけを数えていたため末尾欠落に見えていた。
2. **dataは失われている。滞留ではない。** 2秒待っても0 byte、`T`で突いても返るのは**16 byteのterminatorだけ**。欠けた分は二度と来ない。
3. **30回中4回(13%)。** 欠落量は2,048 B(4 packet)×2、2,560 B(5 packet)×2で、**すべて512 B = bulkの`wMaxPacketSize`の整数倍**。位置は2.2〜3.7 MiBにばらける。
4. **deviceは気づかない。** 30回すべて`usb_written`は全byte、`usb_short`は0。**`USBCDC::write()`の戻り値は「線に出た」ことを意味しない。**
5. **突いた後の経路は正常**(terminatorは4件とも届く)。endpointが止まるのではなく、**途中のpacketだけが消える**。
6. 実効帯域のmedianは7.82 MB/sで[E066](e066_p4_usb_hs_tx_context/README.ja.md)と整合。

**候補(未確認)**: **TinyUSBのCDC TX FIFOに対するapplication taskとusbd taskの跨core競合**。`usbd` taskは`esp32-hal-tinyusb.c:886`で**core指定なし**、`USBCDC::write()`の`tx_lock`はapplication側しか直列化しない。なお[E064](e064_p4_usb_hs_cdc_rate/README.ja.md)は8転送、[E066](e066_p4_usb_hs_tx_context/README.ja.md)は6転送をpattern照合して不一致0で、13%なら約2件出る計算 — **発生率は送出taskのcoreに依存する可能性がある**(E064の送出は`loop()`= core 1)。

**未決**: **送出core別の発生率** `—`(`p4-hs-cdc-drop-rate-by-core`。次の問い)/ **USBPcapで線上を見る** `—`(線に出ていないのかhostが捨てているのか未判定)/ vendor bulkやFS側でも起きるか `—` / ESP-IDF直でFIFOを深くしたら変わるか `—` / 転送量との関係 `—`。

**影響**: **この経路は現状そのままではlogic analyzerのdownloadに使えない。** 13%の転送で数KiBが黙って消え、device側もhost側も気づかない。E064〜E067の帯域の数値(device側時計)は有効だが、**「その帯域でdataが正しく渡る」とは言えない**。当面は**転送に長さとCRCを付け、hostが検証して再送を要求できるようにする**こと、**pattern照合なしの転送を信用しない**ことが要る。

### E067 ESP32-P4: captureとUSB送出のcore競合 — 完了 2026-09-12

全文: [e067_p4_usb_vs_capture_core/README.ja.md](e067_p4_usb_vs_capture_core/README.ja.md)。採用run: `_runs/E067_20260912T04*`。2 channel / 32 MHz(8.0 MB/s)固定で、振ったのはcore配分だけ。

**事実**

1. **captureは同時実行の影響を受けない。** 18回すべてで**8.00 MB/s**(min 7.999 / max 8.000)、queue overflow 0、`callback_bytes`と`copied`の差0。USBが何をしていても揺れなかった。
2. **落ちるのはUSB側だけで7〜16%。** 最良`cap1_usb0`が単独比93.1%、最悪`cap0_usb0`が84.0%。
3. **最良は`cap1_usb0`(harvest = core 1、USB = core 0)の7.42 MB/s**で、仮説の予測どおり。
4. **ただし理由は仮説と違う。** 分離の効果は93%対84%の**約9 point**、対して「USBをcore 0へ」は単独時点で7.97対5.96の**+34%**。**順位を決めているのは主にUSB taskのcore**で、分離はその上の小さな上積み。
5. **同居でも配置次第で分離を上回る**(`cap0_usb0` 6.69 > `cap0_usb1` 5.52)。**反証条件3が部分的に発火** — 「分ければ速い」は成り立たない。
6. [E066](e066_p4_usb_hs_tx_context/README.ja.md)のcore依存はcapture負荷の下でもそのまま残る。

**経路の異常(観測として記録、[README.ja.md §7-6](README.ja.md))**: 掃引完走までの5回中**4回で、device側は全byte書き終えているのにhost側へ末尾が届かない**現象が出た。欠落量は**常に512 B(bulkの`wMaxPacketSize`)の整数倍で2,048〜5,120 B**。送出後に10 ms×10回flushしても届かず(FIFO 512 Bには収まらない量)、**転送長を512 Bの整数倍から外しても消えず**、発生するmodeは実行ごとに変わる。**原因未特定。downloadが末尾を静かに失う経路は使えないので、次に潰すべき最優先**(`p4-hs-cdc-tail-loss`)。帯域の数値はdevice側時計なのでこの欠落に影響されない。

**候補**: **USB送出はcore 0、captureのharvestはcore 1**(採用)/ 譲るならUSB側(captureの方が丈夫)/ **転送完了をhostが確実に知る仕組み**(長さの事前通知・終端marker・CRC)が要る。

**未決**: **末尾欠落の原因** `—`(**最優先**)/ captureが折れるsample rate `—`(32 MHzでは全く揺れない)/ 真のstreaming(ring → USBの受け渡しを挟んだ費用) `—` / vendor bulk `—` / core 1が遅い理由 `—`。

**近直の目標への含み**: 生成8.00 MB/sに対し排出は最良7.42 MB/sで釣り合わない。**釣り合う点は約29.7 Msps**。つまり**2chで約30 Mspsまでは連続streamingが成立する見込み**(末尾欠落を潰した上で)。それ以上はPSRAMへbatchしてから出す。

### E066 ESP32-P4: CDCの帯域は送出contextで決まるか — 完了 2026-09-12

全文: [e066_p4_usb_hs_tx_context/README.ja.md](e066_p4_usb_hs_tx_context/README.ja.md)。run: `_runs/E066_20260912T030*`。1実行で18条件(6 mode × 3)。

**事実**

1. **帯域はcontextで変わる。最大と最小の比は1.53倍**(8.08 対 5.27 MB/s)。
2. **効いているのはpin先coreである。** core 0で走った12条件はすべて**7.43〜8.11 MB/s**、core 1で走った6条件はすべて**5.24〜5.74 MB/s**で、**2群は重ならない**。
3. **優先度は効かない。** core 0の中で優先度1 / 5 / 20は7.96 / 7.64 / 8.08 MB/sで、3回ずつの範囲が重なる。**[E065](e065_p4_usb_hs_dual_cdc_rate/README.ja.md)の「効くのは優先度」は誤り。**
4. **`loop()`が遅いのはcore 1で走るから。** `ARDUINO_RUNNING_CORE`は1で、`loop`の5.60 MB/sは同じcoreに置いた専用task(5.27 MB/s)とほぼ同じ。
5. **[E064](e064_p4_usb_hs_cdc_rate/README.ja.md)の5.59 MB/sを`loop` modeが5.60 MB/sで再現した。** E064とE065の+42%差は**core placementで説明され、2本目のCDCの有無ではない**。E065の事実5は方向は正しく、**帰属が誤っていた**。
6. pinしない場合は3回ともcore 0で走り、core 0群に入る(7.67 MB/s)。
7. 最速でも8.08 MB/s ÷ 512 B = **1 microframeあたり約1.97 transaction**(HSは13まで)。**coreを変えても天井の性質は変わらない。**
8. pattern検証6回で不一致0、短write 0、stall 0。

**候補**: **USBへ流すtaskは`loop()`と別coreへpinする**(採用。`ARDUINO_RUNNING_CORE`=1なのでcore 0)。優先度は既定でよい。[E061](e061_p4_drain_core_split/README.ja.md)の「回収を別coreへ移すとdrainが上がる」と**同じ形の効果**。

**未決**: **core 1が遅い理由** `—`(`USB.begin()`が`setup()`= core 1から呼ばれるのでUSB割り込みがcore 1に登録されているはず、という構造の読み。未測定。`p4-usb-isr-core`)/ 1.97 transaction/microframeの天井を超える手段 `—` / **PARLIO回収も別coreを欲しがるので、captureとUSB送出でcoreを取り合う** `—`(`p4-usb-vs-drain`の中心)/ vendor bulkでの同条件 `—`。

**近直の目標への含み**: 8.08 MB/sは2 channelのPARLIOなら**約32.3 Msps相当の連続streaming**で、「2chで数十Msps」は届く。ただし条件は「送出taskをcore 0」であり、**PARLIO回収との両立は未測定**。

### E065 ESP32-P4: CDCを2本にすると合計帯域は上がるか — 完了 2026-09-12(**仮説は反証された**)

全文: [e065_p4_usb_hs_dual_cdc_rate/README.ja.md](e065_p4_usb_hs_dual_cdc_rate/README.ja.md)。run: `_runs/E065_20260912T025*`。

**事実**

1. **2本にしても合計帯域は上がらない。** 1本7.94 MB/sに対し2本合計7.49 MB/s(比**0.943**)で、わずかに下がる。**律速はendpointごとのturnaroundではなく共有部分にある**(反証条件1が発火)。
2. **2本のとき帯域はほぼ等分される**(device側4.63と3.74 MB/s)。片方を増やせば他方が減る形で、増分は無い。
3. **port 0(core 0にpin)が常にport 1(core 1)より速い**(906〜927 ms 対 1,100〜1,121 ms、3回とも同順)。
4. **共有部分の候補はTinyUSBのdevice task。** `esp32-hal-tinyusb.c:886`は`xTaskCreate(usb_device_task, "usbd", 4096, NULL, configMAX_PRIORITIES - 1, NULL)`で、**全endpointを1本の最高優先度・core非指定taskが捌く**。**構造の読みであって、この実験が直接測ったものではない。**
5. **同条件(1 port / 4 MiB / chunk 4,096 B)で[E064](e064_p4_usb_hs_cdc_rate/README.ja.md)の5.59 MB/sに対し7.94 MB/s(+42%)。** 違いは送出の実行contextで、E064は`loop()`(`loopTask`、優先度1、pin済み)、E065は**専用task(優先度5、coreにpin)**。**ただしE065は2本目のCDCもbuildに含むので変数が1つではない。統制した比較が要る。**
6. 7.94 MB/s ÷ 512 B = 約15,500 transaction/s = **1 microframeあたり約1.94 transaction**(HSは13まで許す)。上限は依然turnaround。
7. pattern検証3回で不一致0、短write 0、stall 0。

**候補**: 送出は`loop()`ではなく専用taskから(事実5。統制前)/ **endpointを増やすのは効かない**(却下)/ 残る手段は共有service taskの負荷を下げる・1 transferのbyte数を増やす(Arduinoでは不可)・**PSRAMへbatchしてから出す**。

**未決**: **事実5の統制** `—`(`p4-hs-tx-context`。次の問い)/ 共有部分が本当にusbd taskか `—` / port 0と1の非対称の理由 `—` / vendor bulkでも同じ天井か `—`(事実1から**vendorに期待する理由は弱まった**)/ 3本以上は`CFG_TUD_CDC=2`のため不可。

**近直の目標への含み**: 7.94 MB/sは2 channelのPARLIO(1 byteに4 sample)なら**約31.8 Msps相当の連続streaming**。「2chで数十Msps」は射程に入ったが、事実5の統制が先。

**反映**: [harness-channels](../references/harness-channels.ja.md) §CDCの上限は endpoint 予算に「**本数を増やしても帯域は増えない**」を実測で付ける。**仕様のstatusは動かない**。

### E064 ESP32-P4: USB HS CDCのdownload帯域 — 完了 2026-09-12

全文: [e064_p4_usb_hs_cdc_rate/README.ja.md](e064_p4_usb_hs_cdc_rate/README.ja.md)。run: `_runs/E064_20260912T024*`。1実行で24条件。

**事実**

1. **実効帯域は約5.6〜5.7 MB/sで飽和する。** chunk 512 B以上ではどの値でも同じ(512 B / 4 KiB / 16 KiB / 64 KiBのmedianが5.60 / 5.74 / 5.70 / 5.73 MB/s)。
2. **chunk 512 Bが膝。** 64 Bでは1.51 MB/s(飽和値の約1/3.8)。512 BはHS bulkの`wMaxPacketSize`であり`CONFIG_TINYUSB_CDC_TX_BUFSIZE`の値でもある。
3. **転送量1 → 16 MiBで帯域は落ちない**(5.51 → 5.65 MB/s)。**16 MiBは2.968秒**で出る。
4. **律速はhost側readerではない。** device側`esp_timer_get_time()`とhost側`perf_counter()`の比が全24条件で1.000〜1.001。**USB経路そのものが上限**。
5. **data化けと欠落は無い。** 検証した8条件で全word一致、短write 0、stall 0。
6. **HS bulk理論上限53.2 MB/sの約10.6%。** 5.6 MB/s ÷ 512 B = 約10,940 transaction/s、microframeは8,000回/秒なので**1 microframeあたり約1.37 transaction**。HSは13まで許すので、**帯域ではなくturnaroundが上限**と読める。TX FIFOがbulk 1 packet分しかないことと整合する。
7. **旧ベンチ(CH343 6 Mbaud、約600 KB/s)の9.4倍。** 16 MiBの見積り約28秒 → **実測2.968秒**。
8. PIDを`1209:0003`に変えるとWindowsは**COM9**を割り当てた(E063の`1209:0002`はCOM8)。PIDごとに別instanceになることの傍証。

**候補**: chunkは4 KiB以上を既定に(採用)/ 帯域が要る経路はvendor bulkへ逃がす(未検証。[harness-channels](../references/harness-channels.ja.md) §6cと同じ結論に実測から到達)/ device側とhost側の時間を両方記録する型(採用)。

**未決**: **vendor bulk(WinUSB)ならいくつ出るか** `—`(`p4-hs-cdc-vs-bulk`。次の問い)/ CDCのTX FIFOを512 Bより深くできるか / 1 microframe 1.37 transactionの直接確認 `—` / CDC複数本の合計帯域(`p4-cdc-budget-hs`)/ OUT方向 `—` / PARLIO同時動作(`p4-usb-vs-drain`)/ usbip経由との差 `—`。

**近直の目標への含み**: 2 channelのPARLIOは1 byteに4 sampleを詰めるので、**5.6 MB/sは約22.4 Msps相当の連続streaming**。「2chで数十Msps」を連続で出すにはvendor bulkで上げるか、**PSRAMへbatchしてから出す**(16 MiB = 2ch/50 Mspsで約1.34秒ぶん、downloadは2.968秒)かのどちらかになる。

**反映**: [p4-logic-analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) §後段のdownload時間の表を実測で置き換え。**仕様のstatusは動かない**。

### E063 ESP32-P4: USB 2.0 OTG HSは列挙するか — 完了 2026-09-12

全文: [e063_p4_usb_hs_enumerate/README.ja.md](e063_p4_usb_hs_enumerate/README.ja.md)。採用run: `_runs/E063_20260912T023520Z_default` / `T023605Z` / `T023646Z`。

**事実**

1. **Arduino-ESP32 3.3.11はP4のOTG HSをTinyUSB deviceとして立ち上げ、High-Speedでnegotiateする。** device側`tud_speed_get()=2`が10 runすべてで同値。UsbTreeViewも`Device Bus Speed 0x02 (High-Speed)`、`bcdUSB 0x200`、**3本のendpointすべて`wMaxPacketSize=512`**。`bMaxPacketSize0`だけ64。
2. **USB-Serial-JTAG(FS)とOTG HS(HS)は同時に成立する。** 1 chipから`303a:1001`と`1209:0002`の2 deviceが同時にWindowsへ列挙され、usbipdでも別busid(`3-1` / `3-2`)。**書込み口を失わずにHS側の構成を変えられる** — 単一portのESP32-S3([E013](e013_usb_descriptor_profiles/README.ja.md))には無い性質。
3. **単機能CDCのつもりの構成がWindowsではcomposite(`usbccgp`)になる。** Arduino-ESP32のUSB stackがIADを付けて`0xEF/0x02/0x01`を名乗るため。COM portは子devnode`&MI_00`側に`usbser`で生え、**driver追加なしにCOM8が出た**。
4. **P4ではUSB serial stringが`"0"`になる。** `cores/esp32/USB.cpp`の`USB_SERIAL`既定は`CONFIG_IDF_TARGET_ESP32S3`のときだけ`"__MAC__"`、他targetは`"0"`。Windowsのinstance IDは`USB\VID_1209&PID_0002\0`。**同じfirmwareの2枚目のP4はWindows上で同一instanceを名乗る。**
5. **Windowsはbus speedをPnP propertyとして公開しない。** `Get-PnpDeviceProperty`の全keyで確認。速度の証拠はUsbTreeView(またはUSBView)のdumpに依る。
6. **Device Qualifier Descriptorの取得が`ERROR_GEN_FAILURE`。** HS deviceでは必須のはずだがWindowsは許容した。
7. **interrupt IN endpointが512 B / `bInterval=1`(HSでは125 us)で開いている。** notificationだけで大きな帯域を予約しており、CDCを増やすときのendpoint予算に効く。
8. **console経路(usbip)が不安定な側。** harness修正後13回中10 pass。失敗3回は**console無出力で15秒timeout**が1回(usbip経由CDCで20秒級の遅延)、**usbipd attach断での書込み失敗**が2回で、**HS側の観測には関与していない**。timeout 45秒で3回連続pass。
9. board `esp32-p4-30eda0e31478` は rev 1.3 / flash **16 MiB** / PSRAM **32 MiB**。E014〜E061の`esp32-p4-e8f60ae0aa24`とは**別個体**で、flash容量が違う。

**候補**: consoleを`HWCDC`の自前宣言で保持する型(採用)/ Windows観測をWSLから`powershell.exe`で駆動する型(採用)/ 速度とdescriptorの証拠はUsbTreeViewのdump(採用)/ 実験ごとにpid.codes test範囲の別PIDを取る(採用。E062の`1209:0001`は温存)。

**未決**: throughputは未測定 `—`(`p4-hs-bulk-rate` / `p4-hs-cdc-vs-bulk` / `p4-batch-download` / `p4-stream-throughput`)/ serial `"0"`を上書きするか([ecosystem-any-hardware](../references/ecosystem-any-hardware.ja.md) §4.5の方針とcore既定の食い違い。E062と同じ論点)/ Device Qualifier無応答が他hostでも許容されるか / interrupt EPの予約がCDC複数本のendpoint予算に効く量(`p4-cdc-budget-hs`)/ 外部hubを介さない直結での再確認 / usbip遅延の原因 / Linux側の列挙は未取得 `—`。

**反映**: [p4-logic-analyzer予備調査](../references/p4-logic-analyzer-investigation.ja.md) §後段の「配線を変えない限り測れない」を解消済みとして更新。**仕様のstatusは動かない**(boardの能力の測定であってprotocolの検証ではない)。

### E061 ESP32-P4: 回収を別coreへ移すとdrainは上がるか — 完了 2026-09-09

全文: [e061_p4_drain_core_split/README.ja.md](e061_p4_drain_core_split/README.ja.md)。採用run: `_runs/E061_20260909T132131Z_default/`。

**事実**

1. Arduinoのloop taskはcore 1。要求 −1と1が同一core、要求 0が分離条件。同一coreの2条件は未読最大54,656で完全一致し対照として機能した。
2. **core分離でISR側の未読最大が54,656→32,256(−41%)、逆算したdrainが82.3→119.7 MB/s(+45%)。**
3. **memcpy帯域も107.6→131.0 MB/s(+22%)。** [E059](e059_p4_drain_breakdown/README.ja.md)の107 MB/sは計時区間の内側でISRがmemcpyを中断していた分を含んでいた。[E020](e020_p4_psram_copy_bandwidth/README.ja.md)のDMA無し138.6〜182.7との残差5〜25%が本当のmemory競合分。
4. 分離後もdrain 119.7はmemcpy帯域131.0の91%で、残る9%は`xQueueReceive`とloop本体。core分離では消えない。
5. **尖頭未読の式は分離条件でも当たる。** drain 119.7で予測32,244に対し実測32,256、差12 byte。
6. 条件2への効果は8 channel・160 MHz・ring 15 chunkで許容window byte長が約107,900→約208,000、**約1.9倍**。逆にwindow長固定ならring容量が半分で済む。

**候補**: 実装では回収をISRと別coreへ置く。driverの生成・enableをloop task上で行い、回収loopだけを`xTaskCreatePinnedToCore`で別coreへ出す。条件2の`drain(rate)`はcore配置ごとの値として持つ。

**未決**: 分離時のdrainのrate依存(本実験は160 MHzのみ) / 残る9%の内訳 / 回収coreで他taskが走る場合の劣化 / ISR自体を明示的に別coreへ割り当てる方法 / triggerなしspool経路でも同じ改善が出るか。

### E060 ESP32-P4: chunkのまとめ取りでdrainは上がるか — 完了 2026-09-09

全文: [e060_p4_drain_batch_coalesce/README.ja.md](e060_p4_drain_batch_coalesce/README.ja.md)。採用run: `_runs/E060_20260909T115516Z_default/`。

**事実**

1. **`xQueueReceive`のまとめ取りは効果ゼロ。** 1 batchの最大が1から12へ増えてもmemcpy時間は0.4%しか変わらず、**ISR側の未読最大は54,656で完全に同一**。
2. **memcpyのまとめも効かない。** 呼び出しが1,070→277(3.9倍減)、平均copy sizeが3,920→15,142 byteになってもmemcpy時間は1%減、帯域は107.8→108.8 MB/s。DMA無しの[E020](e020_p4_psram_copy_bandwidth/README.ja.md)では4 KiBと16 KiBで181と182.7なので、**DMAが動いている状態ではcopy sizeが効かない。**
3. memcpyの累積時間は3条件でほぼ一定(39,078 / 38,912 / 38,535 us)。queue呼び出し回数もmemcpy呼び出し回数も実際のcostに寄与していない。
4. **したがって[E059](e059_p4_drain_breakdown/README.ja.md)の3.6〜5.2 us/chunkはdriver側のISRが支配しており、task側の書き方では下がらない。** 残る手は回収を別coreへ移すことだけ。
5. **memcpyまとめでは未読の測定値が54,656→70,784へ悪化したがdataは正常だった。** `consumed_bytes`がrun単位でしか進まないため最大1 run分(48 KB)遅れて見える。未読70,784は18 chunk相当でringの完全chunk 15個を超えるのに破綻していない。**未読の実測を条件2の判定に使えるのはper-chunk copyのときだけである。**

**候補**: まとめ取りもmemcpyのまとめも入れず、per-chunk copyを維持する(`consumed_bytes`が読み出し位置に密着し未読の実測が意味を持つ)。drainを上げる残りの手はcore分離。

**未決**: **回収を別coreへ移した場合の効果**(task側で残る唯一の手) / ISR本体の実行時間の直接測定 / 80〜100 MHzでmemcpy帯域が飽和する理由 / triggerなしspool経路でも同じ結論になるか。

### E059 ESP32-P4: drain低下はmemory競合かISR overheadか — 完了 2026-09-09

全文: [e059_p4_drain_breakdown/README.ja.md](e059_p4_drain_breakdown/README.ja.md)。採用run: `_runs/E059_20260909T114649Z_default/`。

**事実**

1. **memcpy帯域は100 / 120 / 160 MHzで107.8 / 107.2 / 107.2 MB/sと一定。** drainは95.8 / 92.0 / 82.3 MB/sと下がるので、**drain低下はmemcpyの帯域低下ではない。**
2. 80 MHzのmemcpy帯域は128.2 MB/sで100 MHz以上より16%高い。memory競合は100 MHz以上で飽和し、そこから先のdrain低下には寄与しない。
3. **window byte長固定でwindowあたりのchunk数は23.8個と同じなのに、memcpy占有率は89% → 86% → 77%と下がる。** 1 chunkあたりの非memcpy時間は4.2 / 4.7 / 5.8 usでほぼ一定で、**windowが短くなる分だけ固定costが相対的に大きくなる**のが原因である。
4. 本実験の160 MHzのdrain 82.3はE058の86.1より3.8 MB/s低い。計測のtimer 2回分で1 chunkあたり約0.6 us。差し引くと真の非memcpy時間は3.6〜5.2 us/chunk。
5. gated capture中のmemcpy帯域107 MB/sは[E020](e020_p4_psram_copy_bandwidth/README.ja.md)のDMA無し138.6〜182.7 MB/sより25〜40%低い。この損失はDMAのrateに依存しない。

**候補**: 条件2の`drain(rate)`を「107 MB/s × memcpy占有率(rate)」と分解する。改善は1 chunkあたりのcostを削る方向(複数chunkのまとめ取り、queueを介さない回収、別coreへの分離)。chunk sizeはSoC定義で4,032固定なので大きくできない。memcpy帯域を上げる余地は小さい。

**未決**: 1 chunkあたり3.6〜5.2 usの内訳(ISR本体・`xQueueReceive`・loop本体の分離) / 複数chunkのまとめ取りの効果 / 別coreへの分離の効果 / 80〜100 MHzでmemcpy帯域が飽和する理由 / triggerなしspool経路でも同じ分解が成り立つか。

### E058 ESP32-P4: window中のdrain帯域はrateに依存するか — 完了 2026-09-09

全文: [e058_p4_window_drain_vs_rate/README.ja.md](e058_p4_window_drain_vs_rate/README.ja.md)。採用run: `_runs/E058_20260909T105237Z_default/`。

**事実**

1. window byte長96,000固定で80 / 100 / 120 / 160 MHzの4条件すべて正常に取れた(飛び42〜44対期待43、階差15 / 15、queue overflow 0)。
2. **task側の標本化はちょうど1 chunk(4,032 byte)分だけ尖頭を見落としていた。** 差は4条件すべてで正確に4,032。ISR内で標本化すれば取れる([E057](e057_p4_gated_ring_boundary/README.ja.md)の制約が外れる)。
3. **未読には約8,064 byte(2 chunk)の床がある。** 過負荷が生じない80 MHzでもこの値が出る。chunk通知とqueue投入のpipeline分。
4. **drainはrate依存で、rateが上がるほど下がる。** 床を引いた増分から逆算すると100 MHz以下で100 MB/s以上、120 MHzで95.9、160 MHzで86.1 MB/s。window中はDMAがsample rateでringへ書きながらCPUが同じringから読むためと整合する。
5. **尖頭未読は`8,064 + window byte長 × (1 − drain(rate) ÷ rate)`で表せる。** 120 / 160 MHzは予測27,344 / 52,464に対し実測27,328 / 52,416で**48 byte以内**の一致。E057の境界もこの形で実測どおり(gate 3,000は14 chunk ≤ 15、gate 4,000は17 chunk > 15)。
6. [E036](e036_p4_parlio_rate_seq_verify/README.ja.md)の持続spool帯域98 MB/sは、DMA書き込みが98 MB/s程度だった状態の値としてこの曲線上の一点に収まる。
7. 「drain 82 MB/s・床なし」という近似は160 MHzで尖頭を1〜2 chunk小さく見積もる。安全側ではない。

**候補**: 条件2を`ceil((8,064 + window × (1 − drain(rate) ÷ rate)) ÷ chunk) ≤ min(floor(ring ÷ chunk), queue深さ)`とし、`drain(rate)`を表で持つ。未読の測定はISR内で行う。

**未決**: drainのrate依存の内訳(DMA writeとCPU readの分離) / 100 MHz以下のdrainの上限(過負荷が生じず測れない) / 床8,064が`trans_queue_depth`やchunk sizeでどう変わるか / triggerなしspool経路にも同じ床とdrain曲線が当てはまるか。**chunk size 4,032の根拠はSoC定義(`DMA_DESCRIPTOR_BUFFER_MAX_SIZE_64B_ALIGNED` = 4095 − 63)から確定したので実験不要**(レポートに追記)。

### E057 ESP32-P4: 条件2の境界をalias から外したringで実測 — 完了 2026-09-09

全文: [e057_p4_gated_ring_boundary/README.ja.md](e057_p4_gated_ring_boundary/README.ja.md)。採用run: `_runs/E057_20260909T102539Z_default/`。

**事実**

1. **境界はgate 3,000(正常)と4,000(破綻)の間。** 計画の予測(4,000と5,000の間)より1段手前。queue overflowは全条件0でringだけが効く条件だった。
2. alias から外したring 63,488で、gate 1,000〜3,000は飛びが期待境界数と一致し階差15 / 15、gate 4,000と5,000は飛びが47と79(期待32と26)へ急増し階差一致が10 / 15と1 / 15へ落ちた。
3. **予測が外れたのは容量の取り方。** chunk 4,032 byteに対しring 63,488に入る完全chunkは15個(60,480 byte)で末尾3,008 byteは使えない。
4. **実測の未読最大は真の尖頭を過小に見る。** gate 4,000は破綻しているのに未読最大59,456で完全chunk容量60,480を下回る。`未読 > ring容量`のflagはgate 5,000でしか立たずgate 4,000の破綻を見逃した。**判定はwindow長からの計算で行う。**
5. **条件2の最終形**: `ceil(window byte長 × (1 − drain 82 MB/s ÷ rate) ÷ chunk size) ≤ min(floor(ring容量 ÷ chunk size), queue深さ)`。本実験・[E048](e048_p4_gated_rate_ceiling/README.ja.md)・[E050](e050_p4_gated_window_at_fixed_duty/README.ja.md)・[E055](e055_p4_gated_buffer_source/README.ja.md)の**8条件すべてが一致する**。E050のgate 4,000が境界上で正常だったので条件は`≤`。
6. host側判定の誤り3件(field追加に伴うunpackのずれ2件、削除した定数の参照1件)とfirmwareのmsync非整列1件を修正した。firmwareの計測値は最初のrunから変わっていない。

**候補**: `必要chunk数 ≤ min(floor(ring ÷ chunk), queue深さ)`で申告する。ring容量はchunk sizeの整数倍で取る。drop判定は計算で行う。

**未決**: drain帯域82 MB/sの由来 / chunk sizeが4,032固定である根拠 / 未読を尖頭まで捉える標本化 / triggerなしspool経路にも同じ形が当てはまるか / duty 61%付近の条件1の実測。

### E056 ESP32-P4: ring容量とpattern周期のalias — 完了 2026-09-09

全文: [e056_p4_ring_period_alias/README.ja.md](e056_p4_ring_period_alias/README.ja.md)。採用run: `_runs/E056_20260909T100537Z_default/`。

**事実**

1. **pattern周期4,096 byteの整数倍のring(65,536 / 61,440)では飛びが期待どおり22で階差も15 / 15一致。倍数から外したring(63,488 / 59,392)では飛びが172と86に跳ね階差一致は0 / 15。** 4条件すべてqueue overflowは0で、queueは律速でない。
2. **ringは未読がring容量を超えた時点で実際に上書きされている。** これまでの「正常」判定は、検証用gray code rampの周期がring容量を割り切るためのalias による盲点だった。
3. **`未読 > ring容量`は破綻の指標である。** [E055](e055_p4_gated_buffer_source/README.ja.md)の「指標ではない」は誤り。
4. 正しい条件は2本で、条件2の容量は`min(ring容量, queue深さ × chunk size)`。**[E048](e048_p4_gated_rate_ceiling/README.ja.md)のmodelは形としては正しかった。**
5. **[E050](e050_p4_gated_window_at_fixed_duty/README.ja.md)の「window長は無関係」は成り立たない。** duty 50%固定でもgate 4,000(未読61,504)は正常、gate 6,000(92,288)は無効。条件2が許すwindow byte長は約134,000(gate幅4,200 word相当)で実測の分かれ目と一致する。
6. **[E055](e055_p4_gated_buffer_source/README.ja.md)の「ring容量を倍にしても変わらない」も成り立たない。** ring 128 KiBにしたことで正常になっていた。**ringとqueueは両方が独立に効く。**
7. **検証用patternの周期は経路上のどのbuffer sizeも割り切ってはならない。** [E036](e036_p4_parlio_rate_seq_verify/README.ja.md)が定常性の盲点を直したのに対し、今回は周期性の別の盲点だった。
8. E036とE042はpattern周期がring容量を割り切る構成でありながら破綻を検出できていた。持続的な過負荷では上書き量がring容量の整数倍にならないためと考えられるが、切り分けていない。両実験の結論は変わらない。

**候補**: gated captureを条件1と条件2の2本で申告し、条件2の容量を`min(ring容量, queue深さ × chunk size)`とする。今後の検証patternは周期がring容量・chunk size・queue容量・destination容量のいずれも割り切らないように選ぶ。

**未決**: **alias から外したringでのE049・E050の再測**(境界の実測確定) / triggerなしspool経路がalias で盲にならなかった理由 / window中のdrain帯域82 MB/sの由来 / 条件2の境界をring容量付近で細かく測ること。

### E055 ESP32-P4: gated captureの緩衝はringかqueueか — 完了 2026-09-09

全文: [e055_p4_gated_buffer_source/README.ja.md](e055_p4_gated_buffer_source/README.ja.md)。採用run: `_runs/E055_20260909T095625Z_default/`。

**事実**

1. **queueを64から8へ浅くすると、ring容量64 KiBでも128 KiBでも破綻した**(overflow 502 / 448件、飛びが期待22に対し483 / 465、階差一致0 / 15)。**ring容量を倍にしても結果は変わらない。緩衝はchunk queueである。**
2. **ringは周回bufferではなく線形に歩いている。** chunk offsetは4,032 byte刻みで単調増加し、最大offsetはring 64 KiBで62,976、128 KiBで128,000。
3. **`未読 > ring容量`は破綻の指標ではない。** case 1は未読93,760でring容量65,536超なのに正常、case 3は未読90,752でring容量以内。未読はring容量ではなくwindowの過負荷で決まる。
4. 尖頭の未読は`window byte長 × (1 − window中のdrain ÷ sample rate)`で説明できる。192,000 × (1 − 82/160) = 93,600に対し実測93,760。必要queue深さは24 entry以上と計算できる。
5. **[E048](e048_p4_gated_rate_ceiling/README.ja.md)の過負荷modelが、容量をringからqueueへ差し替えた形で復活する。** gated captureの条件は2本立てになる。条件1は`duty × rate × bytes/sample < 持続spool帯域`、条件2は`queue深さ × chunk size > window byte長 × (1 − drain ÷ rate)`。[E050](e050_p4_gated_window_at_fixed_duty/README.ja.md)でwindow長が効かなく見えたのは、queue 64 entry(258 KiB)が160 MHzで約529,000 byteのwindowまで許容し、試した最大224,000 byteが遠く届いていなかったため。
6. ring 64 KiBで未読がring容量を超えてもdataが正常だった機序は未解明。

**候補**: drop判定をring容量基準から`未読 > queue深さ × chunk size`へ直す。ringは`max_recv_size`としての意味しか持たない。

**未決**: ring容量超の未読でdataが正常な機序(`trans_queue_depth`とtransaction終端) / 条件2の境界をqueue深さ24付近で実測 / triggerなしspool経路でも緩衝がqueueなのか(E036はring容量で説明できていたが偶然の一致かもしれない) / chunk sizeが4,032固定である根拠。

### E054 ESP32-P4: RMT DMA modeが受理する`mem_block_symbols`の最小値 — 完了 2026-09-09

全文: [e054_p4_rmt_dma_block_min/README.ja.md](e054_p4_rmt_dma_block_min/README.ja.md)。採用run: `_runs/E054_20260909T094215Z_default/`。

**事実**

1. **DMA modeは`mem_block_symbols` 24 / 32 / 40をすべて`ESP_ERR_INVALID_ARG`で拒否した。** 48は受理。
2. **受理された48は64として振る舞う。** 1 callbackが64 symbol、間隔153,611 us(= 64 × symbol周期)、初回156,934 us。[E053](e053_p4_rmt_dma_block/README.ja.md)のblock 64指定時と完全一致するので、DMA modeは48を64へ丸めている。
3. **DMA modeの初回遅延は常に`64 × symbol周期`で、non-DMAの`48 × symbol周期`より遅い。DMAでは縮められない。**
4. **window timestampの初回遅延の下限は`48 × symbol周期`で確定した。** 構成はnon-DMA、`mem_block_symbols` 48、user buffer 24(= `mem_block ÷ 2`)。symbol周期0.4 / 0.8 / 1.6 / 2.4 msに対し初回19.2 / 38.4 / 76.8 / 115.2 ms、更新間隔はその半分。
5. instrumentation不具合を1件修正した。E053から引き継いだnon-DMA対照の判定が残っており、本実験に対照が無いためpytestが失敗した。計測値は完走しており、判定を設計へ合わせた採用runでは通る。

**候補**: window timestampをnon-DMA・`mem_block_symbols` 48・user buffer 24で構成し、初回遅延`48 × gate周期`・更新間隔`24 × gate周期`を仕様値とする。DMA modeは使わない。

**未決**: `en_partial_rx=false`の発火条件 / 48 symbol溜まる前に`rmt_disable`して取れる分だけ回収できるか / RMT分解能を落としたときの挙動。

### E053 ESP32-P4: RMT DMA modeで初回遅延を縮められるか — 完了 2026-09-09

全文: [e053_p4_rmt_dma_block/README.ja.md](e053_p4_rmt_dma_block/README.ja.md)。採用run: `_runs/E053_20260909T093735Z_default/`。

**事実**

1. **DMA modeは`mem_block_symbols` 8と16を`ESP_ERR_INVALID_ARG`で拒否した。** 64は受理。
2. **DMA modeの初回遅延はnon-DMAより悪い。** block 64で初回156,935 us、間隔153,601 us(= 64 × symbol周期)、1 callbackは64 symbol。non-DMAの48では初回115.2 ms相当。**DMAでは縮められない。**
3. **non-DMAでuser bufferを32から128へ変えると挙動が変わった。** E052は5回・各24 symbol、本実験は1回・120 symbol。
4. **規則が完成した。** `group = mem_block ÷ 2`、`n = floor(buffer ÷ group)`、1回あたり`n × group` symbol、初回`(mem_block + (n−1) × group) × 周期`、間隔`n × group × 周期`。case 1は予測345.6 msに対し実測346.6 ms。
5. **`n = 0`だと永久に発火しない。** これで[E051](e051_p4_rmt_partial_threshold/README.ja.md)のbuffer 8(group 24)とblock 96(group 48、buffer 32)の両caseが説明できる。E051の解釈は誤りで、発火不能な条件だった。**user bufferはtriggerではなく`group`以上という必要条件。**
6. E045からE053までの16条件すべてがこの規則で説明できる。

**候補**: user bufferを`mem_block_symbols ÷ 2`ちょうどにして初回遅延と更新間隔を最小化する。DMA modeは使わない。

**未決**: **DMA modeの`mem_block_symbols` 32が受理されるか**(受理されれば初回76.8 msでnon-DMAの115.2 msより良い) / `en_partial_rx=false`の発火条件 / 途中で`rmt_disable`して取れる分だけ回収できるか。

### E052 ESP32-P4: RMT callbackの発火時刻と間隔 — 完了 2026-09-09

全文: [e052_p4_rmt_callback_timing/README.ja.md](e052_p4_rmt_callback_timing/README.ja.md)。採用run: `_runs/E052_20260909T092437Z_default/`。

**事実**

1. **1 callbackあたりのsymbol数は全caseで24 = `mem_block_symbols`(48)の半分。**
2. **最初の発火は48 symbol分、以降の間隔は24 symbol分。** gate 6,000(symbol周期2.4 ms)で116,209 us・57,598〜57,605 us、gate 1,000(0.4 ms)で19,398 us・9,600 us。差の約1 msはarmから最初のedgeまでの待ち。
3. **CPU負荷は無関係。** PSRAM copyの有無で最初の発火は116,209 usと116,212 us、差3 us。「copy loopがRMTを遅らせている」は反証された。
4. durationは全caseで期待値と完全一致(gate 6,000で24,000 tick、gate 1,000で4,000 tick、min = max)。
5. **この規則でE045からE052までの14条件すべての実測callback回数が説明できる。** E045とE051で未特定だった閾値はこれで確定。user bufferのsymbol数は関与しない。

**候補**: window timestampの設計値を`初回遅延 = mem_block_symbols × gate周期`、`更新間隔 = その半分`として扱う。P4のnon-DMA modeでは`mem_block_symbols`の最小が48なので、初回遅延を縮めるにはDMA modeを検討する。回収loopと同じcoreで動かしてよい。

**未決**: DMA modeで`mem_block_symbols`を小さくできるか / `en_partial_rx=false`の発火条件 / 48 symbol溜まる前に止めて取れる分だけ回収する方法 / RMT分解能を落としたときの挙動。

### E051 ESP32-P4: RMT partial受信が通知される条件 — 完了 2026-09-09

全文: [e051_p4_rmt_partial_threshold/README.ja.md](e051_p4_rmt_partial_threshold/README.ja.md)。採用run: `_runs/E051_20260909T091955Z_default/`。

**事実**

1. **user bufferを8 symbol(所要19.2 ms)にしても100 msの回収でcallbackは0回。** user bufferのsymbol数は発火の閾値ではない。
2. **`mem_block_symbols`を48から96へ変えても0回のまま。** これも閾値ではない。
3. 回収時間を400 msにすると5回発火し、durationはhigh / lowともに24,000 tick固定で期待値と完全一致した。取れたdataは正しい。
4. PARLIO側は4条件すべて同一かつ正常(飛び22、階差一致15 / 15、overflow 0、bit 7が0のsample 0)。RMT設定はcaptureに影響しない。
5. **[E045](e045_p4_gate_rmt_order/README.ja.md)の「callbackはuser bufferが埋まったときに起きる」は一般則として反証された。** E050とE051の実測回数はbuffer基準・block基準・block半分基準のどのmodelとも合わず、長いloopでは常に1回少ない。**閾値の正体は未特定。**
6. instrumentation不具合を2件見つけて修正した。期待値表示の32 bit溢れ(`6000 × 20,000,000`)と、E049から引き継いだcache line非整列のmsync。後者はE049・E050でもerror logを出していたが、両実験のdataは検証を通っており結論は変わらない。

**候補**: 経験的規則として使う。gate loop周期1.6 ms以下なら100 ms程度の回収でwindow timestampが取れる。2.4 ms以上では回収時間を数百msへ伸ばす。durationの正確さは条件に依存しない。

**未決**: **callbackの発火時刻を`esp_timer`で記録して起動遅延と間隔を直接測る**(食い違いを解く鍵) / RMT ISRとPSRAM copy loopの競合 / `en_partial_rx=false`との比較 / gate loop周期1.6〜2.4 msの境界。

### E050 ESP32-P4: dutyを固定してwindow長だけを振る — 完了 2026-09-09

全文: [e050_p4_gated_window_at_fixed_duty/README.ja.md](e050_p4_gated_window_at_fixed_duty/README.ja.md)。採用run: `_runs/E050_20260909T090830Z_default/`。

**事実**

1. dutyを50%固定でwindow byte長を32,000から224,000まで7倍に振り、**5条件すべてでdataが正常**だった。飛びの数は期待境界数と一致、先頭15個の階差はすべてwindow byte長と厳密に一致、queue overflow 0、bit 7が0のsampleは0件。
2. **E048のwindow過負荷modelは否定された。** gate 6,000 / 7,000でring未読が92,288 / 106,880 byte(ring容量の1.4〜1.6倍)に達してもdataは正常。window byte長224,000はring容量の3.4倍である。
3. **E049の平均byte rate modelが確定した。** `duty × sample rate × bytes/sample < 約98 MB/s`が唯一の条件で、**window長は影響しない。** E049の境界はwindow長ではなくdutyの上昇によるものだった。
4. ring未読 > ring容量でもdataが正常なので、実際の緩衝はring単体ではない。E049の破綻条件では未読442,624 byteがchunk queue容量(約258 KiB)を超えoverflow 51件が出ていた。
5. **loop周期2.4 ms以上のcase(gate 6,000 / 7,000、E049のgate 8,000)でRMTのcallbackが0回になった。** 1.6 ms以下では取得できる。E049の`signal_range_max_ns`到達という解釈は、gate 6,000のhigh 24,000 tickが閾値32,000を大きく下回るため成り立たない。原因は未解明。

**候補**: gated captureを`sample rate ≤ 160 MHz`かつ`duty × sample rate × bytes/sample < 約98 MB/s`の一本で申告する。window長の上限は設けない。8 channelならduty 61%までは常に160 MHzが取れる。

**未決**: **RMTがloop周期2 ms以上でsymbolを返さない原因**(qualificationの実装に直接効く) / ringとqueueのどちらが律速か / duty 61%付近の実測 / window中のdrain帯域が持続値より低い理由 / 長時間持続 / data_width 16でのgated rate。

### E049 ESP32-P4: gated captureの吸収限界 — 完了 2026-09-09

全文: [e049_p4_gated_window_absorption/README.ja.md](e049_p4_gated_window_absorption/README.ja.md)。採用run: `_runs/E049_20260909T090339Z_default/`。

**事実**

1. gate幅2,044 / 4,000 / 5,000 / 6,000は正常。飛びの数が期待境界数と一致し、先頭15個の階差はすべてwindow byte長と厳密に一致した。gate 8,000だけが破綻(queue overflow 51、飛び93対期待16.4、階差一致4 / 15)。
2. **E048のwindow過負荷modelは反証された。** gate 5,000と6,000はring未読が77,632 / 92,288 byteでring容量65,536を超えたが**dataは正常**だった。ring未読 > ring容量はこの構成でdata喪失の指標にならない。
3. **実測の境界は平均byte rate(duty × sample rate)が約98 MB/sを横切る点と一致した。** 6,000で96.0 MB/s正常、8,000で106.7 MB/s破綻。つまり成立条件は`duty × sample rate × bytes/sample < 持続spool帯域`である。gateは持続spool帯域を上げているのではなく、**平均byte rateをその下へ下げている**。
4. 交絡あり: gapを固定したのでgate幅とdutyが一緒に動いた(33.8% → 66.7%)。window長と平均rateは分離できていない。ただし5点すべてを説明できるのは平均rateのmodelだけである。
5. gate 8,000でRMTがhighを記録しなかったのは、high duration 32,000 tickが`signal_range_max_ns`の32,000 tickに達しidle扱いになったため。設定の問題。
6. window区間中の実効drain帯域はring未読量から約82〜83 MB/sと計算でき、E036の持続値98 MB/sより低い。

**候補**: gated captureを`sample rate ≤ 160 MHz`かつ`duty × sample rate × bytes/sample < 約98 MB/s`で申告する。8 channelならduty 61%以上で160 MHzが取れなくなる。`signal_range_max_ns`は想定最長levelより十分大きく取る。

**未決**: dutyを固定してwindow長だけを振る掃引(分離) / ringとqueueのどちらが実際の緩衝か / window中のdrain帯域が持続値より低い理由 / low durationの64 tickばらつき / duty 61%付近の実測 / 長時間持続。

### E048 ESP32-P4: qualification構成でのgated capture rate上限 — 完了 2026-09-09

全文: [e048_p4_gated_rate_ceiling/README.ja.md](e048_p4_gated_rate_ceiling/README.ja.md)。採用run: `_runs/E048_20260909T085853Z_default/`。

**事実**

1. 3者共有qualification構成で20 / 40 / 80 / 100 / 120 / 160 MHzの6条件すべて成立。ring未読最大33,280 byte(容量65,536)、queue overflow 0、bit 7が0のsampleは0件。
2. **飛びの階差は全rateで`RMT high duration × 倍率`と完全一致。** 160 MHzでも9,600 / 22,400 / 48,000 / 65,408でずれ0。RMTのdurationはsample rateに依存せず不変(TX固定の設計どおり)。
3. **gateはsample rateの上限を引き上げる。** triggerなしraw captureの持続限界は約98 MB/s(8 channelで約98 MHz、[E036](e036_p4_parlio_rate_seq_verify/README.ja.md))だが、gated captureは内部clock源の上限160 MHzまで通った。
4. 機構はdutyによる平均低下ではない。gate区間内の瞬間byte rateはsample rateそのもの(160 MHzなら160 MB/s)でcopy段を上回っており、**ringがwindow単位の過負荷を吸収しgapで空になる**ことで成立している。成立条件は`window byte長 × (1 − spool ÷ sample rate) < ring容量`。160 MHz・65,408 byteで25.3 KBと計算でき実測33,280 byteと同じ桁。ring 64 KiBなら160 MHzで1 windowあたり約169 KBまで。
5. destination 1 MiBが160 MHzでは約23.6 msで埋まるため、それ以降のtask負荷は実際より軽い。**data検証が効くのは1 MiB分**で、ring未読の実測値は長時間captureに対して楽観側である。

**候補**: 限界matrixにgate前提のrate行を別に立て、成立条件をdutyではなく最大window長で規定する。

**未決**: destinationを大きくした長時間持続 / 吸収限界を超える条件の実測(現在model のみ) / ring容量を変えたときの線形性 / data_width 16でのgated rate / duty可変時の挙動 / gapが短い場合の限界。

### E047 ESP32-P4: data線・valid線・RMT RXの3者共有 — 完了 2026-09-09

全文: [e047_p4_gate_three_way_share/README.ja.md](e047_p4_gate_three_way_share/README.ja.md)。採用run: `_runs/E047_20260909T085436Z_default/`。

**事実**

1. GPIO 9をPARLIOのdata線7・PARLIOのvalid線・RMT RXの入力へ**同時に**割り当てて全APIが`ESP_OK`。
2. **bit 7が0だったsampleは262,144 sample中0件。** 共有した線はdata channelとしても正しく取得される。
3. RMTのhigh / low durationは期待値と完全一致(high 1,200 / 2,800 / 6,000 / 8,176、low 6,800 / 9,200 / 10,000 / 21,360)。
4. **gray7飛びの階差15箇所すべてがRMT high durationと一致。** data_width 8では`window byte長 = high duration`が割り算なしで成立する。

**判定**: **8 channel logic capture + hardware qualification + hardware window timestampが8 pin・追加channel 0・CPU負荷0で成立する。** GPIO 2〜9の8本で8 channelを取り、1本をqualifierに兼用する。qualifierに選んだchannelはdataとしても残る。払うのはRMT RX channel 1つと1 levelあたり32,767 tick上限だけ。

**制約**: qualifierはgate区間内で常にactiveなので、その線の波形情報は「activeだった」以外に残らない。値の変化に意味のない線(CS / enable / frame同期)へ割り当てる。

**未決**: gating時の最大sample rate / data_width 16での3者共有 / 32,767 tick超のgap / RMT分解能を落としたときの精度 / pulse delimiterとの3者共有 / run中の先頭同期 / hostへ渡すformat。

### E046 ESP32-P4: 幅が可変なgateでwindowを復元する — 完了 2026-09-09

全文: [e046_p4_gate_variable_width/README.ja.md](e046_p4_gate_variable_width/README.ja.md)。採用run: `_runs/E046_20260909T085028Z_default/`。

**事実**

1. 幅300 / 700 / 1,500 / 2,044 wordの4 windowに対し、RMTのhigh durationは1,200 / 2,800 / 6,000 / 8,176 tickの4値だけが循環し期待値と完全一致。low durationも6,800 / 9,200 / 10,000 / 21,360 tickで期待gapと一致した。**保存区間と捨てた区間の両方が分かる。**
2. capture data中のgray step飛びの階差は3,000 / 4,088 / 600 / 1,400の循環で、**RMT high durationを2で割った列と完全一致。未説明の階差は0件(15 / 15)。**
3. 飛びの総数114は回収量からの期待値115とほぼ一致。1 loopあたり保存量9,088 byte、duty 27%も期待どおり。

**判定**: **幅が可変なgateでも、間引いたsample列とRMTのduration列を突き合わせれば時間軸を完全に再構成できる。** capture qualificationは実装候補として確定した。保存量と転送量がdutyに比例して減り、時間軸は失われず、CPUは介在しない。払うのはRMT RX channel 1つと1 levelあたり32,767 tickの上限。

**未決**: data線・valid線・RMT RXの3者同時共有 / gating時の最大sample rate / 32,767 tick超のgap / run中の先頭同期の確立方法 / RMT分解能を落としたときの精度 / data_width 8・16でのqualification。

### E045 ESP32-P4: RMT RXがgate durationを返す条件 — 完了 2026-09-09

全文: [e045_p4_gate_rmt_order/README.ja.md](e045_p4_gate_rmt_order/README.ja.md)。採用run: `_runs/E045_20260909T083514Z_default/`。

**事実**

1. RMTの生成位置をPARLIO TXの前・後どちらにしても結果は同一。**GPIO matrixのfan-outはRMTにも効き、PARLIO TXが同じGPIOを出力にしてもRMTの入力経路は壊れない。** E044の生成順仮説は反証された。
2. **原因は回収時間だった。** 50 msでsymbol 0、300 msで両順序とも取得。`en_partial_rx`のcallbackはuser bufferが埋まったときに起きる。32 symbol × gate周期1.6384 ms = 52.43 msが必要で、51.3 msでは届いていなかった。
3. **duration 16個のhighはすべて8,176 tick、16個のlowはすべて24,592 tickで期待値と完全一致(min = max)。** 分解能20 MHzなので1 tick = 1 sample。gate windowの長さと間隔がsample単位で読める。
4. PARLIO側のgated captureは4条件すべてで無傷。gray stepの飛びはすべて期待window長の整数倍。

**判定**: hardware gatingとhardware window timestampingは同時に成立する。gate線1本をPARLIO validとRMT RXへ共有すれば、PARLIOがgate区間のsampleを間引いて拾い、RMTが各high / lowの長さを記録する。CPUは介在しない。**E043の「可変幅gateでは時間軸を再構成できない」制約が外れる。**

**払うもの**: RMT RX channel 1つ(P4は4) / 1 levelあたり32,767 tick上限(20 MHz分解能で1.638 ms) / callback遅延 = buffer symbol数 × gate周期。channel数は払わない。

**未決**: 実際に可変幅なgateでの復元 / 32,767 tick超のgap / [E041](e041_p4_parlio_shared_valid_line/README.ja.md)のdata線共有との3者同時共有 / RMT分解能を落としたときの精度 / gating時の最大rate / RMT symbolとPARLIO sample列の先頭同期 / data_width 8・16でのqualification。

### E044 ESP32-P4: gate線をRMT RXへ分岐 — 中断 2026-09-09

全文: [e044_p4_gate_rmt_timestamp/README.ja.md](e044_p4_gate_rmt_timestamp/README.ja.md)。採用run: `_runs/E044_20260909T083102Z_default/`。

**事実**

1. 同一GPIOをPARLIO RXの`valid_gpio_num`とRMT RXの`gpio_num`へ同時に割り当て、両peripheralの生成・enable・receiveがすべて`ESP_OK`になった。
2. RMTを足してもPARLIO側のgated captureは壊れなかった。gray stepの飛びは31 / 32件すべてが期待window長の整数倍。
3. **RMTの`on_recv_done`が50 msの回収window中に0回発火し、symbolを取得できなかった。**

**判定**: 選んだ回収時間では反証。原因の候補を生成順とpartial受信のthresholdの二つに絞り、[E045](e045_p4_gate_rmt_order/README.ja.md)へ引き継いだ。E045で**回収時間が原因**と確定し、生成順仮説は反証された。pytestは意図的に失敗する記録として残している。

### E043 ESP32-P4: hardware gate windowの境界は復元できるか — 完了 2026-09-09

全文: [e043_p4_parlio_gate_window_boundary/README.ja.md](e043_p4_parlio_gate_window_boundary/README.ja.md)。採用run: `_runs/E043_20260909T082419Z_default/`。

**事実**

1. gate幅2,044 / 1,020 wordに対し、gray stepの飛びはすべて期待window長(4,088 / 2,040 byte)の整数倍にあった。16回・15回連続で1 byteの狂いも無い。**window長 = gate幅 × 分周比 ÷ sample/byteで一意に決まり、ばらつきは0。**
2. windowは常にbyte境界で終わり、nibble整列のずれは起きなかった。data_width 4でもqualificationは使える。
3. **callbackの`recv_bytes`は両caseとも4,032 byte固定で、gate幅に依存しない。** driverのDMA descriptor分割単位でありgate境界とは無関係。E040のcallback数とgate数の不一致はこれで説明できる。**callback境界は境界情報として使えない。**
4. run長は両caseとも4固定。回収rateはduty比とほぼ一致(12.4% / 5.9%)。

**判定**: window長は決定論的だが、境界はcapture dataの中で自己記述されない。復元できるのはgate幅が既知・一定のときに限る。可変幅gate(実際のCS等)では時間軸を再構成できない。

**候補**: qualificationを二つに分ける。固定幅なら算術でwindowへ切る。可変幅なら**captureする1 channelに周期既知の自由走行信号を入れてgap長を法演算で測る**(本実験でgray rampが境界を見せた原理そのもの)。CPU負荷ゼロで間引きの利得を保てる。callback境界は使わない。

**未決**: 自由走行信号によるgap測定の実証 / gate edgeのsoftware timestampが成立するrate / 可変幅gateでのwindow長決定性 / gating時の最大rate / gate開放位置の絶対sample精度 / data_width 8・16でのqualification / hardwareでwindow timestampを得る経路の有無。

### E042 ESP32-P4: 16 channel rate境界のsample単位再検証 — 完了 2026-09-09

全文: [e042_p4_parlio_16ch_seq_verify/README.ja.md](e042_p4_parlio_16ch_seq_verify/README.ja.md)。採用run: `_runs/E042_20260909T065702Z_default/`。

**事実**

1. 16 channelは48 MHz設定(実効95.884 MB/s)まで連番違反0・ring未読4,928 byteで成立。52 MHzはring未読134,848 byteでring容量を超え違反44件。**E036のburst modelがwidthをまたいで成立した**(予測は48成立 / 52違反)。
2. spool rateは52 / 56 MHzで97.6 / 96.9 MB/sに飽和し、8 channelの約98 MB/sと一致。律速はchannel数ではなくpacking後のbyte rateである。
3. **複製laneの不一致は20 MHzで0件、40 / 48 / 52 / 56 MHzで1,066 / 373 / 345 / 480件。** 遷移1回あたり0.13〜0.41%。不一致の有無はrun長の散らばり(4固定か3〜5か)と完全に対応した。同一GPIOを二つのlane slotへ入れても遷移の瞬間には別の値を読むことがある。
4. gray code検証は40 / 48 MHzで違反0のまま。遷移中のsampleは前後どちらかの値になるので、この現象はgray検証には現れず複製lane検証にだけ現れる。
5. E031の「複製lane不一致0」は8 MHz sampling・100 kHz信号源という疎な条件でのみ成立する記録だった。

**候補**: 16 channel行をsample単位検証済みにする。**channel間のedge位置は±1 sampleの精度として申告し、それ以上細かいtiming差をsample列から読まない。** 1 / 2 / 4 channelはbyte rateに余裕があるため再検証の優先度を下げる。

**未決**: 不一致の原因(lane slot間の到達時間差か遷移中信号の独立確定か) / 不一致率のrate依存性 / 1 / 2 / 4 channelでのrun揺れとlane間食い違い / 独立16 padでの同測定(要配線) / 48〜52 MHz間の境界。

### E041 ESP32-P4: valid線をdata線と同一GPIOで共有 — 完了 2026-09-09

全文: [e041_p4_parlio_shared_valid_line/README.ja.md](e041_p4_parlio_shared_valid_line/README.ja.md)。採用run: `_runs/E041_20260909T065115Z_default/`。

**事実**

1. `valid_gpio_num`をdata線7と同一のGPIO 9にしても`parlio_new_rx_unit`は受理し、20 / 80 / 160 MHzすべてで全APIが`ESP_OK`。受信byteは`eof_data_len` 4,096と完全一致、`gray7` step違反0件。
2. **bit 7が0だったsampleは0件。** 共有GPIOはdata channel 7としてもtrigger線としても機能する。**hardware triggerはchannelを消費しない。**
3. 先頭4 byteは3条件すべて`80 80 80 81`でgate開放位置の値と一致。取得開始のずれは1 sample以内。
4. run長は20 MHzで4固定、80 / 160 MHzで3〜5。**E038の「整数分周なら均一」と一致しない。** 整数分周は十分条件ではない。原因は未切り分け。
5. data_width 8の160 MHz(160 MB/s)で4,096 byteを25.6 usにわたり違反0で取得。**internal RAMへのDMA writeは4 KiB burstで160 MB/sを通す。** E036の約98 MB/sはtask copy段の限界であってDMA writeの限界ではない。

**候補**: trigger能力の申告を「消費channel 1」から「trigger源となるchannelを1つ選ぶ」に変える。hardware trigger付き8 channelを8 pinの標準構成にする。

**未決**: run長均一性を決める条件(分周比か位相か) / internal RAM DMA writeの持続帯域 / 共有線をlevel gateに使う構成 / data_width 16での共有(`valid_sig_line_id`に空きslotが無い可能性) / 共有時のtrigger jitter。

### E040 ESP32-P4: `eof_data_len` = 0のlevel delimiterでDMAは走るか — 完了 2026-09-09

全文: [e040_p4_parlio_level_open_frame/README.ja.md](e040_p4_parlio_level_open_frame/README.ja.md)。採用run: `_runs/E040_20260909T064609Z_default/`。

**事実**

1. `eof_data_len` = 0では`on_receive_done`が発火しないが、payload 16,384 byteは全域が書き換わった。**DMAは走っている。** E039の「不成立」は取得が起きないことではなく完了eventが来ないことだった。
2. 書かれたdataは`gray4`のrun長4の正しい列で、先頭はgate開放位置の値にそろっていた。
3. `partial_rx_en=true`では52,817 usで20 callback・65,536 byteを回収でき、queue overflow 0。hardware gate + software停止の可変長modeとして成立する。
4. **回収byte rateは1,240 KB/sで、raw byte rate 10,000 KB/sの12%だった。gate dutyは12.5%。gateがactiveな区間のsampleだけがDMAへ渡る。** これはCPU負荷ゼロのcapture qualificationであり、同じ容量でduty分だけ長い時間を覆え、spool帯域も同じ比率で緩む。
5. gateが無いcaseはcallback 0、回収0、payloadは全域fill値のままだった。

**候補**: hardware窓を三つのmodeに分ける。(a) pulse + 有限 = 固定長・完了event有り、(b) level + 有限 = 固定長・完了event有り、(c) level + `eof_data_len` 0 + `partial_rx_en` = 可変長・software停止・完了event無し。(c)をcapture qualificationとして「内部圧縮」の選択肢に入れ、qualifier線1本の消費とgate外情報の喪失を明記する。

**未決**: gate境界のsample精度 / gating時の最大rate / duty可変時の線形性 / payload満杯後のDMA挙動 / gate window境界のmetadata復元(現状は連結されて境界が失われる) / `partial_rx_en`とPSRAM spoolの組。

### E039 ESP32-P4: level delimiterによるhardware gating — 完了 2026-09-09

全文: [e039_p4_parlio_level_gate/README.ja.md](e039_p4_parlio_level_gate/README.ja.md)。採用run: `_runs/E039_20260909T031756Z_default/`。

**事実**

1. active high + `eof_data_len` 2,048 byteで受信byteが完全一致し、run 1,023本・run長4固定・gray step違反0。先頭4 sampleはgate開始位置のgray値と一致した。
2. `active_low_en` = trueでも2,048 byteを違反0で取得した。ただしheadは2 runで異なり、**開始位置は再現しない**。armした時点で既にactiveならその瞬間から始まるため。
3. gateが無いcaseはframeが始まらず`ESP_ERR_TIMEOUT`。誤trigger0。
4. **`eof_data_len` = 0による可変長frameは成立しない。** delimiter生成と`receive`が`ESP_OK`でも`on_receive_done`が発火せず500 msでtimeoutした。headerの「0ならenable無効化でEOF」はこの構成では効かない。frameが開始したか否かは本実験では区別していない。

**候補**: hardware側の窓をpulse(単発event)とlevel active high(区間取得)に限り、終了は常に`eof_data_len`(最大65,535 byte)とする。可変長取得はhardwareに期待せず、上限を置いてsoftware側で有効長を判定する。level active lowはarmとenableの前後関係で開始位置が決まることを明記して別扱いにする。

**未決**: `eof_data_len`=0でframeが開始しているか(payloadが書かれるかで判定可) / `partial_rx_en=true`との組 / level delimiterの`timeout_ticks` / gating時の最大rate / gate境界のsample精度 / `has_end_pulse` / data_width 8・16での構成。

### E038 ESP32-P4: hardware pulse triggerのrate上限 — 完了 2026-09-09

全文: [e038_p4_parlio_pulse_trigger_rate/README.ja.md](e038_p4_parlio_pulse_trigger_rate/README.ja.md)。採用run: `_runs/E038_20260909T031300Z_default/`。

**事実**

1. data_width 4 + valid線1本のhardware pulse triggerは20 / 40 / 80 / 100 / 120 / 160 MHzの6条件すべてで成立した。受信byteは両frameとも`eof_data_len` 16,384と完全一致、gray step違反0件。
2. frame間隔から求めた実測rateは設定の99.9〜100.1%。source loop周期という独立した時間基準による絶対測定で、**160 MHzまでsample clockが設定どおりであることを直接確認した**(E036はcallback数からの推定だった)。
3. run長は160 MHzの整数分周(20 / 40 / 80 / 160 MHz)で厳密に4固定、非整数分周(100 / 120 MHz)で3〜5に散った。RX側かTX側かは切り分けていない。trigger位置も整数分周では2 frameのheadが完全一致し、120 MHzだけ1 sampleずれた。
4. E036の約98 MB/sはinternal ring → PSRAM copy段の限界であり、copy段の無い有限frameには効かない。ただしdata_width 4の160 MHzはpacking後80 MB/sなので、**98 MB/s超のbyte rateは試していない**。

**候補**: capabilityを取得方式ごとに分ける。hardware trigger + 有限frame = 160 MHz / 深度65,535 byte以内 / pre-trigger不可、software走査 + spool = 24 MHz / 深度16 MiB / pre-trigger可。公称rateは160 MHzの整数分周に限定し、非整数分周は±1 sample揺れとして別枠にする。

**未決**: data_width 8 / 16でのhardware trigger(9 / 17線必要でpin宣言の拡張から) / 有限frameのbyte rate上限 / `eof_data_len` 65,535超のpost長 / level delimiter / `has_end_pulse`・`pulse_invert` / 連続frame soakと再arm周期 / circular ringとの併用。

### E037 ESP32-P4: PARLIO pulse delimiterによるhardware trigger — 完了 2026-09-09

全文: [e037_p4_parlio_pulse_trigger/README.ja.md](e037_p4_parlio_pulse_trigger/README.ja.md)。採用run: `_runs/E037_20260909T030730Z_default/`。

**事実**

1. `parlio_new_rx_pulse_delimiter`は`valid_sig_line_id`が4でも5でも`ESP_OK`で、data_width 4 + `valid_gpio_num`との組で動作した。結果は両者同一。
2. pulseがあるとき`on_receive_done`が1回発火し、受信byteは`eof_data_len` 16,384と完全一致した。復元frameはrun 8,192本、run長4固定、gray step違反0で全域連続。
3. pulseが無いときframeは一度も始まらず、`wait_all_done`が500 msで`ESP_ERR_TIMEOUT`を返した。誤triggerは0。
4. **`timeout_ticks` = 60,000でも、frame開始前のarm待ちでは`on_timeout`が発火しなかった。** arm待ちのtimeoutはsoftwareで持つ必要がある。
5. frame先頭のrunが3 sampleなので、pulse検出から取得開始までのずれは1 sample(50 ns)以内で、2 caseで再現した。

**候補**: trigger能力をsoftware走査tier(pattern / edge / occurrence / multi-stage、消費channel 0、8 channelで24 MHz)とhardware pulse tier(専用線pulseのみ、消費channel 1、CPU負荷なし)の二段で申告する。

**未決**: hardware trigger時の最大rate / level delimiterのgating / `has_end_pulse`停止 / `pulse_invert`極性 / 8・16 channel構成に要るpin数 / `eof_data_len` 65,535超のpost長 / circular ringとの併用。pulse delimiterはpre-trigger dataを取れないため、pre/post windowとhardware triggerは同時に成立しない。

### E036 ESP32-P4: PARLIO rate境界のsample単位再検証 — 完了 2026-09-09

全文: [e036_p4_parlio_rate_seq_verify/README.ja.md](e036_p4_parlio_rate_seq_verify/README.ja.md)。採用run: `_runs/E036_20260909T025817Z_default/`。

**事実**

1. PARLIO TXとRXは同一group・同一GPIOで共存し、gray code rampを信号源にできた。20 MHzでrun 262,144本、run長4固定、連番違反0。
2. sampling rate(callback byte由来)は120 MHz設定で119.560 MB/s、設定の99.6%を維持した。spool rate(copied由来)は100 MHz以上で97.1〜98.2 MB/sに飽和した。律速はsampling側ではなくring→PSRAM copy側である。
3. ring未読byteの最大値は100 / 104 / 112 / 120 MHzで20,160 / 60,480 / 142,912 / 244,608 byte。ring 65,536 byteを超えた112 / 120 MHzだけ連番違反が18 / 27件出た。**104 MHzは違反0で、E033の不成立判定は覆った。**
4. 違反のある112 / 120 MHzでも`result=ESP_OK`かつ`overflows=0`だった。firmware側のdrop検出は64段queueの満杯を見ており、ring容量の約3.8倍まで見逃す。
5. 最初の違反位置(0.57 / 0.41 Mi sample)は、sampling超過分でringが枯渇するburst budget(0.54 / 0.35 Mi sample)と同じ桁で一致した。

**候補**: rate単独の上限値を出さず、sampling上限・持続spool帯域・ring容量から決まるburst深度の三つに分けて申告する。drop判定はinflight最大 > ring容量とし、queue段数では判定しない。信号源はgray code rampを標準にする。

**未決**: 持続spool帯域の改善余地 / 16 channelの同条件再検証 / 深度別burst境界の実測 / 104〜112 MHz間の1 Mi境界 / trigger・圧縮時のinflight。

### E034 ESP32-P4: ADC1 continuous batch基礎 — 完了 2026-09-09

全文: [e034_p4_adc1_continuous_batch/README.ja.md](e034_p4_adc1_continuous_batch/README.ja.md)。採用run: `_runs/E034_20260909T020135Z_default/`。

**事実**

1. ADC1の1 / 2 / 4 / 8 channelすべてで、最大83,333 conversion/sの1 MiB rawをPSRAMへ保存できた。実効83,251 conversion/s、timeout / overflow / ID不正は0だった。
2. channel別sample数は全条件で均等だった。8 channel時は各32,768 sample、実効約10.4 kSa/s/channelとなる。
3. 83,333 conversion/sの複数channelだけは設定順の単純な循環列ではない。channel IDで分離できるが、配列位置からchannelやskewを推定できない。

**候補**: 12-bit値をcompact化し、analog補助traceとしてdigital batchと共通containerへ格納する。

**未決**: 高速時のchannel列 / ADC2・両unit / 長時間 / 精度・noise / digital同期。

### E033 ESP32-P4: PARLIO 8/16 channel rate精密探索 — 完了 2026-09-09

全文: [e033_p4_parlio_width_rate_fine/README.ja.md](e033_p4_parlio_width_rate_fine/README.ja.md)。採用run: `_runs/E033_20260909T013637Z_default/`。

**事実**

1. 8 channelは100 MHz設定（実効97.869 MB/s）、16 channelは48 MHz設定（実効95.677 MB/s）まで成立した。
2. 8 channel / 104 MHzと16 channel / 52 MHzではoverflow前に約97〜98 MB/sで飽和し、設定rateの95%へ追従できなかった。
3. 16 channelは56 MHzからoverflowし、8 channelは120 MHzまでoverflow 0だがqueue最大63へ達した。

**候補**: 80 MB/sを安定tier、約96 MB/sを短時間burst tierとして分ける。

**未決**: deep capture時の境界 / ring・chunk tuning / trigger・圧縮追加時の上限。

### E032 ESP32-P4: PARLIO width別rate粗探索 — 完了 2026-09-09

全文: [e032_p4_parlio_width_rate_coarse/README.ja.md](e032_p4_parlio_width_rate_coarse/README.ja.md)。採用run: `_runs/E032_20260909T012950Z_default/`。

**事実**

1. 1 / 2 / 4 channelは160 MHzまで成立し、上限未到達だった。
2. 8 channelは80 MHz成立、120 MHzはqueue 62・実効97.496 MHzで追従せず、160 MHzはoverflowした。
3. 16 channelは40 MHz成立、80 MHz以上でoverflowした。全widthで約80 MB/sまでは成立した。

**候補**: packing後raw byte rate 80 MB/sを全width共通の安全tierとする。

**未決**: 狭幅clock上限 / 8・16ch fine boundary / deep capture最高rate / width別trigger rate。

### E031 ESP32-P4: PARLIO channel width — 完了 2026-09-09

全文: [e031_p4_parlio_channel_width/README.ja.md](e031_p4_parlio_channel_width/README.ja.md)。採用run: `_runs/E031_20260909T012045Z_default/`。

**事実**

1. 1 / 2 / 4 / 8 / 16 channelの全幅で各1,048,576 sampleを取得し、queue最大0、overflow 0、data正常だった。
2. 8未満は1 byteへ複数sample、8 channelは1 byte/sample、16 channelはlittle-endian 2 byte/sampleとして復元できた。
3. 8 MHz設定時のbyte rateは幅に比例して0.971〜15.966 MB/s。16 channelの複製lane不一致は0だった。

**候補**: 1〜16 channelをPARLIO高速tier、24 channel以上をCPU snapshot低速tierとして分ける。

**未決**: width別raw/trigger rate上限 / 16本独立pad / 24 channel以上のCPU snapshot。

### E030 ESP32-P4: 16 MiB deep batch capture — 完了 2026-09-09

全文: [e030_p4_deep_batch_capture/README.ja.md](e030_p4_deep_batch_capture/README.ja.md)。採用run: `_runs/E030_20260909T005932Z_default/`。

**事実**

1. 20 MHz / 8-bitを16 MiB PSRAMへ839,063 usで保存し、実効19.995 MB/sで入力へ追従した。
2. callback / dequeueは4,361 / 4,361、queue最大0、overflow 0、全8 laneの16 MiB全域が正常だった。
3. 16 MiBはこのrateで約0.839秒分に相当する。

**候補**: 16 MiBをdeep batchの基準容量、20 MHzをtrigger付き基準rateとする。

**未決**: USB device→PC download帯域 / captureとdownloadの同時実行 / 16 MiB circular trigger / 外部pad入力。

### E029 ESP32-P4: circular ring soak — 完了 2026-09-09

全文: [e029_p4_circular_ring_soak/README.ja.md](e029_p4_circular_ring_soak/README.ja.md)。採用run: `_runs/E029_20260908T213123Z_default/`、`_runs/E029_20260908T213209Z_default/`。

**事実**

1. 個別にupload/resetした2実行で、1 / 2 / 4 wrapを各10組、合計60/60 captureに成功した。
2. 合計178,393,600 sampleでqueue最大1、overflow 0、実効19.954〜19.985 MB/s、ring/window data正常。
3. E027の最初に一度だけ見えた異常は再現しなかった。

**候補**: 20 MHz circular captureをhost download統合の基準構成にする。

**未決**: captureとUSB downloadの同時実行 / 数時間soak / 電源cycle / 外部pad入力。

### E028 ESP32-P4: SUMP 4-stage trigger — 完了 2026-09-09

全文: [e028_p4_sump_four_stage_trigger/README.ja.md](e028_p4_sump_four_stage_trigger/README.ja.md)。採用run: `_runs/E028_20260908T204111Z_default/`。

**事実**

1. lane 7 high → lane 7 falling → lane 0 rising 4回 → low nibble zeroの4 stageが16 MHzで3/3回成立した。
2. 全runでqueue最大0、overflow 0、実効15.971〜15.973 MB/s、data正常。
3. final trigger後256 Ki sampleを取得し、停止overshootは2,298〜2,436 sampleで最大chunk未満。

**候補**: multi-stage triggerを独立capabilityとし、対応stage数・条件種・最大rateを個別に示す。

**未決**: 汎用stage command表現 / 全条件の持続検索rate / SUMP wire互換範囲 / circular ringとの統合。

### E027 ESP32-P4: SUMP circular pre-trigger ring — 完了 2026-09-09

全文: [e027_p4_sump_circular_pretrigger/README.ja.md](e027_p4_sump_circular_pretrigger/README.ja.md)。採用run: `_runs/E027_20260908T155813Z_default/`。

**事実**

1. 1 MiB PSRAM ringを1 / 2 / 4回wrapした後、trigger前後256 Ki sampleのwindowを3回連続で再構成できた。
2. 採用runはqueue最大1、overflow 0、実効19.953〜19.984 MB/s、window data正常。
3. 停止overshootは929〜2,883 sampleで最大chunk 4,032未満。
4. 最初のrun 0だけbase検証に異常値が出たが、ring検証前に停止して原因未確定。その後3回は再現なし。

**候補**: PSRAM circular ringを内部実装とし、外部にはtrim済みの連続sample列を返す。

**未決**: cold-run異常の長時間soak / multi-stage trigger / host転送 / 1 MiB超のring容量。

### E026 ESP32-P4: SUMP pre/post trigger停止 — 完了 2026-09-09

全文: [e026_p4_sump_prepost_stop/README.ja.md](e026_p4_sump_prepost_stop/README.ja.md)。採用run: `_runs/E026_20260908T155118Z_default/`。

**事実**

1. 20 MHzで25/75・50/50・75/25の512 Ki sample windowをすべて構成できた。
2. 3 runともqueue最大1、overflow 0、実効19.862〜19.865 MB/s、data正常。
3. descriptor単位の物理停止overshootは2,834〜2,961 sampleで、最大chunk 4,032 sample未満だった。

**候補**: descriptor単位で停止し、論理windowをsample単位でtrimする方式。

**未決**: PSRAM circular ringのwrap / multi-stage trigger / host転送時のwindow表現。

### E025 ESP32-P4: SUMP基本trigger rate境界 — 完了 2026-09-09

全文: [e025_p4_sump_trigger_rate_boundary/README.ja.md](e025_p4_sump_trigger_rate_boundary/README.ja.md)。採用run: `_runs/E025_20260908T154529Z_default/`。

**事実**

1. 16 / 20 / 24 MHzはno-match / rising / patternの全条件でqueue最大0〜1、overflow 0、data正常だった。
2. 28 MHzはpatternだけが27.871 MB/s、queue 1で追従した。no-match / risingは25.013 / 24.107 MB/s、queue 32 / 43まで滞留した。
3. 32 MHzはno-match / risingが14 / 28 overflow、patternも27.955 MB/s、queue 39で追従しなかった。

**候補**: 共通basic-trigger tier 24 MHz、保守的default 20 MHz、pattern-only tier 28 MHz。

**未決**: 24〜28 MHz間の厳密な境界 / pre/post制御の追加負荷 / multi-stage triggerのrate tier。

### E024 ESP32-P4: SUMP基本trigger 32-bit検索 80 MHz — 完了 2026-09-09

全文: [e024_p4_sump_swar_trigger_80mhz/README.ja.md](e024_p4_sump_swar_trigger_80mhz/README.ja.md)。採用run: `_runs/E024_20260908T153929Z_default/`。

**事実**

1. 32-bit SWAR検索はno-match 26.132、rising 25.605、pattern 30.926 MB/sだった。
2. 80 MHz入力に対しqueueは64まで飽和し、overflowは667 / 683 / 537件。capture dataは不成立。
3. 存在しないpatternはmatchせず、risingとpatternはmatchした。

**候補**: trigger付きcaptureのrate tier、ESP32-P4固有SIMD、edgeだけのhardware支援。

**未決**: software triggerの成立rate上限 / pre/post ring / multi-stage / hardware-assisted external trigger。

### E023 ESP32-P4: SUMP基本trigger検索 80 MHz — 完了 2026-09-09

全文: [e023_p4_sump_basic_trigger_80mhz/README.ja.md](e023_p4_sump_basic_trigger_80mhz/README.ja.md)。採用run: `_runs/E023_20260908T152754Z_default/`。

**事実**

1. 1-byteずつの全sample検索は、no-match 24.555、rising 17.324、pattern 15.078 MB/sだった。
2. 80 MHz入力に対しqueueは64まで飽和し、overflowは722 / 1,096 / 1,286件。capture dataは不成立。
3. 存在しないpatternはmatchせず、risingとpatternはmatchしたが、検出後も全sample走査する負荷には追従できなかった。

**候補**: 32-bit SWAR検索、trigger専用core、trigger有効時のrate制限。

**未決**: optimized basic trigger / pre/post ring / multi-stage / hardware-assisted external trigger。

### E022 ESP32-P4: PARLIO PSRAM退避 80 MHz — 完了 2026-09-09

全文: [e022_p4_parlio_spool_80mhz/README.ja.md](e022_p4_parlio_spool_80mhz/README.ja.md)。採用run: `_runs/E022_20260908T152201Z_default/`。

**事実**

1. 80 MHz / 8-bit / 1 MiBを3/3回、全API成功、queue overflow 0でPSRAMへ退避した。
2. captureは13,171〜13,174 us、実効79.594〜79.612 MB/s。callback / dequeueは273 / 273、queue最大1。
3. 最大duty誤差121 ppm、edge 2,620〜2,622で全8 laneが正常。

**候補**: 80 MHzでSUMP基本trigger相当のpattern / maskとedge検索負荷を測る。

**未決**: trigger検索時のdrop / pre/post trigger / 120〜160 MHz / 外部pad / 32 MiB級capture / host download。

### E021 ESP32-P4: PARLIO internal ringからPSRAM退避 — 完了 2026-09-09

全文: [e021_p4_parlio_psram_spool/README.ja.md](e021_p4_parlio_psram_spool/README.ja.md)。採用run: `_runs/E021_20260908T151406Z_default/`。

**事実**

1. receiverをcaptureごとに再生成すると、8 MHz / 8-bit / 1 MiBを3/3回、queue overflow 0でinternal 64 KiB ringからPSRAMへ退避できた。
2. captureは131,361〜131,362 us、実効7.982 MB/s。callback / dequeueは273 / 273、停止時超過は1,984 byteで全run一致。
3. 最大duty誤差12 ppm、edge 26,214〜26,216で全8 laneが正常。PSRAM syncは568〜571 us。
4. receiver再利用構成はrun 0が正常、run 1が最大duty誤差53,494 ppmとなった。APIとqueue overflowだけでは検出できない再arm時のdata不良がある。

**候補**: stock Arduino環境では64 KiB internal ring→task copy→PSRAMを使い、各armでreceiverを再生成する。

**未決**: sample rate上限 / receiver再利用不良 / basic trigger検索負荷 / pre/post trigger。

### E020 ESP32-P4: PSRAM copy帯域 — 完了 2026-09-09

全文: [e020_p4_psram_copy_bandwidth/README.ja.md](e020_p4_psram_copy_bandwidth/README.ja.md)。採用run: `_runs/E020_20260908T150231Z_default/`。

**事実**

1. 8 MiBのcopyを全9条件で行い、cache syncは全て`ESP_OK`、write/read mismatchは0件だった。
2. flush込みwriteは4 KiB chunkで181.085〜181.128 MB/s、16 KiBで182.654〜182.746 MB/s、64 KiBで138.590〜138.622 MB/s。
3. PSRAM→internal readは170.917〜183.815 MB/s。8 MiB flushは560〜575 us。

**候補**: 16 KiB前後のinternal DMA ringをtaskからPSRAMへ退避する。

**未決**: PARLIO同時動作時のdropなしrate / core分離 / trigger検索との競合 / GDMA memory copy。

### E019 ESP32-P4: PARLIO PSRAM partial ring — 完了 2026-09-08

全文: [e019_p4_parlio_psram_partial_ring/README.ja.md](e019_p4_parlio_psram_partial_ring/README.ja.md)。採用run: `_runs/E019_20260908T145625Z_default/`。

**事実**

1. 1 MiB PSRAM direct partial transactionはAPIに受理され、開始した。
2. `esp_log_level_set("cache", ESP_LOG_NONE)`後も、4,032-byte descriptorの不整列cache sync errorが出力された。
3. 27 error後にCore 1がInterrupt WDTでpanicした。2 runで再現した。
4. stack上のaddressは`esp_cache_msync` → `parlio_rx_default_desc_done_callback` → `gdma_default_rx_isr`へ復号でき、ISR内のdescriptor sync errorと一致した。

**候補**: external-memory alignmentを使うdriver修正、またはinternal DMA ringからPSRAMへのtask退避。

**未決**: PSRAM copy帯域 / spool方式のdropなしrate / driver修正のArduino組込み / 修正版ringの停止精度。

### E018 ESP32-P4: PARLIO PSRAM cache log抑制 — 完了 2026-09-08

全文: [e018_p4_parlio_psram_log_suppression/README.ja.md](e018_p4_parlio_psram_log_suppression/README.ja.md)。採用run: `_runs/E018_20260908T144655Z_default/`。

**事実**

1. 1 MiB PSRAM payloadを確保でき、PARLIO RX unitも`max_recv_size = 1 MiB`で作成できた。
2. soft delimiterの`eof_data_len = 1 MiB`は、最大65,535 byteを超えるため`ESP_ERR_INVALID_ARG`となった。8,192 byteのcontrol delimiterは成功した。
3. 単一有限長soft-delimited transactionでは1 MiB captureを開始できず、予定したlog抑制とdata検証には到達しなかった。

**候補**: PSRAM direct ringを使う`partial_rx_en`で一周を検出して停止する。

**未決**: partial direct mountの成立 / 公開APIだけでの正確な停止 / cache log抑制 / 一周後のcoherency。

### E017 ESP32-P4: PARLIO PSRAM cache sync境界 — 完了 2026-09-08

全文: [e017_p4_parlio_psram_cache_sync/README.ja.md](e017_p4_parlio_psram_cache_sync/README.ja.md)。採用run: `_runs/E017_20260908T143447Z_default/`。

**事実**

1. burst size 0 / 64 / 128 byteで、driver内部のcache警告数に差はなかった。
2. 3,968 / 4,096 / 7,936 byteは各3回とも無警告。8,064 / 8,192 byteは各runで2件警告した。
3. 全45 captureで全APIと完了後のpayload全体M2C syncが`ESP_OK`。全8 laneを復元し、最大duty誤差は6,048 ppmだった。
4. 8,064 byteでは`0xFC0` × 2、8,192 byteでは`0xFC0` + `0x800`のdescriptor callback syncが128-byte境界違反となった。

**候補**: cache logを抑制し、有限長受信完了後にpayload全体を明示M2C syncして、大容量PSRAM direct captureを測る。

**未決**: log抑制の可否 / 大容量時のdata一貫性・安定性・所要時間 / sample rate上限 / driver内部の分割条件。

### E016 ESP32-P4: PARLIO RXからPSRAMへの直接DMA — 完了 2026-09-08

全文: [e016_p4_parlio_psram_direct/README.ja.md](e016_p4_parlio_psram_direct/README.ja.md)。採用run: `_runs/E016_20260908T125457Z_default/`。

**事実**

1. **Arduino-ESP32 3.3.11からESP-IDF PARLIO APIを直接使い、有限長RXをPSRAMへ直接DMAできる。** PSRAM payloadはexternal-DMA-capableで、8 MHz設定・8-bit・8,192 sample × 3回の全APIと全laneが成功。
2. 搭載PSRAMは32 MiB。必要alignmentはinternal RAM 64 byte、external RAM 128 byte。
3. internal RAMの最大duty誤差は1,832 ppm、PSRAMは2,197 ppm。edge範囲はそれぞれ204〜206、205〜207。
4. PSRAMではdriver内部のdescriptor単位cache syncが128-byte境界違反となり、transactionごとに2件、計6件警告した。実験側の完了後payload全体M2C syncは`ESP_OK`で、dataも正しかった。

**候補**: 有限長batchはPSRAMへ直接DMAし、wait後にpayload全体を明示M2C syncする。internal RAM→PSRAM copyはfallback。

**未決**: cache警告の回避またはdriver修正(`p4-parlio-psram-cache-sync`) / direct DMAのrate・大容量安定性 / partial・continuous callback時のcoherency。

### E015 ESP32-P4: LEDC出力とPARLIO RX入力の同一GPIO共存 — 完了 2026-09-08

全文: [e015_p4_parlio_routing_order/README.ja.md](e015_p4_parlio_routing_order/README.ja.md)。採用run: `_runs/E015_20260908T095518Z_default/`。

**事実**

1. **`io_loop_back=false`なら、GPIO 2〜9でLEDC出力とPARLIO RX入力を公開APIだけで共存させられる。** LEDC→PARLIOとPARLIO→LEDCの両方で成立。
2. 最終GPIO mappingは両variantともLEDC SigOut 126〜133、PARLIO SigIn 188〜195、`InputEn=1`、peripheral output enabled。
3. 8 MHz設定・8-bit・8,192 sample × 3回で、全laneの100 kHz PWM dutyを取得。最大duty誤差は2,198 ppm、edgeは204〜207。
4. E014の失敗要因は初期化順ではなく`io_loop_back=true`による出力経路の上書きだった。

**候補**: 既存peripheralを内部観測するとき、PARLIOは`io_loop_back=false`で入力経路だけを追加する。

**未決**: sample rate上限(`p4-parlio-rate`) / PSRAMへの直接DMA([E016](e016_p4_parlio_psram_direct/README.ja.md)) / PSRAM帯域と退避限界(`p4-psram-bandwidth` / `p4-parlio-psram-spool`) / 外部pad上の電気的確認。

### E012 銘板: 版情報の自動埋め込み — 完了 2026-09-04

全文: [e012_banner_autofill/README.ja.md](e012_banner_autofill/README.ja.md)。

**事実**

1. **conftest の `pytest_configure` で設定した環境変数が compile に届く。** `build_config.toml` の `[defines]` が `-DNAME="値"` として展開される。
2. **git の short hash と dirty 判定が銘板に入る** → `dut.log` 1 つで firmware を特定できる。**未コミットで走らせたことも残る**(実験中は常に dirty なので、正直な表示として有用)。
3. **毎回変わる値を入れても host core では実行時間の差が測れない**(8.8〜9.1 s で重なる)。増分ビルドが効いている。

**候補**: **標準の銘板には git のみ。実行時刻は入れない** — コストではなく**冗長**だから(`_runs/` 名と pytest-embedded のログパスに既にある)。core 版も `sketch.yaml` の pin から git 経由で辿れる。

**未決**: 実機(ESP32)の再ビルドコストは未測定。毎回変わる値を入れないと決めたので当面は影響しない。既存 E001〜E011 の銘板は手書きのまま(記録済みのものを後から変えない)。

**反映**: `conftest.py` に `pytest_configure` を追加。規則 §5 に銘板の標準形と注入の仕組みを明記。

### E011 ハーネス: 誤爆防止の実地確認 — 完了 2026-09-04

全文: [e011_collection_guard/README.ja.md](e011_collection_guard/README.ja.md)。

**事実**

1. **命名規約だけで誤爆から守れている。** 実験 11 本の状態で、引数なし / ディレクトリ指定 / カレント全体のいずれも**収集ゼロ**。ファイル指定は 1 件収集。
2. **回帰テスト(`test_*.py`)を置けば引数なしでそれだけが拾われる** — 規則 §0 の「experiment はまとめて走らない / test はまとめて走る」が追加の仕掛けなしに成立。
3. marker やオプションによる選別は**要らなかった**。

### E010 ハーネス: 1 ファイルに複数のテスト関数 — 完了 2026-09-04

全文: [e010_dut_scope/README.ja.md](e010_dut_scope/README.ja.md)。

**事実**

1. **「1 実験 1 テスト関数」は一般的な制約ではなかった。** 実機では 2 関数とも動く(2 回とも 2 passed)。
2. **host core では 2 つ目の DUT 生成が `Connection refused` で失敗する。** 前の実行の後片付けと次の起動の競合と見られる。
3. **skip されるテスト関数でも DUT は作られる**(`pytest.skip()` は fixture setup の後)。
4. **実機では関数ごとに再 upload される**ので、関数を増やすと遅くなる。
5. `dut` fixture は function scope 固定で、scope を変える公開オプションは無い。

**反映**: 規則 §1.3 を「無条件」→「**host core 限定 + 実機では速度上の推奨**」に緩めた。**[E001](e001_smoke_host/README.ja.md) 事実 2 を一般化しすぎていたのを本実験が訂正**。

### E009 退避: `_runs/` の自動保存 — 完了 2026-09-04

全文: [e009_runs_archive/README.ja.md](e009_runs_archive/README.ja.md)。

**事実**

1. **`test_case_tempdir` に依存する autouse fixture の teardown で、成否に関わらず退避できる。** 失敗 run こそログが要るという用途に合う。
2. **peer を使う実験では `peer-device.log` も一緒に入る**(ディレクトリごとコピーするので、conftest は DUT 数を知らなくてよい)。
3. **ID は実験ディレクトリ名から導出できる**(`e001_smoke_host` → `E001`)。ID を path に入れると決めたことがここで実利になった。
4. 連続実行しても上書きしない(名前に UTC 秒を含む)。

**反映**: `experiments/conftest.py` を追加。規則 §3.4 を「手動」→「**自動**」に更新。

### E008 線: SWIO write フレームの検算 — 完了 2026-09-04

全文: [e008_wire_swio_frame/README.ja.md](e008_wire_swio_frame/README.ja.md)。run: `_runs/E008_*_s3_peer/`。

**事実**

1. **SWIO の write フレーム(start + addr7 + rw + data32 = 41 bit)をパルス幅で出し、幅から元の bit 列に復号できる**(5 ベクタ × 3 回すべて一致)。
2. **615 パルスを測って min = med = max**。ジッタが観測されない。
3. `1`(287.5 ns)と `0`(887.5 ns)は**重なりなく分離**。E006 の結果がフレーム全体でも保たれる。

**候補**: **ESP32 の SWIO phy は RMT TX で作る** — bit ごとに幅の違う symbol を並べるだけで、ソフトのタイミングループが要らない。

**未決**: read 位相は未実装(受信側が target を演じる必要がある)/ **実 CH32 の閾値は不明のまま**(`swio-threshold`)/ 立ち上がり時間は RMT では見えない。

**主張できる範囲**: 「自分の符号器が参照実装の読みどおりに出している」まで。`attested` 止まり。**[coverage](../coverage.ja.md) P3-6 は埋まっていない。**

### E007 線: RVSWD host 位相フレームの検算 — 完了 2026-09-04

全文: [e007_wire_rvswd_frame/README.ja.md](e007_wire_rvswd_frame/README.ja.md)。run: `_runs/E007_*_s3_peer/`。

**事実**

1. **RVSWD host 位相の 42 bit を線上に出せる**(5 ベクタ × 3 回すべて一致、半周期 5 us)。
2. **送信器・受信器・手計算の 3 者が一致**(全 1 → ones=41 → parity=0 → `FFFFFFFFFF80`、全 0 → parity=1 → `000000000040`)。送受が同じ誤りを共有している可能性は下がる。
3. [link-to-target §3](../protocols/link-to-target.ja.md) の bit レイアウトが**実行可能な形**になった。CH32RVProbe の RVSWD phy の出発点にできる。

**未決**: **parity の規約が確定していない**(仕様は「odd parity」としか書いておらず、この実験は「parity bit を含めて 1 の個数を奇数にする」と仮定した)。**実 CH32 が応答するかでしか決まらない**。target 応答位相・STOP 条件・初期化 100 clocks は未実装。

**主張できる範囲**: 「**自分の符号器が仕様の読みどおりに bit を並べている**」まで。送受とも自作なので `attested` 止まり。**[coverage](../coverage.ja.md) P3-7 は埋まっていない** — §3 を `verified` にするのは実 CH32 が応答したときだけ。

### E006 道具: パルス幅の生成と記録(RMT)— 完了 2026-09-04

全文: [e006_tool_pulse_capture/README.ja.md](e006_tool_pulse_capture/README.ja.md)。run: `_runs/E006_*_s3_peer/`。

**事実**

1. **分解能 12.5 ns**(`rmtInit` が 80 MHz を受け付ける)。SWIO の短パルス 290 ns = 23 tick。
2. **実測値は量子化された公称値と完全一致し、ばらつきが観測されない**(24 サンプルすべてで min=med=max)。
3. **250 ns でも取りこぼしゼロ**(RX フィルタを 0 にする)。
4. **290 ns と 890 ns は明確に分離できる** → SWIO の 0/1 を幅で見分ける道具として成立。
5. **安価な LA(24 MS/s、290 ns = 約 7 サンプル)より細かい。**

**候補**: SWIO の**送信**も RMT TX で作る(幅を tick 単位で正確に指定でき、ばらつきが無い)。将来の ESP32 phy にそのまま使える。

**限界(誤読注意)**: 示したのは**道具の精度**であって **CH32 が何を受け付けるか**ではない。`swio-threshold` は実 target が必須のまま。立ち上がり波形(~120 ns)は RMT では見えないので、形が要るなら LA が要る。

**反映**: `swio-threshold` の機材欄を「使い捨て + LA 必須」→「**一時、LA は任意**」に更新。

### E005 道具: クロック同期のビット取り込み — 完了 2026-09-04

全文: [e005_tool_clocked_capture/README.ja.md](e005_tool_clocked_capture/README.ja.md)。run: `_runs/E005_*_s3_peer/`。

**事実**

1. **peer 対の 2 線でクロック同期の bit 列を取り込める。** [link-to-target §3](../protocols/link-to-target.ja.md) の規則(data は clock LOW で変化、clock HIGH でサンプル、MSB first)がそのまま動く。
2. **道具の仕様値は半周期 5 us 以上 = 100 kbps まで**(3/3)。2 us で 2/3、1 us で 1/3、遅延なしでは **2 bit しか拾えない**。
3. **失敗の仕方は「取りこぼし」で「位相ずれ」ではない**(拾えた分の値は正しい)。サンプル位相の設計は正しく、純粋に受信側の速度が律速。

**候補**: 検証時は送信側を 100 kbps 以下に落とす(符号の検証には十分)。速くするなら **SPI slave で受ける**のが本命 → 候補 `tool-fast-capture`。

**未決**: 実速度での観測手段 / 送信側の上限(未測定)。

**反映**: 候補 `wire-bitstream` の機材要件を更新 — **LA なしで常設 v2 だけで RVSWD の bit 列を検証できる**(速度を落とす条件付き)。

### E003 スモーク: peer 2 台(環境確認)— 完了 2026-09-04

全文: [e003_smoke_peer/README.ja.md](e003_smoke_peer/README.ja.md)。run: `_runs/E003_*_s3_peer/`。

**事実**

1. **2 台の同時 build / upload / monitor が成立する。** `peers["device"]` で操作でき、両 board とも trigger に 1 発で応答。
2. **繋がっているのは GPIO19↔19 と GPIO20↔20 の 2 本**(双方向)。17・18・21 はどこにも繋がっていない。**ESP32-S3 の native USB ピン(D−=19 / D+=20)**。
3. 入力を pull-down にしたことで未接続が常に 0 になり、**`READ=1` が接続の実証**になった。仮定を検算する形ではなく**走査して topology を発見する形**にしたのが正解。

**候補**: peer 対は **2 線の直結**として使える(RVSWD の SWCLK + SWDIO と形が一致)。ただし 19/20 は native USB ピンなので、線として使う間は native USB を使えない(現在は CH340 経由の UART なので支障なし)。

**未決**: `device-lock`(2 プロセス間の排他)/ この 2 線で RVSWD の速度・波形が成立するか(別実験)。

**手順違反**: 計画を書く前に実装・実行した。最初の実装は誤った仮定を検算するだけで、失敗しても何が繋がっているかは分からなかった。**計画段階で「仮定を置かずに走査する」と決めていれば一度で済んだ** — 規則 §3.2 の実利がそのまま出た例。
