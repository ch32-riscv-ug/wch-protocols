# E140 OEP V003フラッシュ短期反復

状態: **完了（2026-09-20）**

## 方法と結果

通常のRAM loader優先backendで末尾64 byteを使用し、異なるpatternと全FFを50周期、合計100回
erase/programした。10周期ごとに`normalize-user`でsoftware resetし、64 byte全体を読み戻した。

- flash operation: **100/100 success**
- software reset: 5回
- reset後read-back: **5/5完全一致**
- failure result: 0
- 総時間: 43.672秒
- 1回のlatency: min 413.379 ms / median 422.367 ms / p95 431.237 ms / max 432.597 ms
- 最終page: 全FF

最初のrunは30周期後、測定中のUARTを別processでも開いて1 frameを破損させたため統計から除外した。
pageを全FFへ復旧・確認してから、排他的な上記runを取り直した。

## 追加評価

loader終了pollを2000回から20 ms/128回上限へ変更すると、20回の実書込みは全成功し中央値
296.486 msとなった。fresh attachを完了fenceとして使うため永続性は維持した。その後の重複verify
整理後も約294 msで、64 byteごとのloader転送とabstract操作が支配的である。

loaderをRAMに常駐したと仮定する試作は時間を改善せず、状態保証も弱いため採用しなかった。
速度改善は暗黙cacheではなく、将来の複数page operationでloader転送を明示的に償却する。
