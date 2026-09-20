# E134 LinkE V003書き込みの実コマンド列と間隔

状態: **完了（2026-09-20）**

## 問い

ESP32 software SWIOのフラッシュ書き込みを、LinkEの実波形と比較すると何が異なるか。

## 入力

既存の50 MHzロジックアナライザ記録
`captures/fixtures/wire-flash-v003-x035-2026-09-11/wire-v003.sr`を使う。この記録では
LinkEがCH32V003へ4 KiBを書き、同じ内容を読み戻せている。

## 結果

LinkEはホストからフラッシュ制御レジスタを1操作ずつ書いていない。次の順序だった。

1. 約498 BのV003用flash loaderを`0x20000000`へ置く。
2. 書き込みデータを`0x20000200`から1 KiBずつ置く。
3. 引数レジスタを設定し、`DMCONTROL=0x40000001`でターゲットCPUをresumeする。
4. `DMSTATUS`をpollし、停止後に結果を取得する。

データ転送の先頭では`DATA1=0x20000200`、`DATA0=0x03020100`に続き、program bufferを
設定して`COMMAND=0x00040000`を実行している。以後は宛先、DATA0、COMMANDを繰り返す。
これは現在のESP実装のhost-drivenなabstract memory/flash-register列とは別方式である。

SWIOの通常の隣接フレーム間隔は約7 us、stub実行待ちのDMSTATUS poll間隔は約10 usだった。
E133でLinkEに近いpulse幅へ変更しても従来列が成立しなかった結果と合わせ、次の実装は
単発DMI速度の追加調整より、RAM loader方式の再現を優先する。

追加解析では4 KiBを1 KiB×4 blockとして実行していた。各blockは`a0=8`（programのみ）、
`a1=0x08000000 + 0x400*n`、`a2=0x400`であり、unlock/eraseは先行する別invocationである。
loaderを残したまま小pageごとにunlock+erase+program+verifyを繰り返す方式ではない。複数page高速化を
行う場合は、この1 KiB bufferと操作分離を再現する必要がある。

## 再現

```sh
python3 experiments/e134_linke_v003_flash_trace/analyze.py
```

スクリプトは統計を表示するだけでなく、先頭の実コマンド列をassertする。キャプチャのdecodeや
列の解釈が変わった場合は成功したふりをせず失敗する。
