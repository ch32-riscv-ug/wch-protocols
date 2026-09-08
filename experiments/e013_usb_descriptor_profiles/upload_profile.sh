#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <a|b> <upload-port>" >&2
  exit 2
fi

experiment_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
fqbn="esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,UploadMode=default"

case "$1" in
  a|A|1)
    profile_number=1
    profile_name=profile_a
    ;;
  b|B|2)
    profile_number=2
    profile_name=profile_b
    ;;
  *)
    echo "profile must be a or b" >&2
    exit 2
    ;;
esac

"$experiment_dir/build_profiles.sh" "$profile_number"
arduino-cli upload \
  --fqbn "$fqbn" \
  --port "$2" \
  --input-dir "$experiment_dir/build/$profile_name" \
  "$experiment_dir"

