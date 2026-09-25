#!/bin/bash
# More LinkE captures on the same wiring: run_more.sh <out> <linke> <pins> <names> <target> <tag> <rate> <monitor rate>
#   monitors (sdi/dmdata/dmseq/rtt, core examples), 5x repeats, RedetectChip after reset, then the erasing recovers.
set -u
OUT=$1; LINKE=$2; PINS=$3; NAMES=$4; TARGET=$5; TAG=$6; RATE=$7; MRATE=$8; mkdir -p $OUT
R=/home/mt/dev_wch/ch32rv/target/release/ch32rv
cap() {
  local rate=$1 name=$2; shift 2
  echo "=== $name"
  PYTHONUNBUFFERED=1 timeout 240 uv run --no-project --with numpy --with pyserial --with pyusb python linke_cap.py \
    30eda0e343c6-hs $rate $OUT/$name 256 $LINKE $PINS $NAMES $TARGET -- "$@" --capture $OUT/$name.ndjson 2>&1 | tail -1
}
for m in SDI:sdi DMDATA:dmdata DMSeq:dmseq RTT:rtt; do
  e=${m%%:*}; s=${m#*:}
  $R --probe serial:$LINKE flash mon/${TAG}_Hello$e.bin --yes > $OUT/flash_Hello$e.txt 2>&1
  if [ $s = sdi ]; then
    cap $MRATE monitor_sdi_enable monitor sdi enable
    cap $MRATE monitor_sdi --duration 4 monitor --source sdi
  else
    STDIN='abc\n' cap $MRATE monitor_$s --duration 4 monitor --source $s
  fi
done
for i in 1 2 3 4 5; do
  cap $RATE repeat${i}_target_info target info
  cap $RATE repeat${i}_read_ram_256 read --range 0x20000000+256 -o $OUT/repeat${i}_read_ram_256.bin
done
$R --probe serial:$LINKE flash clock_$TAG.bin --yes > $OUT/flash_clock.txt 2>&1
cap $RATE redetect_0_reset reset
CH32RV=$PWD/redetect.sh cap $RATE redetect_1_after_reset 810d0103
CH32RV=$PWD/redetect.sh cap $RATE redetect_2_again 810d0103
CH32RV=$PWD/redetect.sh cap $RATE redetect_3_status_then_redetect 810d01ff 810d0103
cap $RATE redetect_4_target_info target info
cap $RATE recover_power_off recover --method power-off --yes
cap $RATE recover_nrst recover --method nrst --yes
cap $RATE recover_unbrick recover --method unbrick --yes
cap $RATE after_recover_target_info target info
