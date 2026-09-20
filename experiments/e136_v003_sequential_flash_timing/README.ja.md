# E136 V003逐次フラッシュ操作の待機時間

状態: **完了（2026-09-20）**

## 問い

RAM loaderを使わず、hostがFLASH操作を逐次指示する経路の失敗は、FLASH内部処理に対する待機不足で
説明できるか。erase、buffer reset、16回のbuffer load、programを個別に測る。

## 方法

- ESP32 GPIO16→V003 PD1/SWIO、PHY係数8を使う。
- user flash末尾64 byteだけを一時使用し、各case内で全FFへ戻す。
- 各FLASH register write後の追加quiet timeを0/50/200/1000 usで変える。
- 各段階でSTATR.BUSYが消えるまでの時間、poll数、read error数、最終STATRを記録する。
- patternと全FF復元の両方をread-backする。

`readMemoryWord`によるpollにはabstract commandとSWIO通信時間が含まれる。この実験の
`elapsed_us`はFLASH単体の精密時間ではなく、hostが次の操作へ安全に進めるまでの上限側実測値である。

## 結果

4条件すべてでpattern書込みと全FF復元が成功した。quiet 0 usでも逐次書込みは成立した。

| quiet time | erase | program | buffer reset/load | pattern/restore | case全体 |
|---:|---:|---:|---:|---:|---:|
| 0 us | 3.259–3.265 ms / 7 poll | 2.790–3.251 ms / 6–7 poll | 0.461–0.470 ms / 1 poll | 成功/成功 | 395 ms |
| 50 us | 3.248–3.252 ms / 7 poll | 2.784–2.786 ms / 6 poll | 0.461–0.470 ms / 1 poll | 成功/成功 | 402 ms |
| 200 us | 3.248–3.253 ms / 7 poll | 2.776–2.788 ms / 6 poll | 0.461–0.469 ms / 1 poll | 成功/成功 | 414 ms |
| 1000 us | 2.319–2.328 ms / 5 poll | 1.858–1.859 ms / 4 poll | 0.461–0.470 ms / 1 poll | 成功/成功 | 407 ms |

1000 usはFLASH操作開始後、計時開始前に待っている。この1 msを足すとeraseは約3.32 ms、programは
約2.86 msとなり、quiet 0の観測と整合する。1回のSTATR取得は約0.46 msなので、FLASH内部時間の
分解能も約0.46 msである。全stage、全caseでSTATR read errorは0、完了値は`0x00008020`だった。

## 判断

V003の逐次経路自体は必要な機能として成立可能であり、固定の長いdelayは必須ではない。少なくとも
今回の失敗原因を単純な「操作間wait不足」だけでは説明できない。旧実装との差として強いのは、
LinkEに近い係数8によるread安定化と、各FLASH段階でBUSY clearを確認してwriter programを復元して
から次へ進むことである。

実装上は固定delayでerase/programの最大時間を決め打ちせず、STATR.BUSYをpollし、通信read失敗と
FLASH busyを別状態として扱う。timeoutは今回の約3.3 msへ十分な余裕を持たせる必要がある。

run: `_runs/E136_20260920T021010Z_default/`
