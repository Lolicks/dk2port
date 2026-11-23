#!/usr/bin/env bash
# Regenerate OpenKeeper PNG references, decode the same frames with the C
# decoder, and assert byte-exact agreement. Writes only under port/.work/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GAME="$(cd "$ROOT/.." && pwd)"
N="${1:-3}"
OUT="$ROOT/.work/tqi_c_port/validate"
REF="$OUT/reference"
PPM="$OUT/ppm"
mkdir -p "$REF" "$PPM" "$OUT"

bash "$ROOT/tools/tgq_decode_openkeeper.sh" \
    "$GAME/Data/Movies/BullfrogIntro.tgq" "$REF" "$N" > "$OUT/openkeeper.log" 2>&1
"$ROOT/decoder/build/dk2tool" tgqdecode \
    "$GAME/Data/Movies/BullfrogIntro.tgq" "$PPM" --frames "$N" > "$OUT/c_decoder.log" 2>&1

for ((i = 0; i < N; ++i)); do
    printf -v idx '%03d' "$i"
    python3 "$ROOT/decoder/tests/compare_ppm_png.py" \
        "$PPM/frame_$idx.ppm" "$REF/frame_$idx.png" --expect-exact
done
echo "VALIDATED $N frames: byte-exact vs OpenKeeper reference"
