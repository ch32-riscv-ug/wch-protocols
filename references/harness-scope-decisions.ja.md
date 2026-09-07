# 大枠の決定軸 — 何をどこまで対応するか

状態: **決定待ちの軸の一覧**。**選ばない。**
基準日: 2026-09-07

> **[harness-requirement-triage.ja.md](harness-requirement-triage.ja.md)(143 件の feature 判定)は細かすぎた。** feature を 1 つずつ判定しても、**「どの MCU を」「どの series を」「APP か BL か」が決まっていないと判定が空回りする**。本書は**軸(次元)だけ**を並べ、triage はその後に使う。

**軸ごとに粒度が違う**のが要点。全体で 1 回決めるもの、**MCU 別**に決めるもの、**series 別**に決めるものがある。

## 1. 軸の一覧

| # | 軸 | 粒度 | 下流で決まるもの |
|---|---|---|---|
| **D1** | **probe にする MCU をどこまで** | **MCU 別** | 到達できる段 / PID の出どころ / phy の実装手段 |
| **D2** | **覆う target series をどこまで** | **series 別** | **差別化の中心(R-b)**。phy の実装量 | 
| **D3** | 1 線 / 2 線をどちらから | series と連動 | phy の順序 |
| **D4** | **APP か BL か** | **series 別**(§2.4 でほぼ自動的に決まる) | 配布形態 / entry / 既存との住み分け |
| **D5** | **どの段(L0〜L8)まで** | 全体 | **triage の `D` 判定 28 件のうち 14 件** |
| **D6** | どの host から使えるように | 全体 | 互換モードの数 |
| **D7** | 自前 USB を出すか / PID をどう使うか | MCU 別 | descriptor の固定 |
| **D8** | **license 方針** | 全体 | 既存実装との接触の可否 |
| **D9** | 安全・保護をどこまで規定するか | board 別 | ハード設計の要求 |
| **D10** | **配布形態と保守の持ち主** | 全体 | R-c(保守を自分の手に) |
| **D11** | **既存 firmware との関係** | 全体 | 互換モードか置き換えか |
| **D12** | 実装スタック(言語・SDK) | MCU 別 | 移植のしやすさ |

**⚠ `D2` / `D4` / `D8` / `D10` / `D11` / `D12` は、コアの `H-001`〜`H-191` に対応する項目が無い**(§3)。コアは**自分の試験の必要**から要求を書いているので、**「どの chip を、どういう形で、誰が保守するか」は要求として現れない**。

---

## 2. 軸の中身

### 2.1 D1 — probe にする MCU をどこまで

| MCU | 到達できる段 | PID | 判断材料 |
|---|---|---|---|
| **RP2040** | **L0〜L8**(全部) | vendor program `0x2E8A`(無償) | **最安・最安定・PIO で phy が決定論的**。[board-survey](harness-board-survey.ja.md) の基準 |
| RP2350 | L0〜L8 | 同上 | RAM/PIO 倍。**errata E9 が LA 用途に直撃** |
| **ESP32-S3** | L0〜L8(**capture が深い**) | vendor program `0x303A` | Wi-Fi(L2)/ PSRAM。**アナログ弱い。GPIO19/20 が USB** |
| **CH32X03x** | **L0〜L1 + アナログ**(LA 不可) | **pid.codes(唯一必要)** | **PIOC / 5V 直結 / 最安の hardware USB** |
| **CH32V003** | L0〜L1(**software USB**) | pid.codes | **$0.1。連鎖 bootstrap の苗**。低速・Windows 不安定の実地報告 |
| AVR(Uno / Nano) | L0(5V の SWIO のみ) | **0**(既存 bridge) | **手持ちで始められる**。ardulink 互換で今日届く |
| ESP32-P4 | L8 の上(HS / Ethernet / PARLIO) | vendor program | **triage の `E` の 5 件がここに依存** |

**決めること**: **いくつ載せるか**。1 つに絞れば深く作れる。増やすほど `caps` の意味が増える(それは設計原則どおり)が、検証の面積が増える。

### 2.2 D2 — 覆う target series をどこまで(**H-list に無い軸**)

> **⚠ 訂正(2026-09-07)。** 初版は「穴 10 series」としたが、**そのうち L103 以外は未発売**で、**この道具でも救えない**(市場に無いものは試験も書込もできない)。初版は **datasheet が存在する series** を数えていた — **WCH は発売前から資料を公開する**ので、`ch32-device-data` にも EVT にも**入手可否の欄が無い**(§2.2b)。

| 区分 | series | 数 | 意味 |
|---|---|---:|---|
| **既存実装で届く** | V002/003/004/005/006/007, M007, V203/205/208/303/305/307/317, X033/X035 | **16** | **我々が作っても series 被覆としては差にならない** |
| **実在する穴** | **L103** / **V103** | **2** | **ここだけが「今の不満」** |
| **将来の穴(未発売)** | H415 / H416 / H417 / M030 / M103 / V407 / V467 / X305 / X315 | **9** | **発売されたら穴になりうる**。先回りの価値はあるが**今の不満ではない** |

**実在する穴の中身**:

| series | 線 | 状況 |
|---|---|---|
| **L103** | 2 線(PA13/PA14) | **どの既存実装も主張していない**。低消費電力用途で需要がある |
| **V103** | 2 線(PA13/PA14) | **rvswdio が「テスト済み・非対応」と明記**。Swindle は V2xx/V3xx のみ。**唯一「試して駄目だった」記録がある series** → **線の仕様に series 差がある可能性**([link-to-target §3](../protocols/link-to-target.ja.md) の未解決項目に直結) |

**決めること**: **(a) 実在する 2 series(L103 / V103)を取るか、(b) 未発売 9 series に先回りするか、(c) 既存 16 series でも `caps` / 速度 / 保守のために自分で持つか。** (c) は series 被覆とは別の理由。

> **この訂正の帰結は大きい。** **`R-b`(2 線の被覆の穴)は「今の不満」としてはほぼ消える。** → 差別化は **`caps`(情報の壁)/ 速度(batch)/ 保守(自分の手に)/ 上の段(S1 = capture)** に移り、**`D5`(capture を作るか)が相対的にいちばん重い判断になる。**

### 2.2b 入手可否のデータが無い(**データ依頼の候補**)

| 事実 | 意味 |
|---|---|
| `ch32-device-data` の `index/parts.csv` は **27 series・103 型番**を持つが、**release / availability / EOL の列が無い** | **「資料がある」と「買える」が区別できない** |
| EVT の `eval_boards.csv` も**未発売 series に eval board が載っている**(WCH が資料を先行公開する) | eval board の有無は入手可否の代理にならない |

→ **`ch32-device-data` へ「入手可否(released / preview / 未発売 / EOL)」の列を依頼する候補。** これが無いと、**D2 の判断を毎回人の知識に頼ることになる**。同型の依頼が [data/harness-wiring/](data/harness-wiring/README.ja.md) にもある。

### 2.3 D3 — 1 線 / 2 線

| 線 | 対象 | 状況 |
|---|---|---|
| **1 線(SWIO)** | V003 のみが「1 線だけ」。V00x / M0xx / V205 / V407 / V467 / X305 / X315 / H41x は**両対応** | 既存実装が 10 個。**閾値は未測定**(§P14) |
| **2 線(RVSWD)** | V103 / V20x / V30x / X03x / L103 / M103 ほか | **穴の 10 series は全部ここ**。BMP-RV RPC が既存 |

**決めること**: **どちらを先に書くか**。穴を埋めるなら 2 線が先。**両対応 series では 1 線を選ぶと pad が空く**というレバーもある。

### 2.4 D4 — **APP か BL か**(**series 別。BOOT 領域サイズでほぼ決まる**)

**これが「series 別に判断が必要」の実体。** BOOT 領域の大きさが 30 倍違う([custom-bootloader §2a](../protocols/custom-bootloader.ja.md)):

| BOOT 領域 | series | BL に入れられるか |
|---:|---|---|
| **1,920 B** | **V003** / CH641 | **✗ 無理。** 既存 BL の実サイズがほぼ 1,920 B([v003-bootloader-replacement](v003-bootloader-replacement.ja.md))。**APP で。BL は既存(B003)に任せる** |
| **3,328 B** | V002/004/005/006/007, **X033/X035**, L103 | **△ 要見積り。** 1,920 B 版 + 約 1,400 B の余地 |
| 2 KB + 1,792 B(**2 分割**) | **V103** | **△ 分割が障害**。ch32fun BL が「非対応見込み」 |
| **28 KB** | **V20x / V30x / V31x, V407, X315** | **◎ 余裕。** basic USB stack + protocol が余裕で入る |
| **56 / 28 KB** | **H417** | **◎ 余裕** |
| **無し** | **M030** | **✗ 不可**(BOOT 領域が存在しない)→ **APP のみ** |

> **⚠ 前の判断を訂正する。** [定義 §5.8](harness-tool-definition.ja.md) は「**BL は捨て、APP は取る**」を方向候補として出したが、**それは V003(1,920 B)を見て言っていた**。**28 KB ある series では BL に入れるのが自明に楽**で、しかも**穴の 10 series の多く(V407 / X315 / H41x)がそこに入る**。→ **「BL を捨てる」は V003 に限った話**。

**決めること**: series ごとに **APP のみ / APP + BL / BL は既存に任せる**。**BOOT 領域サイズが 3 段(1,920 / 3,328 / 28K+)なので、判断も 3 通りで足りる。**

### 2.5 D5 — どの段まで

L0(書く)/ L1(printf)/ L2(リモート)/ L3(複数)/ L4(GDB)/ L5(LA)/ L6(書きながら見る)/ L7(相手役)/ L8(計測器)。→ [定義 §4](harness-tool-definition.ja.md)。

**決めること**: **上端をどこにするか。** これが **triage の `D` 判定 28 件のうち 14 件を一度に決める**(capture を作るか)。

### 2.6 D6 — どの host から

| host | 経路 | コスト |
|---|---|---|
| **ch32rv** | 我々の protocol の backend | 本線 |
| **Arduino IDE** | ch32rv 経由 | 自動 |
| **minichlink** | **ardulink 互換モード** | 小 |
| **BMDA(Black Magic)** | **BMP-RV RPC 互換モード** | 小。**既存 host stack が使える** |
| GDB | ch32rv の GDB server | ch32rv 側 |
| ブラウザ | WebSerial / WebHID / WebSocket | transport 次第 |

### 2.7 D7 — 自前 USB / PID

→ [harness-choices §2](harness-choices.ja.md) の軸 A〜D。**D1 に従属**(silicon が決まれば PID の出どころが決まる)。

### 2.8 D8 — license(**H-list に無い**)

- 我々の firmware / host の license。
- **既存実装との接触**: **Swindle は GPL-3.0 系**。**protocol を喋るだけなら影響しない**が、**実装を読んで移植すると影響する**。`rvswd.pio` が attic にしかないので、**2 線 phy を「自分で書く」か「読んで書く」かで license の扱いが変わる**。
- rvswdio / NHC-Link042 / PicoRVD は MIT 系。

### 2.9 D9 — 安全・保護

5V target(D1 と連動)/ VBUS 過電圧(PD 試験)/ 誤配線(直列抵抗)/ 過電圧検出。**board のハード設計をどこまで我々が規定するか**。

### 2.10 D10 — 配布形態と保守の持ち主(**H-list に無い**)

- **配布**: UF2(RP2040 は内蔵 BOOTSEL でゼロコスト)/ prebuilt binary / ソースのみ。
- **保守**: **自前で持つ**(R-c)/ **upstream に出す**(既存 project へ PR)/ **両方**。
- **版管理**: fixture manifest が `probe_firmware` を要求している。

### 2.11 D11 — 既存 firmware との関係(**H-list に無い**)

| 選択肢 | 中身 |
|---|---|
| **互換モードを持つ** | 我々の firmware が ardulink / BMP-RV も答える。**受信 1 byte 目で判別でき、衝突が無いことは確認済み**([existing-standards §6.B★](harness-existing-standards.ja.md)) |
| **host 側で吸収する** | ch32rv が既存の口を喋る。**firmware は書かない** |
| **置き換える** | 既存を使わない |

### 2.12 D12 — 実装スタック(**H-list に無い**)

Arduino ライブラリ([generic-probe-design](generic-probe-design.ja.md) の想定)/ Pico SDK / Rust(ch32rv と同じ言語)/ ch32fun。**移植のしやすさと、誰が書けるかが変わる。**

---

## 3. H-list に無い軸(足りていないもの)

| 軸 | なぜ H-list に無いか |
|---|---|
| **D2 覆う target series** | コアはベンチにある 6 枚(V307/V203/V103/L103/X035/V003)で困っていないので、**series の被覆が要求として出てこない**。だが**これが差別化の中心** |
| **D4 APP / BL** | コアは「書ければよい」ので **firmware がどこに住むかに関心が無い** |
| **D8 license** | コアの関心事ではない |
| **D10 配布 / 保守の持ち主** | H-126(更新できる)はあるが「**誰が保守するか**」が無い |
| **D11 既存 firmware との関係** | コアから見ると「動けばよい」 |
| **D12 実装スタック** | 同上 |

→ **これら 6 つは、この repo(すり合わせの場)が持つべき軸。** コアに要求として起票してもらう筋のものではない。

## 4. 落とす候補(**不要かもしれない要求**)

**判定ではなく候補の提示。** 強み(S1 / S2)と結びつかないもの、他が受け持つべきもの:

| ID | 要求 | 落とす理由の候補 |
|---|---|---|
| H-090 | 温度・電圧を変えた再測定 | **環境試験**。恒温槽の話で、この道具の軸に無い |
| H-136 / H-137 | Ethernet / USB HS の相手 | **ESP32-P4 級が要る**。強みは S1(統合)であって帯域ではない([定義 N1](harness-tool-definition.ja.md)) |
| H-138 / H-140 | 並列バス(FSMC/LTDC/DVP)/ QSPI | 同上。**難所は本数ではなく速度** |
| H-088 / H-089 | OPA / コンパレータ / TouchKey の相手 | **コア自身が「対象外」**と書いている領域 |
| H-062 / H-063 | ARGB / PIOC の相手役 | 需要が読めない。**要れば後から足せる**(器の上に載る) |
| H-079 | 長時間の刺激 | 時間がかかるだけ。**運用で足りる** |
| H-164 / H-178 | 未信頼 PR の境界 / ベンチ機材の共用 | **CI と机の運用**。道具の要求ではない |
| H-007 / H-008 | `board-identify` / 常時接続台数 | **OS と運用**。serial を出せば自動 |

**⚠ 落とす判断は「非目標」に格上げするかどうかと同じ**なので、[定義 §8](harness-tool-definition.ja.md) の N1〜N8 と突き合わせる。

## 5. 決める順序(依存関係)

```
D5(どの段まで) ──┬─→ D1(どの MCU)──→ D7(PID)
                 │                 └─→ D12(実装スタック)
D2(どの series)──┼─→ D3(1 線 / 2 線)
                 └─→ D4(APP / BL)──→ D10(配布・保守)
D11(既存との関係)─→ D6(どの host)
D8(license)/ D9(安全) は上流に依存しない(先に決められる)
```

- **上流は D5 と D2 の 2 つだけ。** この 2 つが決まると D1 / D3 / D4 / D7 はほぼ従属的に決まる。
- **D8(license)と D9(安全)は今すぐ決められる**(他に依存しない)。
- **D11 は D6 を決め、そして [existing-standards §6.B★](harness-existing-standards.ja.md) のとおり「我々の host が完成する前に価値が出る」ので、早く決めると効く。**

## 6. 参照

- feature 単位の判定(**大枠が決まってから使う**): [harness-requirement-triage.ja.md](harness-requirement-triage.ja.md)
- 幅の定義と強み: [harness-tool-definition.ja.md](harness-tool-definition.ja.md)
- 既存標準でどこまで / 限界コスト: [harness-existing-standards.ja.md](harness-existing-standards.ja.md)
- MCU 別の到達範囲: [harness-board-survey.ja.md](harness-board-survey.ja.md)
- BOOT 領域の一次データ: [../protocols/custom-bootloader.ja.md](../protocols/custom-bootloader.ja.md) §2a
- series 別の debug 線: [data/harness-wiring/](data/harness-wiring/README.ja.md)
- 所在: [harness-index.ja.md](harness-index.ja.md)
