# E090 S3 peer: I2C slave 基礎比較

GPIO19=SCL、GPIO20=SDAで直結された常設S3 peer対を使う。既存imageは保存せず直接上書きする。
最初の段階ではArduino `Wire` hardware slave（address `0x42`）へ4 byte writeを行い、master statusが
0かつslave callbackが4 byteを受信することを合格とする。速度は1 kHz/10 kHz/100 kHzで個別に測る。

## 結果

2026-09-21、primary `d0cf1359101c` をmaster、peer `d0cf1358fd94` をslaveとして直接UARTで実行した。

| clock | master status | slave callback |
|---:|---:|---:|
| 1 kHz | 0 | 4 byte |
| 10 kHz | 0 | 4 byte |
| 100 kHz | 0 | 4 byte |

Arduino ESP32-S3 `Wire` hardware slave、GPIO19/20直結、現在の電気条件ではaddress ACKと4 byte writeが
成立する。P4/X035で見たNACKは、この基準配線やArduino I2C一般には帰属しない。
