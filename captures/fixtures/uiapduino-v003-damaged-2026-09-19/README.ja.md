# UIAPduino V1.4故障機の復旧前後

取得・復旧日: 2026-09-19

ESP32 E132ジグのSWIO読出しは、各32 bit wordを成功7回読んでbit単位の多数決を取った。長い
実験配線では一時的な1 bit誤読が観測されたため、単発値を復旧判断には使っていない。

未使用基準機`../uiapduino-v003-factory-cebaabcd38d0be49-2026-09-19/`との比較結果:

| 領域 | 復旧前SHA-256 | 基準機との比較 |
|---|---|---|
| BOOT 1,920 B | `968da9139808dd1916cf2e8e39a1a3aa2faad7b7cdd345b906ed3042bac99f8f` | 完全一致、書込みなし |
| option 16 B | `ff8cacb6b5a87ee3913e25710c08b6fda9ddd73e4cc9d3512730b7da49043cd8` | 完全一致、書込みなし |
| application 16 KiB | `064911cbca2af15156a3d4978c5f075237d52383bf9bb8fe56030231d8a294be` | 不一致 |

applicationは64 byte単位で比較し、不一致だった230/256 pageだけへ基準機の初期applicationを
erase/program/verifyした。全領域の多数決再読出し`application-restored.bin`は基準機とbyte単位で
一致し、SHA-256は`3e1a9a41257cd4ba9f45f8dfae37e40e25d061ed139be9e5a119fba11bd627aa`。
一時的なverify誤読が1 pageで発生したが、再読出しで一致した。

復旧後はE132の`N`→`B`で製品HID `1209:b803`が列挙され、`ch32rv boot hid flash`による
Arduino application書込み、起動、GPIO/ADC/UART/I2C/SPI HILまで成功した。今回の症状は
bootloader破損ではなくapplication側だった。

これらのbinaryには製品コードや固有情報が含まれる可能性がある。公開・配布前に内容と権利を
確認すること。
