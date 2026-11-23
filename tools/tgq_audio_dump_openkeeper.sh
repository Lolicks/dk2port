#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <movie.tgq> <out.raw>" >&2
    exit 2
fi

MOVIE="$1"
RAW="$2"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/research/OpenKeeper/src/main/java/toniarts/openkeeper"
BUILD="$ROOT/.work/tqi_openkeeper_classes"
JME="$(find /home/kali/.gradle/caches -name 'jme3-core-*.jar' -print -quit)"
VEC="$(find /home/kali/.gradle/caches -name 'vecmath-*.jar' -print -quit)"

if [ ! -f "$MOVIE" ]; then
    echo "movie not found: $MOVIE" >&2
    exit 2
fi
if [ -z "$JME" ] || [ -z "$VEC" ]; then
    echo "jme3-core/vecmath jars not found in /home/kali/.gradle/caches" >&2
    exit 2
fi

mkdir -p "$BUILD" "$(dirname "$RAW")"
javac -encoding UTF-8 -cp "$JME:$VEC" -d "$BUILD" \
    "$SRC/tools/convert/BitReader.java" \
    "$SRC/tools/convert/BufferedResourceReader.java" \
    "$SRC/tools/convert/ConversionUtils.java" \
    "$SRC/tools/convert/IResourceReader.java" \
    "$SRC/tools/convert/IResourceChunkReader.java" \
    "$SRC/tools/convert/IValueEnum.java" \
    "$SRC/tools/convert/IFlagEnum.java" \
    "$SRC/tools/convert/ResourceChunkReader.java" \
    "$SRC/video/tgq/EAAudioFrame.java" \
    "$SRC/video/tgq/EAAudioHeader.java" \
    "$SRC/video/tgq/TgqFile.java" \
    "$SRC/video/tgq/TgqFrame.java" \
    "$ROOT/tools/TgqAudioDump.java"

java -cp "$BUILD:$JME:$VEC" TgqAudioDump "$MOVIE" "$RAW"
