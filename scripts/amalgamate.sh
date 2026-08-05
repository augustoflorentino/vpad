#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/dist/vpad/vpad.h}
mkdir -p "$(dirname "$out")"

{
    cat "$root/include/vpad/vpad.h"
    echo
    echo "#if defined(VPAD_IMPLEMENTATION) && !defined(VPAD_IMPLEMENTATION_DONE)"
    echo "#define VPAD_IMPLEMENTATION_DONE"
    for src in "$root"/src/*.h "$root"/src/*.c; do
        [ -f "$src" ] && grep -v '^#include "' "$src"
    done
    echo "#endif"
} > "$out"
echo "$out"
