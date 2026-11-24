#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/game/Data/Text/Default"
OUT="$ROOT/ios/DK2Port/Resources/Assets/interface/fonts"
BUILD="$ROOT/build/uigen-fonts"
mkdir -p "$OUT" "$BUILD"
javac -nowarn -d "$BUILD" -sourcepath "$ROOT/research/OpenKeeper/src/main/java:$ROOT/tools/uigen" \
  "$ROOT/tools/uigen/FontMain.java" \
  "$ROOT/tools/uigen/com/jme3/math/FastMath.java" \
  "$ROOT/tools/uigen/com/jme3/math/Vector3f.java" \
  "$ROOT/tools/uigen/javax/vecmath/Vector3f.java"
java -Djava.awt.headless=true -cp "$BUILD" FontMain "$SRC" "$OUT"
echo "wrote $OUT"
