#!/bin/bash
# One LinkE operation after another, each captured on the wire (P4, 50 MHz) and on USB (ch32rv --capture).
set -u
OUT=captures-l103-2026-09-25; mkdir -p $OUT
cap() {   # name, ch32rv args...
  local name=$1; shift
  echo "=== $name"
  PYTHONUNBUFFERED=1 timeout 120 uv run --no-project --with numpy --with pyserial --with pyusb python linke_cap.py \
    30eda0e343c6-hs 50000000 $OUT/$name 256 -- "$@" --capture $OUT/$name.ndjson 2>&1 | tail -3
}
cap target_info target info
cap flash_pattern4k flash pattern-4k.bin --yes
cap verify_pattern4k verify pattern-4k.bin
cap read_flash_4k read --range 0x08000000+4096 -o $OUT/read_flash_4k.bin
cap read_ram_256 read --range 0x20000000+256 -o $OUT/read_ram_256.bin
cap dbg_halt dbg halt
cap dbg_regs dbg regs
cap dbg_step dbg step
cap dbg_resume dbg resume
cap reset reset
cap erase_all erase --all --yes
cap flash_sketch flash l103_image.bin --yes
