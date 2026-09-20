# E141 V003 loader複数page session試作

状態: **不採用（2026-09-20）**

## 問い

明示的なbegin/end operationの間だけRAM loaderを再転送せず、64 byte pageを高速化できるか。

## 結果

beginで498 byte loaderを転送した直後の最初のpageは121.7 msまで短縮した。しかしloader完了後の
`a0`取得を取りこぼしてsessionを無効化し、2 page目以降は逐次fallbackを含む約294 msへ戻った。

最終fresh read-backを権威ある成功条件として`a0`取得を省く試作も行ったが、途中pageで最終照合が
不一致（diagnostic `0x42`）になった。試験pageは通常の単page operationで全FFへ復旧し、software
reset後に64 byte全体が全FFであることを確認した。

## 判断

RAMにloader byte列が残ることと、安全に再実行できるdebug/CPU状態は同義ではない。DPC、DCSR、
SP、halt reason、abstract command stateをpage間でどう再初期化するか、LinkEの1 KiB block間の実列と
比較してから再設計する。現時点ではbegin/end operationをAPIへ残さず、単pageごとの検証転送を維持する。

この失敗からProtocolへsession概念を昇格させる根拠はまだない。
