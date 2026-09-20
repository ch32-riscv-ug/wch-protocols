# E137 OEP RAM loaderでV003 Arduino image全体を書込む

状態: **全page実書込みで完了（2026-09-20）**

## 問い

E135の64 byte単発成功を全Arduino imageへ拡張しても、page retryなしで書込み、reset後に実行できるか。

## 方法

- E131と同じUIAP Arduino coreのfixtureをbuildする。
- 5,220 byteを64 byte境界の5,248 byteへpadする。
- OEP `TargetFlash.programPage64`だけで`0x08000000`から82 pageを書込む。
- 各pageはprobe内のRAM loaderがunlock/erase/program/verifyし、fresh attach後にも照合する。
- `TargetControl.normalizeUser`によるsoftware reset後、`TargetMemory`でRAM markerを読む。

GPIO16→PD1/SWIOだけを使い、GPIO23外部RESETと製品BOOT領域は使用しない。

## 結果

- image: 5,220 byte
- padded: 5,248 byte / 82 page
- page retry: **0**（ただし既存内容との一致pageはbackendが書込みを省略する）
- 全page書込みからreset後marker確認まで: **5.266秒**
- marker address: `0x200000f8`
- marker value: `0xe131b007`（期待値一致、`setup()`実行済み）

後のE140準備で、この実行前からE131と同じfixtureがuser flashへ入っていたことを再確認した。
したがって5.266秒は全82 pageを実際にerase/programした時間ではなく、一致確認と必要pageだけの
更新時間である。「旧逐次経路の約半分」という比較は撤回する。全pageを異なる内容へ変えてから
同じimageへ戻す試験で再測定する。

### 一致省略なしの再測定

5,248 byte全体をfixtureとは異なる決定的patternへ書き、82 pageすべてを独立read-backした後、
fixtureを全82 pageへ戻した。これにより両方向ともalready-matchesによる省略はない。

- pattern書込み: 82/82 success、failure 0、**24.460秒**
- pattern独立read-back: 82/82 page完全一致
- fixture書込み: 82/82 success、failure 0、**23.611秒**
- 合計実書込み: 164 page、48.071秒
- software reset後marker: `0xe131b007`
- GPIO23外部RESET: 未使用

単page APIの実書込みは約288〜298 ms/pageである。旧逐次E131の10.664〜11.169秒より遅い。
原因は64 byte要求ごとに498 byte loaderを検証転送するためで、loader方式そのもののFLASH処理速度
ではない。次の性能試作では明示的な複数page session中だけloaderを再利用する。

続けてOEPから製品bootloader移行を要求するとoperation自体はsuccessを返したが、30秒以内に
Windows/WSLのどちらにも`1209:b803`は現れなかった。user flashの書込み・実行確認とは分け、
USBIP auto-attach状態とboot移行を再調査する。この時Windowsにはbus `11-3`で
`0000:0002`「デバイス記述子要求の失敗」が存在した。製品BOOT領域は上書きしていない。
