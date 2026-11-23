#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
CC=${CC:-clang}
mkdir -p build
SRC="wad/wad.c kmsh/kmsh.c kmf/kmf.c kmf/anim.c tgq/tgq.c tqi/tqi.c audio/xa.c audio/tgq_audio.c"
$CC -std=c11 -O2 -Wall -Wextra -I. \
    -o build/dk2tool tools/dk2tool.c $SRC
$CC -std=c11 -O2 -Wall -Wextra -I. \
    -o build/test_decoder tests/test_decoder.c $SRC
$CC -std=c11 -O2 -Wall -Wextra -I. \
    -o build/test_tqi tests/test_tqi.c $SRC
$CC -std=c11 -O2 -Wall -Wextra -I. \
    -o build/test_audio tests/test_audio.c $SRC
./build/test_decoder "${DK2_GAME_ROOT:-../..}" "${DK2_KMF_DIR:-../build/KMF}"
./build/test_tqi "${DK2_GAME_ROOT:-../..}"
./build/test_audio "${DK2_GAME_ROOT:-../..}" "build/BullfrogIntro_audio.wav"
echo "C DECODER BUILD OK"
