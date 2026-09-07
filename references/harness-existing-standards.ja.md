# 既存標準で足りるものの調査 — ch32rv が層を吸収できる候補

状態: **調査**(公開資料と各 repo の文書から。**実装検証は未**)。**結論を出さない。**
基準日: 2026-09-07

**問い**: 「既存として標準的な機能があって ch32rv で層を吸収できる場合には、無理に手を広げない」— その候補はどれか。

上位: [harness-tool-definition.ja.md](harness-tool-definition.ja.md)(何の道具か)/ [harness-choices.ja.md](harness-choices.ja.md)(各論の選択肢)。所在は [harness-index.ja.md](harness-index.ja.md)。

## 0. 結論(先に)

1. **「既存標準を吸収する」は ch32rv の既定方針と、すでに一致している。** `docs/cli.ja.md` の CLI tree は **`gdb`(P1)/ `dap`(P2)/ `isp`(P2)/ `boot`(P2)/ `monitor --source uart|sdi|dmdata|rtt` / `run`(semihosting)/ `uf2 flash` / `probe firmware`(IAP)** を全部載せていて、**`boot` route は `dfu-util` / `UF2 copy` / `tinyboot` / `rv003usb` を名指ししている**。→ **我々が新たに提案できることは少ない。**
2. **手を広げなくてよいものは §1 のとおり多い。** UF2・DFU・HID BL・factory ISP・GDB server・DAP server・RTT・semihosting・sigrok の出力形式は**全部既存標準で、ch32rv 側か既存ツール側にある**。
3. **ch32rv の計画に無い吸収候補が 3 つある**(§2): **GDB RSP を client 側で喋る** / **Black Magic の RISC-V remote protocol** / **WCH IAP**。
   - **2-b は source で確認できた。DMI 粒度(`addr8` + `data32`)で、host 側と probe 側の両方に実装がある。** `remote_crc32` まである。→ **ch32rv が host 側を実装すれば 2 線の DMI ブリッジが今日手に入る**(chip 知識は ch32rv 側に残せる)。**ただし業界標準ではなく Swindle 固有の拡張。**
4. **どう吸収しても埋まらないものは 4 つ + 1**(§3): `caps` / 時刻 / event / lane、そして **2 線の被覆**。
5. **被覆の穴も確定した(§2-b′)**: **既存で届くのは 16 series、主張なしが 10、非対応と明記が 1(V103)。** → **firmware を書く理由は「10〜11 series の穴」+「§3 の 4 つ」に絞られた。**

## 1. すでに ch32rv の計画に入っている(= 我々は手を出さない)

| 機構 | 標準性 | ch32rv での位置 | 我々の作業 |
|---|---|---|---|
| **UF2** | Microsoft / Adafruit 由来の事実上の標準 | **`uf2 flash`**(volume 検出 → 変換 → copy → 完了監視) | **無し** |
| **DFU** | USB-IF 標準 | `boot` route(`dfu-util`) | 無し |
| **HID scratchpad BL**(B003) | 事実上の標準(host 実装 3 つ) | `boot` route(`rv003usb`) | **無し**(byte 仕様は [custom-bootloader §2b](../protocols/custom-bootloader.ja.md) に既にある) |
| **tinyboot** | project 固有 | `boot` route | 無し |
| **factory ISP** | WCH 仕様 | **`isp` route**(P2) | 無し(仕様は [pc-to-device-isp](../protocols/pc-to-device-isp.ja.md)) |
| **GDB server** | GDB RSP = 標準 | **`gdb`**(P1) | 無し |
| **DAP server** | DAP = 標準(VS Code 等) | `dap`(P2) | 無し |
| **RTT** | SEGGER 由来の事実上の標準 | `monitor --source rtt`(P2) | 無し |
| **semihosting** | RISC-V / ARM 標準 | `run` で exit code 伝搬 | 無し |
| **SDI / DMDATA print** | WCH 独自だが仕様は解読済み | `monitor --source sdi|dmdata`(実装済み) | 無し |
| **advisory lock / exit code / capture-replay** | ch32rv 独自だが**既に contract 化** | `ch32rv-contract`、`--capture` / `--replay` | **踏襲する**(2 つ目の方言を作らない) |
| **CDC / HID class driver、WinUSB 自動 bind** | USB-IF 標準 | — | **無し**(使うだけ) |
| **WebUSB / WebHID / WebSerial** | W3C / Chromium | — | **無し**(ブラウザ経路は作らない) |
| **sigrok / `.sr` / VCD** | 事実上の標準 | — | **無し**(出力形式として使う) |
| **arduino-cli / pytest-embedded** | 既存 | — | 無し |

→ **「書く・読む・monitor・debug・BL 経路」の**大半は既存標準 × ch32rv で埋まる設計になっている。

## 2. ch32rv の計画に無い吸収候補(**調査対象**)

### 2-a. GDB RSP を **client 側**で喋る

| | |
|---|---|
| 何ができる | **probe 上に GDB server を持つ probe が全部使えるようになる** — **Swindle**(RP2040 / **2 線** / V20x・V30x / stable)、**PicoRVD**(1 線 / V003) |
| 標準性 | **GDB Remote Serial Protocol は最も枯れた標準**。実装資料も豊富 |
| **効く穴** | **R-b(2 線を LinkE 無しで書く)に直接効く。** Swindle は「2 線 stable」の唯一の実装 |
| 限界 | **flash は `load` 経由**なので粒度が粗く遅い。**option byte / recover / power 制御 / 機械可読な probe 列挙は GDB に無い**([probe-ecosystem](probe-ecosystem.ja.md) §2 が既に指摘)。chip 知識が **probe 側**にあるので新 chip 対応は probe firmware 待ち |
| 未確認 | ch32rv が GDB client を持つ設計上の妥当性(いま ch32rv は **GDB server 側**を P1 に持っている。client を足すのは方向が逆) |

### 2-b. Black Magic の **RISC-V remote protocol** — **✅ 確認済み(2026-09-07)**

**source を読んで確定した。DMI 粒度で、両端に実装がある。**

#### host 側(BMDA / hosted)

`blackmagic_addon/hosted/remote_rv_protocol.c` が **Black Magic の汎用 RISC-V 層に DMI アクセサを配線している**:

```c
dmi->designer_code = JEP106_MANUFACTURER_WCH;
dmi->version       = RISCV_DEBUG_0_13;   /* "Assumption, unverified" とコメント */
dmi->address_width = 8U;
dmi->read  = remote_ch32_riscv_dmi_read;   /* (dmi, address, *value) */
dmi->write = remote_ch32_riscv_dmi_write;  /* (dmi, address, value)  */
riscv_dmi_init(dmi);
```

`bmda_rv_dm_probe()` / `bmda_rvswd_scan2()` もあり、**chip 知識は PC 側の target 層が持つ**。

#### probe 側(firmware に responder が**ある**)

`swindle/rs/rs_swindle/src/native/rpc_target/mod.rs` の `rpc_rv_packet()`:

| cmd | 値 | probe 側の処理 |
|---|:--:|---|
| `RPC_RV_RESET` | **`'S'`** | `bmp::rv_dm_start()` |
| `RPC_RV_DM_READ` | **`'r'`** | `bmp::bmp_rv_read(address as u8)` |
| `RPC_RV_DM_WRITE` | **`'w'`** | `bmp::bmp_rv_write(address as u8, value)` |

#### wire format(ASCII)

```
host → probe:  'A'  'B'  <cmd>  <hex params…>  '#'  <checksum 2 char>
                ^     ^
        RPC_START  RPC_RV_PACKET(packet class)
```

- address = **2 hex 文字(u8)**、value = **8 hex 文字 LE**(`add_u8_hex` / `add_u32_le`)
- 応答は **status + u32 LE hex**(長さ 8 を検査、値は `reply[1..]` から)。**正確な byte 数は要確認**
- **address が `u8`** なのは [link-to-target §3](../protocols/link-to-target.ja.md) の「7 bit addr」と整合(8 bit 幅で上位未使用)。**この repo の解読を第三者実装が裏づけている**

#### 付随して見つかった primitive

| RPC | 意味 |
|---|---|
| **`remote_crc32(address, length, *crc)`** | **target メモリの CRC32**。**verify 加速の primitive**([generic-probe-design §6](generic-probe-design.ja.md) の「read/verify も stub で」の実例) |
| `remote_bmp_set_frequency` / `get_frequency` | 線の速度 |
| `bmp_gpio_reset_c` | NRST |

#### 含意

1. **dmibridge が定義しようとしている抽象と同じものが、両端の実装つきで既に存在する。** `(addr8, data32)` + reset + CRC32 + frequency + NRST。
2. **ch32rv が host 側を実装すれば、2 線の DMI ブリッジが今日手に入る。** **chip 知識は ch32rv 側に残せる**(protocol が DMI 粒度だから)。2-a(GDB 経由)の弱点が無い。
3. **⚠ ただし「標準」ではない。** repo に `patches_1.10/37blackmagic_hack_remote_protocolv3.patch` があり、**上流 BMP remote protocol v3 に RISC-V packet class を足した Swindle 固有の拡張**と読める。**上流 blackmagic に同等があるかは未確認**(`--depth 1` clone で submodule 未取得)。→ **「実装が 2 つある de facto の口」であって、業界標準ではない。**
4. **被覆は Swindle の対象に限られる**(§2-b′)。

### 2-b′. 被覆の穴 — **✅ 確認済み。10 series(+ V103)**

既存 2 線実装の主張を `debug_pins.csv`(27 series)に突き合わせた。rvswdio の README は明示的に:

> "program, semihost and run basic GDB on a variety of WCH chips including the **CH32V003, 00x, 20x, 30x, x03x, CH57x, CH585, CH59x**."
> "**Tested and not supported: CH32V103**, CH582/3."

| 判定 | series |
|---|---|
| **既存で届く(16)** | V002 / V003 / V004 / V005 / V006 / V007 / M007 / **V203 / V205 / V208 / V303 / V305 / V307 / V317** / X033 / X035 |
| **主張なし(10)** | **H415 / H416 / H417 / L103 / M030 / M103 / V407 / V467 / X305 / X315** |
| **非対応と明記(1)** | **V103**(rvswdio が「テスト済み・非対応」) |

- **Swindle が届くのは 5 series のみ**(V203 / V208 / V303 / V305 / V307)。残りは rvswdio(experimental)が担っている。
- **穴のうち実用上重いのは L103(低消費電力)/ V407・V467(高機能)/ X315(USB)/ H41x(新しい)**。
- **V103 は「非対応」と明記されている唯一の series** — 2 線だが既存 2 実装のどちらも届かない。

→ **R-b の正確な形**: 「2 線が無い」ではなく **「2 線は V2xx / V3xx / X03x / V00x に限られ、10〜11 series が空いている」**。

### 2-c. WCH IAP(app 内 bootloader)

| | |
|---|---|
| 何ができる | **USB を持つ chip を probe 無しで更新**。EVT の IAP sample が 12 シリーズにある |
| 標準性 | WCH 独自だが **[wch-iap.ja.md](../protocols/wch-iap.ja.md) が「attested・実装可」**(3 世代・12 シリーズの配置と byte を転記済み) |
| **ch32rv の位置** | **`boot` route の名指しリストに入っていない**(`dfu-util` / `UF2 copy` / `tinyboot` / `rv003usb` のみ)。→ **候補** |
| 限界 | app が協力する前提(entry 機構が要る)。app が壊れると入れない |

### 2-d. 既存の RP2040 ロジアナ firmware + sigrok(**ch32rv ではなく「作らない」候補**)

| | |
|---|---|
| 何ができる | **L5(単体ロジアナ)を自作しなくて済む可能性**。RP2040 向けの LA firmware と sigrok driver は既にある |
| 効く点 | **我々が作るべきは「同じ装置が debug 線も持つ」統合部分だけ**(§3 の S1)になる。**L5 単体は既存品の領域**([定義 §3](harness-tool-definition.ja.md) が「波形が見えるのは強みでない」と書いたのと整合) |
| 未確認 | 既存 firmware が **PIO を何 SM 使うか**、我々の phy / エミュと同居できるか(命令メモリの競合。[harness-board-survey §4.1](harness-board-survey.ja.md)) |

### 2-e. probe-rs

| | |
|---|---|
| 何ができる | probe-rs は WCH-Link backend を持ち、CH32 を部分サポート。**Rust crate なので ch32rv から使える** |
| 未確認 | ch32rv は `nusb` 直で書き、**依存を意図的に絞っている**(`architecture.ja.md`)。**取り込むかは設計判断** |
| 位置づけ | 「吸収」ではなく「依存」の話。**ch32rv 側の判断で、我々の幅とは独立** |

## 3. どう吸収しても埋まらないもの

| # | 埋まらないもの | なぜ |
|---|---|---|
| **1** | **`caps`(能力の申告)** | 既存の口(GDB / BMP remote / ardulink / HID)に**能力申告が無い**。host が型番の手書き表を持つしかない。**情報の壁(P0f/P0g)の本体** |
| **2** | **時刻** | どの既存 protocol にもタイムスタンプが無い。**後付けできない** → L5〜L8 が構造的に不可 |
| **3** | **event(probe → host の自発送信)** | 既存はすべて request/response(GDB の async stop は例外だが用途が違う) |
| **4** | **lane(複数 target)** | どれも 1 target 前提 |
| **+** | **2 線の被覆** | **これは標準の有無ではなく実装の穴。** Swindle を吸収しても **V20x/V30x まで**。**X03x / L103 / V103 / M030 / H41x はどの既存実装も主張していない**([定義 §5.5](harness-tool-definition.ja.md)) |

→ **§3 の 1〜4 は「protocol を作る理由」、`+` は「firmware を書く理由」。** 性質が違う。

## 4. 次に確かめること

| # | 調べること | 効く判断 | 手間 |
|---|---|---|---|
| ~~1~~ | ~~Swindle の responder / 粒度~~ | **✅ 完了(§2-b)。responder あり、DMI 粒度** | — |
| 2 | GDB RSP client 経路で **flash がどれくらい遅いか**(`load` の粒度) | 2-a の実用性 | 実測 |
| 3 | Swindle を **RP2040 に焼いて V203/V307 に繋ぎ、ch32rv から何が見えるか** | 2-a / 2-b の実地確認 | 実機 |
| ~~4~~ | ~~既存実装の 2 線被覆~~ | **✅ 完了(§2-b′)。穴は 10 series + V103** | — |
| 5 | ch32rv の `boot` route に **WCH IAP** を足す価値(対象 chip 数 × 需要) | 2-c | 文書のみ |
| 6 | 既存 RP2040 LA firmware の **PIO 資源消費** | 2-d(同居できるか) | source |

**1 と 4 は完了。** 残る最優先は:

- **2-b の RPC を ch32rv に実装したときの実速度**(項目 2 の変形)。`remote_crc32` がある分、verify は速い見込み。
- **上流 blackmagic に RISC-V remote があるか**(あれば「Swindle 固有」ではなくなり、標準性の評価が変わる)。
- **穴の 10 series で線が本当に通るか**(2 線の bit フレームは全 series 共通なのか。[link-to-target §3](../protocols/link-to-target.ja.md) の仕様が V103 で通らない理由は何か)。**V103 が「非対応と明記」なのは、線の仕様に series 差がある可能性を示している** — これは我々の phy 設計に直接効く。

## 5. 参照

- ch32rv の CLI 体系と route: ch32rv `docs/cli.ja.md` / `docs/architecture.ja.md`(`DtmAccess` / `ch32rv-probe-<name>` を P2 で予約)
- 既存 probe の landscape と host protocol 方式: [probe-ecosystem.ja.md](probe-ecosystem.ja.md)
- Swindle の remote protocol への言及: [../protocols/link-to-target.ja.md](../protocols/link-to-target.ja.md) §4
- 吸収できない理由の判定規則: [harness-tool-definition.ja.md](harness-tool-definition.ja.md) §5
- 各仕様: [wch-iap](../protocols/wch-iap.ja.md) / [custom-bootloader](../protocols/custom-bootloader.ja.md) §2b / [pc-to-device-isp](../protocols/pc-to-device-isp.ja.md)
