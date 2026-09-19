# 未使用UIAPduino Pro Micro CH32V003 V1.4の初期状態

取得日: 2026-09-19

未使用のUIAPduino Pro Micro CH32V003 V1.4から、製品bootloader、user flashの
初期application、option bytesを読出し専用操作で保全した。復旧対象とは別の基準機である。

## 識別情報

- probe: WCH-LinkE firmware 2.22
- probe USB serial: `49878F06CE37`
- board-identify名: `wch-link-49878f06ce37`
- target: CH32V003、flash 16 KiB
- target UUID: `ce-ba-ab-cd-38-d0-be-49`
- part type: `00-31-05-10`
- read protection: disabled
- option表示: `USER/RDPR=08f7/5aa5`、`DATA1/DATA0=00ff/00ff`、
  `WRPR1/WRPR0=00ff/00ff`、`WRPR3/WRPR2=00ff/00ff`

複数のWCH-Linkが接続されているため、すべての操作でprogrammerとUSB serialを明示した。

```sh
minichlink -C linke -l 49878F06CE37 -i
minichlink -C linke -l 49878F06CE37 -r bootloader-pass1.bin bootloader 1920
minichlink -C linke -l 49878F06CE37 -r application-pass1.bin flash 16384
minichlink -C linke -l 49878F06CE37 -r option-pass1.bin option 16
```

同じ読出しを3回行い、対象ごとに3ファイルがbyte単位で一致することを`cmp`で確認した。
単発読出しや多数決で作ったimageではない。

## ハッシュ

| 対象 | サイズ | SHA-256 | 一致 |
|---|---:|---|---|
| 製品bootloader | 1,920 B | `968da9139808dd1916cf2e8e39a1a3aa2faad7b7cdd345b906ed3042bac99f8f` | 3/3 |
| 初期application | 16,384 B | `3e1a9a41257cd4ba9f45f8dfae37e40e25d061ed139be9e5a119fba11bd627aa` | 3/3 |
| option bytes | 16 B | `ff8cacb6b5a87ee3913e25710c08b6fda9ddd73e4cc9d3512730b7da49043cd8` | 3/3 |

`*-pass1.bin`から`*-pass3.bin`までを意図的に残す。3個を同じ内容へ複製したのではなく、
各ファイルはtargetから独立に読んだ結果である。

## 復旧へ使う前の条件

このcaptureの取得操作ではtargetへのerase/program、option変更、read-protection変更を行って
いない。基準機へ書き戻す用途には使用しない。

故障機を復旧するときも、直ちに全領域を書かない。次の順で扱う。

1. 故障機のBOOT、user flash、option bytesを可能な限り複数回保存する。
2. UUIDとboard revisionを区別し、この基準機と同じ製品であることを確認する。
3. option bytesを比較する。保護解除やmass eraseを先に実行しない。
4. BOOTを64 byte page単位で比較し、不一致pageだけを候補にする。
5. 書込み後はread-backした1,920 byte全体のSHA-256を上記BOOT hashと比較する。
6. `1209:b803`の列挙を確認してから、必要なら初期applicationまたはArduino applicationを
   user flashへ書く。

初期applicationには製品固有情報や未公開コードが含まれる可能性があるため、公開・配布前に
内容と権利を確認すること。
