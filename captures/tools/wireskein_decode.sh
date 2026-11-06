#!/bin/sh
# Decode wire captures with WireSkein (bits, frames, DMI and their timing are WireSkein's): each <file>.sr to
# <out>/<file>.json, the path kept as given (relative to captures/). A file whose path has v003 is SWIO on its one
# channel SWIO; any other RVSWD on SWCLK / SWDIO (the fixtures' channel names).
#
#   cd captures && WIRESKEIN=<wireskein checkout> sh tools/wireskein_decode.sh <out dir> <file.sr>...
set -eu
: "${WIRESKEIN:?set WIRESKEIN to a wireskein checkout}"
out=$1
shift
here=$(pwd)
for f in "$@"; do
    case $f in
        *v003*) spec='{"buses":[{"protocol":"swio","pins":{"dio":"SWIO"}}]}' ;;
        *) spec='{"buses":[{"protocol":"rvswd","pins":{"clk":"SWCLK","dio":"SWDIO"}}]}' ;;
    esac
    dst="$out/${f%.sr}.json"
    mkdir -p "$(dirname "$dst")"
    case $dst in /*) ;; *) dst="$here/$dst" ;; esac
    (cd "$WIRESKEIN" && uv run --quiet wireskein decode "$here/$f" --spec "$spec" --out "$dst" >/dev/null)
done
