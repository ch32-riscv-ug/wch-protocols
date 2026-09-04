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
| **E001** | 実機なしで sketch を build・実行し、出力を assert できるか(profile 解決 / `socket://localhost` / 銘板) | **常設 v0**(実機なし。host Arduino core `lang-ship:host:host`) | (規則そのもの) | **完了**([e001_harness_smoke/](e001_harness_smoke/README.ja.md)) |
| **E002** | 実機で通るか — upload / 実 port(別名パス)の解決 / 実時間の計測 / device lock / DTR reset の挙動 | **常設 v1**(ESP32-S3 1 枚) | [ecosystem-any-hardware](../references/ecosystem-any-hardware.ja.md) §4.5 | 未着手 |
| **E003** | peer は使えるか(**環境確認のみ**)— 2 台同時 upload / lock が 2 枚に効くか / 片方の GPIO 遷移をもう片方が観測できるか | **常設 v2**(既設 peer 2 枚。GPIO18/19 直結済み、配線変更なし) | [README.ja.md §4.5](README.ja.md) v2 | 未着手 |

順序に意味がある([README.ja.md §4.5](README.ja.md)):

- **E001 が最初**なのは、これが通らないうちに測った数値は測定対象ではなく環境を測っているから([§3.1.2](README.ja.md))。**実機を一切使わない**ので、board の有無に関係なく今日始められる。
- **E002** で初めて実機が出る。E001 で切り分け済みなので、ここで落ちたら原因は「実機まわり」に限定される。
- **E003** は peer を**やるかどうかを決めるための確認**であって、peer テストの実装ではない。既設の 2 枚は GPIO18/19 が直結されており、**RVSWD が 2 線なので配線を触らずに 2 線ペアの形が既にある**。使えると分かれば選択肢として残り、使えないと分かれば早く諦められる。

## 2. 候補(未採番)

番号は付けない。参照は slug で行う。**機材の可否を確認してから** §1 へ移して採番する。

「用意」列: **有** = 手元にある / **要調達** = 買う・借りる必要がある / **不明** = 確認していない。

| slug | 問い | ベンチ種別 | 必要な機材 | 用意 | 影響する doc |
|---|---|---|---|---|---|
| `loopback-inject` | loopback phy で DMI status の fail/busy・無応答・CRC 誤りを注入したとき、host は仕様どおり回復するか | **常設 v0**(実機なし) | host Arduino core のみ | 有 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §2–§4/§6 |
| `dut-scope` | DUT を module scope にできるか(1 実験で複数のテスト関数を書きたいとき)。E001 事実 2 の回避策の一般化 | **常設 v0** | host Arduino core のみ | 有 | [README.ja.md §1.3](README.ja.md) |
| `runs-archive` | `_runs/` への結果退避は conftest の teardown で確実に起きるか。失敗した run でも残るか | **常設 v0** | host Arduino core のみ | 有 | [README.ja.md §3.4](README.ja.md) |
| `collection-guard` | bare `pytest` が実験ファイルを収集しないことを、実プロジェクトの構成でも保てるか | **常設 v0** | host Arduino core のみ | 有 | [README.ja.md §1.3](README.ja.md) |
| `banner-autofill` | 銘板の版情報(fw hash / core 版 / 日時)を build 時に自動で埋められるか | **常設 v0** | host Arduino core のみ | 有 | [README.ja.md §5](README.ja.md) |
| `wire-bitstream` | SWIO / RVSWD の **bit 列**(start・addr7・data32・op2・parity)は [link-to-target](../protocols/link-to-target.ja.md) §3 の仕様どおりか。**タイミングは見ない** | **常設 v0**(実機なし) | host Arduino core の GPIO 遷移記録 | 有 | [link-to-target](../protocols/link-to-target.ja.md) §3 |
| `linke-error-frame` | WCH-Link の異常系 error 応答 frame の形式(target 無し等) | **常設**(capture) | LinkE + usbmon | 有 | [pc-to-link](../protocols/pc-to-link.ja.md) §3、P1-1 |
| `isp-xor-key` | factory ISP の XOR key 生成算法と実 frame(chip 系列別) | **一時**(capture) | WCHISPTool(Windows)+ usbmon | 不明 | [pc-to-device-isp](../protocols/pc-to-device-isp.ja.md)、**P2-3(最大の穴)** |
| `iap-order` | WCH IAP の host↔device 往復順序 | **一時**(capture) | WCHMcuIAP + 対応 chip | 不明 | [serial-and-print](../protocols/serial-and-print.ja.md) §6、P2-4 |
| `dap-mode` | DAP mode の切替 byte 手順と CMSIS-DAP v1/v2 判定 | **常設**(capture) | LinkE + usbmon | 有 | [dap](../protocols/dap.ja.md)、P2-5 |
| `uart-dtr-reset` | port open の DTR auto-reset で probe は reset するか。host は `hello` をいつから撃てるか | **一時** | Uno / ESP32 DevKit / Pico | 不明 | [ecosystem-any-hardware](../references/ecosystem-any-hardware.ja.md) §4.5 |
| `set-baud` | `set_baud` の切替はどの手順なら取りこぼさないか。失敗時に 115200 へ戻れるか | **一時** | CH340 / CP2102 / 内蔵 CDC の 3 種 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §9-3 |
| `dmi-latency` | DMI 1 往復の時間は transport ごとに幾らか(UART 115200/1M、USB CDC、TCP、WebSocket) | **一時** | probe(ESP32-S3 / Pico)+ target 1 個 | 不明 | [generic-probe-design](../references/generic-probe-design.ja.md) §6/§9-2 |
| `wire-autodetect` | 1 線/2 線の自動判別はどの手順で確実に効くか。誤判別する条件は | **一時** | probe + V003 と V307 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §9-1 |
| `ardulink-compat` | ardulink 互換モードの 1 byte 目自動判別は、minichlink を無改造で通すか | **一時** | probe + V003 + minichlink | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §7 |
| `flash-time` | 64 KB 書込時間は per-op / batch / batch+`poll`+`write_rep` で幾ら違うか。基準装置(LinkE + ch32rv)と比べて | **一時** | probe + V307 + **LinkE** | 不明 | [generic-probe-design](../references/generic-probe-design.ja.md) §6、[dmi-bridge](../protocols/dmi-bridge.ja.md) §4.3 |
| `lane-independence` | lane を 2 本同時に attach したとき、互いのタイミングは劣化するか。`max_inflight=1` で足りるか | **一時** | ESP32-S3 + V003 と V307 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §6.1 |
| `swio-threshold` | SWIO の LOW パルス幅は 0/1 をどこで分けるか。**動かなくなる境界は両側どこか** | **使い捨て** | probe + V003 + **LA**。SWIO の 290 ns パルスは 24 MS/s で約 7 サンプル、立ち上がり(~120 ns)まで見るなら **100 MS/s 以上** + marker 線 | **要調達?** | [link-to-target](../protocols/link-to-target.ja.md) §3/§5、P3-6、[dmi-bridge](../protocols/dmi-bridge.ja.md) §9-2 |
| `rvswd-frame` | RVSWD の bit フレーム(7+32+2+1 ×2)は実波形と一致するか。STOP 波形とクロック周波数は | **使い捨て** | probe + V203/V307 + **LA 3ch**(SWCLK/SWDIO/marker) | **要調達?** | [link-to-target](../protocols/link-to-target.ja.md) §3、P3-7 |
| `5v-swio` | 5 V board(Uno)から open-drain で SWIO を叩けるか。直列抵抗だけで安全か | **使い捨て** | Uno + V003 + **LA** + 抵抗 | 不明 | [dmi-bridge](../protocols/dmi-bridge.ja.md) §8.2 |

### セッションの束ね方(使い捨てベンチ)

LA を組むベンチは設営が重いので、**組んだら一度に消化する**([README.ja.md §3.1.1](README.ja.md))。現時点で見えている束は 2 つ:

- **SWIO セッション**: `swio-threshold` + `5v-swio`(どちらも SWIO 1 本 + marker。V003 のまま抵抗と board を差し替えるだけ)
- **RVSWD セッション**: `rvswd-frame`(3ch。SWIO セッションとは配線が違うので分ける)

セッションを組む直前に、そのとき残っている候補をもう一度見て「ついでに取れるもの」を足す。

**先に `wire-bitstream` を v0 で通しておく**と、LA セッションで見るべきものが「タイミングだけ」に絞れる。符号の誤りと波形の誤りを同時に相手にしないで済むので、設営の重いベンチほどこの分離が効く。

## 3. 記録

### E001 ハーネスのスモーク(実機なし)— 完了 2026-09-04

計画・結果の全文: [e001_harness_smoke/README.ja.md](e001_harness_smoke/README.ja.md)。run: `_runs/E001_20260904T050917Z_host/`。

**事実**

1. **実機なしで build → 実行 → assert が成立する**(3/3 pass、7.3–7.5 s)。arduino-cli 1.3.1 / lang-ship:host 1.7.1 / pytest-embedded-arduino-cli 1.4.1。→ **常設 v0 は使える**。
2. **DUT はテスト関数ごとに生成される。** 同一ファイルの 2 関数目で `Connection refused`。**1 実験 1 テスト関数**にまとめれば通る。
3. **ビルド生成物は `<実験>/build/<profile>/`**(`output/` ではない)。
4. 生ログは `/tmp/pytest-embedded/<UTC>/<test 名>/dut.log`。銘板を含め期待どおりの 5 行のみ。
5. **実行の明示指定はファイルパスでなければならない**(ディレクトリでは収集 0 件)。

**候補**: 1 実験 1 テスト関数(採用)/ 銘板の `build=` に `__DATE__ __TIME__`(`banner-autofill` で置換予定)。

**未決**: `dut-scope`(DUT の scope 変更が可能か)/ `runs-archive`(退避は今回手動)/ `collection-guard`(bare `pytest` 0 件は観測したのみ)。

**反映**: 規則 §1.3(ファイルパス指定・1 実験 1 テスト関数)、§3.4(`output/` → `build/`)、§8(言語ルール)を更新。仕様の status は動かない。
