#!/bin/bash
# Extra LinkE captures on the current wiring: run_extra.sh <out> <linke> <pins> <names> <target> <clock image> <wrong chip> <protect yes|no>
set -u
OUT=$1; LINKE=$2; PINS=$3; NAMES=$4; TARGET=$5; CLOCK=$6; WRONG=$7; PROTECT=$8; mkdir -p $OUT
cap() {
  local name=$1; shift
  echo "=== $name"
  PYTHONUNBUFFERED=1 timeout 180 uv run --no-project --with numpy --with pyserial --with pyusb python linke_cap.py \
    30eda0e343c6-hs 50000000 $OUT/$name 256 $LINKE $PINS $NAMES $TARGET -- "$@" --capture $OUT/$name.ndjson 2>&1 | tail -1
}
for s in low medium high; do
  cap speed_${s}_target_info --speed $s target info
  cap speed_${s}_read_ram_256 --speed $s read --range 0x20000000+256 -o $OUT/speed_${s}_read_ram_256.bin
  cap speed_${s}_read_flash_4k --speed $s read --range 0x08000000+4096 -o $OUT/speed_${s}_read_flash_4k.bin
done
cap dmi_write_dmcontrol_01 dbg dmi write 0x10 0x00000001
cap dmi_read_dmstatus dbg dmi read 0x11
cap dmi_read_data0 dbg dmi read 0x04
cap dmi_read_abstractcs dbg dmi read 0x16
cap err_read_unmapped read --range 0x60000000+16 -o $OUT/err_read_unmapped.bin
cap err_wrong_chip --chip $WRONG target info
cap option_get target option get
cap option_set_data target option set data0=0x5a data1=0xc3 --yes
cap option_get_after_set target option get
cap read_obr_after_set read --range 0x4002201c+4 -o $OUT/read_obr_after_set.bin
cap option_reset target option reset --yes
cap option_get_after_reset target option get
cap clock_flash flash $CLOCK --yes
cap clock_read_rcc_0 read --range 0x40021000+8 -o $OUT/clock_read_rcc_0.bin
cap clock_read_actlr_0 read --range 0x40022000+4 -o $OUT/clock_read_actlr_0.bin
cap clock_target_info target info
cap clock_read_rcc_1 read --range 0x40021000+8 -o $OUT/clock_read_rcc_1.bin
cap clock_dbg_halt dbg halt
cap clock_read_rcc_2 read --range 0x40021000+8 -o $OUT/clock_read_rcc_2.bin
cap clock_dbg_resume dbg resume
cap clock_reset reset
cap clock_read_rcc_3 read --range 0x40021000+8 -o $OUT/clock_read_rcc_3.bin
cap clock_read_actlr_3 read --range 0x40022000+4 -o $OUT/clock_read_actlr_3.bin
cap connect_under_reset --connect-under-reset target info
if [ "$PROTECT" = yes ]; then
  cap protect_on target protect on --yes
  cap protect_get_on target option get
  cap protect_off target protect off --yes
  cap protect_get_off target option get
fi
