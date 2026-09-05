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
| `linke-error-frame` | WCH-Link の異常系 error 応答 frame の形式(target 無し等) | **常設**(capture) | LinkE + usbmon | 有 | [pc-to-link](../protocols/pc-to-link.ja.md) §3、P1-1 |
| `isp-usb-verify` | factory ISP の USB 実 frame を capture し、minichlink 転記の byte(XOR key = ΣUID・Erase sector 数・Program 56 B chunk・config 12 B の補数位置)と一致するか | **一時**(capture) | WCHISPTool(Windows)+ USBPcap、または minichlink `-c` ISP + usbmon | 不明 | [pc-to-device-isp](../protocols/pc-to-device-isp.ja.md) §3–§4、P2-3(**算法は転記で埋まった。確認待ち**) |
| `wch-iap-capture` | WCHMcuIAP_WinAPP.exe の UART(460800)/ USB(`1A86:55E0`)実 frame は [wch-iap](../protocols/wch-iap.ja.md) §3–§4 の転記(sync・checksum・VERIFY の addr・END 無応答・順序)と一致するか | **一時**(capture) | Windows + WCHMcuIAP + IAP を焼いた V003 または X035 + USBPcap / UART capture | 不明 | [wch-iap](../protocols/wch-iap.ja.md) §7、P2-4 |
| `hid-bl-capture` | rv003usb / ch32fun bootloader と minichlink の HID feature report(ID・scratchpad 構造・`0x1234ABCD`・完了印 `0xFF`)は [custom-bootloader](../protocols/custom-bootloader.ja.md) §2b の転記と一致するか | **一時**(capture) | BL を焼いた V003 or X035 + minichlink + usbmon | 不明 | [custom-bootloader](../protocols/custom-bootloader.ja.md) §2b |
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
