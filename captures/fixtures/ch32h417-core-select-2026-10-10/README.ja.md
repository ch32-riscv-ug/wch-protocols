# CH32H417 / LinkE のコア選択(2026-10-10)

ch32rv の [収載依頼 0009](request-0009.ja.md)から受領した実測資料。今回は既存の測定をオフライン照合して収載し、実機の再測定は行っていない。元ファイルは `ch32rv/docs/data-requests/measured/`、元 package・Cargo.lock は `ch32rv/target/hil-h417-core-select-2026-10-10/`。下の 6 添付は内容を変更せずコピーした。依頼書の添付リンクのみ収載先に合わせた。

| 資料 | 内容 |
|---|---|
| [構造化結果](ch32h417-core-select-2026-10-10.json) | 2 セッション × 5 snapshot の DMI / CSR 値 |
| [run 1 出力](ch32h417-core-select-run-1-2026-10-10.txt) / [USB capture](ch32h417-core-select-run-1-2026-10-10.ndjson) | baseline 0 → 1 → 0 → 1 → 0 → restore → detach |
| [run 2 出力](ch32h417-core-select-run-2-2026-10-10.txt) / [USB capture](ch32h417-core-select-run-2-2026-10-10.ndjson) | 再接続した独立セッション、同じ選択順序 |
| [測定 Rust ソース](ch32h417-core-select-2026-10-10.rs) | ch32rv-usb / wchlink / dmi を path dependencies にする edition 2024 の独立 package 用 |
| [照合 script](check_capture.py) | USB の要求・応答・成功 status・選択順序から DMI / CSR を復元し JSON と出力ログを照合 |

対象は CH32H417QEU6、chip ID `0x4170053d`、family `0xc6`。Probe は WCH-LinkE `49808F06CE30`、`1a86:8010`、FW 2.22(raw `02 16`)、SWIO → PB9、speed low。capture はツール内蔵 USB 転送記録であり、usbmon や物理 SWIO の独立 capture は含まない。2 セッションは同じ測定実装による反復で、独立実装 2 つの一致を意味しない。

## 事実

同一 attach 内で DMCONTROL(`0x10`)へ `0x00010001` / `0x00000001` を書くと、readback と mhartid / marchid / mimpid がコア 1 / 0 に対応して切り替わる。両セッションで再現した。詳細な結果表と手順は [DM 仕様](../../../protocols/riscv-debug-module.ja.md#h417-の単一コア選択と-csr-読み出し)、要求・応答 byte は [PC↔Link §4a](../../../protocols/pc-to-link.ja.md#4a-h417-のコア選択を運ぶ-dmiop)に収載。

照合はリポジトリ直下で実行する(標準 Python のみ、実機不要):

```console
python3 captures/fixtures/ch32h417-core-select-2026-10-10/check_capture.py
```

各 capture は 332 USB 転送(166 往復)。5 snapshot の DMI 4 レジスタと CSR 5 個、選択順序 `1 → 0 → 1 → 0`、最後の hartsel 0 復帰・DetachChip を照合する。DMI status はすべて 0、CSR 読み出しの abstract cmderr / busy は完了時に 0。測定コードの初回 halt は、実際にはコア 0 が既に halted で haltreq を発行していない。コア 1 も選択直後に halted で、コア 1 宛 haltreq は発行されていない。

## 候補

標準 hartsel をコア選択に使う。ツール側は全 DMCONTROL 書き込みで選択を保持する必要がある。今回の収載では ch32rv CLI / DebugModule 実装を改修していない。

## 未決

verified の対象はこの chip / probe FW での hartsel 0 / 1 選択・readback と表の CSR 読み出しに限定する。選択コアの halt / resume / step / reset、DetachChip・再 attach 時の両コア状態・選択保持、他 firmware / 他 H41x、物理 SWIO は未確認。DMSTATUS 全 word は識別値に使わない。dpc 1 が 0 の理由は不明。測定コードは DM 作業レジスタと DMCONTROL を書くが、Flash・option byte・一般メモリの書き込み、erase、reset は行わない。probe 内部の attach / detach 副作用は保証しない。次の確認先は [coverage: H417](../../../coverage.ja.md#h417-core-control)。
