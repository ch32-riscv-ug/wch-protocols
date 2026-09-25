# E163 WCH-LinkE の SetSpeed の値と、線上の SWCLK の実際の速さ

状態: **完了(2026-09-25)**

## 問い

WCH-LinkE の SetSpeed(`81 0c 02 <family> <speed>`)の high `01` / medium `02` / low `03` は、線上でそれぞれ何 Hz になるのか。

- ch32rv はこの 3 段を名目で 6 MHz / 4 MHz / 400 kHz としているが、2026-09-25 fixture では high と medium の区別がつかなかった。
- 01〜03 以外の値は何になるか。
- 接続の後に SetSpeed を送り直すと効くか。

## 手順

- 機材: WCH-LinkE fw 2.22 `0E028F0692F1` → CH32L103C8T6。線は ESP32-P4(`esp32-series-30eda0e343c6`)の OEP logic capture で、50 MHz、LinkE 側で分岐(2026-09-25 fixture と同じ配線)。
- 収録の script は [`linke_cap.py`](linke_cap.py)。fixture の tools の写しで、OEP client を `OEP_CLIENT_SRC` で指定できるようにした。今回は `oep-client-python` の `b9e60ed` を `git archive` で取り出して使った。
- (a) 生のコマンド: [`raw_speed.py`](raw_speed.py) を ch32rv の代わりに走らせる(`CH32RV=raw_speed.py`)。
  - 流れは SetSpeed(family `01`、値 `00`〜`06` / `ff`)→ AttachChip →(任意で、実の family `0x0e` での SetSpeed)→ DmiOp で DMSTATUS の読出し 20 回 → DATA0 の書込み 4 回 → abstract memory read 4 回 → DetachChip。
- (b) ch32rv 0.10.1 の `--speed high|medium|low flash pattern-4k.bin`。USB の往復は `out/flash_*.ndjson`。
- 解析: [`analyze.py`](analyze.py)(a)と、[`captures/tools/rvswd.py`](../../captures/tools/rvswd.py) の START/STOP 区切り。

```console
OEP_CLIENT_SRC=<oep-client b9e60ed>/src CH32RV=$PWD/raw_speed.py uv run --no-project --with numpy --with pyserial --with pyusb \
  python linke_cap.py 30eda0e343c6-hs 50000000 out/pre01 256 0E028F0692F1 14,15 SWCLK,SWDIO l103 -- 01 -
cd ../../captures && uv run python ../experiments/e163_linke_speed_codes/analyze.py ../experiments/e163_linke_speed_codes/out
```

## 結果

**(a) 接続後の単発の DMI**(DATA0 の書込みと memory read の frame の SWCLK 周期の中央値)

| SetSpeed の値 | 周期 |
|---|---|
| `00` / `01` / `02` / `04` / `05` / `06` | 約 1.12 µs(約 0.89 MHz) |
| `03` / `ff` | 約 2.12 µs(約 0.47 MHz) |

- DmiOp による DMSTATUS の単発の読出しは、どの値でも約 2.1 µs だった。
- 接続の後に実の family(L103 = `0x0e`)で SetSpeed を送り直すと、それが効く。`03` → `0e:01` で 1.12 µs になり、`01` → `0e:03` で 2.12 µs になった。
- すべての値で、応答は `82 0c 01 01` だった(範囲外の値も拒否されない)。

**(b) ch32rv の flash(pattern-4k、Program 経路)**

| `--speed` | USB の SetSpeed | 4 KiB のデータ書込み frame | 4 KiB を書く区間 | ch32rv 全体 |
|---|---|---|---|---|
| high | `81 0c 02 01 01` | **約 400 ns(2.5 MHz)** | 102.8 ms | 0.48 s |
| medium | `81 0c 02 01 02` | 約 1.12 µs(0.89 MHz) | 248.0 ms | 0.63 s |
| low | `81 0c 02 01 03` | 約 2.12 µs(0.47 MHz) | 389.5 ms | 0.87 s |

- どれも 4 KiB pattern と一致し、parity の誤りは 0。
- ch32rv は SetSpeed を接続前に family `01` で 1 回送るだけで、接続後には送り直していない。

## 結論

- LinkE 2.22 + L103 の実際の SWCLK:
  - **high: flash の Program 経路だけ 2.5 MHz、それ以外(単発の DMI・読出し・burst)は 0.89 MHz。**
  - **medium: どこでも 0.89 MHz。**
  - **low: どこでも 0.47 MHz。**
  - 接続時の区間は設定に関係なく、long 3.04 µs・short 2.1 µs。
- 名目の 6 MHz / 4 MHz / 400 kHz には、どれも合わない。high と medium で差が出るのは flash 書込みの速さだけ(この試験で 1.3 倍)。
- SetSpeed の値は、実質 `03`(と `ff`)とそれ以外の 2 種類に分かれ、それ以外の中で high だけが Program 経路を速くする。`00`・`04`〜`06` は、単発の DMI では high と同じだった(flash では試していない)。

## 未決

- V203 は高速区間の一部が 60〜100 ns 周期だった(fixture)。target によって LinkE の速さが違うか。
- `00`・`04`〜`06` の flash 経路での速さ。
- 速度の上限と target の HCLK の関係(E162 の X035 の件)。
