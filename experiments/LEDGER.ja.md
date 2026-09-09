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
| **E012** | 銘板の版情報を conftest から build 時に自動で埋められるか。再ビルドのコストは | **常設 v0**(実機なし) | [README.ja.md §5](README.ja.md) | **完了**([e012_banner_autofill/](e012_banner_autofill/README.ja.md)) |
| **E013** | 同一VID:PID・異なる`bcdDevice`でHID-onlyとHID + Vendor + CDC × 1をWindowsが分離でき、Linuxでも各経路が通信できるか | **一時・専用機材**(別途用意するESP32-S3 native USB、Windows 11、Linux) | [probe-feasibility-gates](../references/probe-feasibility-gates.ja.md) Gate 2 / Gate 4 | **計画**([e013_usb_descriptor_profiles/](e013_usb_descriptor_profiles/README.ja.md)) |
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
| **E011** | `test_` を付けない規約は、実験が 10 本を超えた実プロジェクトでも誤爆から守れているか | **常設 v0**(実機なし) | [README.ja.md §1.3](README.ja.md) | **完了**([e011_collection_guard/](e011_collection_guard/README.ja.md)) |
| **E010** | 1 つの実験ファイルに複数のテスト関数を置けるか。置けないならその制約は何によるか | **常設 v0 + v1** | [README.ja.md §1.3](README.ja.md) | **完了**([e010_dut_scope/](e010_dut_scope/README.ja.md)) |
| **E009** | 実験の生ログを `_runs/` へ自動退避できるか。失敗した run でも残るか | **常設 v0**(実機なし) | [README.ja.md §3.4](README.ja.md) | **完了**([e009_runs_archive/](e009_runs_archive/README.ja.md)) |
| **E008** | SWIO write フレーム(start+addr7+rw+data32=41 bit)をパルス幅で出し、幅から復号できるか | **常設 v2** | [link-to-target](../protocols/link-to-target.ja.md) §3/§5(**status は動かさない**) | **完了**([e008_wire_swio_frame/](e008_wire_swio_frame/README.ja.md)) |
| **E007** | RVSWD host 位相の 42 bit(addr7+data32+op2+parity)を線上に出したとき、[link-to-target](../protocols/link-to-target.ja.md) §3 の仕様どおりか | **常設 v2** | [link-to-target](../protocols/link-to-target.ja.md) §3(**status は動かさない**) | **完了**([e007_wire_rvswd_frame/](e007_wire_rvswd_frame/README.ja.md)) |
| **E006** | RMT で SWIO 相当のパルス幅(290 / 890 ns)を生成・測定できるか。分解能は(**道具の性能測定**) | **常設 v2** | (道具)→ 候補 `swio-threshold` の機材要件 | **完了**([e006_tool_pulse_capture/](e006_tool_pulse_capture/README.ja.md)) |
| **E005** | 2 線でクロック同期の bit 列を取り込めるか。取り込める最短のクロック周期は(**道具の性能測定**) | **常設 v2** | (道具)→ 候補 `wire-bitstream` の機材要件 | **完了**([e005_tool_clocked_capture/](e005_tool_clocked_capture/README.ja.md)) |
| **E004** | host からの trigger で応答させれば、起動時出力の取りこぼし(E002)を回避して銘板と実時間を取れるか | **常設 v1** | (規則そのもの)+ [dmi-bridge](../protocols/dmi-bridge.ja.md) §4.1 の裏付け | **完了**([e004_smoke_board_trigger/](e004_smoke_board_trigger/README.ja.md)) |

順序に意味がある([README.ja.md §4.5](README.ja.md)):

- **E001 が最初**なのは、これが通らないうちに測った数値は測定対象ではなく環境を測っているから([§3.1.2](README.ja.md))。**実機を一切使わない**ので、board の有無に関係なく今日始められる。
- **E002** で初めて実機が出る。E001 で切り分け済みなので、ここで落ちたら原因は「実機まわり」に限定される。
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
| `p4-external-clock` | PARLIO external clock入力でfinite/circular batch captureが成立する周波数・停止条件はどこか | **一時・要配線** | PSRAM搭載ESP32-P4 1枚、clock source | 現在不可 | 同上。clock |
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
