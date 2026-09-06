# harness 配線データ(生成物)

状態: **生成データ**。一次データは [`ch32-device-data`](https://github.com/openwch/ch32-device-data) の **`index/`**。ここにあるのは抽出と突き合わせの結果で、**新しい事実は足していない**。

[../../dut-harness-design.ja.md](../../dut-harness-design.ja.md) §8 の根拠。

## 生成の仕方

```sh
CH32_DEVICE_DATA=../../../../ch32-device-data python3 extract.py
```

**手書きしない。** ピン表は必ず腐るので、一次データが更新されたら再生成する。

## なぜ `index/` を引くのか(初版の誤り)

`ch32-device-data` は 3 層に分かれている(`docs/data-layout.ja.md`):

| 層 | 位置づけ |
|---|---|
| `evidence/` | **資料の綴りのまま** |
| **`index/`** | **利用者が引くための表**。`tools/build_index.py` が evidence から組み直し、`tools/check_tables.py` が「索引の行は証拠に戻せる」ことを毎回検証する |
| `catalog/` | 名前 |

**初版は `evidence/` を読んでいた。** その結果:

| | 初版(`evidence/`) | 現在(`index/`) |
|---|---|---|
| debug 線 | `debug_wiring.csv` 26 series。`wire_modes` を**自前で導出** | `debug_interfaces.csv` **27 series**。**`debug_if` が最初から入っている**(導出不要)。章・ページつき |
| route | `remap_routes.csv` 4,836 行 / **全行 `reference`** / series 単位 / **7 series に穴** | `pinout.csv` 24,982 行 / **24,828 行が `confirmed`** / **型番単位 103 部品** / **穴なし** |
| 抽出できた route | 1,655 組 / 20 series | **5,243 組 / 27 series** |
| debug 絡みの衝突 | 30 組 | **47 組** |

**`index/` は「引くための層」だと README に明記されている。** 読み間違えた。

## 出力

| ファイル | 行数 | 内容 |
|---|---:|---|
| `debug_pins.csv` | 27 | series × `debug_if`(1-wire / 2-wire / **both**)+ SWDIO/SWCLK pad + 出典の章 |
| `routes.csv` | 5,243 | series × pad × 役割 × route。**class**(uart/spi/i2c/pioc/clock/analog/timer)で分類。`parts_with_pad` / `parts_in_series` で**小パッケージで欠ける pad が見える** |
| `pin_conflicts.csv` | 1,118 | **同一 pad に 2 つ以上の役割**。`involves_debug` で debug 絡み(47)を分離 |
| `coverage.csv` | 27 | series ごとに class 別の網羅状況 |

**衝突の定義**: 同一 pad に**異なる役割**が来ることを衝突とする。同じ役割が複数 route で同じ pad に出るのは衝突ではない(単に選択肢)。**`class` に `timer` / `analog` を含めた**ので、`analogWrite` の PWM や ADC タップまで衝突判定に入る。

## 分かったこと

1. **穴は無い。27 series すべてに uart/spi/i2c の route がある。** 初版が「V003 / V205 / X035 / X033 / X315 / X305 / H41x に route が無い」としたのは **`evidence/` を読んだ副作用**で、実際には全部ある。**ch32-device-data へのデータ依頼は不要になった。**
2. **1/2 線両対応が 15 series で `confirmed`**、**1 線のみは V003 だけ**。→ [../../../protocols/link-to-target.ja.md](../../../protocols/link-to-target.ja.md) §1 の列挙が不完全(V205 / V407 / V467 / X305 / X315 / H41x が漏れ)。**反映が要る**。
3. **X033 / X035 の PC18/PC19 が最悪の pad**。`PIOC.IO0/IO1` = `DEBUG.SWDIO/SWCLK` = `I2C1.SCL/SDA` が同じ 2 本に乗り、PC19 は **9 役**。→ PIOC を phy に使うと chip 自身の debug port と I2C1 が同時に潰れる。
4. **2 線系でも安全とは限らない**。X315 は `SPI1.SCK/MOSI` が PA13/PA14 = debug に乗り、V205 は `I2C1.SDA/SCL` が同じ場所。
5. **EVT の `@Note` を一次情報にしてはいけない**(§8.3 で実証)。初版の 4 主張のうち **2 件が誤り**で、どちらも**他 series からのコピペ**に見える(M030 に書かれていた I2C 配置は V003 のもの)。

## 未確認

- `debug_interfaces.csv` の `debug_if` が `both` の series で、**1 線モードに入る手順**(option byte / レジスタ)は別データ。→ [../../../protocols/custom-bootloader.ja.md](../../../protocols/custom-bootloader.ja.md) §2a の切替レジスタと突き合わせが要る。
- **MCO** は `pinout.csv` に `RCC` / `MCO` として入っている(`class = clock`)。基準クロック出力として使う案は [../../dut-harness-design.ja.md](../../dut-harness-design.ja.md) §9 の未決に登録。
