# E161 UIAPduino bootloader が留まる条件（PINRSTF）と RAM payload の MIE

状態: **完了（2026-09-22）**

## 問い

E129 の SWIO-only boot entry（BOOT_MODE 設定 + PD4 detach + PFIC software reset）は、なぜ動く時と `0000:0002` のまま
application へ戻る時があるのか。「何か状態を壊している」という利用者の疑いに、機材を変えずに答える。

## 手順

OEP v0 probe（`esp32-d0wd-v3-0070070d9394`、oep-probe-arduino `examples/Esp32V003Probe`）で UIAPduino Pro Micro V1.4 に対し:

1. BOOT 領域 1,920 B と option 16 B を読んで factory capture（`captures/fixtures/uiapduino-v003-factory-*`）と比較する。
2. BOOT 領域を逆アセンブル（`riscv-none-embed-objdump -D -b binary -m riscv:rv32`）し、entry の分岐条件を読む。
3. `RCC_RSTSCKR` を読み、RMVF で消した場合と GPIO23 → PD7/NRST の 20 ms pulse で PINRSTF を立てた場合で、E129 payload 後に
   Windows `usbipd list` に `1209:b803` が現れるかを比べる（`e161_v003_uiap_boot_stay_condition.py`）。
4. payload の実行状態は halt 後の dpc / mcause で確認する。

## 結果

- BOOT 領域・option bytes は factory と完全一致。BOOT_MODE=0、BOOT_LOCK=1。flash 側の「状態破壊」は無い。
- bootloader は entry 直後に `RSTSCKR` の bit26（PINRSTF）を試験し（`0x1ffff0ec`〜`0x1ffff0fa`）、立っていなければ
  `0x1ffff058`（PD4 low → BOOT_MODEKEYR unlock → STATR=0 → PFIC SYSRST）で application へ戻る。
- 実測: RMVF 後の boot payload → `0000:0002` のまま（`RSTSCKR=[SFTRSTF]`）。NRST pulse → boot payload → **1209:b803 が 1 s で列挙**。
  BOOT_MODE=1 を設定して pin reset だけ → application が動く。
- RAM payload を app 実行中から resume すると `mcause=2`（illegal instruction）で app の trap handler に落ちた。app の SysTick ISR が
  payload と同じ RAM 領域（.data/.bss）を書くため。`mstatus=0` を書いてから resume すれば payload は正常に完了する（E135 loader 経路は
  これをしていた）。
- ArduinoCore-CH32 の `CH32.resetReason()` は初回読み出しで RMVF を書き、PINRSTF を含む全 flag を消す。これが「pin reset 無しの
  boot entry が動く時と動かない時がある」の少なくとも一因。

## 結論

boot entry の必要条件は「PINRSTF が立っている」ことで、E129 payload はその上で動く。OEP probe には `target.control reset`
mode 3（NRST pulse）を足し、client の `reset --mode boot` は pin reset → payload の順で行う。core の RMVF をどうするかは利用者判断
（ArduinoCore-CH32 `docs/todo.ja.md`）。
