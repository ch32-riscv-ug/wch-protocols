# harness 配線データ(生成物)

状態: **生成データ**(一次データは [`ch32-device-data`](https://github.com/openwch/ch32-device-data) の `evidence/`。ここにあるのは抽出・突き合わせの結果)。

[../../dut-harness-design.ja.md](../../dut-harness-design.ja.md) §8 の配線・衝突表の**根拠**。初版は EVT サンプルの `@Note` コメントを grep して作っていたが、[ArduinoCore-CH32 の指摘](#経緯)を受けて **`ch32-device-data` の生成物から作り直した**。

## 生成の仕方

```sh
CH32_DEVICE_DATA=../../../../ch32-device-data python3 extract.py
```

既定は `../../../../ch32-device-data`(この repo の隣に clone されている前提)。**手書きしない。** ピン表は必ず腐るので、一次データが更新されたら再生成する。

## 入力(ch32-device-data/evidence/)

| ファイル | 内容 | confidence | 出典 |
|---|---|---|---|
| `debug_wiring.csv` | series ごとの SWDIO/SWCLK pad と 1/2 線両対応の別(26 series) | **`confirmed`** | **WCH-Link User Manual**(zh/en、ページ番号つき) |
| `remap_routes.csv` | selector/value ごとの signal → pad(4,836 行) | `reference` | `candidates(datasheet-pin-table / rm-remap-grid)` |

**証拠の水準が 2 段違う**ことに注意。debug 線は WCH 公式マニュアルからの `confirmed`、周辺の route は datasheet のピン表から導いた `reference`(候補)。

## 出力

| ファイル | 行数 | 内容 |
|---|---:|---|
| `debug_pins.csv` | 26 | series × SWDIO/SWCLK + **`wire_modes` を導出**(`1-wire` / `2-wire` / `1-wire+2-wire`) |
| `bus_routes.csv` | 1,655 | series × USART/UART/SPI/I2C/**PIOC** の全 route(remap value 込み) |
| `pin_conflicts.csv` | 372 | **同一 pad に 2 つ以上の役割**が来る組。`involves_debug` で debug 絡み(30 行)を分離 |
| `coverage.csv` | 27 | series ごとに、どの入力が存在したか(**穴の可視化**) |

**衝突の定義**: 同一 pad に**異なる signal** が来ることを衝突とする。同じ signal が複数の remap value で同じ pad に出るのは衝突ではない(それは単に選択肢)。

## 分かったこと

1. **`wire_modes` を導出できた。1/2 線両対応は 15 series で `confirmed`**: H415/H416/H417、M007、M030、V002/V004/V005/V006/V007、**V205**、**V407**、**V467**、**X305**、**X315**。**1 線のみは CH32V003 だけ**。
   → **[../../../protocols/link-to-target.ja.md](../../../protocols/link-to-target.ja.md) §1 の「1/2 線 切替可」の列挙が不完全**(V205 / V407 / V467 / X305 / X315 / H41x が漏れている)。**反映が要る**。
2. **dual 対応は harness の設計レバーになる。** V005/V006/V007/M007 は 2 線だと **SWCLK = PB3 が SPI_MISO と食い合う**が、**1 線を選べば PB3 が空く**。「線の本数を選べる」ことが「エミュに使える pad を選べる」ことに直結する。
3. **X033 / X035 は PIOC の既定ピンが debug ピンそのもの**(`PC18`/`PC19` = SWDIO/SWCLK)。X035 の remap(`afio-pioc-remap` value 1)でも **IO0 が PC7 に移るだけで IO1 は PC19 = SWCLK のまま**。→ **PIOC を probe の phy に使うと、その chip 自身の debug port が塞がる**([../../harness-board-survey.ja.md](../../harness-board-survey.ja.md) §3.5 の推論がデータで裏付けられた)。X033 には remap 行が無い(**データの穴か、remap 自体が無いのかは未確認**)。
4. **V103 / V203 / V208 は debug pad の衝突が 0 件**。V303/V305/V307/V317 は **USART3 の remap 先だけ**。→ **USART3 を使わなければ harness に最も素直**。
5. **V00x 系がいちばん苦しい**。PD1(SWIO)に **I2C 両線 + USART1 両方向 + USART2_RX** が重なる。
6. **bus route が無い series が 7 つある**: **CH32V003**、CH32V205、CH32X305、CH32X315、CH32H415/H416/H417。→ この 7 つは **EVT 由来のデータが依然として唯一の情報源**(§8b)。特に **V003 と X035 の USART/SPI/I2C** は harness の主要ターゲットなので痛い。→ [request-ch32-device-data](#依頼) 参照。

## 依頼(ch32-device-data へ)

`references/data/bootloader-survey/request-ch32-device-data.ja.md` と同じ形で、次を依頼する候補:

1. **`remap_routes.csv` に USART/SPI/I2C の route を追加してほしい series**: CH32V003、CH32V205、CH32X035、CH32X033、CH32X315、CH32X305、CH32H415/H416/H417。
2. **CH32X033 の `afio-pioc-remap` value 1** の有無(X035 にはあるが X033 には行が無い)。
3. `remap_routes.csv` の confidence を `reference` から上げられるか(RM の remap 表と突き合わせ済みなら `attested` 相当)。

## 経緯

初版(2026-09-06)は EVT サンプルの `@Note` コメント grep + datasheet PDF 抽出で 9 series 分。ArduinoCore-CH32 の `docs/harness-probe.ja.md` §5-8 が「**`ch32-device-data` の生成物から作れ**」と指摘し、確認したところ `debug_wiring.csv` が **26 series・`confirmed`・ページ番号つき出典**で存在した。EVT grep 版より広く強いので置き換えた。

**EVT grep 版で得ていた事実のうち、この生成物に無いもの**(= §8b に残すもの):

- V003 の SPI1 `NSS = PC1` と I2C1 `SDA = PC1` の重複(**V003 は bus route が無い**)
- V307/V407 の **DAC ch0 = PA4 = SPI1_NSS**(DAC は `bus_routes` の対象外)
- X035 の I2C1 `SCL = PA10` と USART1 `RX = PA10` の重複(**X035 は bus route が無い**)
- M030 の USART1 `TX = PC1`(remap)と I2C1 `SDA = PC1` の重複(`bus_routes` の M030 は `UART_*` / `I2C_*` 表記で、インスタンス番号と remap 値の対応が EVT 版と直接比較できない)
