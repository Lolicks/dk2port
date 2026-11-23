#!/usr/bin/env bash
# Local verification: everything that can be validated on Linux.
set -euo pipefail
cd "$(dirname "$0")"

mkdir -p build
python3 -m unittest discover -s tests -v
python3 tests/make_fixture_map.py build/fixture.bin >/dev/null
clang++ -std=c++17 -O2 -Wall -Wextra \
  -o build/engine_tests tests/engine_tests.cpp engine/DK2.cpp engine/GridSimulation.cpp
./build/engine_tests build/fixture.bin
if [ -f build/Assets/maps/Alcatraz.bin ]; then
  ./build/engine_tests build/Assets/maps/Alcatraz.bin
fi
if [ -f build/Assets/maps/Alcatraz.bin ]; then
  clang++ -std=c++17 -O2 -o build/profile tests/profile.cpp \
    engine/DK2.cpp engine/GridSimulation.cpp
  ./build/profile build/Assets/maps/Alcatraz.bin
fi
if [ -f decoder/build.sh ]; then
  (cd decoder && ./build.sh)
fi
echo "ALL LOCAL CHECKS PASSED"
