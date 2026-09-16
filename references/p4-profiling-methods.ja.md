# ESP32-P4 firmwareのプロファイルの取り方 — 何を使っていて、何が便利か

状態: **reference**（2026-09-16。E107〜E119で使った方法の整理。持ち主の問い「自作で空き計算しているのか、FreeRTOS側のtaskあたりのプロファイルは使えるか、どんな取り方が便利か」への回答）

## 0. 結論

- 自作の空き計算ではなく、**FreeRTOSのrun-time stats（taskごとの実行時間）をそのまま使っている**。`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y`（arduino-esp32 3.3.11のP4 buildで有効）なので、`ulTaskGetRunTimeCounter()` / `uxTaskGetSystemState()` がtaskごとの累積時間を返す。coreの空きは**idle task（IDLE0 / IDLE1）の実行時間**として読む。これがE107以降の`idle0_us` / `idle1_us` / `codec0_task_us` / `usbd_us` / `usb_task_us`。
- ただしrun-time statsは**割り込み時間を「中断されたtask」に計上する**ので、割り込みが多いcore（USB ISR＋PARLIO ISRのcore 0）では「idleが少ない＝taskが重い」とは限らない。ISR込みのcore負荷は**priority 1のspin task**で測る（E108 §4）。
- codecの中身のように「この区間が何cycleか」は、**`esp_cpu_get_cycle_count()`（RISC-Vの`mcycle`）**で区間を囲んで数える（E118）。割り込みも含む壁時計cycleなので、core 0の数字はISRぶん膨らむ（E118: 同じencodeがcore 1で980、core 0で1,400）。
- codecだけを切り離して測るには**device上のbench**（descriptorごとに同じencoderを8,192 block走らせる`Q` command、E114）。streaming（DMA ring読み、stage書き、簿記込み）との差が「周辺費用」になる。
- 便利だった順: (1) idle task時間（1行で律速coreが分かる）、(2) 区間cycle counter（原因の場所が分かる）、(3) device bench（codecの版比較に最速）、(4) spin task（ISRの重さ）、(5) host側のURB完了時刻（転送の穴）。

## 1. FreeRTOS run-time stats（taskごと）

### 使い方

```cpp
// 起動時または run 開始時に snapshot、終了時に差分を取る
static uint32_t taskRunTime(TaskHandle_t h) { return ulTaskGetRunTimeCounter(h ? h : xTaskGetCurrentTaskHandle()); }
// idle task は xTaskGetIdleTaskHandleForCore(core)
struct StatSnapshot { uint32_t idle[2]; uint32_t usbd; uint32_t usbTask; uint32_t codec[2]; int64_t wall; };
```

E107〜E119の`E1xx_STATUS`行はこの差分を`idle0_us idle1_us usbd_us usb_task_us codec0_task_us codec1_task_us stat_wall_us`として出す。分解能はesp_timer（1 µs）、run-time counterの単位はESP-IDFの設定でµs（`CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE`、P4 buildでは`esp_timer_get_time`ベース）。

**全taskの表**を一度に取るなら`uxTaskGetSystemState(array, n, &total)`で、task名・core・priority・run timeが揃う。`vTaskGetRunTimeStats(buf)`は文字列でくれるが長時間runでは%が丸まるので、生の`ulRunTimeCounter`を使う。

### 読み方の注意（E107 / E108で踏んだ）

- **ISR時間は中断されたtaskに付く。** idle中に割り込みが入ればidleに計上される。core 0の「idle 20%」はUSB ISR＋PARLIO ISRが入っている20%かもしれない。E108では8-bit 60 Mspsでcore 0のidleが約23%見えたが、spin taskで測ると本当の空きは約23%（一致した例）、E107ではISRが重い配置で「idleが多いのに詰まる」形で出た。
- taskの実行時間は**そのtaskが走っていた壁時計時間**なので、他coreのDMAやbus競合で遅くなったぶんも含む。「codec taskが90%」は「codecの演算が90%」ではない（E118: encode本体は64%、周辺が34%）。
- run-time counterは32 bitでµs単位なら約71分で回る。5分soakは問題なし。

## 2. spin task（ISR込みのcore負荷）

priority 1（idleの直上）のtaskを対象coreにpinし、一定時間`esp_cpu_get_cycle_count()`を回してループ回数を数える。事前に無負荷でcalibrateした「1秒あたりの回数」と比べれば、**ISRを含めてそのcoreが他に取られた割合**が出る（E108 §4、`spin_count / (spin_calib_per_s × spin_us)`）。idle taskが走れない間はspinも走れないので、run-time statsの盲点（ISR）を埋める。副作用: そのcoreのidle taskが飢えるので、IDLE watchdog（5 s）に当たらないよう5 s未満の窓で使う。

## 3. 区間cycle counter（`esp_cpu_get_cycle_count()`）

```cpp
const uint32_t c0 = esp_cpu_get_cycle_count();
encode(in, out);
encCycles += esp_cpu_get_cycle_count() - c0;
++encBlocks;
```

- 1命令、32 bit、core別のmcycle。360 MHzなら約12 sで回るので**差分だけ**を足す。
- 割り込み・他taskへの切替を含む壁時計cycle。区間が短い（数百cycle）なら混入は稀で、平均で見れば十分。長い区間（msync、queue受信）は**blockingを含む**ので、idleと一緒に読む（E118の`q_cyc_per_block`はblockingぶんを含んで見えた）。
- E118ではこれで「encode 980 / msync＋commit 155 / queue受信158 / その他215 cycle/block」と内訳が出て、codecでなく周辺が原因だと分かった。**最初にこれを取っていれば1日短かった**。

## 4. device bench（codecの単体測定）

descriptorを受けるたびに、同じencoderを同じcoreで8,192 block（1 M sample）走らせて`bench_msps`を返す（E114 `Q` command）。入力がL1-hotなので**codecの演算だけ**が出る。E118の`--bench-cold`（ringを歩いてL1-coldにする）との差で入力のcache missの費用（5〜8%）も分かる。streamingとの差（E118: benchの0.58倍）が周辺費用。版の比較（E115 v1 / v2 / 最終、E119の4手）はこれが最速で、板を借りる時間の大半はこれで済んだ。

## 5. host側

- URB完了ごとの`time.perf_counter()`を記録し、間隔の上位4つを出す（E114 `host_urb_gaps_ms`）。deviceの`completion_gap`（arm済みtransferが完了しない間隙）と並べると、穴がdevice側かhost側かが分かる。
- **URB完了callbackの中でPythonの仕事をしない**（E114 §4）。測定toolが測定対象を壊す典型。

## 6. 使わなかったもの

- SEGGER SystemView / FreeRTOSのtrace hooks: 全taskの切替時刻が取れるが、trace bufferの転送経路（UART / RTT）と組み込みが重く、今回の問い（どのcoreが飽和し、blockあたり何cycleか）には上の4つで足りた。
- ESP-IDF `perfmon`（hardware counter）: Xtensa向け。P4（RISC-V）は`mcycle` / `minstret`をCSRで読めるので、`esp_cpu_get_cycle_count()`で代替。
- gprof / sampling: XIPのflash実行なのでサンプリング割り込みが測定を歪める。区間counterのほうが素直。

## 7. 参照

[E107](../experiments/e107_p4_stream_core_placement/README.ja.md)（run-time statsでcore別idle）、[E108](../experiments/e108_p4_zero_copy_stream/README.ja.md) §4（spin task）、[E114](../experiments/e114_p4_dynamic_descriptor/README.ja.md)（bench、host URB間隔、callback規約）、[E118](../experiments/e118_p4_generic_fast_path/README.ja.md)（区間cycle counterでの内訳）。
