# ESP32-P4 probe / ロジアナ — 現在地と今後の計画

状態: **計画**（2026-09-14。[E063](../experiments/e063_p4_usb_hs_enumerate/README.ja.md)〜[E106](../experiments/e106_p4_mixed_rate_capture_stream/README.ja.md)を踏まえた棚卸し）

この文書は、実験ごとの細部ではなく、**いま何を通常仕様として説明でき、何が検証済みで、次に何をするか**を管理する。数値の証拠は各実験、USB単体の経緯は[P4 USB HSまとめ](p4-usb-hs-summary.ja.md)、rate選択規則は[sample rateの選び方](p4-sample-rate-selection.ja.md)を正本とする。

## 1. 現時点の結論

### 1.1 製品向けの説明

- 通常モードのbase sampling rateは、**1〜8 channelで最大約60 Msps、9〜16 channelで最大約40 Msps**を目安にする。
- これは全channelを同じrateでUSBへ送れるという意味ではない。各channelへ割り当てたrateの合計を、USB実測予算以下へ収める。
- CS / INT / buttonなどはchannel単位で`1/2、1/4、1/8、1/16、1/32、1/64…`へ時間解像度を下げられる。
- capture前にP4→PC方向をprobeし、**実測payload帯域の90%**を推奨予算にする。
- 少数channelには通常値を超える技術的余地があるが、連続転送を含む通常仕様としては前面に出さない。必要になれば後で「少数channel高速モード」として分離して検証する。

数値は今後の実装で変わり得る。中心となる特徴は最高rateではなく、**高速信号の分解能を残しながら、低速信号のrateをchannelごとに下げて転送予算を配分できること**である。

### 1.2 実証済みの代表profile

| physical幅 | channel構成 | 内部capture→codec→PSRAM | 現USB経路での連続転送 |
|---:|---|---|---|
| 8 bit | 3 full＋5 D=64 | 61 Msps成立、62 Msps破綻。**安全値60 Msps** | 32 Msps / 100 Mbps、65.536 MB完全検査PASS |
| 16 bit | 3 full＋1 D=8＋12 D=64 | **40 Msps、167,772,160 sampleを3回PASS** | 32 Msps / 106 Mbps、69.468 MB完全検査PASS |
| 16 bit（旧profile） | 3 full＋8 D=64 | 42 Msps成立、43 Msps不安定。**安全値40 Msps** | 32 Msps / 100 Mbps、65.536 MB完全検査PASS |

16 channel / 40 Mspsの代表例は、`3×40 + 40/8 + 12×40/64 = 132.5 Mbps`である。128 sampleを53 byteにまとめることでpaddingをなくした。1/64は625 ksps、時間刻み1.6 usなのでbuttonには十分であり、短いCS / INTは`any_active`でbucket内のactiveを残せる。

現在の接続はHS hub 2段＋usbipd/WSLで、USB-only probeは120.860 Mbps、90%予算は108.774 Mbpsだった。このため132.5 Mbpsの40 Msps profileはrejectし、106 Mbpsの32 Mspsへfallbackする。直結時は改めてprobeして選び直す。

### 1.3 実装上分かったこと

- TinyUSBのsoftware ringとDWC2 hardware TX FIFOは別物。hardware FIFOを1 packetから2 packetへ増やしても25.575 MB/sの天井は変わらず、E090の仮説は反証された。
- P4→PCは、事前生成zero-copyなら36.159 MB/sまで出る。実captureではPARLIO callback、codec、stage queue、PSRAM copy、USBが合成された上限を見る必要がある。
- 8→16-bitでraw入力が1 sampleあたり1→2 byteになり、詰め替え込みの安全値は60→40 Mspsになる。
- codec block境界とUSB packet境界は一致させない。53-byte blockをそのままtransfer終端にするとshort packetが連発してusbipdがerrorになった。codecは128 sample単位で作り、PSRAM FIFOから最終回以外512 byte単位で送ると成立した。
- genericな1-bitずつのpackingでは40 Mspsに足りない。代表profileのD=64部分を固定bit-spread演算にすると40 Mspsを回復できた。
- 現在の16-bit試験は内部TXの8 GPIOを上位laneへ複製している。16本の独立した外部padの電気試験ではない。

## 2. 近い目標の現在地

| 目標 | 状態 |
|---|---|
| mixed-rateロジアナのdata path | **代表profileは成立**。固定profileでcapture、codec、PSRAM、USB、PC復元まで通った |
| `.sr`保存とstock decoder | **達成**。P4でcaptureした`.sr`をsigrok decoderが読める |
| PulseViewへIP経由 | **raw streamでは達成**。mixed-rate descriptorからbase gridへ復元するgatewayは未実装 |
| USB経路の予算測定 | **測定コマンド成立**。90%予算による自動ACCEPT / fallbackは未実装 |
| RVSWDでCH32へ書込 | **未着手**。CH32とP4の配線待ち |
| RVSWD / SWIO decoder | **未着手**。実信号取得は上記配線待ち |

「packet capture」はロジアナで捕ってdecoderで読むことを指す。SPI / UART / JTAGなどstock decoderがあるprotocolは既に処理できる。RVSWD / SWIOだけはWCH固有decoderが必要である。

## 3. 次に再開するときの順序

### Phase A — 設定と正しさを固める

1. PCから`base_rate_hz / sample_count / GPIO mapping / channelごとのmode・D・phase・polarity`を渡すdescriptorを決める。
2. deviceが`physical幅 / block sample数 / payload bit数 / padding / raw入力帯域 / wire帯域`を返し、内部上限を超える設定をREJECTする。
3. `D=2 / 4 / 8 / 16 / 32 / 64`の実機codecを確認する。reference codecはround-trip PASS済み。
4. `D=128 / 256 / 512 / 1024`を通常UIへ出すか決める。形式上は可能だが、複数blockをまたぐ状態、待ち時間、追加の帯域削減量を測ってから決める。
5. `decimate_hold / any_active / edge_latch`について、短pulse、bucket境界、active polarity、端数captureを固定fixtureで検査する。

### Phase B — host統合を固める

1. USB probe結果の90%を予算にし、要求profileを`ACCEPT / REJECT`する。
2. 超過時はbase rate候補を下げ、必要なら32 / 30 Mspsなどを提示または自動選択する。
3. descriptorに従ってmixed-rate streamをbase sample gridへ復元し、PulseView gatewayへ接続する。
4. PulseView要求量よりcodec block単位で多めに受信し、出力時に分割する。`close`時は先読み分を捨ててcaptureを停止する。
5. start / stop / restart、設定変更、端数sample、host切断、timeoutを繰り返す。Monitorや別DOS窓の入力待ちに依存しないCLIにする。

### Phase C — 実機条件を広げる

1. 16本の独立GPIOを外部pattern源へ配線し、lane順、任意GPIO mapping、同時変化を検査する。
2. SPI相当のCLK / MISO / MOSI / CSと、短INT、button相当を同時生成し、`/8`と`/64`の見え方をPulseViewで確認する。
3. 現在のhub 2段、PC直結、Windows native、WSL usbipdで同じprobeと長時間captureを行う。
4. 長時間soak、繰り返し列挙、途中切断後の復帰を確認する。

### Phase D — 最後にチューニングする

1. descriptorをprofile別の固定高速codecへdispatchするか、generic codecを最適化するか比較する。
2. PARLIO callback量、ring / queue / stageサイズ、PSRAM copyの配置を掃引する。
3. USBのshort completion、arm単位、zero-copy化を詰める。ただしE090で反証済みのhardware TX FIFO増量は繰り返さない。
4. 安全marginを再測定し、表向きの60 / 40 Mspsを最終確定する。
5. 少数channel高速モードが実用上必要な場合だけ、通常仕様と分離して着手する。

## 4. いったん保留するもの

- 160 Mspsを主要な製品値として掲げること。誤解を招き、複数channelの連続USB転送とは両立しない。
- RLE / deflateを通常streamへ入れること。最悪入力で膨張しない仕組みが先に必要で、現段階ではchannel別縮約を優先する。
- 1/1024より下の単純間引き。button等はedge/event表現の方が適する可能性が高い。
- USBの最高値だけを追うこと。経路依存性はprobe＋90%予算で吸収し、まず設定・復元・停止の正しさを固める。

## 5. 配線または環境変更が必要な項目

| 項目 | 必要なもの |
|---|---|
| 16 channel独立入力 | 16本を外部pattern源へ接続。現在の内部loopbackは8本複製 |
| 実SPI / INT波形 | P4または別deviceの信号源とGPIO配線 |
| USB直結比較 | 現在のhub 2段経路からPC直結へ差し替え |
| RVSWD / SWIO | CH32とP4の電源、GND、信号線 |

直結比較は最終rateを決める前に必要だが、動的descriptorやhost復元は現在の配線のまま進められる。

## 6. 作業環境と他のprobe課題

- E104〜E106の対象は第三P4（MAC `80:f1:b2:d0:b2:61`、UART `/dev/ttyUSB0`、HSはWindows bus `40-2`をusbipdでWSLへattach）。
- `/home/mt/dev/EspUsbHost/tests/.env`は別のfull testが使用中であり、この検証から変更・流用しない。
- MACの近い2台はHS同士で結線された別リグであり、今回のmixed-rate検証では触れていない。
- firmware uploadでHS deviceは再列挙される。古いusbipd attachやendpoint待ちを残さず、再attachしてdevice nodeを取り直す。
- `n=3`は再現確認であり、分散や保証値を決める統計ではない。最終値は長時間soakと環境差を含めて決める。

ロジアナ以外ではRVSWDでCH32へ書き込む作業が未着手である。電源とGNDを合わせた後、順序付き2本の探索でchip IDが返る組を見つけ、残りの配線を符号化patternで同定する段取りは[pin discovery](pin-discovery.ja.md)にある。RVSWD / SWIO decoderは、この実配線から得た波形と同時に進める。

EspUsbHostのHR-3（HID 1,024 byte）とrelease判断は、ロジアナのmixed-rate data pathとは分けて持ち主判断のまま残す。

## 7. 再開時の完了条件

通常仕様を確定する前に、少なくとも次を満たす。

- 任意のchannel descriptorをdeviceとhostが同じbudgetとして解釈する。
- budget超過設定をcapture開始前にrejectできる。
- 8-bit / 16-bit代表profileを長時間・複数回、欠損0で再現できる。
- PulseViewで高速channelと展開後の低速channelを同時表示できる。
- start / stop / restartとhost切断から回復できる。
- 16本独立GPIOでmappingと値を確認できる。
- hub / 直結差はprobe結果へ反映され、固定のUSB速度を仮定しない。

## 8. 参照

- [E105 mixed-rate形式](../experiments/e105_p4_spi_mixed_rate_codec/README.ja.md)
- [E106 実capture→codec→USB](../experiments/e106_p4_mixed_rate_capture_stream/README.ja.md)
- [sample rateの選び方](p4-sample-rate-selection.ja.md)
- [PulseView / sigrok連携](pulseview-integration.ja.md)
- [capture圧縮](capture-compression.ja.md)
- [P4 USB HSまとめ](p4-usb-hs-summary.ja.md)
- [ピンの当たりを付ける](pin-discovery.ja.md)
- [実験台帳](../experiments/LEDGER.ja.md)
