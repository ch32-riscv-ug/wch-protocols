# DUT harness — 仮想 IF の分離(何を DMI に載せ、何を別にするか)

状態: **draft / 分析**。**結論は出さない**。物理 IF(probe ↔ PC)は [harness-channels.ja.md](harness-channels.ja.md) で整理済みで、本書はその**内側**、つまり **1 本の物理 IF の中でどう論理的に分けるか**を扱う。

**扱わないこと**: **時刻の付与と相関**(複数 source を同じ時間軸に載せるか)は [定義 §9b](harness-tool-definition.ja.md) の**残る論点 2** で、本書とは別。本書で言う「時系列順」は **1 本の列に入れれば到着順が保たれる**という多重化の性質のみを指し、**timestamp を打つ話ではない**。

---

## 1. 通信の全列挙

**凡例** — 形: `R` = req/resp(host 起動) / `P` = push(probe 起動) / `S` = 連続 stream。
落: **落としてよいか**(✗ = 一切落とせない / △ = 落とすなら申告必須 / ○ = 落ちてよい)。

### 1.1 既に仕様にあるもの([dmi-bridge §4](../protocols/dmi-bridge.ja.md))

| # | 通信 | cmd | 形 | 帯域 | latency 要求 | 落 | 1 件の大きさ |
|---|---|---|:--:|---|---|:--:|---|
| 1 | `hello` `caps` `info` `ping` | `0x01`–`04` | R | 極小・一度 | 緩 | ✗ | 小 |
| 2 | `set_baud` `reset_probe` | `0x05` `06` | R | 極小・稀 | 緩 | ✗ | 小 |
| 3 | `lane_attach` `lane_detach` `line_reset` | `0x10`–`12` | R | 極小・稀 | 緩 | ✗ | 小 |
| 4 | **`dmi_read` / `dmi_write`** | `0x20` `21` | R | **高頻度・極小**(6 B) | **最も厳しい**(**往復が支配**) | ✗ | 小 |
| 5 | **`batch`** | `0x22` | R | **中〜大** | **throughput が支配** | ✗ | **大**(`mtu` まで) |
| 6 | `power` `nrst` | `0x30` `31` | R | 極小・稀 | 緩 | ✗ | 小 |
| 7 | `uart_open` `uart_close` `uart_write` | `0x50`–`52` | R | 低〜中 | 中 | ✗ | 中 |
| 8 | `autopoll_set` `autopoll_clear` | `0x60` `61` | R | 極小・稀 | 緩 | ✗ | 小 |
| 9 | **`uart_data`** | `0x80` | **P** | 中(115200 = **11.5 kB/s** / ch) | 中 | **△**(`u16 dropped` で申告) | 中 |
| 10 | `autopoll_hit` | `0x81` | P | 低 | 中 | ✗ | 小 |
| 11 | `log` | `0x82` | P | 低 | 緩 | **○** | 小 |
| 12 | `lane_status`(電源断・線切断・target reset) | `0x83` | P | 極低 | **厳しい**(即知りたい) | ✗ | 小 |

### 1.2 harness で増えるもの(**未設計**)

| # | 通信 | 形 | 帯域 | latency 要求 | 落 | 1 件の大きさ |
|---|---|:--:|---|---|:--:|---|
| 13 | `trace` の設定(trigger 条件 / mask / sample rate / 深さ) | R | 極小・稀 | 緩 | ✗ | 小 |
| 14 | **波形サンプル** | **S** | **0.5〜1 MB/s 以上**(16ch × 数 MHz) | **緩**(まとめ送りでよい) | **○**(ただし**欠落の申告は必須**) | **連続・大** |
| 15 | **ADC サンプル** | S | 中 | 緩 | **○** | 連続 |
| 16 | **意味イベント**(相手役が返した byte 列 / SPI transaction / I2C 応答) | P | 低〜中 | 中 | **✗** | 小〜中 |
| 17 | **DMI 実行印**(この `tag` を実行した) | P | 低 | 緩 | △ | 小 |
| 18 | **`emu` 模型の定義 / preload**(SD image 等) | R | **稀だが極大**(MB 級もありうる) | 緩 | ✗ | **`mtu` を超える → 分割が必要** |
| 19 | `emu` readback | R | 稀・中〜大 | 緩 | ✗ | 大 |
| 20 | 障害注入の設定 | R | 極小・稀 | 緩 | ✗ | 小 |
| 21 | **RTT / SDI / DMDATA の up**(target → host の print) | **今は R**(DMI read の繰り返し)→ **P にしたい** | 中(複数 channel) | 中 | ✗ | 中 |
| 22 | RTT down(host → target の入力) | R | 低 | 中 | ✗ | 小 |
| 23 | **probe 自身の書換え**(`self_update`) | R | **大**(firmware image) | 緩 | ✗ | **大・分割** |
| 24 | **overflow / 背圧の通知** | P | 極低 | **厳しい** | ✗ | 小 |

⚠ **`emu` の live 応答(host が相手役として返す形)は列挙に無い** — **[定義 §8 N7](harness-tool-definition.ja.md) で「host からバス転送を返す形」を非目標にした**(SPI / I2C は待たせる手段が無く原理的に不可)。**相手役は probe 内で完結する**ので、**双方向の実時間 stream は発生しない**。→ **一番厳しいケースが最初から無い。**

---

## 2. 分離を強制する軸はどれか

**8 つの性質を挙げたが、グループ分けを強制するのは 4 つだけ。**

| 軸 | 分離を強制するか | 理由 |
|---|:--:|---|
| **落としてよいか** | **強制する** | **lossless を lossy と同じ列に入れると、lossy 側の overflow に巻き込まれる**。`uart_data`(#9)が波形(#14)の裏で捨てられるのは受け入れられない |
| **req/resp か push か** | **強制する** | **push が詰まると応答 latency が伸びる**。`dmi`(#4)は往復が支配なので直撃 |
| **帯域の桁** | **強制する** | **#14 は #4 の 10 万倍の byte/s**。同じ列に入れると小さい方が待たされる |
| **`mtu` を超えるか** | **強制する**(分割が要る) | #18 #19 #23 は `mtu`(256〜1024 B)に収まらない。**分割と再組立が必要** |
| latency 要求 | しない | 優先度で解ける(列を分けなくてもよい) |
| 発生率 | しない | 稀なものは何処に置いても害が小さい |
| 方向 | しない | 既に `type` で分かれている |
| 相対順序 | **逆に統合を要求する** | **同じ列に入れれば到着順が保たれる**。分けると順序が失われる(→ §3 の trade-off の核) |

→ **「落としてよいか」が最も本質的**で、次が **req/resp か push か**。

---

## 3. グループ化の案

### 案 A — **全部 1 本、到着順**

```
[control req] [resp] [uart_data] [波形] [resp] [波形] [log] [波形] ...
```

| | |
|---|---|
| **得** | **どの物理 IF でも成立する**(UART / CDC / HID の単線でも)。**相対順序がタダで保たれる**。**実装が最小**(現行 L1/L2 のまま) |
| **損** | **波形が control を食う**(帯域の桁が違う)。**overflow したとき何を捨てるか決められない**。**優先度が付けられない** |
| **成立条件** | **波形を入れないなら成立する**。入れると単線では破綻(→ [channels §5.3](harness-channels.ja.md):`trace` に必要な 0.5〜1 MB/s は HID/UART で足りない) |

⚠ **単線 transport(既存 bridge UART / CDC ×1 / HID のみ)では、案 A 以外を選べない。** → **案 A は「選ぶ案」ではなく「最小構成での必然」**。

### 案 B — **2 分: req/resp と push**(**現行仕様が既にこれ**)

```
req/resp 列: type=0x00 / 0x01, tag で対応付け
push 列    : type=0x02, tag=0x00
```

| | |
|---|---|
| **得** | **既に実装されている**。**push が req/resp の tag を汚さない**。**`dropped` で lossy を列の内側で扱う**(#9 が既にそうしている) |
| **損** | **push 列の中で #14(lossy・巨大)と #9 #16 #24(lossless)が同居する**。**波形の overflow が uart_data を巻き込む** |
| **現行の穴** | **event に連番が無い**(`tag = 0x00` 固定)→ **落ちたことを検出する手段が `uart_data.dropped` だけ**。**波形には連番が必要** |
| **現行の穴 2** | **「線の critical section 中は event を送らずキューに積む」**([§7](../protocols/dmi-bridge.ja.md))→ **1 MB/s の波形はキューを即溢れさせる**。**`caps.event_queue` は u16 = 最大 64 KB で 0.06 秒分** |

### 案 C — **3 分: control / lossless push / lossy push**

```
control     : req/resp(#1-8, 13, 18-20, 22-23)
lossless push: #9 #10 #12 #16 #24  ← 一切落とさない。連番つき
lossy push   : #14 #15 (#17?)      ← 落としてよい。連番 + 欠落数つき
```

| | |
|---|---|
| **得** | **overflow 時に lossy だけを捨てられる**(**最も本質的な軸で切っている**)。**背圧の設計が分かれる**(lossless は止める / lossy は間引く) |
| **損** | **列が 3 本要る**。**単線では 3 本を多重化するので、結局 1 本の帯域を分け合う**(分離の効果が薄い) |
| **効く条件** | **物理 IF が複数あるとき**(Vendor bulk を別 endpoint にする / IP で port を分ける → [channels §6c](harness-channels.ja.md)「IP なら別ラインがほぼ無料」) |

### 案 D — **channel ごと完全分離 + 優先度**

各 stream(波形 / ADC / uart ×n / RTT ×n / 意味イベント …)を独立 channel にし、**優先度と帯域配分を宣言する**。

| | |
|---|---|
| **得** | **最大の柔軟性**。**host が「今は波形を止めて serial だけ」と選べる**。channel ごとに `caps` を名乗れる |
| **損** | **最大の複雑さ**。**scheduler が probe 側に要る**(小さい MCU で重い)。**相対順序が完全に失われる** |
| **効く条件** | **capture を作り、かつ同時に多数の stream を回す**とき |

---

## 4. 案の比較

| | A(1 本) | B(2 分・現行) | C(3 分) | D(channel 別) |
|---|:--:|:--:|:--:|:--:|
| **単線 transport で成立** | **○** | ○ | △(多重化するだけ) | △ |
| **波形を載せられる** | ✗(単線では) | △(**キューが溢れる**) | **○** | **○** |
| **lossless を守れる** | ✗ | △(**同居する**) | **○** | **○** |
| **相対順序が保たれる** | **○** | ○(列内) | △ | ✗ |
| **probe 側の実装量** | **最小** | **小**(**既にある**) | 中 | **大** |
| **host 側の実装量** | 最小 | 小 | 中 | 大 |
| **`dmi` の latency を守れる** | ✗ | ○ | **○** | ○ |

→ **A と B は「今できること」、C と D は「capture を作るなら要ること」。**

---

## 5. 何で決まるか

| # | 判断 | 依存先 |
|---|---|---|
| **1** | **capture(#14)を作るか** | **[定義 §9b](harness-tool-definition.ja.md) 残る論点 1**。**作らないなら現行の案 B で足りる** |
| **2** | 意味イベント(#16)を持つか | 残る論点 3(`emu` をどこまで) |
| **3** | 時刻を付けるか | **残る論点 2**(本書では扱わない) |
| **4** | 物理 IF が複数あるか | [channels §6h](harness-channels.ja.md) の推奨構成(`HID + Vendor + CDC`)なら **Vendor に別 endpoint を持てる** |

→ **判断 1 が決まらないと、案 C / D を評価する意味が無い。**

---

## 6. 現行仕様に足りないもの(**capture を作る場合**)

| 穴 | 中身 | 直し方の候補 |
|---|---|---|
| **event に連番が無い** | `tag = 0x00` 固定なので**欠落を検出できない**(`uart_data.dropped` だけが例外) | **event 共通に `u16 seq`**、または **event 種別ごとに独立連番** |
| **event キューが波形に対して小さい** | `caps.event_queue` = u16(最大 64 KB)= **1 MB/s で 0.06 秒** | **lossy 用に別の扱い**(リングで上書き + 欠落数)/ **別 endpoint** |
| **critical section 中は event を積む規約** | 線操作中に停止 → **1 MB/s では即溢れる** | **lossy stream をこの規約から外す** |
| **`mtu` 超えの転送手段が無い** | #18 #19 #23 が `mtu`(256〜1024 B)に収まらない | **分割 cmd**(offset + 断片)/ `info` の `u16 offset` 方式が既に前例 |
| **背圧 / overflow の通知が無い** | #24 が存在しない | **event を 1 種追加** |
| **`max_inflight` = 1** | **MB 級の `emu` preload 中は `dmi` が完全に止まる** | **`max_inflight` を上げる** / **preload を別扱いに** |

⚠ **いずれも「capture を作る」と決めてから直す項目**。**作らないなら現行のままで穴ではない。**

---

## 7. 参照

- 物理 IF(probe ↔ PC): [harness-channels.ja.md](harness-channels.ja.md)
- 仕様本体: [../protocols/dmi-bridge.ja.md](../protocols/dmi-bridge.ja.md)(§3 L2 / §4 L3 / §4.4 event / §5 caps)
- 論点の棚卸し: [harness-tool-definition.ja.md](harness-tool-definition.ja.md) §9b
- 書込経路を byte 同一に保つ制約: [probe-pattern-coexistence.ja.md](probe-pattern-coexistence.ja.md) R3
