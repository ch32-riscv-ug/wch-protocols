#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

experiment_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
fqbn="esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,UploadMode=default"

build_profile() {
  local profile_number="$1"
  local profile_name="$2"
  arduino-cli compile \
    --fqbn "$fqbn" \
    --warnings all \
    --build-property "compiler.cpp.extra_flags=-DOEP_USB_TEST_PROFILE=${profile_number}" \
    --build-path "$experiment_dir/build/$profile_name" \
    "$experiment_dir"
}

case "${1:-all}" in
  a|A|1)
    build_profile 1 profile_a
    ;;
  b|B|2)
    build_profile 2 profile_b
    ;;
  all)
    build_profile 1 profile_a
    build_profile 2 profile_b
    ;;
  *)
    echo "usage: $0 [a|b|all]" >&2
    exit 2
    ;;
esac

