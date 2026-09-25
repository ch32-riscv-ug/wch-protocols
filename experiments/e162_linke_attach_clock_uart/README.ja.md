# E162 LinkE の接続で target の clock が変わるのを、target 自身の UART と RAM 記録で確かめる

状態: **完了(2026-09-25)**

## 問い

WCH-LinkE の AttachChip は、線上で target の RCC を書き換えている([pc-to-link](../../protocols/pc-to-link.ja.md) §11)。これを、probe を通した読み出しではなく **動いている target のコード自身**から確かめる。

- 接続の前後に、アプリが実際に見る RCC / FLASH_ACTLR の値は何か。
- アプリの UART は壊れるか。

同じ probe で前後を読むと、1 回目の読み出し自体が接続なので、書き換えた後しか見えない。そのため線上の解析だけでは「アプリが何を見るか」は言えなかった。

## 手順

機材は WCH-LinkE fw 2.22 の 3 台で、target の USART1 TX は LinkE の UART RX につながっている(CDC)。配線は変えていない。P4 の capture 機は使っていない。

| target | LinkE | CDC |
|---|---|---|
| CH32L103C8T6 | `0E028F0692F1` | `/dev/ttyACM8` |
| CH32V203C8T6 | `FBC18F0680B0` | `/dev/ttyACM9` |
| CH32V003F4P6 | `F90E8F067DFD` | `/dev/ttyACM3` |
| CH32X035C8T6(UID `1ff9abcd880ebc48`、OEP の P4 治具の X035F8U6 とは別基板) | `FC928F068181` | `/dev/ttyACM6` |
| CH32V307VCT6 | `38EF8F06BDC2` | `/dev/ttyACM4` |

1. [`clockwatch/clockwatch.ino`](clockwatch/clockwatch.ino) を ArduinoCore-CH32(`ch32-riscv-ug:ch32v`、xpack riscv-none-elf-gcc 14.3.0-1、既定の clock)で build し、`ch32rv flash --reset run` で書く。
   - sketch は 100 ms ごとに `CW <loop> CTLR=… CFGR0=… ACTLR=…` を 115200 baud で送る。baud は起動時の clock から決まる。
   - RCC_CTLR / CFGR0 / CFGR2 / FLASH_ACTLR のどれかが変わるたびに、RAM の `cw_log[]`(`0x20000030`〜)に記録する。
   - `-DCW_AHB_DIV2`(`--build-property "compiler.cpp.extra_flags=-DCW_AHB_DIV2"`)を付けると、起動時に HPRE を /2 にして BRR を合わせ直す(X035 用)。
   - 書いた image は `clockwatch/clockwatch-<family>.bin`。
2. [`run_clockwatch.py`](run_clockwatch.py) は、UART を読みながら 1.5 s 後に `ch32rv target info`(reset を伴わない接続)を 1 回実行し、さらに 2 s 読む。最後に `ch32rv read --range 0x2000000c+0x2a4` で RAM の記録を取り出す。
3. [`power_check.py`](power_check.py) で LinkE の 3.3V / 5V 出力を切り、UART が止まるか(= target が LinkE から給電されているか)を見る。出力は最後に必ず on に戻す。

```console
uv run --no-project --with pyserial==3.5 python run_clockwatch.py l103 0E028F0692F1 /dev/ttyACM8 clockwatch/clockwatch-CH32L103.bin out
uv run --no-project --with pyserial==3.5 python power_check.py F90E8F067DFD /dev/ttyACM3 3v3
```

## 結果

| target | 接続前(sketch 自身の値) | `target info` の後に sketch が見た値 | UART | ループの速さ |
|---|---|---|---|---|
| L103 | CTLR `0x00107f83` / CFGR0 `0` / ACTLR `0` | CTLR `0x03107f83`(PLL on)/ CFGR0 `0x001c040a` / ACTLR `0x1` | 接続後は**文字化け** | 約 6.1〜6.5 倍 |
| V203 | CTLR `0x03007083` / CFGR0 `0x0028000a` / ACTLR `0` | CTLR 同じ / CFGR0 `0x0034040a` / ACTLR `0` | 接続後は**文字化け** | 約 1.3〜1.4 倍 |
| V003 | CTLR `0x00004683` / CFGR0 `0` / ACTLR `0` | **変化なし**(記録は起動時の 1 件だけ) | 接続後も正常 | 1.00 倍 |
| V307 | CTLR `0x03008e83` / CFGR0 `0x0028000a` / ACTLR `0` | CTLR 同じ / CFGR0 `0x0038040a` / ACTLR `0` | 接続後は**文字化け** | 約 1.3〜1.4 倍 |
| X035(core の既定 clock) | CTLR `0x00004483` / CFGR0 `0` / ACTLR `0x2` | **変化なし** | 接続後も正常 | 1.00 倍 |
| X035(起動時に HPRE = /2) | CFGR0 `0x80` / ACTLR `0x2` | ―(接続で target が応答しなくなった。下記) | 接続の直後から出力なし | ― |

- 生の値は `results/<target>.json`、UART の生 byte は `results/<target>.uart.bin`。
- ループの速さの倍率は、UART の loop 番号(接続前)と RAM の loop 番号(接続後)から出した。接続の終わりと読み出しの始まりの時刻に幅があるので、範囲で示す。
  - L103 は HSI 8 MHz から PLL へ上がり、flash wait が 1 になったことと合う。
  - V203 は PLLMUL の field が 12 倍から 15 倍の位置へ変わり、APB1 が /2 になった。96 → 120 MHz(1.25 倍)なら概ね合うが、周波数は未測定。
- 線上で見た接続後の値(L103 `0x001c040a` / `0x1`、V203 `0x0034040a`、V003 は RCC に触れない)と一致した。
- V307 では、これまで接続後の読み値からの推定だった書き換えを、アプリ側から確認した。
- X035 は、LinkE が接続中に書く値(CFGR0 `0`、ACTLR `0x2`。09-11 fixture の線上の解析)が core の既定の clock と同じなので、既定のままでは変化が見えない。09-11 の target は CFGR0 `0x50`(AHB /6)で動いていて、線上では 0 に書き換えられていた。
- **LinkE の 3.3V・5V 出力を切っても、L103 / V203 / V003 の UART は止まらなかった。** target は LinkE から給電されていないので、「target 無給電での接続」はこの配線ではできない(見送り)。3.3V と 5V は、試験後にどちらも on にした。試験前の状態は記録していない。

## X035 が応答しなくなった件(復旧済み。原因は推定)

X035 で変化を見えるようにするため、`-DCW_AHB_DIV2` で build した版を焼いた(`clockwatch/clockwatch-CH32X035-ahb-div2.bin`)。起動時に HPRE を /2(HCLK 24 MHz)にし、USART1 の BRR を合わせ直す版である。

1. 書込み・起動は正常だった。UART に `CFGR0=80 ACTLR=2` が出ていた。
2. `ch32rv target info`(high 設定)は 0.06 s で終わり、UID と flash 容量が「-」だった(ESIG が読めていない)。**その直後から UART の出力が止まった。**
3. 以後の状態:
   - high 設定: 「no target detected」。
   - low 設定: 接続できる回と、できない回がある。UID は読めない(`uuid-unavailable`)。
   - medium 設定: UID `ffffffffc3ffffff`、flash 65535 KiB と値が化ける。
4. 試した復旧(LinkE `FC92` から):
   - `reset --speed low`: 失敗。
   - `probe power 3v3 off/on`: 変化なし。
   - `recover --method power-off`(3 回): 毎回「issued」になり、直後の 1 コマンドだけ通る。「issued」は特殊消去のコマンドを送ったというだけの意味である。power-off 消去は LinkE が target の電源を切って入れ直す間に消す方式で、この配線では電源が落ちないので、**何も消えていない可能性が高い**。直後の 1 コマンドだけ通るのは、ch32rv が特殊消去の後に送る RedetectChip + detach で probe の状態が一度きれいになるため、と見られる(ch32rv-c8 の指摘)。その窓の中で次を試した。
     - `recover --method unprotect --speed low`: option byte が読めず、書込みは `cmderr 4`(hart が halt しない)で失敗した。abstract command の段階で失敗しているので、flash には届いていないと見ているが、確認はできていない。**通信が化けている間に option byte を書く操作は、状態を悪化させるおそれがあるので、以後はしない。**
     - `dbg dmi write 0x10 0x3`(ndmreset): 書込みは通ったが、直後の DMSTATUS は `0xfffff9ff` → `0x800001c1`(正常時は `0x00030382` 付近)と化け、以後は応答しない。
   - USB の往復: `results/x035-recover-*.ndjson`。
5. NRST は LinkE につながっておらず、target は LinkE から給電されていないので、LinkE 側からの手は尽きた。基板の電源の入れ直しが要る。それでも戻らなければ、X035 の VCC を LinkE の 3V3 から取る配線にすれば、power-off / unbrick の方式で起動直後の窓を狙える(配線の変更になる)。

6. ユーザーが基板の電源を入れ直した。それでも AttachChip は 20/20 で `81 55 01 01`(target 無し)だった(`attach_loop.py`、`results/x035-attach-fail-after-power-cycle.ndjson`)。
7. **特殊消去(`81 0d 02 0f 0d`)を送った直後の同じ USB session の中では、AttachChip・DMI・ChipInfo が安定して通る**ことが分かった(`raw_seq.py`)。その窓の中で読むと、次のとおりだった。
   - DMSTATUS `0x00000382`(halt 中)、flash の先頭 `ffffffff`(sketch は消えていた)。
   - option byte `f8075aa5 ff00ff00 00ff00ff 00ff00ff`: RDPR `a5`(保護なし)、**USER `0x07`**、DATA0/1 `00`、WRPR0〜3 `ff`。2 回読んで一致し、補数もすべて整合。
   - X035 RM では USER[4:3] RST_MODE の復帰値は `11b`(reset pin 無効)で、`00` は「外部 reset pin を有効にする」。C8T6 では PA21 が reset pin になる。
8. 同じ窓の中で、pc-to-link §6b の手順を raw DMI で実行した([`x035_fix_user.py`](x035_fix_user.py)): KEYR / OBKEYR の解錠 → OPTER で消去 → halfword ごとに OPTPG。USER だけを `0x1f`(RST_MODE `11`)にし、ほかは読んだ値に戻した。
   - 1 回目は、RDPR を書いた直後の abstract command が busy(abstractcs bit 12)を返し、script がそれを異常として止めた。busy は flash の書込み中にバスが待たされているだけで、RDPR は書けていた。
   - 2 回目は、消去せずに、残りの `ffff` の halfword だけを書いた(`--fixed --no-erase`)。
   - 最後に PFIC SYSRST を送った。
9. 以後、high / low の通常の接続で chip ID・UID `1ff9abcd880ebc48`・flash 62 KiB が読め、`target option get` を 2 回読んでも同じ値(raw `a55a1fe000ff00ffff00ff00ff00ff00`)だった。

見立て(推定): LinkE は接続中に X035 の RCC / FLASH を書く。target が HCLK 24 MHz で動いていたため、高速設定の DMI 通信が target にとって速すぎ、書込みが化けた可能性がある。化けた書込みが FLASH_CTLR の option byte 関係の bit に当たっていれば、option byte が壊れている可能性もある(ch32rv-c8 セッションの指摘)。ただし ndmreset の後も、電源を入れ直した後も接続できなかったので、clock だけでは説明できない。USER の RST_MODE を `11` に戻しただけで直ったので、**PA21 が外部 reset として働き、chip がほぼ reset に入ったままだった**と見ている。USER `0x07` が事故の前からの値だったのか、化けた書込みで変わったのかは確かめられない(X035 の option byte は事故の前に読んでいない)。予約 bit 7:5 も 0 なので、書込みで bit が落ちたように見えるが、推定にとどまる。特殊消去の直後だけ接続できた理由も分かっていない。

## 結論

- L103 / V203 / V307 では、reset を伴わない接続(`target info`・`read`・`dbg`・`monitor` など)の後、**走っていたアプリは LinkE が決めた clock で動き続ける**。起動時の clock で baud を決めた UART はその時点で読めなくなる。タイマ・PWM・`millis()` も同じ比率でずれるはず(未測定)。
- V003 では何も変わらない。X035 は、core の既定の clock なら結果として変わらない。
- **既定と違う clock で動く X035 に接続した後、target が応答しなくなった**(1 回だけ観測)。USER の RST_MODE が `00` だったことが直接の原因と見られるが、それが接続で起きたのかは未確定。復旧は「特殊消去の直後の同じ session で option byte を書き直す」方法で行えた。
- これは線上の観察(E162 以前は fixture の解析のみ)を、アプリ側の観測で裏づけたもの。1 回目の接続の前の値を probe を使わずに取れたので、「元に戻さない」も確定した。

## 未決

- 接続中の約 10 ms の HSI の区間は、hart が halt しているのでアプリからは見えない。
- HSE + PREDIV / PLL2 を使うアプリで、CFGR2 = 0 の上書きがどう効くか(V203 の基板に HSE が載っているか未確認)。
- X035 の件: 電源の入れ直しによる復旧、option byte の状態、同じ条件での再現、線上で何が化けたか。
