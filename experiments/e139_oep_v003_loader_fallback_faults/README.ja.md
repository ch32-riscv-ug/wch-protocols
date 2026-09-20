# E139 OEP V003 loader→逐次fallbackの故障注入

状態: **完了（2026-09-20）**

## 問い

通常backendのRAM loaderが途中で失敗しても、同じpageを全消去からやり直す逐次経路で安全に
完了できるか。

## 故障点

`OEP_V003_INJECT_LOADER_FAILURE`のbuild値で3段階を強制した。

1. loader転送前（diagnostic `0x7f`）
2. loaderと64 byte dataのRAM転送後、実行前（`0x7e`）
3. loaderがerase/program/verifyを完了した後（`0x7d`）

故障は内部で必ず発生するが、外側の逐次fallbackが成功すればOEP TargetFlash resultはsuccessとなる。
段階3は既に正しく書けたpageを逐次経路がもう一度erase/programする最も厳しいcaseである。

## 結果

- 段階1: 末尾4 pageのpattern 4回、全FF復元4回が全成功
- 段階1: software reset後256 byteが全FFで完全一致、1.574秒
- 段階2: pattern、全FF復元、reset後64 byte照合が成功
- 段階3: pattern、全FF復元、reset後64 byte照合が成功
- GPIO23外部RESETは未使用

loaderが未実行、RAM転送だけ完了、flash書込みまで完了、のどの境界でもfallbackが成立した。
逐次経路がpage全体のeraseから開始することが、部分状態を引き継がない回復条件になっている。

## 未決

現在の成功resultだけではloaderとfallbackのどちらが使われたかhostから分からない。これは通信仕様へ
不用意に診断情報を足さず、将来のprobe telemetry/diagnostic機能として別に扱う。
