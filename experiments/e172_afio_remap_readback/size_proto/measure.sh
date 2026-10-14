#!/bin/bash
# measure.sh <label> <fqbn board> <sketch> <extra -D flags>
E=/home/mt/dev_wch/ArduinoCore-CH32RV/work/uartexp; R=/home/mt/dev_wch/ArduinoCore-CH32RV; W=$R/work/usb
export ARDUINO_DIRECTORIES_USER=$W/user ARDUINO_DIRECTORIES_DATA=$W/data ARDUINO_DIRECTORIES_DOWNLOADS=$W/downloads
G=$R/.tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf
out="$E/build/${1// /_}"; rm -rf "$out"
log=$(arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:$2:pnum=ANY --build-property compiler.path=$(dirname $G)/ \
  --build-property "build.extra_flags=$4" --build-path "$out" "$E/$3" 2>&1) || { echo "$1 FAILED"; echo "$log" | grep -E 'error' | head -5; exit 1; }
$G-size -A "$out/$3.ino.elf" | awk -v l="$1" '/^\.(text|vector|data|bss)/ {s[$1]=$2} END {printf "%-34s flash=%5d  ram=%5d\n", l, s[".vector"]+s[".text"]+s[".data"], s[".data"]+s[".bss"]}'
