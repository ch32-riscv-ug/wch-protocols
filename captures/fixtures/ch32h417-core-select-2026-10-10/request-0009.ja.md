# CH32H417 / LinkE コア選択の実測とプロトコル収載依頼

- 依頼先: `wch-protocols`
- 状態: draft（引き渡し用資料作成済み）
- 優先度: 高（ch32rv の H41x `--core` 対応の根拠）
- 実測日: 2026-10-10

## 結果

CH32H417QEU6 に WCH-LinkE を SWIO 接続し、**同一 attach セッション内で標準 RISC-V DMCONTROL の hartsel を書くことで、コア 0 / 1 の CSR 読み出しを切り替えられた**。独立した 2 セッションで、それぞれ `1 → 0 → 1 → 0` の選択を実施し、選択値の readback とコア固有の識別値を確認した。

| 観測 | hartsel 0 | hartsel 1 |
|---|---|---|
| DMCONTROL (`DMI 0x10`) | `0x00000001` | `0x00010001` |
| DMSTATUS (`DMI 0x11`) | session 1: `0x00400382`、session 2: `0x004c0382` | `0x004c0382` |
| HARTINFO (`DMI 0x12`) | `0x00212340` | `0x00212340` |
| mhartid (`CSR 0xf14`) | `0x00000000` | `0x00000001` |
| marchid (`CSR 0xf12`) | `0xdc68d866` | `0xdc68d8ae` |
| mimpid (`CSR 0xf13`) | `0xdc688002` | `0xdc688001` |
| misa (`CSR 0x301`) | `0x40901127` | `0x40901127` |
| dpc (`CSR 0x7b1`) | `0x201003b0` | `0x00000000` |

両コアとも、選択直後の DMSTATUS で anyhalted / allhalted が立っていた。コア 1 の PC が 0 である理由（未起動、既存 firmware の状態など）は未確認。CSR 読み出し成功だけで実行・リセット制御全体を verified としない。

DMSTATUS の上位状態 bit はセッション間で変化しており、全 word をコア識別値として扱わない。今回 ackhavereset は発行していない。コア識別の再現性は mhartid / marchid / mimpid と DMCONTROL の readback で確認した。

## 接続と手順

- 対象: CH32H417QEU6、chip ID `0x4170053d`、family byte `0xc6`。
- Probe: WCH-LinkE、serial `49808F06CE30`、USB `1a86:8010`、firmware `2.22`（raw `0216`）。
- 配線: 同じ LinkE の SWIO → PB9。speed low。
- 既存 CLI は `--core 1` を拒否するため、既存 ch32rv の USB / WchLink / DMI crate を使う測定プログラムで、接続を保持して実測した。

1. DeviceLock を取得し、DetachChip、ProbeInfo、SetSpeed、AttachChip を実施。対象 ID を照合。
2. コア 0 を halt し、基準値を読み出す。
3. DMI `0x10` へ `dmactive | (hart << 16)` を書き、DMCONTROL / DMSTATUS を読む。abstract command 完了後に選択を変更する。
4. 同じセッションで CSR `0xf14` / `0xf12` / `0xf13` / `0x301` / `0x7b1` を abstract register access で読む。コア選択を 1 / 0 / 1 / 0 と繰り返す。
5. DMCONTROL を `0x00000001` に戻し、DetachChip。再接続した独立セッションでも同じ手順を実施。

USB 上の選択要求は `81 08 06 10 00 01 00 01 02`（hartsel 1）、`81 08 06 10 00 00 00 01 02`（hartsel 0）。DMI read 要求は `81 08 06 10 00 00 00 00 01`。応答と status は添付 capture を参照。

hartsel の bit 配置は [RISC-V Debug Module の一次仕様](https://docs.riscv.org/reference/debug/debug_module.html)に従う。全 hartsel 幅の探索はしていないため、実測の保証範囲は index 0 / 1 のみ。

## 添付資料

- [構造化した結果](ch32h417-core-select-2026-10-10.json)
- [セッション 1 の出力](ch32h417-core-select-run-1-2026-10-10.txt) / [USB capture](ch32h417-core-select-run-1-2026-10-10.ndjson)
- [セッション 2 の出力](ch32h417-core-select-run-2-2026-10-10.txt) / [USB capture](ch32h417-core-select-run-2-2026-10-10.ndjson)
- [測定プログラム](ch32h417-core-select-2026-10-10.rs)

測定プログラムは `ch32rv-wchlink` / `ch32rv-usb` / `ch32rv-dmi` を path dependencies にした独立 Rust package（edition 2024）で実行できる。元の package、Cargo.lock、出力は `target/hil-h417-core-select-2026-10-10/` に保存した。恒久保存された添付資料を優先する。

## 収載してほしい内容と残る確認

`protocols/riscv-debug-module.ja.md` / `protocols/pc-to-link.ja.md` 等の適切な場所に、この条件での **hartsel 0 / 1 の選択と CSR 読み出し**を実測根拠付きで収載してほしい。置き場所・表形式・台帳番号は先方に委ねる。受け入れ条件は、添付 USB 列と結果表の一致、および verified の対象操作・probe firmware・chip の限定が明記されること。

残る調査は、選択したコアの halt / resume / step / reset、DetachChip・再 attach 時の両コア状態、選択の保持、他 firmware / 他 H41x への適用、必要なら物理 SWIO capture。今回、コア 1 は最初から halted だったため、コア 1 宛 haltreq の効果も未実測。

ch32rv の実装では CLI の拒否を外すだけでは足りない。現行 DebugModule の halt / resume / reset / ack は DMCONTROL をコア 0 の固定値で書くため、選択した hartsel を全ての操作で保持する改修が必要。今回 CLI の対応状況は変更していない。

測定コードによる Flash・option byte・一般メモリの書き込み、erase、reset は実施していない。abstract CSR 読み出しに必要な DM の作業レジスタと DMCONTROL は書いている。LinkE 自身の attach / detach の内部副作用は、この USB capture だけでは保証しない。
