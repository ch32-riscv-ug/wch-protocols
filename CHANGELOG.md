# Changelog

この repo の版ごとの変更。版はタグ(`vX.Y.Z`)で示す。外の repo からは、タグを付けた GitHub の URL と、明示したアンカー(`#cmd-rst-out` の形)か E 番号で引く([README](README.md#status))。

## Unreleased(v1 の準備)

v1 に向けた組み直し。protocol の事実の正本をこの repo に一本化し、外から引ける識別子と機械可読の表を用意した。

### 識別子と形式

- `protocols/*.ja.md` のすべての見出しと、すべての WCH-Link コマンドに、明示した ASCII のアンカー(`<a id="…"></a>`)を付けた。見出しの文字を直してもアンカーは変わらない。`tools/check_anchors.py` が、見出しにアンカーがあることと、仕様の文書へのアンカー付きリンクが解決することを確かめる。
- E 番号は変えない。`.ja.md` のパスも変えない(英語版は後から隣に `.md` として足す)。
- WCH-Link のコマンド表の正本を [protocols/data/wchlink-commands.csv](protocols/data/wchlink-commands.csv) にした(schema は [protocols/data/README.ja.md](protocols/data/README.ja.md)、版は `protocols/data/VERSION` = 1)。要求は frame 全体(`81 cmd len payload…`)で書き、len と payload を取り違える余地をなくした。列は status、根拠の種類、範囲(probe の variant と fw、chip)、受理と効果、他の実装での名前、出典。pc-to-link §4 / §5 / §5b / §7 / §8 / §10b の表は `tools/gen_command_tables.py` が CSV から作る。

### status 語彙

- 定義を [README](README.md#status) の 1 か所にまとめ、他の文書(guides/advanced、experiments/README §6、pc-to-link、bootloader-survey)はリンクにした。
- `refuted`(自前の実機で主張が成り立たないと確かめた)を足した。「受理された」と「効果があった」を別に判定し、範囲の外へ status を広げない規則を書いた。
- WCH OpenOCD の source を読んだだけで `verified` にしていた GetRomRam / RstOut / DisableDebug / ChipReset を、`single-source` / `conflict` に直した。語彙の外の表示(reference、実装済み(未実機)、教訓)を除いた。

### 取り込んだ事実(ch32rv の文書と実測から)

- **RstOut** は `81 0d 01 13`(on)/ `81 0d 01 14`(off)。LinkE fw 2.22 は `00` で受理する(NRST の電気的な効果は未測定)。CH549 Link fw 2.12 は `ff`(非対応)。`81 0d 02 01 13` は GetProbeInfo と読まれ(LinkE 2.22 `02 16 12 00`、CH549 2.12 `02 0c 01 00`)、続く DMI の読みを busy にする。
- **SetPower** は LinkE 2.22 でも CH549 2.12 でも受理されるが、測った範囲では電源は切れない(`refuted`)。
- DMI のページ消去が FTER を立てた後に止まると、CH32V003 の FLASH_CTLR に `0x00020000` が残り、reset まで WCH の stub も 1 ページ目しか書かない。
- family byte の実測: V205 `0xce`、X315 `0xe6`、V407 `0xa6`、M030 `0x8e`、V006 `0x4e`。ChipInfo の UID の byte 順と `[12:16]` の値。
- 高速 read の窓(16 KiB)と word 境界、取り消した転送の残り。1 セッション 1 回の書込み、LinkE が host の stub をそのまま走らせる規約、buffer load の幅、option byte の注意、エラー応答の形。
- factory ISP: CH32X035(BTVER 2.60)の USB は `1a86:55e0` で `verified`。入り方、IspEnd の応答を読むまで reset しないこと、待ち時間、WRPR に補数 byte が無いこと、RDPR 中は bootloader が走らないこと。wch-iap / serial-and-print の VID の記述を直した。
- H417 のコア 1 の halt / resume / step。

### 文書

- 仕様の本文から経緯の語り(「以前は…と書いた」、日付つきの進捗)を除き、今の状態だけを書いた。未決の問いの一覧は残した。
- repo の中の食い違いを直した: DmiOp が透過かどうか、long 形式の parity と target の応答、coverage の「最大の穴」、索引の古い status。
- README を索引に戻し、この repo が protocol の事実の正本であることと、外からの引き方を書いた。

### 削除(履歴は書き換えていない)

- `captures/fixtures/wire-linke-p4-2026-09-25/versions/`(ch32rv の版ごとの比較、418 ファイル、21.5 MB)。結論(線上の列は ch32rv の版に依らない)は fixture の README に残した。
- 同 `more/` のうち、仕様が根拠として引いていないもの(228 ファイル、26.0 MB)。残したのは `l103/monitor_sdi*`、`l103/dmiop_*`、`l103/redo_redetect_*`、`<target>/repeat*`。
- `experiments/out.txt`(誤って入った PowerShell のエラー出力)。
- `references/data/harness-wiring/`(生成物。出所の lock が無い)。
- `protocols/dmi-bridge.ja.md`(OEP に置き換わった draft)。リンクは削除前の版(`717e17a`)を指す。
