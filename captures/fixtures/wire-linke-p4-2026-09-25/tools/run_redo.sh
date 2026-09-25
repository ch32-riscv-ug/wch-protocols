#!/bin/bash
# Redo: run_redo.sh <out> <linke> <pins> <names> <target> <tag> <rate> <family>
set -u
OUT=$1; LINKE=$2; PINS=$3; NAMES=$4; TARGET=$5; TAG=$6; RATE=$7; FAM=$8
R=/home/mt/dev_wch/ch32rv/target/release/ch32rv
cap() {
  local name=$1; shift
  echo "=== $name"
  PYTHONUNBUFFERED=1 timeout 240 uv run --no-project --with numpy --with pyserial --with pyusb python linke_cap.py \
    30eda0e343c6-hs $RATE $OUT/$name 256 $LINKE $PINS $NAMES $TARGET -- "$@" --capture $OUT/$name.ndjson 2>&1 | tail -1
}
$R --probe serial:$LINKE flash clock_$TAG.bin --yes > $OUT/flash_clock_redo.txt 2>&1
cap redo_redetect_0_reset reset
CH32RV=$PWD/redetect.sh cap redo_redetect_1_after_reset 810d0103
CH32RV=$PWD/redetect.sh cap redo_redetect_2_again 810d0103
CH32RV=$PWD/redetect.sh cap redo_redetect_3_status_then_redetect 810d01ff 810d0103
cap redo_monitor_sdi_on monitor sdi on
cap redo_monitor_sdi_off monitor sdi off
cap redo_recover_power_off --chip $FAM recover --method power-off --yes
cap redo_recover_nrst --chip $FAM recover --method nrst --yes
cap redo_after_recover_target_info target info
