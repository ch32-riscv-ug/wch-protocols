#!/bin/bash
# The same LinkE operations for any target: run_suite2.sh <out dir> <linke serial> <pins> <names> <target> <image>
set -u
OUT=$1; LINKE=$2; PINS=$3; NAMES=$4; TARGET=$5; IMAGE=$6; mkdir -p $OUT
cap() {
  local name=$1; shift
  echo "=== $name"
  PYTHONUNBUFFERED=1 timeout 120 uv run --no-project --with numpy --with pyserial --with pyusb python linke_cap.py \
    30eda0e343c6-hs 50000000 $OUT/$name 256 $LINKE $PINS $NAMES $TARGET -- "$@" --capture $OUT/$name.ndjson 2>&1 | tail -1
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
cap flash_sketch flash $IMAGE --yes
