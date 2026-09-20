# E138 OEP backendの強制逐次V003書込み

状態: **完了（2026-09-20）**

## 問い

E136で成立したhost-sequenced flash操作をOEP backendへ組込み、RAM loaderを一切含まないbuildでも
複数pageを連続して書込み、reset後に保持できるか。

## 方法

`oep-probe-arduino`を`OEP_V003_FORCE_SEQUENTIAL_FLASH=1`でclean buildした。Arduino CLIの通常cacheは
libraryの追加define変更を拾わない場合があったため、`--clean`を必須とし、通常版と強制逐次版の
firmware SHA-256が異なることも確認した。

強制逐次経路は各FLASH段階でSTATR.BUSYをpollする。erase/programの固定delayは置かない。
user flash末尾4 page `0x08003f00`〜`0x08003fc0`だけを使う。

## 結果

- 異なるpatternを4 pageへ連続program: 4/4 success
- 同じ4 pageを全FFへerase/program: 4/4 success
- `normalize-user` software reset後、4 page×64 byte: 全FF完全一致
- 全8回とreset後read-back: 1.708秒
- GPIO23外部RESET: 未使用

これによりRAM loaderは速度上の標準経路、逐次操作は独立して成立する代替経路として残せる。
通常backendはloader失敗時、同じpageを最初からeraseする逐次経路へfallbackする。強制buildは
fallback試験と原因分離に使い、OEP上のTargetFlash操作は両経路で同一である。
